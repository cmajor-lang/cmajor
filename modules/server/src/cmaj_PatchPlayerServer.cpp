//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  The Cmajor project is subject to commercial or open-source licensing.
//  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
//  visit https://cmajor.dev to learn about our commercial licence options.
//
//  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
//  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
//  DISCLAIMED.

#include "../../compiler/include/cmaj_ErrorHandling.h"
#include "choc/text/choc_TextTable.h"
#include "choc/threading/choc_ThreadSafeFunctor.h"
#include "choc/network/choc_HTTPServer.h"
#include "cmaj_LocalFileCache.h"
#include "../../playback/include/cmaj_PatchPlayer.h"
#include "../../playback/include/cmaj_AudioSources.h"
#include "../../embedded_assets/cmaj_EmbeddedAssets.h"
#include "../include/cmaj_PatchPlayerServer.h"

#define CHOC_ENABLE_HTTP_SERVER_TEST 1
#include "choc/tests/choc_tests.h"

namespace cmaj
{

struct PatchPlayerServer
{
    PatchPlayerServer (std::string address,
                       uint16_t port,
                       const choc::value::Value& engineOptionsToUse,
                       cmaj::BuildSettings& buildSettingsToUse,
                       const cmaj::audio_utils::AudioDeviceOptions& audioOptions,
                       CreateAudioMIDIPlayerFn createPlayer,
                       std::vector<std::filesystem::path> patchLocationsToScan)
        : engineOptions (engineOptionsToUse),
          buildSettings (buildSettingsToUse),
          patchLocations (std::move (patchLocationsToScan)),
          createAudioMIDIPlayer (std::move (createPlayer))
    {
        if (httpServer.open (address, port, 0,
                             [this] { return std::make_unique<ClientRequestHandler> (*this); },
                             [this] (const std::string& error) { handleServerError (error); }))
        {
            setAudioDevicePropsFn = [this] (const choc::value::ValueView& v) { setAudioDeviceProperties (v); };

            writeToConsole ("\nCmajor server active: " + httpServer.getHTTPAddress() + "\n\n");

            audioPlayer = createAudioMIDIPlayer (audioOptions);
            refreshAllSessionAudioDevices();
        }
        else
        {
            writeToConsole ("\nError: Cmajor server failed to start\n\n");
        }
    }

    ~PatchPlayerServer()
    {
        setAudioDevicePropsFn.reset();
        httpServer.close();
    }

    void handleServerError (const std::string& errorMessage)
    {
        if (choc::text::contains (errorMessage, "Socket is not connected"))
            return; // this error is just when the browser refreshes

        writeToConsole (errorMessage);
    }

    void writeToConsole (const std::string& text)
    {
        std::cout << text << std::endl;
    }

    choc::value::Value getAudioDeviceProperties()
    {
        if (audioPlayer != nullptr)
        {
            auto& o = audioPlayer->options;

            return choc::json::create (
                     "availableAPIs", choc::value::createArray (audioPlayer->getAvailableAudioAPIs()),
                     "availableOutputDevices", choc::value::createArray (audioPlayer->getAvailableOutputDevices()),
                     "availableInputDevices", choc::value::createArray (audioPlayer->getAvailableInputDevices()),
                     "sampleRates", choc::value::createArray (audioPlayer->getAvailableSampleRates()),
                     "blockSizes", choc::value::createArray (audioPlayer->getAvailableBlockSizes()),
                     "audioAPI", o.audioAPI,
                     "output", o.outputDeviceName,
                     "input", o.inputDeviceName,
                     "rate", static_cast<int32_t> (o.sampleRate),
                     "blockSize", static_cast<int32_t> (o.blockSize));
        }

        return {};
    }

    void broadcastAudioDeviceProperties()
    {
        auto p = getAudioDeviceProperties();

        if (p.isObject())
            broadcastToAllSessions (choc::json::create ("type", "audio_device_properties",
                                                        "message", p));
    }

    void sendAudioDeviceProperties (choc::value::Value options)
    {
        choc::messageloop::postMessage ([f = setAudioDevicePropsFn, options = std::move (options)] { f (options); });
    }

