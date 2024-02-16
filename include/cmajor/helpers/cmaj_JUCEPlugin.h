//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2022 Sound Stacks Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  Cmajor may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#pragma once

#include "cmaj_PatchWebView.h"
#include "cmaj_GeneratedCppEngine.h"
#include "../../choc/memory/choc_xxHash.h"

#include <utility>

#if JUCE_LINUX
 #define Font FontX  // Gotta love these C headers with global symbol clashes.. sigh..
 #define Time TimeX
 #define Drawable DrawableX
 #include <gtk/gtkx.h>
 #undef Font
 #undef Time
 #undef Drawable
#endif

namespace cmaj::plugin
{

//==============================================================================
/// This base class is used in creating a juce::AudioPluginInstance that either
/// JIT-compiles patches dynamically, or which is specialised to run a pre-generated
/// C++ version of a patch.
///
/// See the cmaj::plugin::JITLoaderPlugin and cmaj::plugin::GeneratedPlugin
/// types below for how to use it in these different modes.
///
template <typename EngineType>
class JUCEPluginBase  : public juce::AudioPluginInstance,
                        private juce::MessageListener
{
public:
    JUCEPluginBase (std::unique_ptr<Patch> patchToLoad)
        : juce::AudioPluginInstance (getDefaultBusLayout (patchToLoad.get())),
          patch (std::move (patchToLoad)),
          dllLoadedSuccessfully (initialiseDLL())
    {
        juce::MessageManager::callAsync ([] { choc::messageloop::initialise(); });

        if (dllLoadedSuccessfully)
        {
            patch->stopPlayback    = [this] { suspendProcessing (true); };
            patch->startPlayback   = [this] { suspendProcessing (false); };
            patch->patchChanged    = [this]
            {
                const auto executeOrDeferToMessageThread = [] (auto&& fn) -> void
                {
                    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
                        return fn();

                    juce::MessageManager::callAsync (std::forward<decltype (fn)> (fn));
                };

                executeOrDeferToMessageThread ([this] { handlePatchChange(); });
            };
            patch->statusChanged   = [this] (const auto& s) { setStatusMessage (s.statusMessage, s.messageList.hasErrors()); };

            patch->handleOutputEvent = [this] (uint64_t frame, std::string_view endpointID, const choc::value::ValueView& v)
            {
                handleOutputEvent (frame, endpointID, v);
            };

            if constexpr (EngineType::isPrecompiled)
            {
                patch->createEngine = +[] { return cmaj::createEngineForGeneratedCppProgram<typename EngineType::PerformerClass>(); };

                patch->setPlaybackParams (getPlaybackParams (44100, 128));
                setNewState (createEmptyState ({}));
            }
            else
            {
                // for a JIT plugin, we can't recreate parameter objects without hosts crashing, so
                // will just create a big flat list and re-use its parameter objects when things change
                ensureNumParameters (100);
            }
        }
        else
        {
            setStatusMessage ("Could not load the required Cmajor DLL", true);
        }
    }

    ~JUCEPluginBase() override
    {
        patch->patchChanged = [] {};
        patch->unload();
        patch.reset();
    }

    //==============================================================================
    void loadPatch (const std::filesystem::path& fileToLoad)
    {
        if (dllLoadedSuccessfully)
            setNewStateAsync (createEmptyState (fileToLoad));
    }

    void loadPatch (const PatchManifest& manifest)
    {
        if (dllLoadedSuccessfully)
        {
            Patch::LoadParams loadParams;
            loadParams.manifest = manifest;
            patch->loadPatch (loadParams);
        }
    }

    void unload()
    {
        unload ({}, false);
    }

    std::function<void(const char*)> handleConsoleMessage;
    std::function<void(JUCEPluginBase&)> patchChangeCallback;

    //==============================================================================
    const juce::String getName() const override          { return patch->getName(); }

    juce::StringArray getAlternateDisplayNames() const override
    {
        juce::StringArray s;
        s.add (patch->getName());

        if (auto n = patch->getDescription(); ! n.empty())
            s.add (n);

        return s;
    }

    juce::AudioProcessorEditor* createEditor() override   { return new Editor (*this); }
    bool hasEditor() const override                       { return true; }

    bool acceptsMidi() const override                     { return patch->hasMIDIInput() || ! patch->isLoaded(); }
    bool producesMidi() const override                    { return patch->hasMIDIOutput(); }
    bool supportsMPE() const override                     { return acceptsMidi(); }
    bool isMidiEffect() const override                    { return patch->hasMIDIInput() && ! patch->hasAudioOutput(); }
    double getTailLengthSeconds() const override          { return 0; }

    int getNumPrograms() override                               { return 1; }
    int getCurrentProgram() override                            { return 0; }
    void setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override            { return "None"; }
    void changeProgramName (int, const juce::String&) override  {}

    //==============================================================================
    static constexpr const char* getPluginFormatName()      { return "Cmajor"; }
    static constexpr const char* getIdentifierPrefix()      { return "Cmajor:"; }

    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        if (patch->isLoaded())
        {
            d.name                = patch->getName();
            d.descriptiveName     = patch->getDescription().empty() ? patch->getName() : patch->getDescription();
            d.category            = patch->getCategory();
            d.manufacturerName    = patch->getManufacturer();
            d.version             = patch->getVersion();
            d.lastFileModTime     = juce::File (patch->getPatchFile()).getLastModificationTime();
            d.isInstrument        = patch->isInstrument();
            d.uniqueId            = static_cast<int> (std::hash<std::string>{} (patch->getUID()));
        }
        else
        {
            d.name                = "Cmajor Patch-loader";
            d.descriptiveName     = d.name;
            d.category            = {};
            d.manufacturerName    = "Sound Stacks Ltd.";
            d.version             = {};
            d.lastFileModTime     = {};
            d.isInstrument        = true;
            d.uniqueId            = {};
        }

        d.fileOrIdentifier    = createPatchID (*patch);
        d.pluginFormatName    = getPluginFormatName();
        d.lastInfoUpdateTime  = juce::Time::getCurrentTime();
        d.deprecatedUid       = d.uniqueId;
    }

