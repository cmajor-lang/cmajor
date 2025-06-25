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

#include "cmajor/helpers/cmaj_PluginHelpers.h"
#include "cmajor/helpers/cmaj_PatchHelpers.h"
#include "cmajor/helpers/cmaj_Patch.h"
#include "cmajor/helpers/cmaj_PatchWebView.h"

#include "choc/containers/choc_SingleReaderSingleWriterFIFO.h"
#include "choc/platform/choc_Platform.h"
#include "choc/gui/choc_DesktopWindow.h"

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cmaj::plugin::clap
{

using PluginDeleter = auto (*) (clap_plugin*) -> void;
using PluginPtr = std::unique_ptr<clap_plugin, PluginDeleter>;

//==============================================================================

PluginPtr create (const clap_plugin_descriptor&,
                  const clap_host&,
                  const std::filesystem::path& pathToManifest,
                  const Environment&);

template <typename PatchClass>
clap_plugin_entry_t createGeneratedCppPluginEntryPoint();

} // cmaj::plugin::clap

//==============================================================================
//        _        _           _  _
//     __| |  ___ | |_   __ _ (_)| | ___
//    / _` | / _ \| __| / _` || || |/ __|
//   | (_| ||  __/| |_ | (_| || || |\__ \ _  _  _
//    \__,_| \___| \__| \__,_||_||_||___/(_)(_)(_)
//
//   Code beyond this point is implementation detail...
//
//==============================================================================

namespace cmaj::plugin::clap
{

namespace detail
{

//==============================================================================
class Plugin
{
public:
    static PluginPtr create (const clap_plugin_descriptor_t&,
                             const clap_host_t&,
                             const std::filesystem::path&,
                             const Environment&);
    ~Plugin();

    Plugin (const Plugin&) = delete;
    Plugin (Plugin&&) = delete;
    Plugin& operator= (const Plugin&) = delete;
    Plugin& operator= (Plugin&&) = delete;

    void update (const std::filesystem::path&);

private:
    struct PluginFactoryArgs
    {
        const clap_host_t& host;
        std::filesystem::path pathToManifest;
        Environment environment;
    };

    Plugin (clap_plugin&, const PluginFactoryArgs&);

    static bool clapPlugin_init (const clap_plugin_t*);
    static void clapPlugin_destroy (const clap_plugin_t*);
    static const void* clapPlugin_getExtension (const clap_plugin_t*, const char* id);

    clap_plugin& owner;

    PluginFactoryArgs cachedArgs;

    struct Impl;
    std::unique_ptr<Impl> impl;

    clap_plugin_audio_ports_t audioPortsExtension;
    clap_plugin_note_ports_t notePortsExtension;
    clap_plugin_latency_t latencyExtension;
    clap_plugin_state_t stateExtension;
    clap_plugin_params_t parametersExtension;
    clap_plugin_gui_t guiExtension;
};

struct Plugin::Impl
{
    Impl (const clap_host_t& hostToUse,
          const std::filesystem::path& pathToManifest,
          const Environment& environmentToUse)
        : host (hostToUse),
          lastRequestedManifestPath (pathToManifest),
          environment (environmentToUse)
    {
        // needed to consume the events dispatched from the ClientEventQueue thread on Windows.
        // this, amongst other things, delivers the parameter value changes to the views.
        choc::messageloop::initialise();
        environment.initialisePatch (patch);
        patch.setHostDescription ("CLAP");
        editorToProcessorEventQueue.reset (8192);

        if (environment.engineType == Environment::EngineType::AOT)
        {
            [[maybe_unused]] const auto compiled = loadPatch (lastRequestedManifestPath, { 44100, 128 });
            CMAJ_ASSERT (compiled);

            updateParameterInfoCachesFromLoadedPatch();
            updateAudioPortInfoCachesFromLoadedPatch();
        }
    }

    Impl (const Impl&) = delete;
    Impl (Impl&&) = delete;
    Impl& operator= (const Impl&) = delete;
    Impl& operator= (Impl&&) = delete;

    void update (const std::filesystem::path& nextPathToManifest)
    {
        if (! blockRestartRequests)
        {
            isResetRequestPending = true;
            lastRequestedManifestPath = nextPathToManifest;
            host.request_restart (std::addressof (host));
        }
    }

    //==============================================================================

    // clap_plugin_t
    bool clapPlugin_activate (double, uint32_t, uint32_t);
    void clapPlugin_deactivate();
    bool clapPlugin_startProcessing();
    void clapPlugin_stopProcessing();
    void clapPlugin_reset();
    clap_process_status clapPlugin_process (const clap_process_t*);
    void clapPlugin_onMainThread();

    // clap_plugin_audio_ports_t
    uint32_t clapAudioPorts_count (bool);
    bool clapAudioPorts_get (uint32_t, bool, clap_audio_port_info_t*);

    // clap_plugin_audio_ports_t
    uint32_t clapNotePorts_count (bool);
    bool clapNotePorts_get (uint32_t, bool, clap_note_port_info_t*);

    // clap_plugin_latency_t
    uint32_t clapLatency_get();

    // clap_plugin_state_t
    bool clapState_save (const clap_ostream_t*);
    bool clapState_load (const clap_istream_t*);

    // clap_plugin_params_t
    uint32_t clapParameters_count();
    bool clapParameters_getInfo (uint32_t, clap_param_info_t*);
    bool clapParameters_getValue (clap_id, double*);
    bool clapParameters_valueToText (clap_id, double, char*, uint32_t);
    bool clapParameters_textToValue (clap_id, const char*, double*);
    void clapParameters_flush (const clap_input_events_t*, const clap_output_events_t*);

    // clap_plugin_gui_t
    bool clapGui_isApiSupported (const char*, bool);
    bool clapGui_getPreferredApi (const char**, bool*);
    bool clapGui_create (const char*, bool);
    void clapGui_destroy();
    bool clapGui_setScale (double);
    bool clapGui_getSize (uint32_t*, uint32_t*);
    bool clapGui_canResize();
    bool clapGui_getResizeHints (clap_gui_resize_hints_t*);
    bool clapGui_adjustSize (uint32_t*, uint32_t*);
    bool clapGui_setSize (uint32_t, uint32_t);
    bool clapGui_setParent (const clap_window_t*);
    bool clapGui_setTransient (const clap_window_t*);
    void clapGui_suggestTitle (const char*);
    bool clapGui_show();
    bool clapGui_hide();

private:
    //==============================================================================
    void updateParameterInfoCachesFromLoadedPatch();
    void updateAudioPortInfoCachesFromLoadedPatch();
    void updateNotePortInfoCachesFromLoadedPatch();

    void resetIfRequestIsPending();

    void consumeEventsFromEditor (const clap_output_events_t&);
    void dispatchEvent (const clap_event_header_t&);

    //==============================================================================
    static void copyAndNullTerminateTruncatingIfNecessary (const std::string& from, char* to, size_t capacity);

    static clap_event_midi_t toClapMidiEvent (uint32_t sampleOffset, uint16_t portIndex, const choc::midi::ShortMessage&);
    static clap_event_note_t toClapNoteEvent (uint32_t sampleOffset,
                                              uint16_t portIndex,
                                              uint16_t noteType,
                                              uint16_t channel,
                                              uint16_t key,
                                              double velocity);

    //==============================================================================
    const clap_host_t& host;

    std::filesystem::path lastRequestedManifestPath;
    Environment environment;

    cmaj::Patch patch;

    std::atomic<bool> blockEditorDispatch = false;
    std::atomic<bool> blockRestartRequests = false; // Bitwig seems to crash when requesting restart whilst being restarted
    std::atomic<bool> isResetRequestPending = false; // Doesn't actually need to be atomic unless we do something in start/stop processing

    // currently ramp frames are not applied when setting via host automation
    using SetParameterValueFromProcessFn = std::function<void(cmaj::EndpointHandle, float)>;
    SetParameterValueFromProcessFn setParameterValueFromProcess;

    struct MappingFunctions
    {
        using ValueMappingFn = std::function<float(float)>;

        ValueMappingFn toClapValue;
        ValueMappingFn fromClapValue;
    };

