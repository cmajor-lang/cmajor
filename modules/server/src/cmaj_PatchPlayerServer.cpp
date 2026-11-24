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

namespace cmaj
{

struct PatchPlayerServer
{
    PatchPlayerServer (const ServerOptions& serverOptionsToUse,
                       const choc::value::Value& engineOptionsToUse,
                       cmaj::BuildSettings& buildSettingsToUse,
                       const choc::audio::io::AudioDeviceOptions& audioOptions,
                       CreateAudioMIDIPlayerFn createPlayer)
        : engineOptions (engineOptionsToUse),
          buildSettings (buildSettingsToUse),
          serverOptions (serverOptionsToUse),
          createAudioMIDIPlayer (std::move (createPlayer))
    {
        checkForSinglePatchMode();

        setAudioDevicePropsFn = [this] (const choc::value::ValueView& v) { setAudioDeviceProperties (v); };
        broadcastAudioDevicePropsFn = [this] { broadcastAudioDeviceProperties(); };
        audioDeviceOptionsChangedFn = [this] { handleAudioDeviceOptionsChanged(); };

        // The device is opened before any client can connect: opening it can take a couple
        // of seconds (e.g. when CoreAudio has to switch the device to the requested rate),
        // and a client that asked for the device properties in the meantime would be told
        // there's no device, with no later update to correct it
        openAudioPlayer (audioOptions);

        if (httpServer.open (serverOptions.address, serverOptions.port, 0,
                             [this] { return std::make_unique<ClientRequestHandler> (*this); },
                             [this] (const std::string& error) { handleServerError (error); }))
        {
            writeToConsole ("\nCmajor server active: " + httpServer.getHTTPAddress() + "\n\n");
        }
        else
        {
            writeToConsole ("\nError: Cmajor server failed to start\n\n");
        }
    }

    ~PatchPlayerServer()
    {
        setAudioDevicePropsFn.reset();
        broadcastAudioDevicePropsFn.reset();
        audioDeviceOptionsChangedFn.reset();
        httpServer.close();
    }

    /// Returns true if the server started up and is listening for connections.
    bool isOpen() const                     { return httpServer.isOpen(); }

    /// Returns the port that the server is actually listening on. Note that this may
    /// not be the port that was requested, as it falls back to the next one available.
    uint16_t getPort() const                { return httpServer.getPort(); }