    void setAudioDeviceProperties (const choc::value::ValueView& options)
    {
        if (audioPlayer == nullptr || ! options.isObject())
            return;

        const auto& o = audioPlayer->options;
        auto newOptions = o;

        newOptions.audioAPI = options["audioAPI"].getWithDefault<std::string> (o.audioAPI);
        newOptions.outputDeviceName = options["output"].getWithDefault<std::string> (o.outputDeviceName);
        newOptions.inputDeviceName = options["input"].getWithDefault<std::string> (o.inputDeviceName);
        newOptions.sampleRate = static_cast<uint32_t> (options["rate"].getWithDefault<int32_t> (static_cast<int32_t> (o.sampleRate)));
        newOptions.blockSize = static_cast<uint32_t> (options["blockSize"].getWithDefault<int32_t> (static_cast<int32_t> (o.blockSize)));

        if (newOptions.audioAPI != o.audioAPI
             || newOptions.outputDeviceName != o.outputDeviceName
             || newOptions.inputDeviceName != o.inputDeviceName
             || newOptions.sampleRate != o.sampleRate
             || newOptions.blockSize != o.blockSize)
        {
            audioPlayer = nullptr;
            refreshAllSessionAudioDevices();
            audioPlayer = createAudioMIDIPlayer (newOptions);
            refreshAllSessionAudioDevices();
            broadcastAudioDeviceProperties();
        }
    }

    static uint64_t createRandomUint64()
    {
        std::random_device seed;
        std::mt19937 rng (seed());
        std::uniform_int_distribution<uint64_t> dist (0, 0x7ffffffffll);
        return dist (rng);
    }

    static std::string createRandomSessionID()
    {
        return choc::text::createHexString (createRandomUint64());
    }

    static constexpr size_t maxNumSessions = 16;
    static constexpr int clientTimeoutMilliseconds = 5000;

    struct Session;

    //==============================================================================
    struct ClientRequestHandler  : public choc::network::HTTPServer::ClientInstance
    {
        ClientRequestHandler (PatchPlayerServer& s) : owner (s)
        {
            messageThread.start (0, [this] { processPendingMessages(); });
        }

        ~ClientRequestHandler() override
        {
            messageThread.stop();
            setCurrentSession ({});
        }

        void upgradedToWebSocket (std::string_view path) override
        {
            auto sessionID = std::string (path.substr (1));
            CMAJ_ASSERT (currentSession == nullptr);
            setCurrentSession (owner.getOrCreateSession (sessionID));
        }

        void setCurrentSession (std::shared_ptr<Session> newSession)
        {
            if (newSession != currentSession)
            {
                if (currentSession != nullptr)
                    currentSession->activeClientList.remove (*this);

                currentSession = newSession;

                if (currentSession != nullptr)
                    currentSession->activeClientList.add (*this);
            }
        }

        choc::network::HTTPContent getHTTPContent (std::string_view path) override
        {
            if (auto p = path.find ("?"); p != std::string::npos)
                path = path.substr (0, path.find ("?"));

            if (path == "/")
                return choc::network::HTTPContent::forHTML (createRedirectToNewSessionPage());

            if (auto sessionIndex = path.find ("/session_"); sessionIndex != std::string_view::npos)
            {
                auto sessionIDStartIndex = sessionIndex + 9;
                auto sessionID = path.substr (sessionIDStartIndex);

                if (auto slash = sessionID.find ("/"); slash != std::string_view::npos)
                {
                    sessionID = sessionID.substr (0, slash);
                    path = path.substr (sessionIDStartIndex + slash);

                    if (currentSession == nullptr || currentSession->sessionID != sessionID)
                        if (auto session = owner.getOrCreateSession (std::string (sessionID)))
                            setCurrentSession (session);

                    if (currentSession != nullptr)
                        if (auto result = currentSession->serveHTTPRequest (path))
                            return result;
                }
            }

            auto relativePath = std::filesystem::path (path).relative_path().generic_string();

            if (relativePath == "cmaj-patch-server.js")
                return choc::network::HTTPContent::forContent (getPatchServerModule());

            if (auto content = EmbeddedAssets::getInstance().findContent (relativePath); ! content.empty())
                return choc::network::HTTPContent::forContent (content);

            return {};
        }