    std::vector<clap_param_info_t> automatableParameterInfo;
    std::unordered_map<cmaj::EndpointHandle, cmaj::PatchParameterPtr> automatableParametersByHandle;
    std::unordered_map<cmaj::EndpointHandle, MappingFunctions> automatableParameterMappingFunctionsByHandle;

    uint32_t hostSupportedNotePortDialects = 0;
    std::vector<clap_note_port_info_t> infoForInputNotePorts;
    std::vector<cmaj::EndpointID> inputNotePortEndpointIds;

    std::vector<clap_note_port_info_t> infoForOutputNotePorts;

    std::vector<clap_audio_port_info_t> infoForInputAudioPorts;
    std::vector<clap_audio_port_info_t> infoForOutputAudioPorts;

    std::vector<const float*> flattenedInputChannelsScratchBuffer;
    std::vector<float*> flattenedOutputChannelsScratchBuffer;

    double frequency = 0;
    uint32_t maxBlockSize = 0;

    using Bytes = std::vector<uint8_t>;

    struct StateCache
    {
        Bytes serialised;
        choc::value::Value fullStoredState;
    };

    std::optional<StateCache> pendingStateToApplyWhenActivated;

    struct BeginGesture
    {
        cmaj::EndpointHandle handle;
    };

    struct EndGesture
    {
        cmaj::EndpointHandle handle;
    };

    struct SendValue
    {
        cmaj::EndpointHandle handle;
        float value;
    };

    using EditorParameterEvent = std::variant<BeginGesture, EndGesture, SendValue>;
    choc::fifo::SingleReaderSingleWriterFIFO<EditorParameterEvent> editorToProcessorEventQueue;

    struct ViewHolder
    {
        ViewHolder (cmaj::Patch& patchToUse, std::optional<double> initialScaleFactorToUse)
            : webview (std::make_unique<cmaj::PatchWebView> (patchToUse, findDefaultViewForPatch (patchToUse)))
        {
            if (initialScaleFactorToUse)
                setScaleFactor (*initialScaleFactorToUse);
        }

        struct Size
        {
            uint32_t width;
            uint32_t height;
        };

        bool setScaleFactor (double factor)
        {
            scaleFactor = factor;
            inverseScaleFactor = 1.0 / factor;

            return true;
        }

        Size size() const
        {
            return { scaled (webview->width),
                     scaled (webview->height) };
        }

        bool setSize (const Size& sizeToUse)
        {
            webview->width  = unscaled (sizeToUse.width);
            webview->height = unscaled (sizeToUse.height);

            return updateNativeViewSize();
        }

        bool resizable() const
        {
            return webview->resizable;
        }

        bool setParent (void* parent)
        {
            if (! cmaj::plugin::addChildView (parent, nativeViewHandle()))
                return false;

            return updateNativeViewSize();
        }

    private:
        void* nativeViewHandle() const                  { return webview->getWebView().getViewHandle(); }
        uint32_t scaled (uint32_t x) const              { return scaleFactor ? toIntegerPixel (*scaleFactor * x) : x; }
        uint32_t unscaled (uint32_t x) const            { return scaleFactor ? toIntegerPixel (*inverseScaleFactor * x) : x; }
        static uint32_t toIntegerPixel (double x)       { return static_cast<uint32_t> (0.5 + x); }

        bool updateNativeViewSize()
        {
            const auto [width, height] = size();

            return cmaj::plugin::setViewSize (nativeViewHandle(), width, height);
        }

        std::unique_ptr<cmaj::PatchWebView> webview;
        std::optional<double> scaleFactor;
        std::optional<double> inverseScaleFactor;
    };

    std::optional<double> cachedViewScaleFactor; // workaround Bitwig only passing the scale factor the first time the view is shown
    std::optional<ViewHolder> editor;