    static std::string createPatchID (const PatchManifest& m)
    {
        return getIdentifierPrefix()
                 + choc::json::toString (choc::json::create ("ID", m.ID,
                                                             "name", m.name,
                                                             "location", m.manifestFile),
                                         false);
    }

    static std::string createPatchID (const Patch& p)
    {
        if (auto m = p.getManifest())
            return createPatchID (*m);

        return getIdentifierPrefix() + std::string ("{}");
    }

    static bool isCmajorIdentifier (const juce::String& fileOrIdentifier)
    {
        return fileOrIdentifier.startsWith (getIdentifierPrefix());
    }

    static choc::value::Value getPropertyFromPluginID (const juce::String& fileOrIdentifier, std::string_view property)
    {
        if (isCmajorIdentifier (fileOrIdentifier))
        {
            try
            {
                auto json = choc::json::parse (fileOrIdentifier.fromFirstOccurrenceOf (getIdentifierPrefix(), false, true).toStdString());
                return choc::value::Value (json[property]);
            }
            catch (...) {}
        }

        return {};
    }

    static std::filesystem::path getFileFromPluginID (const juce::String& fileOrIdentifier)
    {
        auto file = getPropertyFromPluginID (fileOrIdentifier, "location");
        return file.getWithDefault<std::string> (fileOrIdentifier.toStdString());
    }

    static std::string getIDFromPluginID (const juce::String& fileOrIdentifier)
    {
        return getPropertyFromPluginID (fileOrIdentifier, "ID").toString();
    }

    static std::string getNameFromPluginID (const juce::String& fileOrIdentifier)
    {
        return getPropertyFromPluginID (fileOrIdentifier, "name").toString();
    }

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        if (dllLoadedSuccessfully)
            patch->setPlaybackParams (getPlaybackParams (sampleRate, static_cast<uint32_t> (samplesPerBlock)));
    }

    void releaseResources() override
    {
    }

    static bool isLayoutOK (const juce::Array<BusProperties>& patchLayouts,
                            const juce::Array<juce::AudioChannelSet>& suggestedLayouts)
    {
        if (patchLayouts.isEmpty())
            return suggestedLayouts.isEmpty() || suggestedLayouts.getReference(0).size() == 0;

        for (int i = 0; i < juce::jmin (patchLayouts.size(), suggestedLayouts.size()); ++i)
            if (patchLayouts.getReference(i).defaultLayout.size() != suggestedLayouts.getReference(i).size())
                return false;

        return true;
    }

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        if (! patch->isLoaded())
            return true;

        auto patchBuses = getBusesProperties (patch->getInputEndpoints(),
                                              patch->getOutputEndpoints());

        return isLayoutOK (patchBuses.inputLayouts, layout.inputBuses)
            && isLayoutOK (patchBuses.outputLayouts, layout.outputBuses);
    }

    bool applyBusLayouts (const BusesLayout& layouts) override
    {
        auto result = juce::AudioPluginInstance::applyBusLayouts (layouts);
        patch->setPlaybackParams (getPlaybackParams (getSampleRate(), static_cast<uint32_t> (getBlockSize())));
        return result;
    }

    void processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override
    {
        if (! patch->isPlayable() || isSuspended())
        {
            audio.clear();
            midi.clear();
            return;
        }

        juce::ScopedNoDenormals noDenormals;

        if (auto ph = getPlayHead())
            updateTimelineFromPlayhead (*ph);

        auto audioChannels = audio.getArrayOfWritePointers();
        auto numFrames = static_cast<choc::buffer::FrameCount> (audio.getNumSamples());

        for (auto m : midi)
            patch->addMIDIMessage (m.samplePosition, m.data, static_cast<uint32_t> (m.numBytes));

        midi.clear();

        patch->process (audioChannels, numFrames,
                        [&] (uint32_t frame, choc::midi::ShortMessage m)
                        {
                            midi.addEvent (m.data, m.length(), static_cast<int> (frame));
                        });
    }

    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override { CMAJ_ASSERT_FALSE; }

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& data) override
    {
        juce::MemoryOutputStream m (data, false);
        getUpdatedState().writeToStream (m);
    }

    void setStateInformation (const void* data, int size) override
    {
        choc::hash::xxHash64 hash (1);
        hash.addInput (data, static_cast<size_t> (size));
        auto stateHash = hash.getHash();

        if (lastLoadedStateHash != stateHash)
        {
            lastLoadedStateHash = stateHash;
            setNewStateAsync (juce::ValueTree::readFromData (data, static_cast<size_t> (size)));
        }
    }

    std::unique_ptr<Patch> patch;
    std::string statusMessage;
    bool isStatusMessageError = false;
    bool dllLoadedSuccessfully = false;