        std::string getPatchServerModule() const
        {
            return choc::text::replace (EmbeddedAssets::getInstance().getContent ("embedded_patch_session_template.js"),
                                        "SOCKET_URL", choc::json::getEscapedQuotedString (owner.httpServer.getWebSocketAddress()));
        }

        std::string createRedirectToNewSessionPage()
        {
            auto sessionURL = "/session_" + createRandomSessionID() + "/cmaj-patch-chooser.html";

            return choc::text::replace ("<!DOCTYPE html><html><head>"
                                        "<meta http-equiv=\"refresh\" content=\"0; URL='SESSION_URL'\" />"
                                        "</head></html>",
                                        "SESSION_URL", sessionURL);
        }

        void handleWebSocketMessage (std::string_view m) override
        {
            try
            {
                auto v = choc::json::parse (m);

                if (! v.isObject())
                    return;

                std::scoped_lock l (messageQueueLock);

                if (currentSession != nullptr
                     && currentSession->handleMessageFromClientConcurrently (v))
                        return;

                messageQueue.push_back (std::make_unique<choc::value::Value> (std::move (v)));
                messageThread.trigger();
            }
            catch (const std::exception& e)
            {
                std::cerr << e.what() << '\n';
            }
        }

        std::unique_ptr<choc::value::Value> popNextMessage()
        {
            std::scoped_lock l (messageQueueLock);

            if (messageQueue.empty())
                return {};

            auto message = std::move (messageQueue.front());
            messageQueue.erase (messageQueue.begin());
            return message;
        }

        void processPendingMessages()
        {
            for (;;)
            {
                if (auto m = popNextMessage())
                    handleMessageFromClient (*m);
                else
                    break;
            }
        }

        void handleMessageFromClient (const choc::value::ValueView& message)
        {
            if (currentSession != nullptr)
                if (currentSession->handleMessageFromClient (message))
                    return;

            if (auto typeMember = message["type"]; typeMember.isString())
            {
                auto type = typeMember.getString();

                if (type == "req_audio_device_props")
                    owner.broadcastAudioDeviceProperties();
                else if (type == "set_audio_device_props")
                    owner.sendAudioDeviceProperties (choc::value::Value (message["properties"]));
             }
        }

        PatchPlayerServer& owner;
        std::shared_ptr<Session> currentSession;
        std::mutex messageQueueLock;
        std::vector<std::unique_ptr<choc::value::Value>> messageQueue;
        choc::threading::TaskThread messageThread;
    };

    //==============================================================================
    struct ActiveClientList
    {
        ActiveClientList (Session& s) : session (s) {}

        ~ActiveClientList()
        {
            cleanUpTimer.clear();
        }

        void add (ClientRequestHandler& r)
        {
            std::scoped_lock sl (clientLock);
            clients.insert (std::addressof (r));
            cleanUpTimer.clear();
        }

        void remove (ClientRequestHandler& r)
        {
            std::scoped_lock sl (clientLock);
            clients.erase (std::addressof (r));
            cleanUpTimer.clear();

            if (clients.empty())
            {
                cleanUpTimer = choc::messageloop::Timer (10000, [sessionID = session.sessionID,
                                                                 owner = std::addressof (session.owner)]
                {
                    owner->removeSession (sessionID);
                    return false;
                });
            }
        }

        void send (const choc::value::ValueView& message)
        {
            auto json = choc::json::toString (message);
            std::scoped_lock sl (clientLock);

            for (auto* c : clients)
                c->sendWebSocketMessage (json);
        }

    private:
        Session& session;
        std::mutex clientLock;
        std::unordered_set<ClientRequestHandler*> clients;
        choc::messageloop::Timer cleanUpTimer;
    };

    //==============================================================================
    struct Session
    {
        Session (PatchPlayerServer& s, std::string sessionIDToUse)
           : owner (s), sessionID (std::move (sessionIDToUse))
        {
            httpPath = "/session_" + sessionID + "/";
            httpRootURL = owner.httpServer.getHTTPAddress() + httpPath;

            pingTimer = choc::messageloop::Timer (2000, [this]
            {
                sendPing();
                return true;
            });
        }

        ~Session()
        {
            pingTimer.clear();
            codeGenThread.stop();
            patchFileScanThread.stop();
            view.reset();
            patchPlayer.reset();
        }

        //==============================================================================
        void writeToConsole (const std::string& text)
        {
            owner.writeToConsole (text);
        }