    bool loadPatch (const std::filesystem::path& pathToManifest, FrequencyAndBlockSize frequencyAndBlockSize)
    {
        const auto manifest = environment.makePatchManifest (pathToManifest);

        using InitialParameterValues = std::unordered_map<std::string, float>;
        InitialParameterValues initialParameterValues {};

        for (const auto& parameter : patch.getParameterList())
            initialParameterValues[parameter->properties.endpointID] = parameter->currentValue;

        if (! patch.preload (manifest))
            return {};

        const auto sumTotalChannelsAcrossAudioEndpoints = [] (const cmaj::EndpointDetailsList& endpoints) -> uint32_t
        {
            uint32_t totalChannelCount = 0;

            for (const auto& endpoint : endpoints)
                totalChannelCount += endpoint.getNumAudioChannels();

            return totalChannelCount;
        };

        patch.setPlaybackParams ({
            frequencyAndBlockSize.frequency,
            frequencyAndBlockSize.maxBlockSize,
            sumTotalChannelsAcrossAudioEndpoints (patch.getInputEndpoints()),
            sumTotalChannelsAcrossAudioEndpoints (patch.getOutputEndpoints()),
        });

        if (! patch.loadPatch ({ manifest, initialParameterValues }, true))
            return {};

        if (! patch.isPlayable())
            return {};

        flattenedInputChannelsScratchBuffer.resize (sumTotalChannelsAcrossAudioEndpoints (patch.getInputEndpoints()));
        flattenedOutputChannelsScratchBuffer.resize (sumTotalChannelsAcrossAudioEndpoints (patch.getOutputEndpoints()));

        return true;
    }
};

template <typename T>
T* unsafeCastToPtr (const clap_plugin_t* plugin)
{
    return reinterpret_cast<T*> (plugin->plugin_data);
}

template <typename T>
T& unsafeCastToRef (const clap_plugin_t* plugin)
{
    return *unsafeCastToPtr<T> (plugin);
}

template <typename T>
auto getExtension (const clap_host_t& host, const char* extension)
{
    return reinterpret_cast<const T*> (host.get_extension (std::addressof (host), extension));
}

inline bool Plugin::Impl::clapPlugin_activate (double frequencyToUse, uint32_t, uint32_t maxBlockSizeToUse)
{
    frequency = frequencyToUse;
    maxBlockSize = maxBlockSizeToUse;

    if (! loadPatch (lastRequestedManifestPath, { frequency, maxBlockSize }))
        return false;

    // currently caches are always recalculated. however, in the generated plugin case this is wasteful.
    updateParameterInfoCachesFromLoadedPatch();
    updateAudioPortInfoCachesFromLoadedPatch();
    updateNotePortInfoCachesFromLoadedPatch();

    if (pendingStateToApplyWhenActivated)
    {
        patch.setFullStoredState (pendingStateToApplyWhenActivated->fullStoredState);

        pendingStateToApplyWhenActivated = {};
    }

    // currently notifying unconditionally, but ought to figure out explicitly what changed
    const auto notifyHostAboutLatestChanges = [&, this]
    {
        if (const auto* hostLatency = getExtension<clap_host_latency_t> (host, CLAP_EXT_LATENCY))
            hostLatency->changed (std::addressof (host));

        if (const auto* hostNotePorts = getExtension<clap_host_note_ports_t> (host, CLAP_EXT_NOTE_PORTS))
        {
            hostSupportedNotePortDialects = hostNotePorts->supported_dialects (std::addressof (host));
            hostNotePorts->rescan (std::addressof (host), CLAP_NOTE_PORTS_RESCAN_ALL);
        }

        if (const auto* hostAudioPorts = getExtension<clap_host_audio_ports_t> (host, CLAP_EXT_AUDIO_PORTS))
            if (hostAudioPorts->is_rescan_flag_supported (std::addressof (host), CLAP_AUDIO_PORTS_RESCAN_LIST))
                hostAudioPorts->rescan (std::addressof (host), CLAP_AUDIO_PORTS_RESCAN_LIST);

        if (const auto* hostParameters = getExtension<clap_host_params_t> (host, CLAP_EXT_PARAMS))
            hostParameters->rescan (std::addressof (host), CLAP_PARAM_RESCAN_ALL);
    };

    notifyHostAboutLatestChanges();

    return true;
}

inline void Plugin::Impl::updateParameterInfoCachesFromLoadedPatch()
{
    const auto createDiscreteParameterMappingFunctions = [] (const auto& properties)
    {
        struct FloatRange
        {
            float min;
            float max;
        };

        const auto clapRange = FloatRange { 0.0f, static_cast<float> (properties.getNumDiscreteOptions() - 1) };
        const auto performerRange = FloatRange { properties.minValue, properties.maxValue };

        const auto remap = [] (auto v, const auto& from, const auto& to)
        {
            return to.min + (v - from.min) * (to.max - to.min) / (from.max - from.min);
        };

        const auto toClapValue = [=] (auto v) { return remap (v, performerRange, clapRange); };
        const auto fromClapValue = [=] (auto v) { return remap (v, clapRange, performerRange); };

        return MappingFunctions { toClapValue, fromClapValue };
    };

    const auto toParameterInfo = [=] (const auto& endpointHandle, const auto& properties) -> clap_param_info_t
    {
        // currently these are just laid out in the order they end up in the parameter list.
        // Endpoint handles are currenty used as IDs, which is brittle.
        clap_param_info_t out {};

        out.id = endpointHandle;
        out.flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
        out.cookie = nullptr;
        copyAndNullTerminateTruncatingIfNecessary (properties.name, out.name, CLAP_NAME_SIZE);
        copyAndNullTerminateTruncatingIfNecessary (properties.group, out.module, CLAP_PATH_SIZE);

        if (properties.getNumDiscreteOptions() > 0)
        {
            out.flags |= CLAP_PARAM_IS_STEPPED;
            // stepped parameters must be integers. so, have this be an index into the options array.
            out.min_value = 0;
            out.max_value = static_cast<double> (properties.getNumDiscreteOptions() - 1);
            out.default_value = createDiscreteParameterMappingFunctions (properties).toClapValue (properties.defaultValue);
        }
        else
        {
            out.min_value = static_cast<double> (properties.minValue);
            out.max_value = static_cast<double> (properties.maxValue);
            out.default_value = static_cast<double> (properties.defaultValue);
        }

        return out;
    };

    const auto dispatchIfEditorOpen = [this] (const EditorParameterEvent& event)
    {
        if (blockEditorDispatch)
            return;

        if (! editor) // deliberately a different condition than the above, as not technically thread safe
            return;

        const auto mapped = std::visit ([this] (auto&& arg) -> EditorParameterEvent
        {
            using T = std::decay_t<decltype (arg)>;

            if constexpr (std::is_same_v<T, SendValue>)
            {
                auto out = arg;

                if (auto maybeMappers = automatableParameterMappingFunctionsByHandle.find (arg.handle);
                    maybeMappers != automatableParameterMappingFunctionsByHandle.end())
                {
                    out.value = maybeMappers->second.toClapValue (out.value);
                }

                return out;
            }
            else
            {
                return arg;
            }

        }, event);

        editorToProcessorEventQueue.push (mapped);

        if (const auto* hostParameters = getExtension<clap_host_params_t> (host, CLAP_EXT_PARAMS))
            hostParameters->request_flush (std::addressof (host));
    };

    automatableParameterInfo.clear();
    automatableParametersByHandle.clear();
    automatableParameterMappingFunctionsByHandle.clear();

    for (const auto& parameter : patch.getParameterList())
    {
        const auto& properties = parameter->properties;
        const auto& endpointHandle = parameter->endpointHandle;

        if (! properties.automatable)
            continue;

        automatableParameterInfo.push_back (toParameterInfo (endpointHandle, properties));
        automatableParametersByHandle[endpointHandle] = parameter;

        if (properties.getNumDiscreteOptions() > 0)
            automatableParameterMappingFunctionsByHandle[endpointHandle] = createDiscreteParameterMappingFunctions (properties);

        parameter->valueChanged = [dispatchIfEditorOpen, handle = endpointHandle] (auto value)
        {
            dispatchIfEditorOpen (SendValue { handle, value });
        };
        parameter->gestureStart = [dispatchIfEditorOpen, handle = endpointHandle]
        {
            dispatchIfEditorOpen (BeginGesture { handle });
        };
        parameter->gestureEnd = [dispatchIfEditorOpen, handle = endpointHandle]
        {
            dispatchIfEditorOpen (EndGesture { handle });
        };
    }

    // it isn't possible to distinguish an update from the audio thread vs the editor
    // via `PatchParameter::valueChanged`. so, use a flag when setting the value from the audio
    // thread, so that we can avoid dispatching it again via the gui.
    const auto toEditorUpdateBlockingFunction = [this] (const auto& fn)
    {
        return [=] (auto&&... args)
        {
            blockEditorDispatch = true;
            fn (std::forward<decltype (args)> (args)...);
            blockEditorDispatch = false;
        };
    };

    setParameterValueFromProcess = toEditorUpdateBlockingFunction ([this] (auto handle, auto value)
    {
        if (auto maybeMappers = automatableParameterMappingFunctionsByHandle.find (handle);
            maybeMappers != automatableParameterMappingFunctionsByHandle.end())
        {
            value = maybeMappers->second.fromClapValue (value);
        }

        if (auto parameterEntry = automatableParametersByHandle.find (handle);
            parameterEntry != automatableParametersByHandle.end())
        {
            parameterEntry->second->setValue (value, false, -1, 0);
        }
    });
}

inline void Plugin::Impl::updateAudioPortInfoCachesFromLoadedPatch()
{
    infoForInputAudioPorts.clear();
    infoForOutputAudioPorts.clear();

    const auto mapEndpointsToPorts = [] (const auto& endpoints, auto& infoForPortsToAppendTo)
    {
        const auto toPortType = [] (uint32_t count) -> const char*
        {
            if (count == 1)
                return CLAP_PORT_MONO;

            if (count == 2)
                return CLAP_PORT_STEREO;

            return nullptr;
        };

        const auto toFlags = [] (uint32_t index, const cmaj::EndpointDetails&) -> uint32_t
        {
            // for now, first endpoint is implicitly main. there can only be one, and it must be index 0
            return index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
        };

        uint32_t indexId = 0;
        for (const auto& endpoint : endpoints)
        {
            if (const auto channelCount = endpoint.getNumAudioChannels())
            {
                clap_audio_port_info_t port {};
                port.id = indexId;
                copyAndNullTerminateTruncatingIfNecessary (endpoint.endpointID.toString(), port.name, CLAP_NAME_SIZE);

                port.channel_count = channelCount;
                port.flags = toFlags (indexId, endpoint);
                port.port_type = toPortType (channelCount);
                port.in_place_pair = CLAP_INVALID_ID; // we don't currently support in-place processing

                infoForPortsToAppendTo.push_back (port);

                ++indexId;
            }
        }
    };

    const auto hostSupportsChangingPortList = [this]
    {
        if (const auto* hostAudioPorts = getExtension<clap_host_audio_ports_t> (host, CLAP_EXT_AUDIO_PORTS))
            return hostAudioPorts->is_rescan_flag_supported (std::addressof (host), CLAP_AUDIO_PORTS_RESCAN_LIST);

        return false;
    };

    if (environment.engineType == Environment::EngineType::AOT || hostSupportsChangingPortList())
    {
        mapEndpointsToPorts (patch.getInputEndpoints(), infoForInputAudioPorts);
        mapEndpointsToPorts (patch.getOutputEndpoints(), infoForOutputAudioPorts);
    }
    else
    {
        // TODO: in the JIT case we need to fall back to stereo or something
    }
}

inline void Plugin::Impl::updateNotePortInfoCachesFromLoadedPatch()
{
    infoForInputNotePorts.clear();
    inputNotePortEndpointIds.clear();

    infoForOutputNotePorts.clear();
    inputNotePortEndpointIds.clear();

    const auto mapEndpointsToPorts = [] (const auto& endpoints, auto& infoForPortsToAppendTo, auto& endpointIdsToAppendTo)
    {
        uint32_t indexId = 0;
        for (const auto& endpoint : endpoints)
        {
            if (endpoint.isMIDI())
            {
                clap_note_port_info_t port {};
                port.id = indexId;
                port.supported_dialects = CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_CLAP;
                port.preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
                copyAndNullTerminateTruncatingIfNecessary (endpoint.endpointID.toString(), port.name, CLAP_NAME_SIZE);

                infoForPortsToAppendTo.push_back (port);
                endpointIdsToAppendTo.push_back (endpoint.endpointID);
                ++indexId;
            }
        };
    };

    mapEndpointsToPorts (patch.getInputEndpoints(), infoForInputNotePorts, inputNotePortEndpointIds);

    {
        // limited to one note output port by the current `Patch` implementation
        std::vector<cmaj::EndpointID> outputNotePortEndpointIds;
        decltype (infoForOutputNotePorts) scratchInfoForOutputNotePorts;

        mapEndpointsToPorts (patch.getOutputEndpoints(), scratchInfoForOutputNotePorts, outputNotePortEndpointIds);

        if (! scratchInfoForOutputNotePorts.empty())
            infoForOutputNotePorts.push_back (scratchInfoForOutputNotePorts.front());
    }
}

inline void Plugin::Impl::clapPlugin_deactivate()
{
    isResetRequestPending = false;
}

inline bool Plugin::Impl::clapPlugin_startProcessing()
{
    blockRestartRequests = false;
    return patch.isPlayable();
}

inline void Plugin::Impl::clapPlugin_stopProcessing()
{
    blockRestartRequests = true;
}

inline void Plugin::Impl::clapPlugin_reset()
{
}

template <typename T>
struct Range
{
    using SizeType = T;