private:
    uint64_t lastLoadedStateHash = 0;

    //==============================================================================
    static bool initialiseDLL()
    {
        if constexpr (! EngineType::isPrecompiled)
        {
            static bool initialised = false;

            if (initialised)
                return true;

            auto tryLoading = [&] (const juce::File& dll)
            {
                if (dll.existsAsFile())
                    initialised = cmaj::Library::initialise (dll.getFullPathName().toStdString());

                return initialised;
            };

            auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            auto dllName = cmaj::Library::getDLLName();

           #if CHOC_OSX
            auto bundleFolder = juce::File::getSpecialLocation (juce::File::currentApplicationFile);

            return tryLoading (bundleFolder.getChildFile ("Contents/Resources").getChildFile (dllName))
                        || tryLoading (exe.getSiblingFile (dllName))
                        || tryLoading (bundleFolder.getSiblingFile (dllName));
           #else
            return tryLoading (exe.getSiblingFile (dllName));
           #endif
        }
        else
        {
            return true;
        }
    }

    //==============================================================================
    Patch::PlaybackParams getPlaybackParams (double rate, uint32_t requestedBlockSize)
    {
        auto layout = getBusesLayout();

        return Patch::PlaybackParams (rate, requestedBlockSize,
                                      static_cast<choc::buffer::ChannelCount> (layout.getMainInputChannels()),
                                      static_cast<choc::buffer::ChannelCount> (layout.getMainOutputChannels()));
    }

    void unload (const std::string& message, bool isError)
    {
        if constexpr (! EngineType::isPrecompiled)
        {
            patch->unload();
            setStatusMessage (message, isError);
        }
    }

    static BusesProperties getBusesProperties (const EndpointDetailsList& inputs,
                                               const EndpointDetailsList& outputs)
    {
        BusesProperties layout;

        for (auto& input : inputs)
            if (auto chans = input.getNumAudioChannels())
                layout.addBus (true, input.endpointID.toString(), juce::AudioChannelSet::canonicalChannelSet ((int) chans), true);

        for (auto& output : outputs)
            if (auto chans = output.getNumAudioChannels())
                layout.addBus (false, output.endpointID.toString(), juce::AudioChannelSet::canonicalChannelSet ((int) chans), true);

        return layout;
    }

    static BusesProperties getDefaultBusLayout (Patch* p)
    {
        if constexpr (EngineType::isPrecompiled)
        {
            auto programDetailsJSON = choc::json::parse (EngineType::PerformerClass::programDetailsJSON);

            return getBusesProperties (EndpointDetailsList::fromJSON (programDetailsJSON["inputs"], true),
                                       EndpointDetailsList::fromJSON (programDetailsJSON["outputs"], false));
        }
        else
        {
            if constexpr (EngineType::isFixedPatch)
            {
                return getBusesProperties (p->getInputEndpoints(),
                                           p->getOutputEndpoints());
            }

            BusesProperties layout;
            layout.addBus (true,  "Input",  juce::AudioChannelSet::stereo(), true);
            layout.addBus (false, "Output", juce::AudioChannelSet::stereo(), true);
            return layout;
        }
    }

    void handlePatchChange()
    {
        auto changes = AudioProcessorListener::ChangeDetails::getDefaultFlags();

        auto newLatency = (int) patch->getFramesLatency();

        changes.latencyChanged           = newLatency != getLatencySamples();
        changes.parameterInfoChanged     = updateParameters();
        changes.programChanged           = false;
        changes.nonParameterStateChanged = true;

        setLatencySamples (newLatency);
        notifyEditorPatchChanged();
        updateHostDisplay (changes);

        if (patchChangeCallback)
            patchChangeCallback (*this);
    }

    void setStatusMessage (const std::string& newMessage, bool isError)
    {
        if (statusMessage != newMessage || isStatusMessageError != isError)
        {
            statusMessage = newMessage;
            isStatusMessageError = isError;
            notifyEditorStatusMessageChanged();
        }
    }

    void notifyEditorStatusMessageChanged()
    {
        if (auto e = dynamic_cast<Editor*> (getActiveEditor()))
            e->onStatusMessageChanged();
    }

    void notifyEditorPatchChanged()
    {
        if (auto* e = dynamic_cast<Editor*> (getActiveEditor()))
            e->onPatchChanged();
    }

    //==============================================================================
    juce::ValueTree createEmptyState (std::filesystem::path location) const
    {
        juce::ValueTree state (ids.Cmajor);
        state.setProperty (ids.location, juce::String (location.string()), nullptr);
        return state;
    }

    juce::ValueTree getUpdatedState()
    {
        auto state = createEmptyState (patch->getPatchFile());

        if (isViewResizable() && lastEditorWidth != 0 && lastEditorHeight != 0)
        {
            state.setProperty (ids.viewWidth, lastEditorWidth, nullptr);
            state.setProperty (ids.viewHeight, lastEditorHeight, nullptr);
        }

        if (const auto& values = patch->getStoredStateValues(); ! values.empty())
        {
            juce::ValueTree stateValues (ids.STATE);

            for (auto& v : values)
            {
                juce::ValueTree value (ids.VALUE);
                value.setProperty (ids.key,   juce::String (v.first.data(),  v.first.length()), nullptr);
                value.setProperty (ids.value, juce::String (choc::json::toString (v.second)), nullptr);
                stateValues.appendChild (value, nullptr);
            }

            state.appendChild (stateValues, nullptr);
        }

        juce::ValueTree paramList (ids.PARAMS);

        for (auto& p : patch->getParameterList())
            paramList.appendChild (juce::ValueTree (ids.PARAM,
                                                    { { ids.ID, juce::String (p->properties.endpointID) },
                                                      { ids.V, p->currentValue } }),
                                   nullptr);

        state.appendChild (paramList, nullptr);
        return state;
    }

    void setNewStateAsync (juce::ValueTree&& newState)
    {
        auto m = std::make_unique<NewStateMessage>();
        m->newState = std::move (newState);
        postMessage (m.release());
    }

    void setNewState (const juce::ValueTree& newState)
    {
        if (! dllLoadedSuccessfully)
            return;

        Patch::LoadParams loadParams;
        bool reloadParams = false;

        if constexpr (EngineType::isPrecompiled)
        {
            loadParams.manifest = EngineType::createManifest();

            reloadParams = newState.isValid() && newState.hasType (ids.Cmajor);
        }
        else
        {
            if (! newState.isValid())
                return unload();

            if (! newState.hasType (ids.Cmajor))
                return unload ("Failed to load: invalid state", true);

            auto location = newState.getProperty (ids.location).toString().toStdString();

            if (location.empty())
                return unload();

            try
            {
                loadParams.manifest.initialiseWithFile (location);
            }
            catch (const std::runtime_error& e)
            {
                return unload (e.what(), true);
            }

            reloadParams = ! patch->isLoaded() || loadParams.manifest.manifestFile == patch->getPatchFile();
        }

        if (reloadParams)
            if (auto params = newState.getChildWithName (ids.PARAMS); params.isValid())
                for (auto param : params)
                    if (auto endpointIDProp = param.getPropertyPointer (ids.ID))
                        if (auto endpointID = endpointIDProp->toString().toStdString(); ! endpointID.empty())
                            if (auto valProp = param.getPropertyPointer (ids.V))
                                loadParams.parameterValues[endpointID] = static_cast<float> (*valProp);

        if (isViewResizable())
        {
            if (auto w = newState.getPropertyPointer (ids.viewWidth))
                if (w->isInt())
                    lastEditorWidth = *w;

            if (auto h = newState.getPropertyPointer (ids.viewHeight))
                if (h->isInt())
                    lastEditorHeight = *h;
        }
        else
        {
            lastEditorWidth = 0;
            lastEditorHeight = 0;
        }

        if (auto state = newState.getChildWithName (ids.STATE); state.isValid())
        {
            for (const auto& v : state)
            {
                if (v.hasType (ids.VALUE))
                {
                    if (auto key = v.getPropertyPointer (ids.key))
                    {
                        if (auto value = v.getPropertyPointer (ids.value))
                        {
                            if (key->isString() && key->toString().isNotEmpty() && ! value->isVoid())
                                patch->setStoredStateValue (key->toString().toStdString(), choc::json::parse (value->toString().toStdString()));
                        }
                    }
                }
            }
        }

        patch->loadPatch (loadParams);
    }

    bool isViewResizable() const
    {
        if (auto manifest = patch->getManifest())
            for (auto& v : manifest->views)
                if (! v.isResizable())
                    return false;

        return true;
    }

    struct NewStateMessage  : public juce::Message
    {
        juce::ValueTree newState;
    };

    void handleMessage (const juce::Message& message) override
    {
        if (auto m = dynamic_cast<const NewStateMessage*> (&message))
            setNewState (const_cast<NewStateMessage*> (m)->newState);
    }

    void handleOutputEvent (uint64_t, std::string_view endpointID, const choc::value::ValueView& value)
    {
        if (endpointID == cmaj::getConsoleEndpointID())
        {
            auto text = cmaj::convertConsoleMessageToString (value);

            if (handleConsoleMessage != nullptr)
                handleConsoleMessage (text.c_str());
            else
                std::cout << text << std::flush;
        }
    }

    //==============================================================================
    void updateTimelineFromPlayhead (juce::AudioPlayHead& ph)
    {
        if (patch->wantsTimecodeEvents())
        {
            if (auto pos = ph.getPosition())
            {
                uint32_t timeout = 0;

                if (auto timeSig = pos->getTimeSignature())
                    patch->sendTimeSig (timeSig->numerator, timeSig->denominator, timeout);

                if (auto bpm = pos->getBpm())
                    patch->sendBPM (static_cast<float> (*bpm), timeout);

                patch->sendTransportState (pos->getIsRecording(),
                                           pos->getIsPlaying(),
                                           pos->getIsLooping(),
                                           timeout);

                if (auto timeSamps = pos->getTimeInSamples())
                {
                    double ppq = 0, ppqBar = 0;

                    if (auto p = pos->getPpqPosition())
                        ppq = *p;

                    if (auto p = pos->getPpqPositionOfLastBarStart())
                        ppqBar = *p;

                    patch->sendPosition (static_cast<int64_t> (*timeSamps), ppq, ppqBar, timeout);
                }
            }
        }
    }

    //==============================================================================
    struct Parameter  : public juce::HostedAudioProcessorParameter
    {
        Parameter (juce::String&& pID)
            : HostedAudioProcessorParameter (1),
              paramID (std::move (pID))
        {
        }

        ~Parameter() override
        {
            detach();
        }

        bool setPatchParam (PatchParameterPtr p)
        {
            if (patchParam == p)
                return false;

            detach();
            patchParam = std::move (p);

            patchParam->valueChanged = [this] (float v)
            {
                sendValueChangedMessageToListeners (patchParam->properties.convertTo0to1 (v));
            };

            patchParam->gestureStart = [this] { beginChangeGesture(); };
            patchParam->gestureEnd   = [this] { endChangeGesture(); };
            return true;
        }

        void detach()
        {
            if (patchParam != nullptr)
            {
                patchParam->valueChanged = [] (float) {};
                patchParam->gestureStart = [] {};
                patchParam->gestureEnd   = [] {};
            }
        }

        void forceValueChanged()
        {
            if (patchParam != nullptr)
                patchParam->valueChanged (patchParam->currentValue);
        }

        juce::String getParameterID() const override                { return paramID; }
        juce::String getName (int maxLength) const override         { return patchParam == nullptr ? "unknown" : patchParam->properties.name.substr (0, (size_t) maxLength); }
        juce::String getLabel() const override                      { return patchParam == nullptr ? juce::String() : patchParam->properties.unit; }
        Category getCategory() const override                       { return Category::genericParameter; }
        bool isDiscrete() const override                            { return patchParam != nullptr && patchParam->properties.discrete; }
        bool isBoolean() const override                             { return patchParam != nullptr && patchParam->properties.boolean; }
        bool isAutomatable() const override                         { return patchParam == nullptr || patchParam->properties.automatable; }
        bool isMetaParameter() const override                       { return patchParam != nullptr && patchParam->properties.hidden; }

        juce::StringArray getAllValueStrings() const override
        {
            juce::StringArray result;

            if (patchParam != nullptr)
                for (auto& s : patchParam->properties.valueStrings)
                    result.add (s);

            return result;
        }

        float getDefaultValue() const override       { return patchParam != nullptr ? patchParam->properties.convertTo0to1 (patchParam->properties.defaultValue) : 0.0f; }
        float getValue() const override              { return patchParam != nullptr ? patchParam->properties.convertTo0to1 (patchParam->currentValue) : 0.0f; }
        void setValue (float newValue) override      { if (patchParam != nullptr) patchParam->setValue (patchParam->properties.convertFrom0to1 (newValue), false, -1, 0); }

        juce::String getText (float v, int length) const override
        {
            if (patchParam == nullptr)
                return "0";

            juce::String result = patchParam->properties.getValueAsString (patchParam->properties.convertFrom0to1 (v));
            return length > 0 ? result.substring (0, length) : result;
        }

        float getValueForText (const juce::String& text) const override
        {
            if (patchParam != nullptr)
            {
                if (auto value = patchParam->properties.getStringAsValue (text.toStdString()))
                    return *value;

                return patchParam->properties.defaultValue;
            }

            return 0;
        }

        int getNumSteps() const override
        {
            if (patchParam != nullptr)
                if (auto steps = patchParam->properties.getNumDiscreteOptions())
                    return static_cast<int> (steps);

            return AudioProcessor::getDefaultNumParameterSteps();
        }

        PatchParameterPtr patchParam;
        const juce::String paramID;
    };

    static constexpr bool useFixedParamTree = EngineType::isPrecompiled || EngineType::isFixedPatch;

    void createParameterTree()
    {
        // for a precompiled plugin, we can build a complete group structure
        if constexpr (useFixedParamTree)
        {
            struct ParameterTreeBuilder
            {
                Parameter* add (const PatchParameterPtr& param)
                {
                    auto newParam = std::make_unique<Parameter> (param->properties.endpointID);
                    auto rawParam = newParam.get();

                    if (! param->properties.group.empty())
                        getOrCreateGroup (tree, {}, param->properties.group).addChild (std::move (newParam));
                    else
                        tree.addChild (std::move (newParam));

                    return rawParam;
                }

                juce::AudioProcessorParameterGroup& getOrCreateGroup (juce::AudioProcessorParameterGroup& targetTree,
                                                                      const std::string& parentPath,
                                                                      const std::string& subPath)
                {
                    auto fullPath = parentPath + "/" + subPath;
                    auto& targetGroup = groups[fullPath];

                    if (targetGroup != nullptr)
                        return *targetGroup;

                    if (auto slash = subPath.find ('/'); slash != std::string::npos)
                    {
                        auto firstPathPart = subPath.substr (0, slash);
                        auto& parentGroup = getOrCreateGroup (targetTree, parentPath, firstPathPart);
                        return getOrCreateGroup (parentGroup, parentPath + "/" + firstPathPart, subPath.substr (slash + 1));
                    }

                    auto newGroup = std::make_unique<juce::AudioProcessorParameterGroup> (fullPath, subPath, "/");
                    targetGroup = newGroup.get();
                    targetTree.addChild (std::move (newGroup));
                    return *targetGroup;
                }

                std::map<std::string, juce::AudioProcessorParameterGroup*> groups;
                juce::AudioProcessorParameterGroup tree;
            };

            ParameterTreeBuilder builder;

            for (auto& p : patch->getParameterList())
            {
                auto param = builder.add (p);
                parameters.push_back (param);
                param->setPatchParam (p);
            }

            for (auto p : parameters)
                p->forceValueChanged();

            setHostedParameterTree (std::move (builder.tree));
        }
    }

    bool updateParameters()
    {
        bool changed = false;
        auto params = patch->getParameterList();

        if constexpr (useFixedParamTree)
        {
            if (parameters.empty())
                createParameterTree();
        }
        else
        {
            ensureNumParameters (params.size());
        }

        for (size_t i = 0; i < params.size(); ++i)
            changed = parameters[i]->setPatchParam (params[i]) || changed;

        return changed;
    }

    void ensureNumParameters (size_t num)
    {
        while (parameters.size() < num)
        {
            auto p = std::make_unique<Parameter> ("P" + juce::String (parameters.size()));
            parameters.push_back (p.get());
            addHostedParameter (std::move (p));
        }
    }

    std::vector<Parameter*> parameters;

    //==============================================================================
    //==============================================================================
    struct Editor  : public juce::AudioProcessorEditor
    {
        Editor (JUCEPluginBase& p)
            : juce::AudioProcessorEditor (p), owner (p),
              patchGUIHolder (PatchWebView::create (*p.patch, derivePatchViewSize (p)))
        {
            lookAndFeel.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            lookAndFeel.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
            setLookAndFeel (&lookAndFeel);

            onPatchChanged (false);

            addAndMakeVisible (extraComp);
            onStatusMessageChanged();

            juce::Font::setDefaultMinimumHorizontalScaleFactor (1.0f);
        }

        ~Editor() override
        {
            owner.editorBeingDeleted (this);
            setLookAndFeel (nullptr);
        }

        void onStatusMessageChanged()
        {
            extraComp.refresh();
        }

        static cmaj::PatchManifest::View derivePatchViewSize (const JUCEPluginBase& owner)
        {
            auto view = cmaj::PatchManifest::View
            {
                choc::json::create ("width", owner.lastEditorWidth,
                                    "height", owner.lastEditorHeight)
            };

            if (auto manifest = owner.patch->getManifest())
                if (auto v = manifest->findDefaultView())
                    view = *v;

            if (view.getWidth()  == 0)  view.view.setMember ("width", defaultWidth);
            if (view.getHeight() == 0)  view.view.setMember ("height", defaultHeight);

            return view;
        }

        void onPatchChanged (bool forceReload = true)
        {
            const auto loaded = owner.patch->isPlayable();

            if (loaded)
            {
                patchGUIHolder.patchView->setActive (true);
                patchGUIHolder.update (derivePatchViewSize (owner));

                setResizable (patchGUIHolder.patchView->resizable, false);

                addAndMakeVisible (patchGUIHolder);
                childBoundsChanged (nullptr);
            }
            else
            {
                removeChildComponent (std::addressof (patchGUIHolder));

                patchGUIHolder.patchView->setActive (false);
                patchGUIHolder.setVisible (false);

                setSize (defaultWidth, defaultHeight);
                setResizable (true, false);
            }

            if (forceReload)
                patchGUIHolder.patchView->reload();
        }

        void childBoundsChanged (Component*) override
        {
            if (! isResizing && patchGUIHolder.isVisible())
                setSize (std::max (50, patchGUIHolder.getWidth()),
                         std::max (50, patchGUIHolder.getHeight() + extraComp.height));
        }

        void resized() override
        {
            isResizing = true;
            juce::AudioProcessorEditor::resized();

            auto r = getLocalBounds();

            if (patchGUIHolder.isVisible())
            {
                patchGUIHolder.setBounds (r.removeFromTop (getHeight() - extraComp.height));
                r.removeFromTop (4);

                if (getWidth() > 0 && getHeight() > 0)
                {
                    owner.lastEditorWidth = patchGUIHolder.getWidth();
                    owner.lastEditorHeight = patchGUIHolder.getHeight();
                }
            }

            extraComp.setBounds (r);
            isResizing = false;
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
        }

        //==============================================================================
       #if JUCE_MAC
        using NativeUIBase = juce::NSViewComponent;
       #elif JUCE_IOS
        using NativeUIBase = juce::UIViewComponent;
       #elif JUCE_WINDOWS
        using NativeUIBase = juce::HWNDComponent;
       #else
        using NativeUIBase = juce::XEmbedComponent;
       #endif

        struct PatchGUIHolder  : public NativeUIBase
        {
            PatchGUIHolder (std::unique_ptr<PatchWebView> webView) :
               #if JUCE_LINUX
                juce::XEmbedComponent (getWindowID (*webView), true, false),
               #endif
                patchView (std::move (webView))
            {
                setSize ((int) patchView->width, (int) patchView->height);

               #if JUCE_MAC || JUCE_IOS
                setView (patchView->getWebView().getViewHandle());
               #elif JUCE_WINDOWS
                setHWND (patchView->getWebView().getViewHandle());
               #endif
            }

            ~PatchGUIHolder()
            {
               #if JUCE_MAC || JUCE_IOS
                setView ({});
               #elif JUCE_WINDOWS
                setHWND ({});
               #elif JUCE_LINUX
                removeClient();
               #endif
            }

           #if JUCE_LINUX
            static unsigned long getWindowID (PatchWebView& v)
            {
                auto childWidget = GTK_WIDGET (v.getWebView().getViewHandle());
                auto plug = gtk_plug_new (0);
                gtk_container_add (GTK_CONTAINER (plug), childWidget);
                gtk_widget_show_all (plug);
                return gtk_plug_get_id (GTK_PLUG (plug));
            }
           #endif

            void update (const PatchManifest::View& view)
            {
                patchView->update (view);
                setSize ((int) patchView->width, (int) patchView->height);
            }

            std::unique_ptr<PatchWebView> patchView;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchGUIHolder)
        };

        //==============================================================================
        JUCEPluginBase& owner;
        typename EngineType::template ExtraEditorComponent<JUCEPluginBase> extraComp { owner };

        PatchGUIHolder patchGUIHolder;
        juce::LookAndFeel_V4 lookAndFeel;
        bool isResizing = false;

        static constexpr int defaultWidth = 500, defaultHeight = 400;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
    };

    int lastEditorWidth = 0, lastEditorHeight = 0;

    //==============================================================================
    struct IDs
    {
        const juce::Identifier Cmajor     { "Cmajor" },
                               PARAMS     { "PARAMS" },
                               PARAM      { "PARAM" },
                               ID         { "ID" },
                               V          { "V" },
                               STATE      { "STATE" },
                               VALUE      { "VALUE" },
                               location   { "location" },
                               key        { "key" },
                               value      { "value" },
                               viewWidth  { "viewWidth" },
                               viewHeight { "viewHeight" };
    } ids;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JUCEPluginBase)
};