        bool handleMessageFromClientConcurrently (const choc::value::ValueView& message)
        {
            return fileCache.handleMessageFromClientConcurrently (message);
        }

        bool handleMessageFromClient (const choc::value::ValueView& message)
        {
            createPlayer();

            lastMessageTime = std::chrono::steady_clock::now();

            try
            {
                auto typeMember = message["type"];

                if (typeMember.isString() && typeMember.getString() == "load_patch" && ! message["file"].toString().empty())
                    writeToConsole ("Loading patch: " + message["file"].toString());

                if (patchPlayer != nullptr && patchPlayer->handleClientMessage (*view, message))
                    return true;

                if (typeMember.isString())
                {
                    auto type = typeMember.getString();

                    if (type == "ping")
                        return true;

                    if (fileCache.handleMessageFromClient (type, message))
                        return true;

                    if (type == "load_patch")
                        return loadPatchFromLocalFile (message["file"].toString());

                    if (type == "req_session_status")
                    {
                        sendStatus();
                        return true;
                    }

                    if (type == "req_patchlist")
                    {
                        requestPatchList (message["replyType"].toString());
                        return true;
                    }

                    if (type == "req_codegen")
                    {
                        requestCodeGen (message["codeType"].toString(),
                                        message["replyType"].toString(),
                                        message["options"]);
                        return true;
                    }

                    if (type == "set_audio_playback_active")
                    {
                        setAudioPlaybackActive (message["active"].getWithDefault<bool> (true));
                        return true;
                    }

                    if (type == "req_audio_input_mode")
                    {
                        sendAudioInputModeStatus (cmaj::EndpointID::create (message["endpoint"].toString()));
                        return true;
                    }

                    if (type == "set_custom_audio_input")
                    {
                        setAudioEndpointSource (cmaj::EndpointID::create (message["endpoint"].toString()), message);
                        return true;
                    }
                }
            }
            catch (const std::exception& e)
            {
                writeToConsole ("Server: error processing message from client: " + std::string (e.what()));
                return true;
            }

            return false;
        }

        void sendMessageToClient (const choc::value::ValueView& message)
        {
            activeClientList.send (message);
        }

        void sendMessageToClient (std::string_view type, const choc::value::ValueView& message)
        {
            sendMessageToClient (choc::json::create ("type", type,
                                                     "message", message));
        }

        choc::network::HTTPContent serveHTTPRequest (std::string_view path)
        {
            if (path == "/cmaj-patch-chooser.html")  return choc::network::HTTPContent::forHTML (createPatchChooserPage());
            if (path == "/cmaj-patch-runner.html")   return choc::network::HTTPContent::forHTML (createPatchRunnerPage());

            if (patchPlayer != nullptr)
                if (auto manifest = patchPlayer->patch.getManifest())
                    if (auto content = manifest->readFileContent (std::string (path)))
                        return choc::network::HTTPContent::forContent (*content);

            return {};
        }

        //==============================================================================
        void sendStatus()
        {
            auto status = choc::json::create ("httpRootURL", httpRootURL,
                                              "playing", patchPlayer != nullptr && patchPlayer->isPlaying());

            bool loaded = false;
            std::string buildLog;

            if (patchPlayer != nullptr)
            {
                if (auto f = patchPlayer->patch.getManifestFile(); ! f.empty())
                    status.setMember ("manifestFile", f);

                if (auto manifest = patchPlayer->patch.getManifest())
                {
                    loaded = patchPlayer->patch.isPlayable();
                    status.setMember ("manifest", manifest->manifest);
                    status.setMember ("details", patchPlayer->patch.getProgramDetails());
                    status.setMember ("codeGenTargets", owner.getAvailableCodeGenTargets());
                }

                buildLog = patchPlayer->patch.getLastBuildLog();
            }

            status.setMember ("loaded", loaded);

            if (! statusMessage.empty())
                status.setMember ("status", statusMessage);

            if (! errorMessage.empty())
                status.setMember ("error", errorMessage);

            if (! buildLog.empty())
                status.setMember ("log", buildLog);

            if (buildLog != lastBuildLog)
            {
                lastBuildLog = buildLog;

                if (! buildLog.empty())
                    writeToConsole (buildLog);
            }

            sendMessageToClient ("session_status", status);
        }