    T start;
    T end; // one after the last

    T size() const noexcept { return end - start; }
    bool operator== (const Range<T>& other) const { return start == other.start && end == other.end; }
    bool operator!= (const Range<T>& other) const { return start != other.start || end != other.end; }
};

using EventTimeRange = Range<uint32_t>;

template <typename PredicateFn, typename WithEventFn, typename WithBlockFn>
void forEachFilteredEventRange (const EventTimeRange& range,
                                const clap_input_events_t& events,
                                const PredicateFn& matches,
                                const WithEventFn& withEvent,
                                const WithBlockFn& withBlock)
{
    using SizeType = EventTimeRange::SizeType;

    SizeType rangeStartOffset = range.start;
    const auto eventCount = events.size (std::addressof (events));

    for (SizeType eventIndex = 0; eventIndex < eventCount; ++eventIndex)
    {
        const auto* event = events.get (std::addressof (events), eventIndex);
        if (! matches (*event))
            continue;

        const auto eventTime = event->time;

        if (eventTime > rangeStartOffset)
        {
            withBlock (EventTimeRange { rangeStartOffset, eventTime });
            rangeStartOffset = eventTime;
        }

        withEvent (*event);
    }

    if (rangeStartOffset < range.end)
        withBlock (EventTimeRange { rangeStartOffset, range.end });
}

inline clap_process_status Plugin::Impl::clapPlugin_process (const clap_process_t* process)
{
    if (! patch.isPlayable())
        return CLAP_PROCESS_ERROR;

    const auto processTransportForBlock = [this] (const auto* transport)
    {
        if (transport)
            dispatchEvent (transport->header);
    };

    const auto processInChunksDelimitedByEvent = [this] (const clap_process_t& state)
    {
        const auto& inputQueue = *state.in_events;
        auto& outputQueue = *state.out_events;
        auto* inputs = state.audio_inputs;
        auto* outputs = state.audio_outputs;
        const auto count = state.frames_count;

        const auto shouldConsumeEvent = [] (const auto& e) -> bool
        {
            return e.space_id == CLAP_CORE_EVENT_SPACE_ID
                && (e.type == CLAP_EVENT_MIDI
                ||  e.type == CLAP_EVENT_NOTE_ON
                ||  e.type == CLAP_EVENT_NOTE_OFF
                ||  e.type == CLAP_EVENT_PARAM_VALUE);
        };

        const auto toChannelArrayView = [] (auto& scratch, const auto& portInfos, auto* clapBuffers, auto blockSize)
        {
            // currently only handling single precision, i.e. data32. when setting up the audio ports we don't set any 64 bit flags.
            const auto populateFlattenedChannelsScatchBuffer = [] (auto& flattened, const auto& info, auto* buffers)
            {
                size_t flattenedChannelsIndex = 0;

                for (size_t i = 0; i < info.size(); ++i)
                {
                    const auto& buffer = buffers[i];
                    const auto channelCount = info[i].channel_count;

                    for (uint32_t channel = 0; channel < channelCount; ++channel)
                        flattened[flattenedChannelsIndex++] = buffer.data32[channel];
                }
            };

            populateFlattenedChannelsScatchBuffer (scratch, portInfos, clapBuffers);

            return choc::buffer::createChannelArrayView (scratch.data(), static_cast<uint32_t> (scratch.size()), blockSize);
        };

        auto inputChannels = toChannelArrayView (flattenedInputChannelsScratchBuffer, infoForInputAudioPorts, inputs, count);
        auto outputChannels = toChannelArrayView (flattenedOutputChannelsScratchBuffer, infoForOutputAudioPorts, outputs, count);

        forEachFilteredEventRange ({ 0, count },
                                   inputQueue,
                                   shouldConsumeEvent,
                                   [this] (const auto& event) { dispatchEvent (event); },
                                   [&] (const auto& range)
        {
            const bool replaceOutput = true;

            patch.process ({
                inputChannels.getFrameRange ({ range.start, range.end }),
                outputChannels.getFrameRange ({ range.start, range.end }),
                {}, // handle splitting events externally, for sample-accurate automation etc.
                [&, this] (auto frameIndex, const auto& message)
                {
                    if (infoForOutputNotePorts.empty())
                        return;

                    const uint16_t portIndex = 0;
                    const auto frameOffsetRelativeToProcessBlockStart = range.start + frameIndex;

                    const auto pushClapMidiEvent = [&]
                    {
                        const auto clapEvent = toClapMidiEvent (
                            frameOffsetRelativeToProcessBlockStart,
                            portIndex,
                            message
                        );

                        outputQueue.try_push (std::addressof (outputQueue), std::addressof (clapEvent.header));
                    };

                    // prefer MIDI if host supports it
                    if (hostSupportedNotePortDialects & CLAP_NOTE_DIALECT_MIDI)
                        return pushClapMidiEvent();

                    if (hostSupportedNotePortDialects & CLAP_NOTE_DIALECT_CLAP)
                    {
                        if (message.isNoteOn() || message.isNoteOff())
                        {
                            const auto clapEvent = toClapNoteEvent (
                                frameOffsetRelativeToProcessBlockStart,
                                portIndex,
                                static_cast<uint16_t> (message.isNoteOn() ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF),
                                message.getChannel0to15(),
                                message.getNoteNumber(),
                                message.getVelocity() / 127.0
                            );

                            outputQueue.try_push (std::addressof (outputQueue), std::addressof (clapEvent.header));
                            return;
                        }
                    }

                    // just map to midi and hope the host can do something with it
                    return pushClapMidiEvent();
                }
            }, replaceOutput);
        });
    };

    consumeEventsFromEditor (*process->out_events);
    processTransportForBlock (process->transport);
    processInChunksDelimitedByEvent (*process);

    return CLAP_PROCESS_CONTINUE;
}

inline void Plugin::Impl::consumeEventsFromEditor (const clap_output_events_t& outputEventQueue)
{
    if (editorToProcessorEventQueue.getUsedSlots() != 0)
    {
        EditorParameterEvent e;

        while (editorToProcessorEventQueue.pop (e))
        {
            std::visit ([&outputEventQueue] (auto&& event)
            {
                using T = std::decay_t<decltype (event)>;

                const auto makeClapGestureEvent = [] (auto type, const auto& uiEvent)
                {
                    clap_event_param_gesture clapEvent {};

                    clapEvent.header.size = sizeof (clap_event_param_gesture);
                    clapEvent.header.type = static_cast<uint16_t> (type);
                    clapEvent.header.time = 0;
                    clapEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    clapEvent.header.flags = 0;
                    clapEvent.param_id = uiEvent.handle;

                    return clapEvent;
                };

                if constexpr (std::is_same_v<T, BeginGesture>)
                {
                    auto clapEvent = makeClapGestureEvent (CLAP_EVENT_PARAM_GESTURE_BEGIN, event);
                    outputEventQueue.try_push (std::addressof (outputEventQueue), std::addressof (clapEvent.header));
                }
                else if constexpr (std::is_same_v<T, EndGesture>)
                {
                    auto clapEvent = makeClapGestureEvent (CLAP_EVENT_PARAM_GESTURE_END, event);
                    outputEventQueue.try_push (std::addressof (outputEventQueue), std::addressof (clapEvent.header));
                }
                else if constexpr (std::is_same_v<T, SendValue>)
                {
                    (void) makeClapGestureEvent;
                    clap_event_param_value clapEvent {};
                    clapEvent.header.size = sizeof (clap_event_param_value);
                    clapEvent.header.type = static_cast<uint16_t> (CLAP_EVENT_PARAM_VALUE);
                    clapEvent.header.time = 0;
                    clapEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    clapEvent.header.flags = 0;
                    clapEvent.param_id = event.handle;
                    clapEvent.value = event.value;

                    outputEventQueue.try_push (std::addressof (outputEventQueue), std::addressof (clapEvent.header));
                }

            }, e);
        }
    }
}

inline void Plugin::Impl::dispatchEvent (const clap_event_header_t& eventHeader)
{
    const auto sendMIDIInputEvent = [this] (auto portIndex, const choc::midi::ShortMessage& msg)
    {
        if (portIndex < 0 || static_cast<size_t> (portIndex) >= inputNotePortEndpointIds.size())
            return;

        patch.sendMIDIInputEvent (inputNotePortEndpointIds[static_cast<size_t> (portIndex)], msg, 0);
    };

    switch (eventHeader.type)
    {
        case CLAP_EVENT_NOTE_ON:
        {
            const auto& event = reinterpret_cast<const clap_event_note_t&> (eventHeader);

            sendMIDIInputEvent (event.port_index, {
                static_cast<uint8_t> (0x90 + (event.channel & 0xf)),
                static_cast<uint8_t> (event.key),
                static_cast<uint8_t> (event.velocity * 127.0)
            });
            break;
        }
        case CLAP_EVENT_NOTE_OFF:
        {
            const auto& event = reinterpret_cast<const clap_event_note_t&> (eventHeader);

            sendMIDIInputEvent (event.port_index, {
                static_cast<uint8_t> (0x80 + (event.channel & 0xf)),
                static_cast<uint8_t> (event.key),
                static_cast<uint8_t> (event.velocity * 127.0)
            });
            break;
        }
        case CLAP_EVENT_PARAM_VALUE:
        {
            const auto& event = reinterpret_cast<const clap_event_param_value_t&> (eventHeader);
            setParameterValueFromProcess (event.param_id, static_cast<float> (event.value));
            break;
        }
        case CLAP_EVENT_TRANSPORT:
        {
            if (! patch.wantsTimecodeEvents())
                return;

            const auto& event = reinterpret_cast<const clap_event_transport_t&> (eventHeader);
            const bool isRecording = event.flags & CLAP_TRANSPORT_IS_RECORDING;
            const bool isPlaying   = event.flags & CLAP_TRANSPORT_IS_PLAYING;
            const bool isLooping   = event.flags & CLAP_TRANSPORT_IS_LOOP_ACTIVE;

            patch.sendTransportState (isRecording, isPlaying, isLooping, 0);

            if (event.flags & CLAP_TRANSPORT_HAS_TEMPO)
                patch.sendBPM (static_cast<float> (event.tempo), 0);

            if (event.flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE)
                patch.sendTimeSig (static_cast<int> (event.tsig_num), static_cast<int> (event.tsig_denom), 0);

            struct TimelinePosition
            {
                // frameIndex, is supposedly required. though, we actually may not be able to supply it.
                int64_t frameIndex = 0;

                double quarterNote = 0;
                double barStartQuarterNote = 0;
            };

            const auto calculateTimelinePosition = [&, this]() -> std::optional<TimelinePosition>
            {
                // there doesn't seem to be a way to get the frame index of the hosts timeline directly.
                // so, we'll derive it either from seconds or beats.
                const auto fromFixedPointSeconds = [] (auto seconds)
                {
                    return static_cast<double> (seconds) / CLAP_SECTIME_FACTOR;
                };

                const auto toFrameIndexFromSeconds = [fromFixedPointSeconds, this] (auto seconds)
                {
                    return static_cast<int64_t> (frequency * fromFixedPointSeconds (seconds));
                };

                if (event.flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE)
                {
                    const auto quarterNote = static_cast<double> (event.song_pos_beats) / CLAP_BEATTIME_FACTOR;
                    const auto barStartQuarterNote = static_cast<double> (event.bar_start) / CLAP_BEATTIME_FACTOR;

                    // prefer seconds to calculate frameIndex
                    if (event.flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE)
                    {
                        return TimelinePosition
                        {
                            toFrameIndexFromSeconds (event.song_pos_seconds),
                            quarterNote,
                            barStartQuarterNote
                        };
                    }

                    if (event.flags & CLAP_TRANSPORT_HAS_TEMPO)
                    {
                        const auto tempoInSeconds = (60.0 / event.tempo);

                        return TimelinePosition
                        {
                            static_cast<int64_t> (quarterNote * tempoInSeconds * frequency),
                            quarterNote,
                            barStartQuarterNote
                        };
                    }

                    return {};
                }

                if (event.flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE)
                {
                    const auto calculateQuarterNoteIfPossible = [fromFixedPointSeconds] (const auto& e) -> double
                    {
                        if (! (e.flags & CLAP_TRANSPORT_HAS_TEMPO))
                            return {};

                        const auto tempoInSeconds = (60.0 / e.tempo);
                        return fromFixedPointSeconds (e.song_pos_seconds) / tempoInSeconds;
                    };

                    const auto calculateBarStartQuarterNoteIfPossible = [] (const auto& e) -> double
                    {
                        if (! (e.flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE))
                            return {};

                        const auto quarterNotesPerBar = (4.0 * e.tsig_num) / e.tsig_denom;
                        return e.bar_number * quarterNotesPerBar;
                    };

                    return TimelinePosition
                    {
                        toFrameIndexFromSeconds (event.song_pos_seconds),
                        calculateQuarterNoteIfPossible (event),
                        calculateBarStartQuarterNoteIfPossible (event),
                    };
                }

                return {};
            };

            if (const auto position = calculateTimelinePosition())
                patch.sendPosition (position->frameIndex,
                                    position->quarterNote,
                                    position->barStartQuarterNote,
                                    0);

            break;
        }
        case CLAP_EVENT_MIDI:
        {
            const auto& event = reinterpret_cast<const clap_event_midi_t&> (eventHeader);
            sendMIDIInputEvent (event.port_index, { event.data, 3 });

            break;
        }
        default: break;
    }
}

inline void Plugin::Impl::clapPlugin_onMainThread()
{
}

// clap_plugin_audio_ports_t
inline uint32_t Plugin::Impl::clapAudioPorts_count (bool input)
{
    resetIfRequestIsPending();

    const auto& ports = input ? infoForInputAudioPorts : infoForOutputAudioPorts;
    return static_cast<uint32_t> (ports.size());
}

inline bool Plugin::Impl::clapAudioPorts_get (uint32_t index, bool input, clap_audio_port_info_t* out)
{
    const auto& ports = input ? infoForInputAudioPorts : infoForOutputAudioPorts;

    if (index >= ports.size())
        return false;

    *out = ports[index];
    return true;
}

// clap_plugin_note_ports_t
inline uint32_t Plugin::Impl::clapNotePorts_count (bool input)
{
    const auto& ports = input ? infoForInputNotePorts : infoForOutputNotePorts;
    return static_cast<uint32_t> (ports.size());
}

inline bool Plugin::Impl::clapNotePorts_get (uint32_t index, bool input, clap_note_port_info_t* out)
{
    const auto& ports = input ? infoForInputNotePorts : infoForOutputNotePorts;

    if (index >= ports.size())
        return false;

    *out = ports[index];
    return true;
}

// clap_plugin_latency_t
inline uint32_t Plugin::Impl::clapLatency_get()
{
    resetIfRequestIsPending();

    return static_cast<uint32_t> (patch.getFramesLatency());
}

// clap_plugin_state_t
inline bool Plugin::Impl::clapState_save (const clap_ostream_t* stream)
{
    const auto serialise = [this]() -> Bytes
    {
        // using stored `getFullStoreState` has some caveats:
        // * serialises the endpoint names. this is brittle. need a more robust solution for this + automation ids
        // * won't serialise values that are set to the current defaults

        const auto state = patch.getFullStoredState();
        const auto useLineBreaks = false;
        const auto serialised = choc::json::toString (state, useLineBreaks);
        return Bytes (reinterpret_cast<const uint8_t*> (serialised.data()),
                      reinterpret_cast<const uint8_t*> (serialised.data()) + serialised.size());
    };

    const auto& buffer = pendingStateToApplyWhenActivated ? pendingStateToApplyWhenActivated->serialised : serialise();
    const auto totalBytesToWrite = buffer.size();
    auto readPointer = buffer.data();
    size_t readIndex = 0;

    while (readIndex < totalBytesToWrite)
    {
        const auto desiredChunkSize = static_cast<uint32_t> (totalBytesToWrite - readIndex);
        const auto result = stream->write (stream, readPointer, desiredChunkSize);

        if (result < 0)
            return false;

        const auto numBytesWritten = static_cast<size_t> (result);

        readPointer += numBytesWritten;
        readIndex += numBytesWritten;
    }

    return totalBytesToWrite == readIndex;
}

inline bool Plugin::Impl::clapState_load (const clap_istream_t* stream)
{
    Bytes serialised;

    std::array<uint8_t, 4096> block {};
    int64_t result;

    while ((result = stream->read (stream, block.data(), block.size())) > 0)
        std::copy (block.data(), block.data() + static_cast<size_t> (result), std::back_inserter (serialised));

    if (result < 0)
        return false;

    if (serialised.empty())
        return false;

    try
    {
        const auto state = choc::json::parse ({ reinterpret_cast<const char*> (serialised.data()), serialised.size() });
        const auto activated = patch.isPlayable();

        if (! activated)
        {
            pendingStateToApplyWhenActivated = { serialised, state };
            return true;
        }

        patch.setFullStoredState (state);
        return true;
    }
    catch (...) {}

    return false;
}

// clap_plugin_params_t
inline uint32_t Plugin::Impl::clapParameters_count()
{
    return static_cast<uint32_t> (automatableParameterInfo.size());
}

inline bool Plugin::Impl::clapParameters_getInfo (uint32_t index, clap_param_info_t* out)
{
    if (index >= clapParameters_count())
        return false;

    *out = automatableParameterInfo[index];
    return true;
}

inline bool Plugin::Impl::clapParameters_getValue (clap_id id, double* out)
{
    if (auto parameterEntry = automatableParametersByHandle.find (id);
        parameterEntry != automatableParametersByHandle.end())
    {
        // TODO: make `PatchParameter::currentValue` an atomic
        auto clapValue = parameterEntry->second->currentValue;

        if (auto maybeMappers = automatableParameterMappingFunctionsByHandle.find (id);
            maybeMappers != automatableParameterMappingFunctionsByHandle.end())
        {
            clapValue = maybeMappers->second.toClapValue (clapValue);
        }

        *out = clapValue;
        return true;
    }

    return false;
}

inline bool Plugin::Impl::clapParameters_valueToText (clap_id id, double value, char* out, uint32_t capacity)
{
    if (auto parameterEntry = automatableParametersByHandle.find (id);
        parameterEntry != automatableParametersByHandle.end())
    {
        if (auto maybeMappers = automatableParameterMappingFunctionsByHandle.find (id);
            maybeMappers != automatableParameterMappingFunctionsByHandle.end())
        {
            value = maybeMappers->second.fromClapValue (static_cast<float> (value));
        }

        const auto& properties = parameterEntry->second->properties;
        copyAndNullTerminateTruncatingIfNecessary (properties.getValueAsString (static_cast<float> (value)),
                                                   out, capacity);
        return true;
    }

    return false;
}

inline bool Plugin::Impl::clapParameters_textToValue (clap_id id, const char* text, double* out)
{
    if (auto parameterEntry = automatableParametersByHandle.find (id);
        parameterEntry != automatableParametersByHandle.end())
    {
        if (auto maybeValue = parameterEntry->second->properties.getStringAsValue (text))
        {
            auto value = *maybeValue;

            if (auto maybeMappers = automatableParameterMappingFunctionsByHandle.find (id);
                maybeMappers != automatableParameterMappingFunctionsByHandle.end())
            {
                value = maybeMappers->second.toClapValue (value);
            }

            *out = value;
            return true;
        }
    }

    return false;
}

inline void Plugin::Impl::clapParameters_flush (const clap_input_events_t* inputQueue, const clap_output_events_t*)
{
    const auto eventCount = inputQueue->size (inputQueue);

    for (uint32_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
        dispatchEvent (*inputQueue->get (inputQueue, eventIndex));
}

// clap_plugin_gui_t
inline bool Plugin::Impl::clapGui_isApiSupported (const char* api, bool floating)
{
  #if CHOC_OSX
    return ! floating && std::string_view (api) == CLAP_WINDOW_API_COCOA;
  #elif CHOC_WINDOWS
    return ! floating && std::string_view (api) == CLAP_WINDOW_API_WIN32;
  #else
    (void) api;
    (void) floating;
    // TODO: support linux
    return false;
  #endif
}

inline bool Plugin::Impl::clapGui_getPreferredApi (const char** api, bool* floating)
{
  #if CHOC_OSX
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;

    return true;
  #elif CHOC_WINDOWS
    *api = CLAP_WINDOW_API_WIN32;
    *floating = false;

    return true;
  #else
    (void) api;
    (void) floating;
    // TODO: support linux
    return false;
  #endif
}

inline bool Plugin::Impl::clapGui_create (const char*, bool)
{
    editor = ViewHolder (patch, cachedViewScaleFactor);
    return true;
}

inline void Plugin::Impl::clapGui_destroy()
{
    editor = {};
}

inline bool Plugin::Impl::clapGui_setScale (double scaleFactor)
{
    cachedViewScaleFactor = scaleFactor;

    return editor && editor->setScaleFactor (scaleFactor);
}

inline bool Plugin::Impl::clapGui_getSize (uint32_t* width, uint32_t* height)
{
    if (! editor)
        return false;

    const auto size = editor->size();
    *width = size.width;
    *height = size.height;

    return true;
}

inline bool Plugin::Impl::clapGui_canResize()
{
    return editor && editor->resizable();
}

inline bool Plugin::Impl::clapGui_getResizeHints (clap_gui_resize_hints_t*)
{
    return {};
}

inline bool Plugin::Impl::clapGui_adjustSize (uint32_t*, uint32_t*)
{
    return {};
}

inline bool Plugin::Impl::clapGui_setSize (uint32_t width, uint32_t height)
{
    return editor && editor->setSize ({ width, height });
}

inline bool Plugin::Impl::clapGui_setParent (const clap_window_t* window)
{
    const auto toNativeHandle = [] (const auto* w)
    {
      #if CHOC_OSX
        return w->cocoa;
      #elif CHOC_WINDOWS
        return w->win32;
      #else
        (void) w;
        return nullptr; // TODO: support linux
      #endif
    };

    return editor && editor->setParent (toNativeHandle (window));
}

inline bool Plugin::Impl::clapGui_setTransient (const clap_window_t*)
{
    return {};
}

inline void Plugin::Impl::clapGui_suggestTitle (const char*)
{
}

inline bool Plugin::Impl::clapGui_show()
{
    return true;
}

inline bool Plugin::Impl::clapGui_hide()
{
    return true;
}

inline void Plugin::Impl::resetIfRequestIsPending()
{
    if (! isResetRequestPending)
        return;

    isResetRequestPending = false;
    clapPlugin_activate (frequency, maxBlockSize, maxBlockSize);
}

void Plugin::Impl::copyAndNullTerminateTruncatingIfNecessary (const std::string& from, char* to, size_t capacity)
{
    snprintf (to, capacity, "%s", from.c_str());
}

clap_event_midi_t Plugin::Impl::toClapMidiEvent (uint32_t sampleOffset,
                                                 uint16_t portIndex,
                                                 const choc::midi::ShortMessage& msg)
{
    clap_event_midi_t clapEvent {};

    clapEvent.header.size = sizeof (clap_event_midi_t);
    clapEvent.header.type = static_cast<uint16_t> (CLAP_EVENT_MIDI);
    clapEvent.header.time = sampleOffset;
    clapEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    clapEvent.header.flags = 0;
    clapEvent.port_index = portIndex;
    std::copy (std::begin (msg.midiData.bytes), std::end (msg.midiData.bytes), std::begin (clapEvent.data));

    return clapEvent;
}

clap_event_note_t Plugin::Impl::toClapNoteEvent (uint32_t sampleOffset,
                                                 uint16_t portIndex,
                                                 uint16_t noteType,
                                                 uint16_t channel,
                                                 uint16_t key,
                                                 double velocity)
{
    clap_event_note_t clapEvent {};

    clapEvent.header.size = sizeof (clap_event_note_t);
    clapEvent.header.type = noteType;
    clapEvent.header.time = sampleOffset;
    clapEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    clapEvent.header.flags = 0;
    clapEvent.note_id = -1;
    clapEvent.port_index = static_cast<int16_t> (portIndex);
    clapEvent.channel = static_cast<int16_t> (channel);
    clapEvent.key = static_cast<int16_t> (key);
    clapEvent.velocity = velocity;

    return clapEvent;
}

//==============================================================================
inline PluginPtr Plugin::create (const clap_plugin_descriptor_t& description,
                                 const clap_host_t& host,
                                 const std::filesystem::path& pathToManifest,
                                 const Environment& environment)
{
    const auto toOwned = [] (clap_plugin* p) -> PluginPtr
    {
        const auto deleter = [] (clap_plugin* ptr)
        {
            if (ptr)
            {
                ptr->destroy (ptr);
                delete ptr;
            }
        };

        return std::unique_ptr<clap_plugin, decltype (deleter)> (p, deleter);
    };

    auto plugin = toOwned (new clap_plugin_t);
    plugin->desc = std::addressof (description);
    plugin->plugin_data = new Plugin (*plugin, { host, pathToManifest, environment }); // deleted by `clap_plugin_t::destroy`

    return plugin;
}

inline Plugin::Plugin (clap_plugin& ownerToUse, const PluginFactoryArgs& argsToUse)
    : owner (ownerToUse),
      cachedArgs (argsToUse)
{
    owner.init = Plugin::clapPlugin_init;
    owner.destroy = Plugin::clapPlugin_destroy;
    owner.get_extension = Plugin::clapPlugin_getExtension;
}

inline Plugin::~Plugin() = default;

inline bool Plugin::clapPlugin_init (const clap_plugin_t* p)
{
    auto& self = unsafeCastToRef<Plugin> (p);

    try
    {
        auto& args = self.cachedArgs;
        self.impl = std::unique_ptr<Impl> (new Impl (args.host, args.pathToManifest, args.environment));
    }
    catch (...)
    {
        return false;
    }

    // clap_plugin_t
    self.owner.activate = [] (const clap_plugin_t* plugin, double frequencyToUse, uint32_t minFrameCount, uint32_t maxFrameCount) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapPlugin_activate (frequencyToUse, minFrameCount, maxFrameCount);
    };

    self.owner.deactivate = [] (const clap_plugin_t* plugin) -> void
    {
        unsafeCastToRef<Plugin> (plugin).impl->clapPlugin_deactivate();
    };

    self.owner.start_processing = [] (const clap_plugin_t* plugin) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapPlugin_startProcessing();
    };