//==============================================================================
/// Used along with JUCEPluginBase to create a JIT-compiling JUCE plugin class.
struct JUCEPluginType_DynamicJIT
{
    static constexpr bool isPrecompiled = false;
    static constexpr bool isFixedPatch = false;

    template <typename Plugin>
    struct ExtraEditorComponent  : public juce::Component,
                                   public juce::FileDragAndDropTarget
    {
        ExtraEditorComponent (Plugin& p) : plugin (p)
        {
            messageBox.setMultiLine (true);
            messageBox.setReadOnly (true);

            unloadButton.onClick = [this] { plugin.unload(); };

            addAndMakeVisible (messageBox);
            addAndMakeVisible (unloadButton);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4);
            messageBox.setBounds (r);
            unloadButton.setBounds (r.removeFromTop (30).removeFromRight (80));
        }

        void refresh()
        {
            unloadButton.setVisible (plugin.patch->isLoaded());

            juce::Font f (18.0f);
            f.setTypefaceName (juce::Font::getDefaultMonospacedFontName());
            messageBox.setFont (f);

            auto text = plugin.statusMessage;

            if (text.empty())
                text = "Drag-and-drop a .cmajorpatch file here to load it";

            messageBox.setText (text);
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (isDragOver)
                g.fillAll (juce::Colours::lightgreen.withAlpha (0.3f));
        }