        bool loadPatchFromLocalFile (const std::string& file)
        {
            if (fileCache.getFileSize (file) != 0 && patchPlayer != nullptr)
            {
                cmaj::PatchManifest manifest;

                if (fileCache.initialiseManifest (manifest, file))
                    return patchPlayer->patch.loadPatchFromManifest (std::move (manifest));
            }

            return false;
        }

        void sendPing()
        {
            sendMessageToClient (choc::json::create ("type", "ping"));

            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastMessage = now - lastMessageTime;
            auto timeInMillisecs = std::chrono::duration_cast<std::chrono::milliseconds> (timeSinceLastMessage).count();

            if (timeInMillisecs > clientTimeoutMilliseconds && patchPlayer != nullptr)
            {
                if (patchPlayer->patch.isLoaded())
                {
                    writeToConsole ("Client ping time-out - unloading");
                    patchPlayer->patch.unload();
                }

                lastMessageTime = now;
            }
        }

        void setAudioEndpointSource (const EndpointID& endpointID, const choc::value::ValueView& message)
        {
            if (patchPlayer != nullptr)
            {
                Patch::CustomAudioSourcePtr source;

                if (auto filename = message["file"]; filename.isString())
                    source = audio_utils::LoopingFilePlayerSource::createFromCachedFile (fileCache, filename.toString());
                else if (message["mute"].getWithDefault<bool> (false))
                    source = std::make_shared<audio_utils::MuteAudioSource>();

                patchPlayer->patch.setCustomAudioSourceForInput (endpointID, source);
                sendAudioInputModeStatus (endpointID);
            }
        }

        void sendAudioInputModeStatus (const cmaj::EndpointID& endpointID)
        {
            if (patchPlayer != nullptr)
            {
                std::string mode = "live";

                if (auto customSource = patchPlayer->patch.getCustomAudioSourceForInput (endpointID))
                {
                    auto namedSource = dynamic_cast<audio_utils::NamedAudioSource*> (customSource.get());
                    CMAJ_ASSERT (namedSource != nullptr);
                    mode = namedSource->getName();
                }

                sendMessageToClient ("audio_input_mode_" + endpointID.toString(), choc::value::createString (mode));
            }
        }

        void requestCodeGen (const std::string& type,
                             const std::string& replyType,
                             const choc::value::ValueView& options)
        {
            if (patchPlayer != nullptr)
            {
                if (auto manifest = patchPlayer->patch.getManifest())
                {
                    cmaj::Patch::LoadParams params;
                    params.manifest = *manifest;

                    codeGenThread.stop();
                    codeGenThread.start (0, [this, params, type, replyType, options = choc::value::Value (options)]
                    {
                        cmaj::Patch p;
                        p.setPlaybackParams (patchPlayer->patch.getPlaybackParams());
                        p.createEngine = [] { return cmaj::Engine::create(); };
                        p.createContextForPatchWorker = [] { return std::unique_ptr<Patch::WorkerContext>(); };
                        auto output = p.generateCode (params, type, choc::json::toString (options));

                        sendMessageToClient (replyType,
                                             choc::json::create ("code", output.generatedCode,
                                                                 "mainClass", output.mainClassName,
                                                                 "messages", output.messages.toJSON()));

                    });

                    codeGenThread.trigger();
                }
            }
        }

        void requestPatchList (const std::string& replyType)
        {
            patchFileScanThread.stop();

            patchFileScanThread.start (0, [this, replyType]
            {
                sendMessageToClient (replyType, owner.scanForPatches());
            });

            patchFileScanThread.trigger();
        }