    self.owner.stop_processing = [] (const clap_plugin_t* plugin) -> void
    {
        unsafeCastToRef<Plugin> (plugin).impl->clapPlugin_stopProcessing();
    };

    self.owner.reset = [] (const clap_plugin_t* plugin) -> void
    {
        unsafeCastToRef<Plugin> (plugin).impl->clapPlugin_reset();
    };

    self.owner.process = [] (const clap_plugin_t* plugin, const clap_process_t* process) -> clap_process_status
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapPlugin_process (process);
    };

    self.owner.on_main_thread = [] (const clap_plugin_t* plugin) -> void
    {
        unsafeCastToRef<Plugin> (plugin).impl->clapPlugin_onMainThread();
    };

    // clap_plugin_audio_ports_t
    self.audioPortsExtension.count = [] (const clap_plugin_t* plugin, bool input) -> uint32_t
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapAudioPorts_count (input);
    };

    self.audioPortsExtension.get = [] (const clap_plugin_t* plugin, uint32_t index, bool input, clap_audio_port_info_t* info) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapAudioPorts_get (index, input, info);
    };

    // clap_plugin_note_ports_t
    self.notePortsExtension.count = [] (const clap_plugin_t* plugin, bool input) -> uint32_t
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapNotePorts_count (input);
    };

    self.notePortsExtension.get = [] (const clap_plugin_t* plugin, uint32_t index, bool input, clap_note_port_info_t* info) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapNotePorts_get (index, input, info);
    };

    // clap_plugin_latency_t
    self.latencyExtension.get = [] (const clap_plugin_t* plugin) -> uint32_t
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapLatency_get();
    };

    // clap_plugin_state_t
    self.stateExtension.save = [] (const clap_plugin_t* plugin, const clap_ostream_t* stream) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapState_save (stream);
    };

    self.stateExtension.load = [] (const clap_plugin_t* plugin, const clap_istream_t* stream) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapState_load (stream);
    };

    // clap_plugin_params_t
    self.parametersExtension.count = [] (const clap_plugin_t* plugin) -> uint32_t
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapParameters_count();
    };

    self.parametersExtension.get_info = [] (const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* out) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapParameters_getInfo (index, out);
    };

    self.parametersExtension.get_value = [] (const clap_plugin_t* plugin, clap_id id, double* out) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapParameters_getValue (id, out);
    };

    self.parametersExtension.value_to_text = [] (const clap_plugin_t* plugin, clap_id id, double value, char* out, uint32_t capacity) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapParameters_valueToText (id, value, out, capacity);
    };

    self.parametersExtension.text_to_value = [] (const clap_plugin_t* plugin, clap_id id, const char* text, double* out) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapParameters_textToValue (id, text, out);
    };

    self.parametersExtension.flush = [] (const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out) -> void
    {
        unsafeCastToRef<Plugin> (plugin).impl->clapParameters_flush (in, out);
    };

    // clap_plugin_gui_t
    self.guiExtension.is_api_supported = [] (const clap_plugin_t* plugin, const char* api, bool floating) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_isApiSupported (api, floating);
    };

    self.guiExtension.get_preferred_api = [] (const clap_plugin_t* plugin, const char** api, bool* floating) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_getPreferredApi (api, floating);
    };

    self.guiExtension.create = [] (const clap_plugin_t* plugin, const char* api, bool floating) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_create (api, floating);
    };

    self.guiExtension.destroy = [] (const clap_plugin_t* plugin) -> void
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_destroy();
    };

    self.guiExtension.set_scale = [] (const clap_plugin_t* plugin, double scale) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_setScale (scale);
    };

    self.guiExtension.get_size = [] (const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_getSize (width, height);
    };

    self.guiExtension.can_resize = [] (const clap_plugin_t* plugin) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_canResize();
    };

    self.guiExtension.get_resize_hints = [] (const clap_plugin_t* plugin, clap_gui_resize_hints_t* hints) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_getResizeHints (hints);
    };

    self.guiExtension.adjust_size = [] (const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_adjustSize (width, height);
    };

    self.guiExtension.set_size = [] (const clap_plugin_t* plugin, uint32_t width, uint32_t height) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_setSize (width, height);
    };

    self.guiExtension.set_parent = [] (const clap_plugin_t* plugin, const clap_window_t* window) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_setParent (window);
    };

    self.guiExtension.set_transient = [] (const clap_plugin_t* plugin, const clap_window_t* window) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_setTransient (window);
    };

    self.guiExtension.suggest_title = [] (const clap_plugin_t* plugin, const char* title) -> void
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_suggestTitle (title);
    };

    self.guiExtension.show = [] (const clap_plugin_t* plugin) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_show();
    };

    self.guiExtension.hide = [] (const clap_plugin_t* plugin) -> bool
    {
        return unsafeCastToRef<Plugin> (plugin).impl->clapGui_hide();
    };

    return true;
}