        bool isInterestedInFileDrag (const juce::StringArray& files) override
        {
            return files.size() == 1 && files[0].endsWith (".cmajorpatch");
        }

        void fileDragEnter (const juce::StringArray&, int, int) override       { setDragOver (true); }
        void fileDragExit (const juce::StringArray&) override                  { setDragOver (false); }

        void filesDropped (const juce::StringArray& files, int, int) override
        {
            setDragOver (false);

            if (isInterestedInFileDrag (files))
                plugin.loadPatch (files[0].toStdString());
        }

        void setDragOver (bool b)
        {
            if (isDragOver != b)
            {
                isDragOver = b;
                repaint();
            }
        }

        //==============================================================================
        Plugin& plugin;
        bool isDragOver = false;

        juce::TextEditor messageBox;
        juce::TextButton unloadButton { "Unload" };

        static constexpr int height = 50;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExtraEditorComponent)
    };
};

//==============================================================================
/// Used along with JUCEPluginBase to create a JUCE plugin class for a given single patch
struct JUCEPluginType_SinglePatchJIT
{
    static constexpr bool isPrecompiled = false;
    static constexpr bool isFixedPatch = true;

    template <typename Plugin>
    struct ExtraEditorComponent  : public juce::Component
    {
        ExtraEditorComponent (Plugin& p) : plugin (p)
        {
            messageBox.setMultiLine (true);
            messageBox.setReadOnly (true);
            addAndMakeVisible (messageBox);
        }