        void createPlayer()
        {
            if (patchPlayer == nullptr)
            {
                patchPlayer = std::make_unique<cmaj::PatchPlayer> (owner.engineOptions, owner.buildSettings, true);

                patchPlayer->setTempo (120.0f);
                patchPlayer->setTimeSig (4, 4);
                patchPlayer->setTransportState (true, false);

                patchPlayer->setAudioMIDIPlayer (owner.audioPlayer);
                patchPlayer->startPlayback();
                patchPlayer->onPatchLoaded   = [this] { sendStatus(); };
                patchPlayer->onPatchUnloaded = [this] { sendStatus(); };

                patchPlayer->onStatusChange = [this] (const cmaj::Patch::Status& s)
                {
                    statusMessage = s.statusMessage;
                    errorMessage = s.messageList.toString();
                    sendStatus();
                };

                patchPlayer->patch.patchFilesChanged = [this] (auto change)
                {
                    sendMessageToClient ("patch_source_changed",
                                         choc::json::create ("cmajorFilesChanged", change.cmajorFilesChanged,
                                                             "assetFilesChanged", change.assetFilesChanged,
                                                             "manifestChanged", change.manifestChanged));
                };

                patchPlayer->patch.handleInfiniteLoop = [this]
                {
                    writeToConsole ("Infinite loop detected! Terminating..");
                    sendMessageToClient ("infinite_loop_detected", {});

                    static bool terminating = false;

                    if (! terminating)
                    {
                        terminating = true;

                        // allow a pause for the message to get sent to any clients, and then die...
                        static auto terminateTask = std::async (std::launch::async, +[]
                        {
                            std::this_thread::sleep_for (std::chrono::milliseconds (1000));
                            std::terminate();
                        });
                    }
                };

                view = std::make_unique<ProxyPatchView> (*this, patchPlayer->patch);
            }
        }

        std::string createPatchRunnerPage()
        {
            return choc::text::replace (EmbeddedAssets::getInstance().getContent ("embedded_patch_runner_template.html"),
                                        "SESSION_ID", sessionID);
        }

        std::string createPatchChooserPage()
        {
            return choc::text::replace (EmbeddedAssets::getInstance().getContent ("embedded_patch_chooser_template.html"),
                                        "SESSION_ID", sessionID);
        }

        std::string getSessionAge() const
        {
            auto age = std::chrono::steady_clock::now() - creationTime;
            return choc::text::getDurationDescription (age);
        }

        void setAudioPlaybackActive (bool active)
        {
            if (patchPlayer != nullptr)
            {
                if (active)
                    patchPlayer->startPlayback();
                else
                    patchPlayer->stopPlayback();
            }

            sendStatus();
        }

        PatchPlayerServer& owner;
        ActiveClientList activeClientList { *this };
        LocalFileCache<Session> fileCache { *this };
        std::unique_ptr<cmaj::PatchPlayer> patchPlayer;
        std::string sessionID, httpRootURL, httpPath, statusMessage, errorMessage;
        choc::threading::TaskThread codeGenThread, patchFileScanThread;
        choc::messageloop::Timer pingTimer;
        std::chrono::steady_clock::time_point creationTime { std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point lastMessageTime { std::chrono::steady_clock::now() };
        std::string lastBuildLog;

        //==============================================================================
        struct ProxyPatchView  : public cmaj::PatchView
        {
            ProxyPatchView (Session& s, cmaj::Patch& p) : PatchView (p), session (s) {}

            void sendMessage (const choc::value::ValueView& m) override
            {
                session.sendMessageToClient (m);
            }

            Session& session;
        };

        std::unique_ptr<ProxyPatchView> view;
    };

    //==============================================================================
    std::shared_ptr<Session> findSession (const std::string& sessionID)
    {
        std::scoped_lock sl (activeSessionLock);

        auto s = activeSessions.find (sessionID);

        if (s != activeSessions.end())
            return s->second;

        return nullptr;
    }

    std::shared_ptr<Session> getOrCreateSession (const std::string& sessionID)
    {
        std::scoped_lock sl (activeSessionLock);

        auto s = activeSessions.find (sessionID);

        if (s != activeSessions.end())
            return s->second;

        if (activeSessions.size() >= maxNumSessions)
            return {};

        auto newSession = std::make_shared<Session> (*this, sessionID);
        activeSessions[sessionID] = newSession;
        writeToConsole ("Session created: " + sessionID);
        dumpActiveSessionStats();
        return newSession;
    }

    void removeSession (std::string sessionID)
    {
        std::scoped_lock sl (activeSessionLock);
        activeSessions.erase (sessionID);
        writeToConsole ("Session deleted: " + sessionID);
        dumpActiveSessionStats();
    }

    void refreshAllSessionAudioDevices()
    {
        std::scoped_lock sl (activeSessionLock);

        for (auto& s : activeSessions)
            if (s.second->patchPlayer != nullptr)
                s.second->patchPlayer->setAudioMIDIPlayer (audioPlayer);
    }