inline void Plugin::clapPlugin_destroy (const clap_plugin_t* plugin)
{
    auto* self = unsafeCastToPtr<Plugin> (plugin);
    delete self;
}

inline const void* Plugin::clapPlugin_getExtension (const clap_plugin_t* plugin, const char* id)
{
    auto& self = unsafeCastToRef<Plugin> (plugin);
    auto stringID = std::string_view (id);

    if (stringID == CLAP_EXT_LATENCY)      return std::addressof (self.latencyExtension);
    if (stringID == CLAP_EXT_STATE)        return std::addressof (self.stateExtension);
    if (stringID == CLAP_EXT_AUDIO_PORTS)  return std::addressof (self.audioPortsExtension);
    if (stringID == CLAP_EXT_NOTE_PORTS)   return std::addressof (self.notePortsExtension);
    if (stringID == CLAP_EXT_PARAMS)       return std::addressof (self.parametersExtension);
    if (stringID == CLAP_EXT_GUI)          return std::addressof (self.guiExtension);

    return {};
}

inline void Plugin::update (const std::filesystem::path& pathToManifest)
{
    if (impl)
        impl->update (pathToManifest);
}

//==============================================================================

using PluginFactoryFn = auto (*) (const clap_plugin_descriptor_t&, const clap_host_t&) -> const clap_plugin_t*;
using DescriptorFactoryFn = auto (*) () -> clap_plugin_descriptor_t;

