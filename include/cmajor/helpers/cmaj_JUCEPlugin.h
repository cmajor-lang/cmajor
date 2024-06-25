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

#pragma once

#if JUCE_LINUX
 #define Font FontX  // Gotta love these C headers with global symbol clashes.. sigh..
 #define Time TimeX
 #define Drawable DrawableX
 #define Status StatusX
 #include <gtk/gtkx.h>
 #undef Font
 #undef Time
 #undef Drawable
 #undef Status
#endif

#include <utility>
#include "../../choc/memory/choc_xxHash.h"
#include "cmaj_PatchWebView.h"
#include "cmaj_GeneratedCppEngine.h"

#if CMAJ_USE_QUICKJS_WORKER
 #include "cmaj_PatchWorker_QuickJS.h"
#else
 #include "cmaj_PatchWorker_WebView.h"
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
template <typename DerivedType>
class JUCEPluginBase  : public juce::AudioPluginInstance,
                        private juce::MessageListener
{
public:
    JUCEPluginBase (std::shared_ptr<cmaj::Patch> patchToUse, BusesProperties buses)
        : juce::AudioPluginInstance (std::move (buses)),
          patch (std::move (patchToUse)),
          dllLoadedSuccessfully (initialiseDLL())
    {
        juce::MessageManager::callAsync ([] { choc::messageloop::initialise(); });

        if (! dllLoadedSuccessfully)
        {
            setStatusMessage ("Could not load the required Cmajor DLL", true);
            return;
        }

        patch->setHostDescription (std::string (getWrapperTypeDescription (wrapperType)));

        patch->stopPlayback  = [this] { suspendProcessing (true); };
        patch->startPlayback = [this] { suspendProcessing (false); };

        patch->patchChanged = [this]
        {
            const auto executeOrDeferToMessageThread = [] (auto&& fn) -> void
            {
                if (juce::MessageManager::getInstance()->isThisTheMessageThread())
                    return fn();

                juce::MessageManager::callAsync (std::forward<decltype (fn)> (fn));
            };

            executeOrDeferToMessageThread ([this] { handlePatchChange(); });
        };

        patch->statusChanged = [this] (const auto& s) { setStatusMessage (s.statusMessage, s.messageList.hasErrors()); };

        patch->handleOutputEvent = [this] (uint64_t frame, std::string_view endpointID, const choc::value::ValueView& v)
        {
            handleOutputEvent (frame, endpointID, v);
        };

       #if CMAJ_USE_QUICKJS_WORKER
        enableQuickJSPatchWorker (*patch);
       #else
        enableWebViewPatchWorker (*patch);
       #endif
    }

    ~JUCEPluginBase() override
    {
        patch->patchChanged = [] {};
        patch->unload();
        patch.reset();
    }

    //==============================================================================
    void unload()
    {
        unload ({}, false);
    }

    std::function<void(const char*)> handleConsoleMessage;
    std::function<void(DerivedType&)> patchChangeCallback;

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

    juce::AudioProcessorEditor* createEditor() override   { return new Editor (static_cast<DerivedType&> (*this)); }
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
            d.lastFileModTime     = getManifestFile (*patch).getLastModificationTime();
            d.isInstrument        = patch->isInstrument();
            d.uniqueId            = static_cast<int> (std::hash<std::string>{} (patch->getUID()));
        }
        else
        {
            d.name                = "Cmajor Patch-loader";
            d.descriptiveName     = d.name;
            d.category            = {};
            d.manufacturerName    = "Cmajor Software Ltd.";
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
                                                             "location", m.getFullPathForFile (m.manifestFile)),
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

    static juce::File getManifestFile (const Patch& p)
    {
        if (auto m = p.getManifest())
            return juce::File (m->getFullPathForFile (m->manifestFile));

        return {};
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
        applyRateAndBlockSize (sampleRate, static_cast<uint32_t> (samplesPerBlock));
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
        applyCurrentRateAndBlockSize();
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
                            midi.addEvent (m.data(), m.length(), static_cast<int> (frame));
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

    Patch::PlaybackParams getPlaybackParams (double rate, uint32_t requestedBlockSize)
    {
        auto layout = getBusesLayout();

        return Patch::PlaybackParams (rate, requestedBlockSize,
                                      static_cast<choc::buffer::ChannelCount> (layout.getMainInputChannels()),
                                      static_cast<choc::buffer::ChannelCount> (layout.getMainOutputChannels()));
    }

    void applyRateAndBlockSize (double sampleRate, uint32_t samplesPerBlock)
    {
        if (dllLoadedSuccessfully)
            patch->setPlaybackParams (getPlaybackParams (sampleRate, samplesPerBlock));
    }

    void applyCurrentRateAndBlockSize()
    {
        applyRateAndBlockSize (getSampleRate(), static_cast<uint32_t> (getBlockSize()));
    }

    std::shared_ptr<Patch> patch;
    std::string statusMessage;
    bool isStatusMessageError = false;
    bool dllLoadedSuccessfully = false;

protected:
    uint64_t lastLoadedStateHash = 0;

    //==============================================================================
    static bool initialiseDLL()
    {
        if constexpr (cmaj::Library::isUsingDLL && ! DerivedType::isPrecompiled)
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

    void unload (const std::string& message, bool isError)
    {
        if constexpr (! DerivedType::isPrecompiled)
        {
            patch->unload();
            setStatusMessage (message, isError);
        }
    }

    void handlePatchChange()
    {
        auto changes = juce::AudioProcessorListener::ChangeDetails::getDefaultFlags();

        auto newLatency = (int) patch->getFramesLatency();

        changes.latencyChanged           = newLatency != getLatencySamples();
        changes.parameterInfoChanged     = updateParameters();
        changes.programChanged           = false;
        changes.nonParameterStateChanged = true;

        setLatencySamples (newLatency);
        notifyEditorPatchChanged();
        updateHostDisplay (changes);

        if (patchChangeCallback)
            patchChangeCallback (static_cast<DerivedType&> (*this));
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
            e->statusMessageChanged();
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

        if constexpr (! DerivedType::isFixedPatch)
            state.setProperty (ids.location, juce::String (location.string()), nullptr);

        return state;
    }

    juce::ValueTree getUpdatedState()
    {
        auto state = createEmptyState (patch->getManifestFile());

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
                auto serialised = v.second.serialise();
                value.setProperty (ids.value, juce::var (serialised.data.data(), serialised.data.size()), nullptr);
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

    virtual bool prepareManifest (Patch::LoadParams&, const juce::ValueTree& newState) = 0;

    void setNewState (const juce::ValueTree& newState)
    {
        if (! dllLoadedSuccessfully)
            return;

        if (newState.isValid() && ! newState.hasType (ids.Cmajor))
            return unload ("Failed to load: invalid state", true);

        Patch::LoadParams loadParams;

        try
        {
            if (! prepareManifest (loadParams, newState))
                return unload();
        }
        catch (const std::runtime_error& e)
        {
            return unload (e.what(), true);
        }

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
                                patch->setStoredStateValue (key->toString().toStdString(), convertVarToValue (*value));
                        }
                    }
                }
            }
        }

        if (getSampleRate() > 0)
            applyCurrentRateAndBlockSize();

        patch->loadPatch (loadParams, DerivedType::isPrecompiled);
    }

    void readParametersFromState (Patch::LoadParams& loadParams, const juce::ValueTree& newState) const
    {
        if (auto params = newState.getChildWithName (ids.PARAMS); params.isValid())
            for (auto param : params)
                if (auto endpointIDProp = param.getPropertyPointer (ids.ID))
                    if (auto endpointID = endpointIDProp->toString().toStdString(); ! endpointID.empty())
                        if (auto valProp = param.getPropertyPointer (ids.V))
                            loadParams.parameterValues[endpointID] = static_cast<float> (*valProp);
    }

    static choc::value::Value convertVarToValue (const juce::var& v)
    {
        if (v.isVoid() || v.isUndefined())  return {};
        if (v.isString())                   return choc::value::createString (v.toString().toStdString());
        if (v.isBool())                     return choc::value::createBool (static_cast<bool> (v));
        if (v.isInt() || v.isInt64())       return choc::value::createInt64 (static_cast<juce::int64> (v));
        if (v.isDouble())                   return choc::value::createFloat64 (static_cast<double> (v));

        if (v.isArray())
        {
            auto a = choc::value::createEmptyArray();

            for (auto& i : *v.getArray())
                a.addArrayElement (convertVarToValue (i));
        }

        if (v.isObject())
        {
            auto json = juce::JSON::toString (v, juce::JSON::FormatOptions().withSpacing (juce::JSON::Spacing::none));
            return choc::json::parse (json.toStdString());
        }

        if (v.isBinaryData())
        {
            auto* block = v.getBinaryData();
            auto  inputData = choc::value::InputData { (unsigned char *) block->begin(), (unsigned char *) block->end() };
            return choc::value::Value::deserialise (inputData);
        }

        jassertfalse;
        return {};
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

    void createParameterTree()
    {
        // for a precompiled plugin, we can build a complete group structure
        if constexpr (DerivedType::isPrecompiled || DerivedType::isFixedPatch)
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

        if constexpr (DerivedType::isPrecompiled || DerivedType::isFixedPatch)
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
        Editor (DerivedType& p)
            : juce::AudioProcessorEditor (p), owner (p),
              patchWebView (std::make_unique<cmaj::PatchWebView> (*p.patch, derivePatchViewSize (p)))
        {
            patchWebViewHolder = choc::ui::createJUCEWebViewHolder (patchWebView->getWebView());
            patchWebViewHolder->setSize ((int) patchWebView->width, (int) patchWebView->height);

            setResizeLimits (250, 160, 32768, 32768);

            lookAndFeel.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            lookAndFeel.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
            setLookAndFeel (&lookAndFeel);

            extraComp = owner.createExtraComponent();

            onPatchChanged (false);

            if (extraComp)
                addAndMakeVisible (*extraComp);

            statusMessageChanged();

            juce::Font::setDefaultMinimumHorizontalScaleFactor (1.0f);
        }

        ~Editor() override
        {
            owner.editorBeingDeleted (this);
            setLookAndFeel (nullptr);
            patchWebViewHolder.reset();
            patchWebView.reset();
        }

        void statusMessageChanged()
        {
            owner.refreshExtraComp (extraComp.get());
            patchWebView->setStatusMessage (owner.statusMessage);
        }

        static cmaj::PatchManifest::View derivePatchViewSize (const DerivedType& owner)
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
            if (owner.isViewVisible())
            {
                patchWebView->setActive (true);
                patchWebView->update (derivePatchViewSize (owner));
                patchWebViewHolder->setSize ((int) patchWebView->width, (int) patchWebView->height);

                setResizable (patchWebView->resizable, false);

                addAndMakeVisible (*patchWebViewHolder);
                childBoundsChanged (nullptr);
            }
            else
            {
                removeChildComponent (patchWebViewHolder.get());

                patchWebView->setActive (false);
                patchWebViewHolder->setVisible (false);

                setSize (defaultWidth, defaultHeight);
                setResizable (true, false);
            }

            if (forceReload)
                patchWebView->reload();
        }

        void childBoundsChanged (Component*) override
        {
            if (! isResizing && patchWebViewHolder->isVisible())
                setSize (std::max (50, patchWebViewHolder->getWidth()),
                         std::max (50, patchWebViewHolder->getHeight() + DerivedType::extraCompHeight));
        }

        void resized() override
        {
            isResizing = true;
            juce::AudioProcessorEditor::resized();

            auto r = getLocalBounds();

            if (patchWebViewHolder->isVisible())
            {
                patchWebViewHolder->setBounds (r.removeFromTop (getHeight() - DerivedType::extraCompHeight));
                r.removeFromTop (4);

                if (getWidth() > 0 && getHeight() > 0)
                {
                    owner.lastEditorWidth = patchWebViewHolder->getWidth();
                    owner.lastEditorHeight = patchWebViewHolder->getHeight();
                }
            }

            if (extraComp)
                extraComp->setBounds (r);

            isResizing = false;
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
        }

        //==============================================================================
        DerivedType& owner;

        std::unique_ptr<cmaj::PatchWebView> patchWebView;
        std::unique_ptr<juce::Component> patchWebViewHolder, extraComp;

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
/// This class is a juce::AudioPluginInstance which runs a JIT-compiled engine.
class JITLoaderPlugin  : public JUCEPluginBase<JITLoaderPlugin>
{
public:
    JITLoaderPlugin (std::shared_ptr<Patch> patchToUse)
        : JUCEPluginBase<JITLoaderPlugin> (patchToUse, getBusLayout())
    {
        // for a JIT plugin, we can't recreate parameter objects without hosts crashing, so
        // will just create a big flat list and re-use its parameter objects when things change
        ensureNumParameters (100);
    }

    static constexpr bool isPrecompiled = false;
    static constexpr bool isFixedPatch = false;

    void loadPatch (const std::filesystem::path& fileToLoad)
    {
        setNewStateAsync (createEmptyState (fileToLoad));
    }

    void loadPatch (const PatchManifest& manifest)
    {
        if (dllLoadedSuccessfully)
        {
            Patch::LoadParams loadParams;
            loadParams.manifest = manifest;
            patch->loadPatch (loadParams, false);
        }
    }

    bool prepareManifest (Patch::LoadParams& loadParams, const juce::ValueTree& newState) override
    {
        if (! newState.isValid())
            return false;

        auto location = newState.getProperty (ids.location).toString().toStdString();

        if (location.empty())
            return false;

        loadParams.manifest.initialiseWithFile (location);

        if (! patch->isLoaded() || loadParams.manifest.manifestFile == patch->getPatchFile())
            readParametersFromState (loadParams, newState);

        return true;
    }

    static BusesProperties getBusLayout()
    {
        BusesProperties layout;
        layout.addBus (true,  "Input",  juce::AudioChannelSet::stereo(), true);
        layout.addBus (false, "Output", juce::AudioChannelSet::stereo(), true);
        return layout;
    }

    bool isViewVisible()
    {
        return patch->isPlayable();
    }

    struct ExtraEditorComponent  : public juce::Component,
                                   public juce::FileDragAndDropTarget
    {
        ExtraEditorComponent (JITLoaderPlugin& p) : plugin (p)
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

           #if JUCE_MAJOR_VERSION == 8
            juce::Font f (juce::FontOptions (18.0f));
           #else
            juce::Font f (18.0f);
           #endif

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
        JITLoaderPlugin& plugin;
        bool isDragOver = false;

        juce::TextEditor messageBox;
        juce::TextButton unloadButton { "Unload" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExtraEditorComponent)
    };

    static constexpr int extraCompHeight = 50;

    std::unique_ptr<ExtraEditorComponent> createExtraComponent()
    {
        return std::make_unique<ExtraEditorComponent> (*this);
    }

    void refreshExtraComp (juce::Component* c)
    {
        if (auto v = dynamic_cast<ExtraEditorComponent*> (c))
            v->refresh();
    }
};

//==============================================================================
/// This class is a juce::AudioPluginInstance which runs a JIT-compiled engine.
class SinglePatchJITPlugin  : public JUCEPluginBase<SinglePatchJITPlugin>
{
public:
    SinglePatchJITPlugin (std::shared_ptr<cmaj::Patch> patchToUse,
                          std::filesystem::path manifestLocationToUse)
        : JUCEPluginBase<SinglePatchJITPlugin> (patchToUse, preloadBusLayout (*patchToUse, manifestLocationToUse)),
          manifestLocation (std::move (manifestLocationToUse))
    {
        setNewStateAsync (createEmptyState (manifestLocation));
    }

    bool prepareManifest (Patch::LoadParams& loadParams, const juce::ValueTree& newState) override
    {
        if (! newState.isValid())
            return false;

        loadParams.manifest.initialiseWithFile (manifestLocation);
        readParametersFromState (loadParams, newState);
        return true;
    }

    static BusesProperties preloadBusLayout (cmaj::Patch& p, std::filesystem::path location)
    {
        cmaj::PatchManifest m;
        m.initialiseWithFile (location);
        p.preload (m);

        return getBusesProperties (p.getInputEndpoints(),
                                   p.getOutputEndpoints());
    }

    static constexpr bool isPrecompiled = false;
    static constexpr bool isFixedPatch = true;

    std::filesystem::path manifestLocation;

    static constexpr int extraCompHeight = 0;
    static bool isViewVisible()  { return true; }
    std::unique_ptr<juce::Component> createExtraComponent() { return {}; }
    void refreshExtraComp (juce::Component*) {}
};

//==============================================================================
/// This class is a juce::AudioPluginInstance which loads a generated C++ patch
template <typename GeneratedInfoClass>
class GeneratedPlugin  : public JUCEPluginBase<GeneratedPlugin<GeneratedInfoClass>>
{
public:
    using super = JUCEPluginBase<GeneratedPlugin<GeneratedInfoClass>>;

    GeneratedPlugin (std::shared_ptr<cmaj::Patch> patchToUse)
        : super (std::move (patchToUse), getBusLayout())
    {
        this->patch->createEngine = +[] { return cmaj::createEngineForGeneratedCppProgram<typename GeneratedPlugin::PerformerClass>(); };

        this->applyRateAndBlockSize (44100, 128);
        super::setNewState (this->createEmptyState ({}));
    }

    bool prepareManifest (Patch::LoadParams& loadParams, const juce::ValueTree& newState) override
    {
        loadParams.manifest.needsToBuildSource = false;

        loadParams.manifest.initialiseWithVirtualFile (std::string (PatchClass::filename),
            [] (const std::string& f) -> std::shared_ptr<std::istream>
            {
                for (auto& file : PatchClass::files)
                    if (f == file.name)
                        return std::make_shared<std::istringstream> (std::string (file.content), std::ios::binary);

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

        this->readParametersFromState (loadParams, newState);
        return true;
    }

    static auto getBusLayout()
    {
        auto programDetailsJSON = choc::json::parse (PerformerClass::programDetailsJSON);

        return super::getBusesProperties (cmaj::EndpointDetailsList::fromJSON (programDetailsJSON["inputs"], true),
                                          cmaj::EndpointDetailsList::fromJSON (programDetailsJSON["outputs"], false));
    }

    using PatchClass = GeneratedInfoClass;
    using PerformerClass = typename PatchClass::PerformerClass;
    static constexpr bool isPrecompiled = true;
    static constexpr bool isFixedPatch = true;

    static constexpr int extraCompHeight = 0;
    static bool isViewVisible()  { return true; }
    std::unique_ptr<juce::Component> createExtraComponent() { return {}; }
    void refreshExtraComp (juce::Component*) {}
};


} // namespace cmaj::plugin