        void refresh()
        {
            juce::Font f (18.0f);
            f.setTypefaceName (juce::Font::getDefaultMonospacedFontName());
            messageBox.setFont (f);
            messageBox.setText (plugin.statusMessage);
        }

        void resized() override
        {
            messageBox.setBounds (getLocalBounds().reduced (4));
        }

        Plugin& plugin;
        juce::TextEditor messageBox;

        static constexpr int height = 0;
    };
};

//==============================================================================
/// Used along with JUCEPluginBase to create a JUCE plugin class that is specialised
/// for loading the C++ class specified by the GeneratedInfoClass argument.
template <typename GeneratedInfoClass>
struct JUCEPluginType_Cpp
{
    using PatchClass = GeneratedInfoClass;
    using PerformerClass = typename PatchClass::PerformerClass;
    static constexpr bool isPrecompiled = true;
    static constexpr bool isFixedPatch = true;

    static cmaj::PatchManifest createManifest()
    {
        cmaj::PatchManifest m;
        m.needsToBuildSource = false;

        m.initialiseWithVirtualFile (std::string (PatchClass::filename),
            [] (const std::string& f) -> std::shared_ptr<std::istream>
            {
                for (auto& file : PatchClass::files)
                    if (f == file.name)
                        return std::make_shared<std::istringstream> (std::string (file.content));

                return {};
            },
            [] (const std::string& name) -> std::string { return name; },
            [] (const std::string&) -> std::filesystem::file_time_type { return {}; },
            [] (const std::string& f)
            {
                for (auto& file : PatchClass::files)
                    if (f == file.name)
                        return true;

                return false;
            });

        return m;
    }