template <DescriptorFactoryFn makeDescriptor, PluginFactoryFn makePlugin>
const void* factorySingleton (const char* factoryId)
{
    static const clap_plugin_descriptor_t descriptor = makeDescriptor();

    static const auto factory = ([]
    {
        clap_plugin_factory_t f;
        f.get_plugin_count = [] (const clap_plugin_factory*) -> uint32_t { return 1; };
        f.get_plugin_descriptor = [] (const clap_plugin_factory*, uint32_t) -> const clap_plugin_descriptor_t*
        {
            return std::addressof (descriptor);
        };
        f.create_plugin = [] (const clap_plugin_factory*, const clap_host_t* host, const char* id) -> const clap_plugin_t*
        {
            if (! clap_version_is_compatible (host->clap_version))
                return nullptr;

            if (std::string_view (id) == descriptor.id)
                return makePlugin (descriptor, *host);

            return nullptr;
        };
        return f;
    })();

    if (std::string_view (factoryId) == CLAP_PLUGIN_FACTORY_ID)
        return std::addressof (factory);

    return nullptr;
}

// currently we generate the descriptor at runtime, so we need some storage for the strings.
// this could be moved to generate time.
struct StoredDescriptor
{
    std::string id;
    std::string name;
    std::string vendor;
    std::string url;
    std::string manualUrl;
    std::string supportUrl;
    std::string version;
    std::string description;