    void broadcastToAllSessions (const choc::value::ValueView& message)
    {
        std::scoped_lock sl (activeSessionLock);

        for (auto& s : activeSessions)
            s.second->sendMessageToClient (message);
    }

    void dumpActiveSessionStats()
    {
        choc::text::TextTable table;
        table << "Session" << "Age" << "URL";
        table.newRow();
        table.newRow();

        for (auto& s : activeSessions)
        {
            table << s.first << s.second->getSessionAge() << s.second->httpRootURL + "cmaj-patch-chooser.html";
            table.newRow();
        }

        if (activeSessions.empty())
        {
            table << "(None)" << "" << "";
            table.newRow();
        }

        auto rows = table.getRows ("| ", " | ", " |");
        auto divider = std::string (rows.front().size(), '-');

        writeToConsole ("\n"
                          + divider + "\n"
                          + choc::text::joinStrings (rows, "\n") + "\n"
                          + divider);
    }

    //==============================================================================
    choc::value::Value scanForPatches() const
    {
        auto list = choc::value::createEmptyArray();
        size_t total = 0;
        constexpr size_t maxNumPatches = 200;

        auto addPatch = [&] (const std::filesystem::path& file) -> bool
        {
            if (file.extension() == ".cmajorpatch")
            {
                if (++total >= maxNumPatches)
                    return false;

                try
                {
                    PatchManifest manifest;

                    try
                    {
                        manifest.initialiseWithFile (file);
                    }
                    catch (...) {}

                    auto m = manifest.getStrippedManifest();

                    if (! m.isObject())
                        m = choc::value::createObject ({});

                    m.setMember ("manifestFile", manifest.getFullPathForFile (manifest.manifestFile));

                    if (! m.hasObjectMember ("name"))
                        m.setMember ("name", manifest.manifestFile);

                    list.addArrayElement (std::move (m));
                    return true;
                }
                catch (...) {}
            }

            return false;
        };

        for (auto& f : patchLocations)
        {
            if (exists (f))
            {
                if (addPatch (f))
                    continue;

                if (is_directory (f))
                    for (auto& file : std::filesystem::recursive_directory_iterator (f))
                        addPatch (file.path());
            }
        }

        return list;
    }

    //==============================================================================
    const choc::value::ValueView& getAvailableCodeGenTargets()
    {
        if (codeGenTargets.isVoid())
        {
            std::vector<std::string> types;

            for (auto& t : cmaj::Engine::create().getAvailableCodeGenTargetTypes())
                if (t != "graph")
                    types.push_back (t);

            codeGenTargets = choc::value::createArray (types);
        }

        return codeGenTargets;
    }

private:
    //==============================================================================
    choc::value::Value engineOptions;
    cmaj::BuildSettings buildSettings;
    std::vector<std::filesystem::path> patchLocations;
    choc::value::Value codeGenTargets;
    CreateAudioMIDIPlayerFn createAudioMIDIPlayer;

    choc::threading::ThreadSafeFunctor<std::function<void(const choc::value::ValueView&)>> setAudioDevicePropsFn;
    std::shared_ptr<cmaj::audio_utils::AudioMIDIPlayer> audioPlayer;
    choc::network::HTTPServer httpServer;

    std::unordered_map<std::string, std::shared_ptr<Session>> activeSessions;
    std::mutex activeSessionLock;
};

//==============================================================================
void runPatchPlayerServer (std::string address,
                           uint16_t port,
                           const choc::value::Value& engineOptions,
                           cmaj::BuildSettings& buildSettings,
                           const cmaj::audio_utils::AudioDeviceOptions& audioOptions,
                           CreateAudioMIDIPlayerFn createPlayer,
                           std::vector<std::filesystem::path> patchLocations)
{
    PatchPlayerServer server (address, port, engineOptions, buildSettings,
                              audioOptions, std::move (createPlayer), patchLocations);

    std::cout << std::endl
              << "------------------------------------------------" << std::endl
              << std::endl;

    choc::messageloop::run();
}

void runServerUnitTests (choc::test::TestProgress& progress)
{
    choc_unit_tests::testHTTPServer (progress);
}

} // namespace cmaj