    template <typename Plugin>
    struct ExtraEditorComponent  : public juce::Component
    {
        ExtraEditorComponent (Plugin&) {}
        void refresh() {}
        static constexpr int height = 0;
    };
};

//==============================================================================
/// This class is a juce::AudioPluginInstance which runs a JIT-compiled engine.
using JITLoaderPlugin = JUCEPluginBase<JUCEPluginType_DynamicJIT>;

/// This class is a juce::AudioPluginInstance which loads a generated C++ patch
template <typename GeneratedInfoClass>
using GeneratedPlugin = JUCEPluginBase<JUCEPluginType_Cpp<GeneratedInfoClass>>;


//==============================================================================
//==============================================================================
///
/// An implementation of a juce::AudioPluginFormat that can be used to search
/// for and load patches as JUCEPluginBase plugins
///
class JUCEPluginFormat   : public juce::AudioPluginFormat
{
public:
    using PluginInstance = JUCEPluginBase<JUCEPluginType_SinglePatchJIT>;

    JUCEPluginFormat (CacheDatabaseInterface::Ptr compileCache,
                      std::function<void(PluginInstance&)> patchChangeCallbackFn)
       : cache (std::move (compileCache)),
         patchChangeCallback (std::move (patchChangeCallbackFn))
    {
    }