    std::array<const char*, 2> features {{ nullptr, nullptr }};
};

inline StoredDescriptor toStoredDescriptor (const cmaj::PatchManifest& manifest,
                                            const cmaj::EndpointDetailsList& inputs,
                                            const cmaj::EndpointDetailsList& outputs)
{
    const auto deducePluginType = [] (const auto& m, const auto& ins, const auto& outs)
    {
        if (m.isInstrument)
            return CLAP_PLUGIN_FEATURE_INSTRUMENT;

        // TODO: extract into shared helper (this is duplicated from elsewhere)
        bool hasMidiIn =  false;
        bool hasMidiOut = false;
        bool hasAudioIn = false;
        bool hasAudioOut = false;

        for (auto& e : ins)
        {
            if (e.isMIDI())
                hasMidiIn = true;
            else if (e.getNumAudioChannels() != 0)
                hasAudioIn = true;
        }

        for (auto& e : outs)
        {
            if (e.isMIDI())
                hasMidiOut = true;
            else if (e.getNumAudioChannels() != 0)
                hasAudioOut = true;
        }

        const auto usesAudio = hasAudioIn || hasAudioOut;
        const auto usesMidi = hasMidiIn || hasMidiOut;

        if (! usesAudio && usesMidi)
            return CLAP_PLUGIN_FEATURE_NOTE_EFFECT;

        return CLAP_PLUGIN_FEATURE_AUDIO_EFFECT;
    };

    return
    {
        manifest.ID,
        manifest.name,
        manifest.manufacturer,
        manifest.manifest["URL"].getWithDefault<std::string> ({}),
        {},
        {},
        manifest.version,
        manifest.description,
        {{ deducePluginType (manifest, inputs, outputs), nullptr }}
    };
}

// caller must ensure that the provided `StoredDescriptor` lives longer than the populated descriptor
inline clap_plugin_descriptor toClapDescriptor (const StoredDescriptor& storage)
{
    return
    {
        CLAP_VERSION,
        storage.id.c_str(),
        storage.name.c_str(),
        storage.vendor.c_str(),
        storage.url.c_str(),
        storage.manualUrl.c_str(),
        storage.supportUrl.c_str(),
        storage.version.c_str(),
        storage.description.c_str(),
        storage.features.data()
    };
}

template <typename PatchClass>
StoredDescriptor createGeneratedCppStoredDescriptor()
{
    auto environment = createGeneratedCppEnvironment<PatchClass>();
    auto engine = environment.createEngine();
    auto manifest = environment.makePatchManifest (PatchClass::filename);

    return toStoredDescriptor (manifest, engine.getInputEndpoints(), engine.getOutputEndpoints());
}

template <typename PatchClass>
clap_plugin_descriptor_t makeGeneratedCppPluginDescriptor()
{
    // TODO: this could all happen at generated time, which would avoid another roundtrip via
    // a `PatchManifest`, and avoid needing additional runtime storage, as we could just
    // generate string literals.
    static const auto storedDescriptor = createGeneratedCppStoredDescriptor<PatchClass>();

    return toClapDescriptor (storedDescriptor);
}

template <typename PatchClass>
const clap_plugin_t* makeGeneratedCppPlugin (const clap_plugin_descriptor_t& descriptor, const clap_host_t& host)
{
    const auto environment = createGeneratedCppEnvironment<PatchClass>();

    return create (descriptor, host, PatchClass::filename, environment).release();
}

template <typename PatchClass>
auto makeGeneratedCppPluginFactory()
{
    return factorySingleton<makeGeneratedCppPluginDescriptor<PatchClass>, makeGeneratedCppPlugin<PatchClass>>;
}

} // namespace detail

//==============================================================================
inline PluginPtr create (const clap_plugin_descriptor& descriptor,
                         const clap_host& host,
                         const std::filesystem::path& pathToManifest,
                         const Environment& environment)
{
    return detail::Plugin::create (descriptor, host, pathToManifest, environment);
}

struct PluginPtrUpdatePair
{
    using UpdateFn = std::function<void(const std::filesystem::path&)>;
    PluginPtr plugin;
    UpdateFn update;
};

inline PluginPtrUpdatePair createUpdatablePlugin (const clap_plugin_descriptor_t& descriptor,
                                                  const clap_host_t& host,
                                                  const std::filesystem::path& pathToManifest,
                                                  const Environment& environment)
{
    auto plugin = create (descriptor, host, pathToManifest, environment);
    auto& glue = detail::unsafeCastToRef<detail::Plugin> (plugin.get());

    return
    {
        std::move (plugin),
        [&glue] (const auto& nextPathToManifest) { glue.update (nextPathToManifest); }
    };
}

template <typename PatchClass>
clap_plugin_entry_t createGeneratedCppPluginEntryPoint()
{
    return
    {
        CLAP_VERSION,
        [] (auto*) { return true; }, // shared library init
        [] {}, // shared library destroy
        detail::makeGeneratedCppPluginFactory<PatchClass>()
    };
}

} // namespace cmaj::plugin::clap