    /// Returns the address that the server is actually listening on, e.g. "http://127.0.0.1:8081"
    std::string getHTTPAddress() const      { return httpServer.getHTTPAddress(); }

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
                     "availableOutputDevices", convertDeviceList (audioPlayer->getAvailableOutputDevices()),
                     "availableInputDevices", convertDeviceList (audioPlayer->getAvailableInputDevices()),
                     "sampleRates", convertSampleRateVector (audioPlayer->getAvailableSampleRates()),
                     "blockSizes", convertSampleRateVector (audioPlayer->getAvailableBlockSizes()),
                     "audioAPI", o.audioAPI,
                     "output", o.outputDeviceID,
                     "input", o.inputDeviceID,
                     "rate", static_cast<int32_t> (o.sampleRate),
                     "blockSize", static_cast<int32_t> (o.blockSize));
        }

        return choc::json::create();
    }

    static choc::value::Value convertDeviceList (const std::vector<choc::audio::io::AudioDeviceInfo>& devices)
    {
        return choc::value::createArray ((uint32_t) devices.size(), [&] (uint32_t i)
        {
            auto& d = devices[i];
            return choc::json::create ("ID", d.deviceID,
                                       "name", d.name);
        });
    }

    static choc::value::Value convertSampleRateVector (const std::vector<uint32_t>& rates)
    {
        std::vector<int32_t> result;
        result.reserve (rates.size());

        for (auto rate : rates)
            result.push_back (static_cast<int32_t> (rate));

        return choc::value::createArray (result);
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
        choc::messageloop::postMessage ([f = setAudioDevicePropsFn, o = std::move (options)] { f (o); });
    }

    /// Sends the device properties to every client, from the message thread (which is the
    /// only one that replaces the audio player). This can be called from any thread.
    void postAudioDevicePropertiesBroadcast()
    {
        choc::messageloop::postMessage ([f = broadcastAudioDevicePropsFn] { f(); });
    }

    /// Creates the audio player (falling back to the default device), and points every
    /// session's patch player at it. The device itself can change its options, e.g. when
    /// another app changes a CoreAudio device's rate: see handleAudioDeviceOptionsChanged().
    void openAudioPlayer (const choc::audio::io::AudioDeviceOptions& options)
    {
        audioPlayer = createAudioMIDIPlayer (options);

        if (audioPlayer == nullptr)
            audioPlayer = createAudioMIDIPlayer ({});

        if (audioPlayer != nullptr)
        {
            audioPlayer->options.midiClientName = midiClientName;
            // (the player is shared with the patch players, so this mustn't refer to the server)
            audioPlayer->deviceOptionsChanged = [f = audioDeviceOptionsChangedFn]
            {
                choc::messageloop::postMessage ([f] { f(); });
            };
        }

        refreshAllSessionAudioDevices();
    }

    /// Called on the message thread after the device changed its own options (its rate).
    /// AudioMIDIPlayer::updateSampleRate only tells the patch players that are registered
    /// with it, i.e. those that are playing a patch, so a stopped or empty player is brought
    /// up to date here (which rebuilds a stopped patch at the new rate, and does nothing to
    /// one that has already heard about it). Then every client is told.
    void handleAudioDeviceOptionsChanged()
    {
        {
            std::scoped_lock sl (activeSessionLock);

            for (auto& s : activeSessions)
                if (s.second->patchPlayer != nullptr)
                    s.second->patchPlayer->updatePlaybackParams (false);
        }

        broadcastAudioDeviceProperties();
    }

    void setAudioDeviceProperties (const choc::value::ValueView& options)
    {
        if (audioPlayer == nullptr || ! options.isObject())
            return;

        const auto& o = audioPlayer->options;
        auto newOptions = o;

        newOptions.audioAPI = options["audioAPI"].getWithDefault<std::string> (o.audioAPI);
        newOptions.outputDeviceID = options["output"].getWithDefault<std::string> (o.outputDeviceID);
        newOptions.inputDeviceID = options["input"].getWithDefault<std::string> (o.inputDeviceID);
        newOptions.sampleRate = static_cast<uint32_t> (options["rate"].getWithDefault<int32_t> (static_cast<int32_t> (o.sampleRate)));
        newOptions.blockSize = static_cast<uint32_t> (options["blockSize"].getWithDefault<int32_t> (static_cast<int32_t> (o.blockSize)));
        newOptions.midiClientName = midiClientName;

        if (newOptions.audioAPI != o.audioAPI
             || newOptions.outputDeviceID != o.outputDeviceID
             || newOptions.inputDeviceID != o.inputDeviceID
             || newOptions.sampleRate != o.sampleRate
             || newOptions.blockSize != o.blockSize)
        {
            audioPlayer = nullptr;
            refreshAllSessionAudioDevices();
            openAudioPlayer (newOptions);
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

    static constexpr auto midiClientName = "Cmajor";

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
                {
                    currentSession->removeTransportInfoRequest (*this);
                    currentSession->activeClientList.remove (*this);
                }

                currentSession = newSession;

                if (currentSession != nullptr)
                    currentSession->activeClientList.add (*this);
            }
        }

        choc::network::HTTPContent getHTTPContent (const choc::network::HTTPRequest& request) override
        {
            auto path = request.path;

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
            auto sessionURL = "/session_" + createRandomSessionID()
                                  + (owner.isSinglePatch ? "/cmaj-patch-runner.html"
                                                         : "/cmaj-patch-chooser.html");

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
            DiagnosticMessageList messageList;

            catchAllErrors (messageList, [&]
            {
                if (currentSession != nullptr)
                    if (currentSession->handleMessageFromClient (message, this))
                        return;

                if (auto typeMember = message["type"]; typeMember.isString())
                {
                    auto type = typeMember.getString();

                    // (handled on the message thread, which owns the audio player)
                    if (type == "req_audio_device_props")
                        owner.postAudioDevicePropertiesBroadcast();
                    else if (type == "set_audio_device_props")
                        owner.sendAudioDeviceProperties (choc::value::Value (message["properties"]));
                }
            });

            if (! messageList.empty())
                std::cout << messageList.toJSONString (true) << std::endl;
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
            // (no lock: there are no clients left to change it by now)
            transportInfoTimer.clear();
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

        /// The sender is only used to tell the clients' transport info rate requests apart.
        bool handleMessageFromClient (const choc::value::ValueView& message, const ClientRequestHandler* sender = nullptr)
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

                    if (type == "req_graph")
                    {
                        enableGraphStructure();
                        sendGraphStructure (message["replyType"].toString());
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

                    if (type == "req_audio_input_generator")
                    {
                        sendAudioInputGeneratorSettings (cmaj::EndpointID::create (message["endpoint"].toString()));
                        return true;
                    }

                    if (type == "set_custom_audio_input")
                    {
                        setAudioEndpointSource (cmaj::EndpointID::create (message["endpoint"].toString()), message);
                        return true;
                    }

                    if (type == "set_transport")
                    {
                        setTransport (message);
                        return true;
                    }

                    if (type == "req_transport_state")
                    {
                        sendTransportState();
                        return true;
                    }

                    if (type == "set_transport_info_rate")
                    {
                        setTransportInfoRate (sender, getFiniteNumber (message["intervalMs"]).value_or (0));
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

        /// A path that starts with a version folder, e.g. "/~v3/ui/view.js", is served as if it
        /// were "/ui/view.js". The panel asks for its patch's files under a new one after they
        /// change, so the browser loads fresh copies, including of the modules that they import
        /// by relative paths.
        static std::string_view removeVersionFolder (std::string_view path)
        {
            if (choc::text::startsWith (path, "/~v"))
            {
                auto end = path.find ('/', 3);

                if (end != std::string_view::npos && end > 3
                     && std::all_of (path.begin() + 3, path.begin() + static_cast<std::ptrdiff_t> (end), [] (char c) { return c >= '0' && c <= '9'; }))
                    return path.substr (end);
            }

            return path;
        }

        choc::network::HTTPContent serveHTTPRequest (std::string_view path)
        {
            path = removeVersionFolder (path);

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

            if (patchPlayer != nullptr && patchPlayer->patch.isLoaded())
            {
                if (auto f = patchPlayer->patch.getManifestFile(); ! f.empty())
                    status.setMember ("manifestFile", f);

                if (auto manifest = patchPlayer->patch.getManifest())
                {
                    loaded = patchPlayer->patch.isPlayable();
                    status.setMember ("manifest", manifest->manifest);
                    status.setMember ("details", patchPlayer->patch.getProgramDetails());
                    status.setMember ("codeGenTargets", owner.getAvailableCodeGenTargets());

                    // (the graph itself is fetched with req_graph, when this changes)
                    if (auto graphVersion = patchPlayer->patch.getGraphStructureVersion(); ! graphVersion.empty())
                        status.setMember ("graphVersion", graphVersion);
                }

                // (goes up with every build, good or bad)
                status.setMember ("buildNumber", static_cast<int64_t> (patchPlayer->patch.getBuildNumber()));

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

            auto now                  = std::chrono::steady_clock::now();
            auto timeSinceLastMessage = now - lastMessageTime;
            auto timeInMillisecs      = std::chrono::duration_cast<std::chrono::milliseconds> (timeSinceLastMessage).count();

            if (timeInMillisecs > owner.serverOptions.clientTimeoutMs &&
                owner.serverOptions.clientTimeoutMs > 0 &&
                patchPlayer != nullptr)
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
                auto currentSource = patchPlayer->patch.getCustomAudioSourceForInput (endpointID);
                Patch::CustomAudioSourcePtr source;

                if (auto filename = message["file"]; filename.isString())
                    source = audio_utils::LoopingFilePlayerSource::createFromCachedFile (fileCache, filename.toString());
                else if (auto generator = message["generator"]; generator.isObject())
                    source = audio_utils::SignalGeneratorSource::createOrUpdate (currentSource, generator,
                                                                                 getGeneratorSettings (endpointID));
                else if (message["mute"].getWithDefault<bool> (false))
                    source = std::make_shared<audio_utils::MuteAudioSource>();

                // (a generator that's already playing is updated in place, so there's no gap)
                if (source != currentSource)
                    patchPlayer->patch.setCustomAudioSourceForInput (endpointID, source);

                sendAudioInputModeStatus (endpointID);
            }
        }

        /// Sends `audio_input_mode_<endpoint>`, and if the input is playing a generator, its
        /// settings, as `audio_input_generator_<endpoint>`.
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

                if (mode == "generator")
                    sendAudioInputGeneratorSettings (endpointID);
            }
        }

        /// Sends `audio_input_generator_<endpoint>` with the settings of the endpoint's generator,
        /// or if it isn't playing one, those of the last generator it played (or the defaults).
        void sendAudioInputGeneratorSettings (const cmaj::EndpointID& endpointID)
        {
            sendMessageToClient ("audio_input_generator_" + endpointID.toString(),
                                 getGeneratorSettings (endpointID).toValue());
        }

        /// Returns the settings of the endpoint's generator, or if it isn't playing one, those of
        /// the last generator it played in this session (or the defaults). The panel can go back
        /// to a generator with `generator: {}` and get the settings that it had before.
        audio_utils::SignalGeneratorSource::Settings getGeneratorSettings (const cmaj::EndpointID& endpointID)
        {
            auto key = endpointID.toString();

            if (patchPlayer != nullptr)
            {
                if (auto generator = std::dynamic_pointer_cast<audio_utils::SignalGeneratorSource> (patchPlayer->patch.getCustomAudioSourceForInput (endpointID)))
                {
                    auto settings = generator->getSettings();
                    lastGeneratorSettings[key] = settings;
                    return settings;
                }
            }

            if (auto last = lastGeneratorSettings.find (key); last != lastGeneratorSettings.end())
                return last->second;

            return {};
        }

        /// Replies with the graph structure JSON that was made by the current build (see
        /// Patch::generateGraphStructure), its version, which matches the status's graphVersion,
        /// and the manifest file of the patch it's for (as the patch may have changed since the
        /// client asked). They're all empty if there's no successful build.
        void sendGraphStructure (const std::string& replyType)
        {
            std::string graph, version, manifestFile;

            if (patchPlayer != nullptr)
            {
                graph = patchPlayer->patch.getGraphStructure();
                version = patchPlayer->patch.getGraphStructureVersion();

                if (! graph.empty())
                    manifestFile = patchPlayer->patch.getManifestFile();
            }

            sendMessageToClient (replyType, choc::json::create ("graph", graph,
                                                                "version", version,
                                                                "manifestFile", manifestFile));
        }

        /// Each build only makes the graph structure (which costs a second load of the program)
        /// once a client has shown that it wants it, by asking for a graph. From then on, every
        /// build of this session makes it, and the status has its graphVersion. (Until then, the
        /// graph comes from a separate code generation build, as with an older server.)
        void enableGraphStructure()
        {
            wantsGraphStructure = true;

            if (patchPlayer != nullptr)
                patchPlayer->patch.generateGraphStructure = true;
        }

        void requestCodeGen (const std::string& type,
                             const std::string& replyType,
                             const choc::value::ValueView& options)
        {
            if (type == "graph")
                enableGraphStructure();

            // the current build has already made the graph (with no options), so there's no
            // need to build it again
            bool hasOptions = options.isObject() && options.size() > 0;

            if (type == "graph" && patchPlayer != nullptr && ! hasOptions)
            {
                if (auto graph = patchPlayer->patch.getGraphStructure(); ! graph.empty())
                {
                    sendMessageToClient (replyType,
                                         choc::json::create ("code", graph,
                                                             "mainClass", std::string(),
                                                             "messages", choc::value::createEmptyArray()));
                    return;
                }
            }

            if (patchPlayer != nullptr && patchPlayer->patch.isLoaded())
            {
                if (auto manifest = patchPlayer->patch.getManifest())
                {
                    cmaj::Patch::LoadParams params;
                    params.manifest = *manifest;

                    codeGenThread.stop();
                    codeGenThread.start (0, [this, params, type, replyType, o = choc::value::Value (options)]
                    {
                        cmaj::Patch p;
                        p.setPlaybackParams (patchPlayer->patch.getPlaybackParams());
                        p.createEngine = [] { return cmaj::Engine::create(); };
                        p.createContextForPatchWorker = [] (const std::string&) { return std::unique_ptr<Patch::WorkerContext>(); };
                        enableWebViewPatchWorker (p);

                        auto output = p.generateCode (params, type, choc::json::toString (o));

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
                patchPlayer->patch.generateGraphStructure = wantsGraphStructure.load();

                {
                    // (the session's transport settings outlive any player)
                    std::scoped_lock sl (transportLock);
                    applyTransportSettings (*patchPlayer);
                }

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

        //==============================================================================
        /// The session owns the transport: createPlayer() applies these settings to each new
        /// player, so they survive it. (The position belongs to the player.)
        struct TransportSettings
        {
            float tempo = 120.0f;
            uint32_t numerator = 4, denominator = 4;
            bool playing = true, recording = false, looping = false;
        };

        static constexpr double minTempo = 1.0, maxTempo = 999.0;
        static constexpr uint32_t maxTimeSigNumerator = 64, maxTimeSigDenominator = 32;
        static constexpr uint32_t minTransportInfoIntervalMs = 30, maxTransportInfoIntervalMs = 2000,
                                  transportInfoTimerTickMs = 10;

        static std::optional<double> getFiniteNumber (const choc::value::ValueView& v)
        {
            if (v.isInt() || v.isFloat())
                if (auto d = v.getWithDefault<double> (0); std::isfinite (d))
                    return d;

            return {};
        }

        /// Returns the largest power of two between 1 and maxTimeSigDenominator that isn't
        /// more than the given value (rounded).
        static uint32_t getTimeSigDenominator (double requested)
        {
            auto limit = static_cast<uint32_t> (std::clamp (std::round (requested), 1.0, static_cast<double> (maxTimeSigDenominator)));
            uint32_t denominator = 1;

            while (denominator * 2 <= limit)
                denominator *= 2;

            return denominator;
        }

        // (call with transportLock held)
        void applyTransportSettings (cmaj::PatchPlayer& player)
        {
            player.setTempo (transportSettings.tempo);
            player.setTimeSig (transportSettings.numerator, transportSettings.denominator);
            player.setTransportState (transportSettings.playing, transportSettings.recording, transportSettings.looping);
        }

        /// Handles `set_transport`, which changes only the fields that it includes, and then
        /// broadcasts the new `transport_state`.
        void setTransport (const choc::value::ValueView& message)
        {
            {
                std::scoped_lock sl (transportLock);
                auto& s = transportSettings;

                if (auto tempo = getFiniteNumber (message["tempo"]))
                    s.tempo = static_cast<float> (std::clamp (*tempo, minTempo, maxTempo));

                if (auto numerator = getFiniteNumber (message["numerator"]))
                    s.numerator = static_cast<uint32_t> (std::clamp (std::round (*numerator), 1.0, static_cast<double> (maxTimeSigNumerator)));

                if (auto denominator = getFiniteNumber (message["denominator"]))
                    s.denominator = getTimeSigDenominator (*denominator);

                if (auto v = message["playing"]; v.isBool())     s.playing = v.getBool();
                if (auto v = message["recording"]; v.isBool())   s.recording = v.getBool();
                if (auto v = message["looping"]; v.isBool())     s.looping = v.getBool();

                if (patchPlayer != nullptr)
                {
                    applyTransportSettings (*patchPlayer);

                    if (message["rewind"].getWithDefault<bool> (false))
                        patchPlayer->rewind();
                    else if (auto position = getFiniteNumber (message["position"]))
                        patchPlayer->setPosition (static_cast<int64_t> (std::max (0.0, std::floor (*position))));
                }
            }

            sendTransportState();
        }

        /// Returns the `transport_state` message: the session's settings, plus the player's
        /// position (0 if there's no player yet).
        choc::value::Value getTransportState()
        {
            TransportSettings s;

            {
                std::scoped_lock sl (transportLock);
                s = transportSettings;
            }

            int64_t frame = 0;
            double sampleRate = 0;

            if (patchPlayer != nullptr)
            {
                auto info = patchPlayer->getTransportInfo();
                frame = info.frame;
                sampleRate = info.sampleRate;
            }

            if (sampleRate <= 0 && owner.audioPlayer != nullptr)
                sampleRate = static_cast<double> (owner.audioPlayer->options.sampleRate);

            double quarterNote = 0, barStartQuarterNote = 0;
            cmaj::PatchPlayer::calculateQuarterNotePosition (frame, sampleRate, s.tempo, s.numerator, s.denominator,
                                                             quarterNote, barStartQuarterNote);

            return choc::json::create ("tempo", static_cast<double> (s.tempo),
                                       "numerator", static_cast<int32_t> (s.numerator),
                                       "denominator", static_cast<int32_t> (s.denominator),
                                       "playing", s.playing,
                                       "recording", s.recording,
                                       "looping", s.looping,
                                       "frame", frame,
                                       "quarterNote", quarterNote,
                                       "barStartQuarterNote", barStartQuarterNote,
                                       "sampleRate", sampleRate);
        }

        void sendTransportState()
        {
            sendMessageToClient ("transport_state", getTransportState());
        }

        /// Handles `set_transport_info_rate`. Each client has its own request (0 = none); the
        /// session broadcasts at the shortest interval that any of them asks for, and forgets a
        /// client's request when it disconnects. A non-zero request is answered straight away
        /// with a `transport_state`.
        void setTransportInfoRate (const ClientRequestHandler* client, double intervalMs)
        {
            // (lock order: transportTimerLock, then transportLock or the timer list's lock: the
            // timer callback takes transportLock while choc holds its timer list's lock)
            std::scoped_lock tl (transportTimerLock);
            bool needsNewTimer = false;

            {
                std::scoped_lock sl (transportLock);

                if (intervalMs > 0)
                    transportInfoRequests[client] = static_cast<uint32_t> (std::clamp (std::round (intervalMs),
                                                                                       static_cast<double> (minTransportInfoIntervalMs),
                                                                                       static_cast<double> (maxTransportInfoIntervalMs)));
                else
                    transportInfoRequests.erase (client);

                uint32_t newInterval = 0;

                for (auto& r : transportInfoRequests)
                    if (newInterval == 0 || r.second < newInterval)
                        newInterval = r.second;

                if (newInterval != transportInfoInterval)
                {
                    transportInfoInterval = newInterval;
                    lastPeriodicTransportInfoTime = std::chrono::steady_clock::now();
                    wasPlayingAtLastTransportInfo = false;
                }

                if (newInterval != 0 && ! transportInfoTimerRunning)
                    transportInfoTimerRunning = needsNewTimer = true;
            }

            // A running choc Timer is never replaced or deleted here: on the Mac, deleting one with
            // a callback pending can make a new one at the same address get called twice as often.
            // Instead it ticks at a fixed rate and stops itself (by returning false) once nobody
            // wants updates, and is only replaced after that.
            if (needsNewTimer)
                transportInfoTimer = choc::messageloop::Timer (transportInfoTimerTickMs, [this] { return handleTransportInfoTimerTick(); });

            if (intervalMs > 0)
                sendTransportState();
        }

        void removeTransportInfoRequest (const ClientRequestHandler& client)
        {
            setTransportInfoRate (std::addressof (client), 0);
        }

        /// The timer callback: every transportInfoInterval, sends the state if the transport is
        /// playing, or if it has stopped since the last time.
        bool handleTransportInfoTimerTick()
        {
            bool shouldSend = false;

            {
                std::scoped_lock sl (transportLock);

                if (transportInfoInterval == 0)
                {
                    transportInfoTimerRunning = false;
                    return false;
                }

                auto now = std::chrono::steady_clock::now();

                if (now - lastPeriodicTransportInfoTime + std::chrono::milliseconds (transportInfoTimerTickMs / 2)
                      >= std::chrono::milliseconds (transportInfoInterval))
                {
                    lastPeriodicTransportInfoTime = now;
                    auto playing = transportSettings.playing;
                    shouldSend = playing || wasPlayingAtLastTransportInfo;
                    wasPlayingAtLastTransportInfo = playing;
                }
            }

            if (shouldSend)
                sendTransportState();

            return true;
        }

        PatchPlayerServer& owner;
        ActiveClientList activeClientList { *this };
        LocalFileCache<Session> fileCache { *this };
        std::unique_ptr<cmaj::PatchPlayer> patchPlayer;
        std::string sessionID, httpRootURL, httpPath, statusMessage, errorMessage;
        std::atomic<bool> wantsGraphStructure { false };   // (see enableGraphStructure())
        std::unordered_map<std::string, audio_utils::SignalGeneratorSource::Settings> lastGeneratorSettings;
        choc::threading::TaskThread codeGenThread, patchFileScanThread;
        choc::messageloop::Timer pingTimer;
        std::chrono::steady_clock::time_point creationTime { std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point lastMessageTime { std::chrono::steady_clock::now() };
        std::string lastBuildLog;

        std::mutex transportLock, transportTimerLock;
        TransportSettings transportSettings;                                            // (transportLock)
        std::unordered_map<const ClientRequestHandler*, uint32_t> transportInfoRequests;  // (transportLock)
        uint32_t transportInfoInterval = 0;                                             // (transportLock)
        bool transportInfoTimerRunning = false, wasPlayingAtLastTransportInfo = false;  // (transportLock)
        std::chrono::steady_clock::time_point lastPeriodicTransportInfoTime;            // (transportLock)
        choc::messageloop::Timer transportInfoTimer;                                    // (transportTimerLock)

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

        if (activeSessions.size() >= serverOptions.maxNumSessions)
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
    void checkForSinglePatchMode()
    {
        isSinglePatch = false;

        if (serverOptions.patchLocations.size() == 1)
        {
            auto f = serverOptions.patchLocations.front();

            if (exists (f) && f.extension() == ".cmajorpatch")
                isSinglePatch = true;
        }
    }

    choc::value::Value scanForPatches()
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

        for (auto& f : serverOptions.patchLocations)
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
    choc::value::Value  engineOptions;
    cmaj::BuildSettings buildSettings;
    cmaj::ServerOptions serverOptions;
    bool isSinglePatch = false;
    choc::value::Value codeGenTargets;
    CreateAudioMIDIPlayerFn createAudioMIDIPlayer;

    choc::threading::ThreadSafeFunctor<std::function<void(const choc::value::ValueView&)>> setAudioDevicePropsFn;
    choc::threading::ThreadSafeFunctor<std::function<void()>> broadcastAudioDevicePropsFn, audioDeviceOptionsChangedFn;
    std::shared_ptr<choc::audio::io::AudioMIDIPlayer> audioPlayer;
    choc::network::HTTPServer httpServer;

    std::unordered_map<std::string, std::shared_ptr<Session>> activeSessions;
    std::mutex activeSessionLock;
};

//==============================================================================
void runPatchPlayerServer (const ServerOptions& serverOptions,
                           const choc::value::Value& engineOptions,
                           cmaj::BuildSettings& buildSettings,
                           const choc::audio::io::AudioDeviceOptions& audioOptions,
                           CreateAudioMIDIPlayerFn createPlayer)
{
    PatchPlayerServer server (serverOptions, engineOptions, buildSettings, audioOptions, std::move (createPlayer));

    std::cout << std::endl
              << "------------------------------------------------" << std::endl
              << std::endl;

    choc::messageloop::run();
}

namespace
{
    struct CallHistory
    {
        void addCall (const std::string& s)
        {
            calls.push_back (s);
        }

        std::string get()
        {
            std::ostringstream oss;

            for (auto c : calls)
                oss << c << std::endl;

            return oss.str();
        }

        std::vector<std::string> calls;
    };

    class StubAudioMidiPlayer : public choc::audio::io::AudioMIDIPlayer
    {
    public:
        StubAudioMidiPlayer (CallHistory& ch, const choc::audio::io::AudioDeviceOptions& o)
            : choc::audio::io::AudioMIDIPlayer (o), callHistory (ch)
        {
        }

        std::vector<std::string> getAvailableAudioAPIs() override
        {
            callHistory.addCall ("getAvailableAudioAPIs()");
            return {};
        }

        std::vector<uint32_t> getAvailableSampleRates() override
        {
            callHistory.addCall ("getAvailableSampleRates()");
            return { 44100, 48000 };
        }

        std::vector<uint32_t> getAvailableBlockSizes() override
        {
            callHistory.addCall ("getAvailableBlockSizes()");
            return { 32, 64, 128};
        }

        std::vector<choc::audio::io::AudioDeviceInfo> getAvailableInputDevices() override
        {
            callHistory.addCall ("getAvailableInputDevices()");
            return { { "in", "in" } };
        }

        std::vector<choc::audio::io::AudioDeviceInfo> getAvailableOutputDevices() override
        {
            callHistory.addCall ("getAvailableOutputDevices()");
            return { { "out", "out" } };
        }

        std::vector<std::string> getAvailableMIDIInputDevices() override
        {
            callHistory.addCall ("getAvailableMIDIInputDevices()");
            return {};
        }

        std::vector<std::string> getAvailableMIDIOutputDevices() override
        {
            callHistory.addCall ("getAvailableMIDIOutputDevices()");
            return {};
        }

        void start() override
        {
            callHistory.addCall ("start()");
        }

        void stop() override
        {
            callHistory.addCall ("stop()");
        }

        std::string getLastError() override
        {
            callHistory.addCall ("getLastError()");
            return {};
        }

        void handleOutgoingMidiMessage (const void* data, uint32_t length) override
        {
            callHistory.addCall ("handleOutgoingMidiMessage()");
            (void) data;
            (void) length;
        }

        /// Acts like a device whose rate is changed from outside (as a CoreAudio device's is,
        /// when another app changes it), which a real player reports on one of its own threads
        void simulateDeviceRateChange (uint32_t newRate)
        {
            updateSampleRate (newRate);
        }

    private:
        CallHistory& callHistory;
    };

    struct TestClient
    {
        TestClient (std::string host, uint16_t port, std::string clientId)
        {
            // Look up the domain name
            auto results = resolver.resolve (host, std::to_string (port));

            // Make the connection on the IP address we get from a lookup
            boost::asio::connect (ws.next_layer(), results.begin(), results.end());

            // Set a decorator to change the User-Agent of the handshake
            ws.set_option (boost::beast::websocket::stream_base::decorator(
                [] (boost::beast::websocket::request_type& req)
                {
                    req.set (boost::beast::http::field::user_agent,
                        std::string (BOOST_BEAST_VERSION_STRING) + " websocket-client-coro");
                }));

            // Perform the websocket handshake
            ws.handshake (host, "/" + clientId);
            ws.async_read (destBuffer, [this] (auto code, auto bytes) { readMessage (code, bytes); });

            std::atomic<bool> threadStarted { false };

            connection = std::thread ([this, &threadStarted]
            {
                try
                {
                    threadStarted = true;

                    while (! threadShouldExit)
                        ioc.run();
                }
                catch (std::exception const&) {}
            });

            // Wait for thread to be waiting for events
            while (! threadStarted)
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }

        ~TestClient()
        {
            try
            {
                threadShouldExit = true;
                ioc.stop();

                if (! connectionClosed)
                    ws.close (boost::beast::websocket::close_code::normal);
            }
            catch (std::exception const& e)
            {
                std::cerr << "Error: " << e.what() << std::endl;
            }

            connection.join();
        }

        /// Waits for the server to close our end of the websocket, returning false if
        /// that doesn't happen within a few seconds
        bool waitForConnectionToClose()
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);

            while (! connectionClosed)
            {
                if (std::chrono::steady_clock::now() >= deadline)
                    return false;

                std::this_thread::sleep_for (std::chrono::milliseconds (1));
            }

            return true;
        }

        choc::value::Value sendRequest (std::string text, uint32_t timeoutMs = 500)
        {
            messageText.clear();

            ws.write (boost::asio::buffer (text));

            std::unique_lock<std::mutex> lock (mutex);
            if (! condition.wait_for (lock, std::chrono::milliseconds (timeoutMs), [&] { return ! messageText.empty(); }))
                return {};

            return choc::json::parse (messageText);
        }

        /// For requests that cause several messages: sends one, and waits for the message of
        /// the given type which should be the last that it causes. Returns an object holding the
        /// message part of each message that arrived until then, keyed by type (or a void value
        /// if the last one never came).
        choc::value::Value sendRequestAndCollectReplies (std::string text, std::string_view lastReplyType, uint32_t timeoutMs = 1000)
        {
            {
                std::unique_lock<std::mutex> lock (mutex);
                receivedMessages.clear();
            }

            ws.write (boost::asio::buffer (text));

            std::unique_lock<std::mutex> lock (mutex);
            auto replies = choc::value::createObject ({});

            auto gotLastReply = condition.wait_for (lock, std::chrono::milliseconds (timeoutMs), [&]
            {
                for (auto& m : receivedMessages)
                    if (choc::json::parse (m)["type"].toString() == lastReplyType)
                        return true;

                return false;
            });

            if (! gotLastReply)
                return {};

            for (auto& m : receivedMessages)
            {
                auto message = choc::json::parse (m);
                auto type = message["type"].toString();
                replies.setMember (type, message["message"]);

                if (type == lastReplyType)
                    break;
            }

            return replies;
        }

        /// Forgets the messages received so far, waits for the given time, and returns how many
        /// messages of the given type arrived meanwhile.
        size_t countMessagesDuring (std::string_view type, uint32_t milliseconds)
        {
            {
                std::unique_lock<std::mutex> lock (mutex);
                receivedMessages.clear();
            }

            std::this_thread::sleep_for (std::chrono::milliseconds (milliseconds));
            return countReceivedMessages (type);
        }

        size_t countReceivedMessages (std::string_view type)
        {
            std::unique_lock<std::mutex> lock (mutex);
            size_t total = 0;

            for (auto& m : receivedMessages)
                if (choc::json::parse (m)["type"].toString() == type)
                    ++total;

            return total;
        }

        void clearReceivedMessages()
        {
            std::unique_lock<std::mutex> lock (mutex);
            receivedMessages.clear();
        }

        /// Waits for a message of the given type to arrive (since the last clear), without
        /// sending anything, and returns its message part (void if none came in time).
        choc::value::Value waitForMessage (std::string_view type, uint32_t timeoutMs = 1000)
        {
            std::unique_lock<std::mutex> lock (mutex);
            choc::value::Value result;

            condition.wait_for (lock, std::chrono::milliseconds (timeoutMs), [&]
            {
                for (auto& m : receivedMessages)
                    if (auto message = choc::json::parse (m); message["type"].toString() == type)
                        result = choc::value::Value (message["message"]);

                return ! result.isVoid();
            });

            return result;
        }

        boost::asio::io_context ioc;
        boost::asio::ip::tcp::resolver resolver { ioc };
        boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws { ioc };
        boost::beast::flat_buffer destBuffer;

        std::thread connection;
        std::atomic<bool> threadShouldExit { false };
        std::atomic<bool> connectionClosed { false };
        boost::beast::error_code closeError;

        void readMessage (boost::beast::error_code ec, std::size_t numBytes)
        {
            if (ec)
            {
                closeError = ec;       // NB: written before the flag that publishes it
                connectionClosed = true;
                return;
            }

            if (numBytes > 0)
            {
                std::unique_lock<std::mutex> lock (mutex);

                auto text = (boost::beast::buffers_to_string (destBuffer.data()));

                destBuffer.clear();

                // Prepare another read callback to be processed by the thread
                ws.async_read (destBuffer,
                            [this] (auto code, auto bytes) { readMessage (code, bytes); });

                // Discard ping messages as these are from a timer and will otherwise mess with the
                // expected messages
                if (text != "{\"type\": \"ping\"}")
                {
                    messageText = text;
                    receivedMessages.push_back (text);
                    condition.notify_all();
                }
            }
        }

        std::mutex mutex;
        std::condition_variable condition;

        std::string messageText;
        std::vector<std::string> receivedMessages;
    };

    void testServer (choc::test::TestProgress& progress)
    {
        CHOC_TEST (testServer);

        choc::value::Value engineOptions;
        cmaj::BuildSettings buildSettings;
        choc::audio::io::AudioDeviceOptions audioOptions;

        CallHistory callHistory;

        cmaj::ServerOptions serverOptions;

        serverOptions.address = "127.0.0.1";
        serverOptions.port    = 8081;

        PatchPlayerServer server (serverOptions, engineOptions, buildSettings, audioOptions,
                                  [&] (const choc::audio::io::AudioDeviceOptions& options)
                                  {
                                      return std::make_unique<StubAudioMidiPlayer> (callHistory, options);
                                  });

        if (! server.isOpen())
        {
            CHOC_FAIL ("Failed to start the server");
            return;
        }

        // NB: the server falls back to the next free port if the one we asked for is taken,
        // so the clients must connect to the port it actually got - otherwise they'd silently
        // connect to whatever else happens to be listening, and then hang
        auto serverPort = server.getPort();
        auto url = server.getHTTPAddress() + "/";

        auto t = std::thread ([&]
        {
            try
            {
                TestClient client1 (serverOptions.address, serverPort, "client1");
                TestClient client2 (serverOptions.address, serverPort, "client2");

                choc::value::Value response;

                response = client1.sendRequest ("{ \"type\": \"ping\" }");
                CHOC_EXPECT_EQ (response.isVoid(), true);

                // Check client1 status
                response = client1.sendRequest ("{ \"type\": \"req_session_status\" }");
                CHOC_EXPECT_EQ (response.isObject(), true);
                CHOC_EXPECT_EQ (response["type"].toString(), "session_status");
                CHOC_EXPECT_EQ (response["message"]["httpRootURL"].toString(), url + "session_client1/");
                CHOC_EXPECT_EQ (response["message"]["playing"].getBool(), true);
                CHOC_EXPECT_EQ (response["message"]["loaded"].getBool(), false);

                // Disable playback on client1
                response = client1.sendRequest ("{ \"type\": \"set_audio_playback_active\", \"active\":false }");
                CHOC_EXPECT_EQ (response.isObject(), true);
                CHOC_EXPECT_EQ (response["type"].toString(), "session_status");
                CHOC_EXPECT_EQ (response["message"]["playing"].getBool(), false);

                // Check client2 status
                response = client2.sendRequest ("{ \"type\": \"req_session_status\" }");
                CHOC_EXPECT_EQ (response.isObject(), true);
                CHOC_EXPECT_EQ (response["type"].toString(), "session_status");
                CHOC_EXPECT_EQ (response["message"]["httpRootURL"].toString(), url + "session_client2/");
                CHOC_EXPECT_EQ (response["message"]["playing"].getBool(), true);
                CHOC_EXPECT_EQ (response["message"]["loaded"].getBool(), false);

                // Re-enable playback on client1
                response = client1.sendRequest ("{ \"type\": \"set_audio_playback_active\", \"active\":true }");
                CHOC_EXPECT_EQ (response.isObject(), true);
                CHOC_EXPECT_EQ (response["type"].toString(), "session_status");
                CHOC_EXPECT_EQ (response["message"]["playing"].getBool(), true);

                // Retrieve the patch list
                response = client1.sendRequest ("{ \"type\": \"req_patchlist\" }");
                CHOC_EXPECT_EQ (response.isObject(), true);
                CHOC_EXPECT_EQ (response["type"].toString(), "");
                CHOC_EXPECT_EQ (response["message"].isArray(), true);

                // Audio device properties
                response = client1.sendRequest (R"({ "type": "req_audio_device_props" })");
                CHOC_EXPECT_EQ (response["type"].toString(), "audio_device_properties");
                CHOC_EXPECT_EQ (response["message"]["sampleRates"].isArray(), true);
                CHOC_EXPECT_EQ (response["message"]["sampleRates"][0].get<int32_t>(), 44100);
                CHOC_EXPECT_EQ (response["message"]["sampleRates"][1].get<int32_t>(), 48000);
                CHOC_EXPECT_EQ (response["message"]["blockSizes"].isArray(), true);
                CHOC_EXPECT_EQ (response["message"]["blockSizes"][0].get<int32_t>(), 32);
                CHOC_EXPECT_EQ (response["message"]["blockSizes"][1].get<int32_t>(), 64);
                CHOC_EXPECT_EQ (response["message"]["blockSizes"][2].get<int32_t>(), 128);
                CHOC_EXPECT_EQ (response["message"]["rate"].get<int32_t>(), 0);
                CHOC_EXPECT_EQ (response["message"]["blockSize"].get<int32_t>(), 0);

                // Set the audio device sample rate / block size
                response = client1.sendRequest (R"({ "type": "set_audio_device_props", "properties": { "rate":44100, "blockSize":64 } })");
                CHOC_EXPECT_EQ (response["type"].toString(), "audio_device_properties");
                CHOC_EXPECT_EQ (response["message"]["rate"].get<int32_t>(), 44100);
                CHOC_EXPECT_EQ (response["message"]["blockSize"].get<int32_t>(), 64);

                // Device shared between clients
                response = client2.sendRequest (R"({ "type": "req_audio_device_props" })");
                CHOC_EXPECT_EQ (response["type"].toString(), "audio_device_properties");
                CHOC_EXPECT_EQ (response["message"]["rate"].get<int32_t>(), 44100);
                CHOC_EXPECT_EQ (response["message"]["blockSize"].get<int32_t>(), 64);
            }
            catch (boost::system::system_error&)
            {
                CHOC_FAIL ("Failed to connect to server");
            }
            catch (choc::value::Error&)
            {
                CHOC_FAIL ("Invalid choc value");
            }

            choc::messageloop::stop();
        });

        choc::messageloop::run();

        t.join();
    }

    static bool isAnythingListeningOn (const std::string& address, uint16_t port)
    {
        try
        {
            boost::asio::io_context ioc;
            boost::asio::ip::tcp::socket socket (ioc);
            socket.connect ({ boost::asio::ip::make_address (address), port });
            return true;
        }
        catch (const std::exception&) {}

        return false;
    }

    /// Checks that the clients always know the device's real rate: the device is opened before
    /// any client can connect (opening one can take seconds, and a client used to be told there
    /// was no device, then never updated), and a rate change made by the device itself (e.g.
    /// another app changing a CoreAudio device's rate) is broadcast to every client, and reaches
    /// the transport state
    void testServerDeviceRate (choc::test::TestProgress& progress)
    {
        CHOC_TEST (deviceRate);

        choc::value::Value engineOptions;
        cmaj::BuildSettings buildSettings;
        choc::audio::io::AudioDeviceOptions audioOptions;
        audioOptions.sampleRate = 44100;
        audioOptions.blockSize = 128;

        CallHistory callHistory;
        cmaj::ServerOptions serverOptions;
        serverOptions.address = "127.0.0.1";
        serverOptions.port    = 8084;

        // (if something else has the port, the server will use another, and this can't be checked)
        auto portWasFree = ! isAnythingListeningOn (serverOptions.address, serverOptions.port);
        auto listeningWhileDeviceOpened = false;
        StubAudioMidiPlayer* stub = nullptr;

        PatchPlayerServer server (serverOptions, engineOptions, buildSettings, audioOptions,
                                  [&] (const choc::audio::io::AudioDeviceOptions& options)
                                  {
                                      listeningWhileDeviceOpened = isAnythingListeningOn (serverOptions.address, serverOptions.port);
                                      auto player = std::make_unique<StubAudioMidiPlayer> (callHistory, options);
                                      stub = player.get();
                                      return player;
                                  });

        if (! server.isOpen())
        {
            CHOC_FAIL ("Failed to start the server");
            return;
        }

        if (portWasFree)
            CHOC_EXPECT_FALSE (listeningWhileDeviceOpened);

        auto serverPort = server.getPort();
        CHOC_EXPECT_TRUE (stub != nullptr);

        auto t = std::thread ([&]
        {
            try
            {
                TestClient client1 (serverOptions.address, serverPort, "rateClient1");
                TestClient client2 (serverOptions.address, serverPort, "rateClient2");

                auto getTransportRate = [&]
                {
                    auto replies = client1.sendRequestAndCollectReplies (R"({ "type": "req_transport_state" })", "transport_state");
                    return replies.isObject() ? replies["transport_state"]["sampleRate"].getWithDefault<double> (0) : 0.0;
                };

                // the first request, straight after connecting, gets the open device's rate
                auto response = client1.sendRequest (R"({ "type": "req_audio_device_props" })");
                CHOC_EXPECT_EQ (response["type"].toString(), "audio_device_properties");
                CHOC_EXPECT_EQ (response["message"]["rate"].getWithDefault<int32_t> (0), 44100);
                CHOC_EXPECT_EQ (response["message"]["blockSize"].getWithDefault<int32_t> (0), 128);
                CHOC_EXPECT_NEAR (getTransportRate(), 44100.0, 0.0001);

                // the device changes its own rate, on a thread of its own: every client is told
                client1.clearReceivedMessages();
                client2.clearReceivedMessages();

                if (stub != nullptr)
                    stub->simulateDeviceRateChange (48000);

                CHOC_EXPECT_EQ (client1.waitForMessage ("audio_device_properties")["rate"].getWithDefault<int32_t> (0), 48000);
                CHOC_EXPECT_EQ (client2.waitForMessage ("audio_device_properties")["rate"].getWithDefault<int32_t> (0), 48000);
                CHOC_EXPECT_NEAR (getTransportRate(), 48000.0, 0.0001);

                // and anyone asking now is told the new rate
                response = client2.sendRequest (R"({ "type": "req_audio_device_props" })");
                CHOC_EXPECT_EQ (response["message"]["rate"].getWithDefault<int32_t> (0), 48000);
            }
            catch (boost::system::system_error&)
            {
                CHOC_FAIL ("Failed to connect to server");
            }
            catch (choc::value::Error&)
            {
                CHOC_FAIL ("Invalid choc value");
            }

            choc::messageloop::stop();
        });

        choc::messageloop::run();

        t.join();
    }

    /// Checks the `set_custom_audio_input { generator }` messages: installing a generator,
    /// merging partial updates into it, the settings that are broadcast, the request for
    /// them, and that mute and clearing the input still work alongside it
    void testServerAudioInputGenerator (choc::test::TestProgress& progress)
    {
        CHOC_TEST (audioInputGenerator);

        choc::value::Value engineOptions;
        cmaj::BuildSettings buildSettings;
        choc::audio::io::AudioDeviceOptions audioOptions;
        CallHistory callHistory;
        cmaj::ServerOptions serverOptions;

        serverOptions.address = "127.0.0.1";
        serverOptions.port    = 8082;

        PatchPlayerServer server (serverOptions, engineOptions, buildSettings, audioOptions,
                                  [&] (const choc::audio::io::AudioDeviceOptions& options)
                                  {
                                      return std::make_unique<StubAudioMidiPlayer> (callHistory, options);
                                  });

        if (! server.isOpen())
        {
            CHOC_FAIL ("Failed to start the server");
            return;
        }

        auto serverPort = server.getPort();

        auto t = std::thread ([&]
        {
            try
            {
                TestClient client (serverOptions.address, serverPort, "generatorClient");

                // each change replies with the input's mode, followed by the generator's settings if it has one
                auto setInput = [&] (const std::string& fields, std::string_view lastReplyType = "audio_input_generator_in")
                {
                    return client.sendRequestAndCollectReplies (R"({ "type": "set_custom_audio_input", "endpoint": "in")" + fields + " }", lastReplyType);
                };

                auto replies = setInput (R"(, "generator": { "type": "sine", "frequency": 1000 })");
                CHOC_EXPECT_EQ (replies["audio_input_mode_in"].toString(), "generator");

                choc::value::Value settings (replies["audio_input_generator_in"]);
                CHOC_EXPECT_EQ (settings["type"].toString(), "sine");
                CHOC_EXPECT_NEAR (settings["frequency"].get<double>(), 1000.0, 0.0001);
                CHOC_EXPECT_NEAR (settings["level"].get<double>(), -12.0, 0.0001);
                CHOC_EXPECT_NEAR (settings["startFrequency"].get<double>(), 20.0, 0.0001);
                CHOC_EXPECT_NEAR (settings["endFrequency"].get<double>(), 20000.0, 0.0001);
                CHOC_EXPECT_NEAR (settings["sweepSeconds"].get<double>(), 5.0, 0.0001);
                CHOC_EXPECT_NEAR (settings["impulseRate"].get<double>(), 2.0, 0.0001);

                // a partial update is merged into the current settings
                replies = setInput (R"(, "generator": { "level": -6 })");
                CHOC_EXPECT_EQ (replies["audio_input_mode_in"].toString(), "generator");
                settings = replies["audio_input_generator_in"];
                CHOC_EXPECT_EQ (settings["type"].toString(), "sine");
                CHOC_EXPECT_NEAR (settings["frequency"].get<double>(), 1000.0, 0.0001);
                CHOC_EXPECT_NEAR (settings["level"].get<double>(), -6.0, 0.0001);

                // ...and clamped, ignoring unknown types and non-numeric values
                settings = setInput (R"(, "generator": { "type": "banana", "frequency": 1e9, "level": 20, "sweepSeconds": "x" })")["audio_input_generator_in"];
                CHOC_EXPECT_EQ (settings["type"].toString(), "sine");
                CHOC_EXPECT_NEAR (settings["frequency"].get<double>(), 48000.0, 0.0001);
                CHOC_EXPECT_NEAR (settings["level"].get<double>(), 0.0, 0.0001);
                CHOC_EXPECT_NEAR (settings["sweepSeconds"].get<double>(), 5.0, 0.0001);

                settings = setInput (R"(, "generator": { "type": "pink", "frequency": 500 })")["audio_input_generator_in"];
                CHOC_EXPECT_EQ (settings["type"].toString(), "pink");

                // the settings can be asked for, and are replayed after the mode
                settings = client.sendRequestAndCollectReplies (R"({ "type": "req_audio_input_generator", "endpoint": "in" })", "audio_input_generator_in")["audio_input_generator_in"];
                CHOC_EXPECT_EQ (settings["type"].toString(), "pink");
                CHOC_EXPECT_NEAR (settings["frequency"].get<double>(), 500.0, 0.0001);

                replies = client.sendRequestAndCollectReplies (R"({ "type": "req_audio_input_mode", "endpoint": "in" })", "audio_input_generator_in");
                CHOC_EXPECT_EQ (replies["audio_input_mode_in"].toString(), "generator");
                CHOC_EXPECT_EQ (replies["audio_input_generator_in"]["type"].toString(), "pink");

                // mute still works, and replaces the generator (with no settings message)...
                replies = setInput (R"(, "mute": true)", "audio_input_mode_in");
                CHOC_EXPECT_EQ (replies["audio_input_mode_in"].toString(), "mute");
                CHOC_EXPECT_FALSE (replies.hasObjectMember ("audio_input_generator_in"));

                // ...whose settings are remembered, and used as the base for the next one
                settings = client.sendRequestAndCollectReplies (R"({ "type": "req_audio_input_generator", "endpoint": "in" })", "audio_input_generator_in")["audio_input_generator_in"];
                CHOC_EXPECT_EQ (settings["type"].toString(), "pink");

                replies = setInput (R"(, "generator": { "level": -20 })");
                CHOC_EXPECT_EQ (replies["audio_input_mode_in"].toString(), "generator");
                settings = replies["audio_input_generator_in"];
                CHOC_EXPECT_EQ (settings["type"].toString(), "pink");
                CHOC_EXPECT_NEAR (settings["frequency"].get<double>(), 500.0, 0.0001);
                CHOC_EXPECT_NEAR (settings["level"].get<double>(), -20.0, 0.0001);

                // a file that can't be found leaves the input live, as before
                replies = setInput (R"(, "file": "no_such_file.wav")", "audio_input_mode_in");
                CHOC_EXPECT_EQ (replies["audio_input_mode_in"].toString(), "live");

                // clearing the input goes back to live
                replies = setInput (R"(, "generator": {})");
                CHOC_EXPECT_EQ (replies["audio_input_mode_in"].toString(), "generator");
                replies = setInput ({}, "audio_input_mode_in");
                CHOC_EXPECT_EQ (replies["audio_input_mode_in"].toString(), "live");

                // an endpoint that has never had a generator reports the defaults
                settings = client.sendRequestAndCollectReplies (R"({ "type": "req_audio_input_generator", "endpoint": "other" })", "audio_input_generator_other")["audio_input_generator_other"];
                CHOC_EXPECT_EQ (settings["type"].toString(), "sine");
                CHOC_EXPECT_NEAR (settings["frequency"].get<double>(), 440.0, 0.0001);
                CHOC_EXPECT_NEAR (settings["level"].get<double>(), -12.0, 0.0001);
            }
            catch (boost::system::system_error&)
            {
                CHOC_FAIL ("Failed to connect to server");
            }
            catch (choc::value::Error&)
            {
                CHOC_FAIL ("Invalid choc value");
            }

            choc::messageloop::stop();
        });

        choc::messageloop::run();

        t.join();
    }

    /// Checks the transport messages: `set_transport` partial updates and clamping, the
    /// `transport_state` that's broadcast to every client of the session, `req_transport_state`,
    /// the periodic updates of `set_transport_info_rate`, and that the settings outlive the player
    void testServerTransport (choc::test::TestProgress& progress)
    {
        CHOC_TEST (transport);

        choc::value::Value engineOptions;
        cmaj::BuildSettings buildSettings;
        choc::audio::io::AudioDeviceOptions audioOptions;
        CallHistory callHistory;
        cmaj::ServerOptions serverOptions;

        serverOptions.address = "127.0.0.1";
        serverOptions.port    = 8083;

        PatchPlayerServer server (serverOptions, engineOptions, buildSettings, audioOptions,
                                  [&] (const choc::audio::io::AudioDeviceOptions& options)
                                  {
                                      return std::make_unique<StubAudioMidiPlayer> (callHistory, options);
                                  });

        if (! server.isOpen())
        {
            CHOC_FAIL ("Failed to start the server");
            return;
        }

        auto serverPort = server.getPort();

        auto t = std::thread ([&]
        {
            try
            {
                // two clients of the same session
                TestClient client1 (serverOptions.address, serverPort, "transportSession");
                TestClient client2 (serverOptions.address, serverPort, "transportSession");

                auto request = [&] (TestClient& client, const std::string& text)
                {
                    return choc::value::Value (client.sendRequestAndCollectReplies (text, "transport_state")["transport_state"]);
                };

                // (for the messages that have no reply)
                auto post = [] (TestClient& client, const std::string& text)
                {
                    client.ws.write (boost::asio::buffer (text));
                    std::this_thread::sleep_for (std::chrono::milliseconds (50));
                };

                auto setTransport = [&] (const std::string& fields)
                {
                    return request (client1, R"({ "type": "set_transport", )" + fields + " }");
                };

                // the defaults, before there's a patch: 120, 4/4, playing, at frame 0
                auto state = request (client1, R"({ "type": "req_transport_state" })");
                CHOC_EXPECT_NEAR (state["tempo"].get<double>(), 120.0, 0.0001);
                CHOC_EXPECT_EQ (state["numerator"].get<int32_t>(), 4);
                CHOC_EXPECT_EQ (state["denominator"].get<int32_t>(), 4);
                CHOC_EXPECT_TRUE (state["playing"].getBool());
                CHOC_EXPECT_FALSE (state["recording"].getBool());
                CHOC_EXPECT_FALSE (state["looping"].getBool());
                CHOC_EXPECT_EQ (state["frame"].get<int64_t>(), static_cast<int64_t> (0));
                CHOC_EXPECT_NEAR (state["quarterNote"].get<double>(), 0.0, 0.0001);
                CHOC_EXPECT_NEAR (state["barStartQuarterNote"].get<double>(), 0.0, 0.0001);
                CHOC_EXPECT_TRUE (state.hasObjectMember ("sampleRate"));

                // (gives the device a known rate)
                client1.sendRequest (R"({ "type": "set_audio_device_props", "properties": { "rate": 44100, "blockSize": 64 } })");

                // a partial update changes only what it includes...
                state = setTransport (R"("tempo": 90)");
                CHOC_EXPECT_NEAR (state["tempo"].get<double>(), 90.0, 0.0001);
                CHOC_EXPECT_EQ (state["numerator"].get<int32_t>(), 4);
                CHOC_EXPECT_TRUE (state["playing"].getBool());
                CHOC_EXPECT_NEAR (state["sampleRate"].get<double>(), 44100.0, 0.0001);

                state = setTransport (R"("numerator": 7, "denominator": 8, "recording": true)");
                CHOC_EXPECT_NEAR (state["tempo"].get<double>(), 90.0, 0.0001);
                CHOC_EXPECT_EQ (state["numerator"].get<int32_t>(), 7);
                CHOC_EXPECT_EQ (state["denominator"].get<int32_t>(), 8);
                CHOC_EXPECT_TRUE (state["recording"].getBool());
                CHOC_EXPECT_TRUE (state["playing"].getBool());     // (recording doesn't clear it)

                client2.clearReceivedMessages();
                state = setTransport (R"("looping": true, "playing": false)");
                CHOC_EXPECT_TRUE (state["looping"].getBool());
                CHOC_EXPECT_FALSE (state["playing"].getBool());
                CHOC_EXPECT_TRUE (state["recording"].getBool());

                // ...and is broadcast to every client of the session
                std::this_thread::sleep_for (std::chrono::milliseconds (100));
                CHOC_EXPECT_EQ (client2.countReceivedMessages ("transport_state"), static_cast<size_t> (1));

                // the position is in frames: 1 second at 90 bpm is 1.5 quarter-notes, and 7/8 bars are 3.5 long
                state = setTransport (R"("position": 44100)");
                CHOC_EXPECT_EQ (state["frame"].get<int64_t>(), static_cast<int64_t> (44100));
                CHOC_EXPECT_NEAR (state["quarterNote"].get<double>(), 1.5, 0.0001);
                CHOC_EXPECT_NEAR (state["barStartQuarterNote"].get<double>(), 0.0, 0.0001);

                state = setTransport (R"("position": 176400)");
                CHOC_EXPECT_NEAR (state["quarterNote"].get<double>(), 6.0, 0.0001);
                CHOC_EXPECT_NEAR (state["barStartQuarterNote"].get<double>(), 3.5, 0.0001);

                state = setTransport (R"("rewind": true, "position": 1000)");
                CHOC_EXPECT_EQ (state["frame"].get<int64_t>(), static_cast<int64_t> (0));

                // clamping, and ignoring values of the wrong type
                state = setTransport (R"("tempo": 5000, "numerator": 0, "denominator": 12, "playing": 1, "recording": "no")");
                CHOC_EXPECT_NEAR (state["tempo"].get<double>(), 999.0, 0.0001);
                CHOC_EXPECT_EQ (state["numerator"].get<int32_t>(), 1);
                CHOC_EXPECT_EQ (state["denominator"].get<int32_t>(), 8);
                CHOC_EXPECT_FALSE (state["playing"].getBool());
                CHOC_EXPECT_TRUE (state["recording"].getBool());

                state = setTransport (R"("tempo": -3, "numerator": 1000, "denominator": 100, "position": -50)");
                CHOC_EXPECT_NEAR (state["tempo"].get<double>(), 1.0, 0.0001);
                CHOC_EXPECT_EQ (state["numerator"].get<int32_t>(), 64);
                CHOC_EXPECT_EQ (state["denominator"].get<int32_t>(), 32);
                CHOC_EXPECT_EQ (state["frame"].get<int64_t>(), static_cast<int64_t> (0));

                state = setTransport (R"("tempo": "fast", "denominator": 0.4)");
                CHOC_EXPECT_NEAR (state["tempo"].get<double>(), 1.0, 0.0001);
                CHOC_EXPECT_EQ (state["denominator"].get<int32_t>(), 1);

                setTransport (R"("tempo": 120, "numerator": 4, "denominator": 4, "recording": false, "looping": false)");

                // periodic updates: an immediate reply, then nothing while stopped...
                state = request (client1, R"({ "type": "set_transport_info_rate", "intervalMs": 50 })");
                CHOC_EXPECT_FALSE (state["playing"].getBool());
                CHOC_EXPECT_EQ (client1.countMessagesDuring ("transport_state", 400), static_cast<size_t> (0));

                // ...then every interval while playing, to every client...
                setTransport (R"("playing": true)");
                auto numWhilePlaying = client2.countMessagesDuring ("transport_state", 600);
                CHOC_EXPECT_TRUE (numWhilePlaying >= 5 && numWhilePlaying <= 14);

                // ...and once more when it stops (after the reply to the change)
                client1.clearReceivedMessages();
                setTransport (R"("playing": false)");
                std::this_thread::sleep_for (std::chrono::milliseconds (300));
                auto numAfterStopping = client1.countReceivedMessages ("transport_state");
                CHOC_EXPECT_TRUE (numAfterStopping >= 2 && numAfterStopping <= 3);
                CHOC_EXPECT_EQ (client1.countMessagesDuring ("transport_state", 300), static_cast<size_t> (0));

                // the clients' requests combine as the shortest interval; 0 withdraws one, and
                // intervals are clamped to 30-2000 ms
                request (client2, R"({ "type": "set_transport_info_rate", "intervalMs": 1000 })");
                post (client1, R"({ "type": "set_transport_info_rate", "intervalMs": 0 })");
                setTransport (R"("playing": true)");
                CHOC_EXPECT_TRUE (client1.countMessagesDuring ("transport_state", 600) <= 1);

                request (client1, R"({ "type": "set_transport_info_rate", "intervalMs": 1 })");
                auto numAtFastestRate = client1.countMessagesDuring ("transport_state", 600);
                CHOC_EXPECT_TRUE (numAtFastestRate >= 8 && numAtFastestRate <= 22);

                post (client1, R"({ "type": "set_transport_info_rate", "intervalMs": 0 })");
                post (client2, R"({ "type": "set_transport_info_rate", "intervalMs": 0 })");
                CHOC_EXPECT_EQ (client1.countMessagesDuring ("transport_state", 300), static_cast<size_t> (0));

                // the settings belong to the session, so a new player gets them
                setTransport (R"("tempo": 77, "numerator": 3, "denominator": 8, "looping": true)");

                if (auto session = server.findSession ("transportSession"))
                {
                    std::promise<void> playerDeleted;

                    choc::messageloop::postMessage ([&]
                    {
                        session->view.reset();
                        session->patchPlayer.reset();
                        playerDeleted.set_value();
                    });

                    playerDeleted.get_future().wait();

                    state = request (client1, R"({ "type": "req_transport_state" })");
                    CHOC_EXPECT_NEAR (state["tempo"].get<double>(), 77.0, 0.0001);
                    CHOC_EXPECT_EQ (state["numerator"].get<int32_t>(), 3);
                    CHOC_EXPECT_EQ (state["denominator"].get<int32_t>(), 8);
                    CHOC_EXPECT_TRUE (state["looping"].getBool());
                    CHOC_EXPECT_TRUE (state["playing"].getBool());

                    if (session->patchPlayer != nullptr)
                    {
                        auto info = session->patchPlayer->getTransportInfo();
                        CHOC_EXPECT_EQ (info.tempo, 77.0f);
                        CHOC_EXPECT_EQ (info.numerator, 3u);
                        CHOC_EXPECT_EQ (info.denominator, 8u);
                        CHOC_EXPECT_TRUE (info.playing);
                        CHOC_EXPECT_TRUE (info.looping);
                        CHOC_EXPECT_FALSE (info.recording);
                    }
                    else
                    {
                        CHOC_FAIL ("Expected the session to have made a new player");
                    }
                }
                else
                {
                    CHOC_FAIL ("Expected to find the session");
                }
            }
            catch (boost::system::system_error&)
            {
                CHOC_FAIL ("Failed to connect to server");
            }
            catch (choc::value::Error&)
            {
                CHOC_FAIL ("Invalid choc value");
            }

            choc::messageloop::stop();
        });

        choc::messageloop::run();

        t.join();
    }

    /// Checks that a server which is shut down while clients are still connected closes
    /// their websockets cleanly instead of abandoning them - pending operations which are
    /// left dangling keep their session (and everything it owns) alive forever
    void testServerShutdownWithLiveClients (choc::test::TestProgress& progress)
    {
        CHOC_TEST (shutdownWithLiveClients);

        choc::value::Value engineOptions;
        cmaj::BuildSettings buildSettings;
        choc::audio::io::AudioDeviceOptions audioOptions;

        CallHistory callHistory;

        cmaj::ServerOptions serverOptions;

        serverOptions.address = "127.0.0.1";
        serverOptions.port    = 8081;

        std::optional<PatchPlayerServer> server;

        server.emplace (serverOptions, engineOptions, buildSettings, audioOptions,
                        [&] (const choc::audio::io::AudioDeviceOptions& options)
                        {
                            return std::make_unique<StubAudioMidiPlayer> (callHistory, options);
                        });

        if (! server->isOpen())
        {
            CHOC_FAIL ("Failed to start the server");
            return;
        }

        auto serverPort = server->getPort();

        // NB: unlike the test above, these clients are deliberately kept connected
        // while the server is shut down
        std::unique_ptr<TestClient> client1, client2;

        auto t = std::thread ([&]
        {
            try
            {
                client1 = std::make_unique<TestClient> (serverOptions.address, serverPort, "client1");
                client2 = std::make_unique<TestClient> (serverOptions.address, serverPort, "client2");

                // make sure both clients are fully up and running before the server goes away
                auto response = client1->sendRequest ("{ \"type\": \"req_session_status\" }");
                CHOC_EXPECT_EQ (response["type"].toString(), "session_status");

                response = client2->sendRequest ("{ \"type\": \"req_session_status\" }");
                CHOC_EXPECT_EQ (response["type"].toString(), "session_status");
            }
            catch (boost::system::system_error&)
            {
                CHOC_FAIL ("Failed to connect to server");
            }
            catch (choc::value::Error&)
            {
                CHOC_FAIL ("Invalid choc value");
            }

            choc::messageloop::stop();
        });

        choc::messageloop::run();

        t.join();

        if (client1 == nullptr || client2 == nullptr)
            return;

        // Keep weak references to the sessions that the clients are using: if their websockets
        // are abandoned instead of being closed, the pending operations on them will keep their
        // client handlers - and hence these sessions - alive for ever
        std::weak_ptr<PatchPlayerServer::Session> session1 = server->findSession ("client1");
        std::weak_ptr<PatchPlayerServer::Session> session2 = server->findSession ("client2");

        CHOC_EXPECT_FALSE (session1.expired());
        CHOC_EXPECT_FALSE (session2.expired());

        auto start = std::chrono::steady_clock::now();
        server.reset();
        auto timeTaken = std::chrono::steady_clock::now() - start;

        // Nothing should be left holding onto the sessions once the server has gone
        CHOC_EXPECT_TRUE (session1.expired());
        CHOC_EXPECT_TRUE (session2.expired());

        // The closing handshakes should all complete, so this must not have needed to fall
        // back on forcibly aborting the connections after a timeout
        CHOC_EXPECT_TRUE (timeTaken < choc::network::gracefulShutdownTimeout);

        // ..and rather than just having their sockets vanish, both clients should have been
        // sent a websocket close frame to tell them that the server was going away
        CHOC_EXPECT_TRUE (client1->waitForConnectionToClose());
        CHOC_EXPECT_TRUE (client2->waitForConnectionToClose());
        CHOC_EXPECT_TRUE (client1->closeError == boost::beast::websocket::error::closed);
        CHOC_EXPECT_TRUE (client2->closeError == boost::beast::websocket::error::closed);
    }

    /// Checks that closing a server releases all of the state it was holding. Its pending
    /// asynchronous operations hold shared references back to the server itself, so if any of
    /// them are abandoned rather than being allowed to complete, the server, its io_context,
    /// its thread pool and all of its client sessions are leaked along with them.
    void testServerShutdownReleasesResources (choc::test::TestProgress& progress)
    {
        CHOC_TEST (shutdownReleasesResources);

        struct TestInstance  : public choc::network::HTTPServer::ClientInstance
        {
            TestInstance (std::atomic<bool>& flag) : websocketOpen (flag) {}

            choc::network::HTTPContent getHTTPContent (const choc::network::HTTPRequest&) override  { return {}; }
            void upgradedToWebSocket (std::string_view) override    { websocketOpen = true; }
            void handleWebSocketMessage (std::string_view) override  {}

            std::atomic<bool>& websocketOpen;
        };

        std::atomic<bool> websocketOpen { false };

        // This is owned by the function object which the server holds internally, so it stays
        // alive for exactly as long as the server's internal state does
        auto sentinel = std::make_shared<int> (0);
        std::weak_ptr<int> serverState = sentinel;

        choc::network::HTTPServer server;

        if (! server.open ("127.0.0.1", 0, 0,
                           [&websocketOpen, keepAlive = std::move (sentinel)]
                           {
                               return std::make_shared<TestInstance> (websocketOpen);
                           },
                           {}))
        {
            CHOC_FAIL ("Failed to start the server");
            return;
        }

        CHOC_EXPECT_FALSE (serverState.expired());

        {
            // Connect a client and leave its websocket open while the server is closed
            TestClient client (server.getHost(), server.getPort(), "test");

            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);

            while (! websocketOpen && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for (std::chrono::milliseconds (1));

            CHOC_EXPECT_TRUE (websocketOpen);

            server.close();

            CHOC_EXPECT_TRUE (serverState.expired());
        }
    }
}

void runServerUnitTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (Server);
    testServer (progress);
    testServerDeviceRate (progress);
    testServerAudioInputGenerator (progress);
    testServerTransport (progress);
    testServerShutdownWithLiveClients (progress);
    testServerShutdownReleasesResources (progress);
}

} // namespace cmaj