    static constexpr auto pluginFormatName    = "Cmajor";
    static constexpr auto patchFileExtension  = ".cmajorpatch";
    static constexpr auto patchFileWildcard   = "*.cmajorpatch";

    //==============================================================================
    juce::String getName() const override           { return pluginFormatName; }

    static bool fillInDescription (juce::PluginDescription& desc, const juce::String& fileOrIdentifier)
    {
        try
        {
            PatchManifest manifest;
            manifest.initialiseWithFile (PluginInstance::getFileFromPluginID (fileOrIdentifier));

            desc.name                = manifest.name;
            desc.descriptiveName     = manifest.description;
            desc.category            = manifest.category;
            desc.manufacturerName    = manifest.manufacturer;
            desc.version             = manifest.version;
            desc.fileOrIdentifier    = PluginInstance::createPatchID (manifest);
            desc.lastFileModTime     = juce::File (fileOrIdentifier).getLastModificationTime();
            desc.isInstrument        = manifest.isInstrument;
            desc.uniqueId            = static_cast<int> (std::hash<std::string>{} (manifest.ID));
            desc.pluginFormatName    = pluginFormatName;
            desc.lastInfoUpdateTime  = juce::Time::getCurrentTime();
            desc.deprecatedUid       = desc.uniqueId;

            return true;
        }
        catch (...) {}

        return false;
    }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                              const juce::String& fileOrIdentifier) override
    {
        auto d = std::make_unique<juce::PluginDescription>();

        if (fillInDescription (*d, fileOrIdentifier))
            results.add (std::move (d));
    }

    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override
    {
        return PluginInstance::isCmajorIdentifier (fileOrIdentifier)
                || juce::File::createFileWithoutCheckingPath (fileOrIdentifier).hasFileExtension (patchFileExtension);
    }

    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override
    {
        if (auto name = PluginInstance::getNameFromPluginID (fileOrIdentifier); ! name.empty())
            return name;

        try
        {
            PatchManifest manifest;
            manifest.initialiseWithFile (PluginInstance::getFileFromPluginID (fileOrIdentifier));
            return manifest.name;
        }
        catch (...) {}

        return juce::File::createFileWithoutCheckingPath (fileOrIdentifier).getFileNameWithoutExtension();
    }

    bool pluginNeedsRescanning (const juce::PluginDescription& desc) override
    {
        juce::PluginDescription d;

        if (fillInDescription (d, desc.fileOrIdentifier))
            return d.lastFileModTime != desc.lastFileModTime;

        return false;
    }

    bool doesPluginStillExist (const juce::PluginDescription& desc) override
    {
        return fileMightContainThisPluginType (desc.fileOrIdentifier)
                && juce::File::createFileWithoutCheckingPath (desc.fileOrIdentifier).existsAsFile();
    }

    bool canScanForPlugins() const override             { return true; }
    bool isTrivialToScan() const override               { return true; }

    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath& directoriesToSearch, bool recursive, bool) override
    {
        juce::StringArray results;

        for (int j = 0; j < directoriesToSearch.getNumPaths(); ++j)
            searchForPatches (results, directoriesToSearch[j], recursive);

        return results;
    }

    static void searchForPatches (juce::StringArray& results, const juce::File& dir, bool recursive)
    {
        for (const auto& i : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
        {
            const auto f = i.getFile();

            if (f.isDirectory())
            {
                if (recursive)
                    searchForPatches (results, f, true);
            }
            else if (f.hasFileExtension (patchFileExtension))
            {
                results.add (f.getFullPathName());
            }
        }
    }

    juce::FileSearchPath getDefaultLocationsToSearch() override
    {
        juce::FileSearchPath path;

       #if JUCE_WINDOWS
        path.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                    .getChildFile ("Common Files\\Cmajor"));
       #elif JUCE_MAC || JUCE_IOS
        path.add (juce::File ("/Library/Audio/Plug-Ins/Cmajor"));
        path.add (juce::File ("~/Library/Audio/Plug-Ins/Cmajor"));
       #endif

        return path;
    }

    void createPluginInstance (const juce::PluginDescription& desc, double initialSampleRate,
                               int initialBufferSize, PluginCreationCallback callback) override
    {
        try
        {
            PatchManifest manifest;
            manifest.initialiseWithFile (PluginInstance::getFileFromPluginID (desc.fileOrIdentifier));

            auto patch = std::make_unique<Patch> (false, true);
            patch->createEngine = +[] { return cmaj::Engine::create(); };
            patch->cache = cache;
            patch->preload (manifest);

            auto plugin = std::make_unique<PluginInstance> (std::move (patch));
            plugin->patchChangeCallback = patchChangeCallback;
            plugin->setRateAndBufferSizeDetails (initialSampleRate, initialBufferSize);
            plugin->patch->setPlaybackParams (cmaj::Patch::PlaybackParams (initialSampleRate,
                                                                           static_cast<uint32_t> (initialBufferSize),
                                                                           2, 2));

            plugin->loadPatch (manifest);

            if (! plugin->isStatusMessageError)
                callback (std::move (plugin), {});
            else
                callback ({}, plugin->statusMessage);
        }
        catch (...)
        {
            callback ({}, "Failed to load manifest");
            return;
        }
    }

    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override   { return false; }

    CacheDatabaseInterface::Ptr cache;
    std::function<void(PluginInstance&)> patchChangeCallback;
};


} // namespace cmaj::plugin
