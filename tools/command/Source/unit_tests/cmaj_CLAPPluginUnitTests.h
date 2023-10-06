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

#include "../../../modules/plugin/include/clap/cmaj_CLAPPlugin.h"

#include "cmajor/helpers/cmaj_PatchHelpers.h"

#include "choc/platform/choc_Assert.h"
#include "choc/tests/choc_UnitTest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <sstream>
#include <vector>

namespace cmaj::plugin::clap::test
{

template <typename Context>
clap_input_events_t toInputEventQueue (Context& ctx)
{
    return
    {
        std::addressof (ctx),
        [] (const clap_input_events_t* self) -> uint32_t
        {
            const auto& context = *reinterpret_cast <const Context*> (self->ctx);
            return static_cast<uint32_t> (context.size());
        },
        [] (const clap_input_events_t* self, uint32_t index) -> const clap_event_header_t*
        {
            const auto& context = *reinterpret_cast <const Context*> (self->ctx);
            return std::addressof (context[index].header);
        }
    };
}

inline clap_event_param_value_t makeParameterEvent (uint32_t sampleOffset, clap_id id, float value)
{
    clap_event_param_value_t clapEvent {};
    clapEvent.header.size = sizeof (clap_event_param_value_t);
    clapEvent.header.type = static_cast<uint16_t> (CLAP_EVENT_PARAM_VALUE);
    clapEvent.header.time = sampleOffset;
    clapEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    clapEvent.header.flags = 0;
    clapEvent.param_id = id;
    clapEvent.value = value;
    return clapEvent;
}

inline clap_event_midi_t makeMidiEvent (uint32_t sampleOffset, uint16_t portIndex, const std::array<uint8_t, 3>& bytes)
{
    clap_event_midi_t clapEvent {};
    clapEvent.header.size = sizeof (clap_event_midi_t);
    clapEvent.header.type = static_cast<uint16_t> (CLAP_EVENT_MIDI);
    clapEvent.header.time = sampleOffset;
    clapEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    clapEvent.header.flags = 0;
    clapEvent.port_index = portIndex;
    std::copy (std::begin (bytes), std::end (bytes), std::begin (clapEvent.data));

    return clapEvent;
}

inline clap_event_note_t makeNoteEvent (uint32_t sampleOffset, uint16_t noteType, uint16_t portIndex, uint16_t channel, uint16_t key, double velocity)
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

template <typename T, T TimeFactor>
T toFixedPoint (double v)
{
    return static_cast<T> (std::round (TimeFactor * v)); // N.B. as per clap/fixedpoint.h
}

inline auto toFixedPointBeatTime (double beat)
{
    return toFixedPoint<clap_beattime, CLAP_BEATTIME_FACTOR> (beat);
}

inline auto toFixedPointSeconds (double seconds)
{
    return toFixedPoint<clap_sectime, CLAP_SECTIME_FACTOR> (seconds);
}

inline clap_event_transport_t makeTransportStateEventWithFlags (uint32_t sampleOffset, uint32_t flags)
{
    clap_event_transport_t clapEvent {};
    clapEvent.header.size = sizeof (clap_event_transport_t);
    clapEvent.header.type = static_cast<uint16_t> (CLAP_EVENT_TRANSPORT);
    clapEvent.header.time = sampleOffset;
    clapEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;

    clapEvent.flags = flags;

    clapEvent.song_pos_beats = {};
    clapEvent.song_pos_seconds = {};

    clapEvent.tempo = {};
    clapEvent.tempo_inc = {};

    clapEvent.loop_start_beats = {};
    clapEvent.loop_end_beats = {};
    clapEvent.loop_start_seconds = {};
    clapEvent.loop_end_seconds = {};

    clapEvent.bar_start = {};
    clapEvent.bar_number = {};

    clapEvent.tsig_num = {};
    clapEvent.tsig_denom = {};

    return clapEvent;
}

struct ScopedActivator
{
    ScopedActivator (const clap_plugin_t& pluginToUse,
                     double frequency,
                     uint32_t minBlockSize,
                     uint32_t maxBlockSize)
        : plugin (pluginToUse),
          activated (plugin.activate (std::addressof (pluginToUse), frequency, minBlockSize, maxBlockSize))
    {
    }

    ~ScopedActivator()
    {
        if (activated)
            plugin.deactivate (std::addressof (plugin));
    }

    const clap_plugin_t& plugin;
    const bool activated;
};

template <size_t frameCount>
struct StubStereoAudioPortBackingData
{
    std::array<float, frameCount> left {{}};
    std::array<float, frameCount> right {{}};
    std::array<float*, 2> buffers { { left.data(), right.data() } };
};

template <size_t ChannelCount, size_t FrameCount>
struct StubAudioPortBackingData
{
    static constexpr size_t channelCount = ChannelCount;

    std::array<std::array<float, FrameCount>, channelCount> channels {{}};
    std::array<float*, channelCount> buffers {};

    StubAudioPortBackingData()
    {
        for (size_t i = 0; i < channelCount; ++i)
            buffers[i] = channels[i].data();
    }
};

template <typename Backing>
clap_audio_buffer_t toClapAudioBuffer (Backing&& backing)
{
    return
    {
        /*.data32 = */backing.buffers.data(),
        /*.data64 = */nullptr,
        /*.channel_count = */static_cast<uint32_t> (backing.buffers.size()),
        /*.latency = */0,
        /*.constant_mask = */0
    };
}

struct HostData
{
    using OnLatencyChangedFn = std::function<void()>;
    using OnRescanNotePortsFn = std::function<void(uint32_t)>;
    using OnRescanAudioPortsFn = std::function<void(uint32_t)>;
    using OnRescanParametersFn = std::function<void(clap_param_rescan_flags)>;
    struct ClearParameterArgs
    {
        clap_id paramId;
        clap_param_clear_flags flags;
    };
    using OnClearParameterFn = std::function<void(const ClearParameterArgs&)>;
    using OnRequestFlushParametersFn = std::function<void()>;
    using OnRequestRestartFn = std::function<void(const clap_plugin_t*)>;

    struct Callbacks
    {
        OnLatencyChangedFn onLatencyChanged;
        OnRescanNotePortsFn onRescanNotePorts;
        OnRescanAudioPortsFn onRescanAudioPorts;
        OnRescanParametersFn onRescanParameters;
        OnClearParameterFn onClearParameter;
        OnRequestFlushParametersFn onRequestFlushParameters;
        OnRequestRestartFn onRequestRestart;
    };

    HostData (uint32_t notePortDialects, const Callbacks& callbacksToUse)
        : supportedNotePortDialects (notePortDialects),
          callbacks (callbacksToUse)
    {
        notePortsExtension.supported_dialects = [] (const auto* self) -> uint32_t
        {
            const auto& impl = *reinterpret_cast <const HostData*> (self->host_data);
            return impl.supportedNotePortDialects;
        };

        notePortsExtension.rescan = [] (const auto* self, auto flags)
        {
            const auto& impl = *reinterpret_cast <const HostData*> (self->host_data);
            if (impl.callbacks.onRescanNotePorts)
                impl.callbacks.onRescanNotePorts (flags);
        };

        audioPortsExtension.is_rescan_flag_supported = [] (const auto* self, auto) -> bool
        {
            const auto& impl = *reinterpret_cast <const HostData*> (self->host_data);
            return impl.callbacks.onRescanAudioPorts != nullptr;
        };

        audioPortsExtension.rescan = [] (const auto* self, auto flags)
        {
            const auto& impl = *reinterpret_cast <const HostData*> (self->host_data);
            if (impl.callbacks.onRescanAudioPorts)
                impl.callbacks.onRescanAudioPorts (flags);
        };

        parametersExtension.rescan = [] (const auto* self, auto flags)
        {
            const auto& impl = *reinterpret_cast <const HostData*> (self->host_data);
            if (impl.callbacks.onRescanParameters)
                impl.callbacks.onRescanParameters (flags);
        };

        parametersExtension.clear = [] (const auto* self, auto id, auto flags)
        {
            const auto& impl = *reinterpret_cast <const HostData*> (self->host_data);
            if (impl.callbacks.onClearParameter)
                impl.callbacks.onClearParameter ({ id, flags });
        };

        parametersExtension.request_flush = [] (const auto* self)
        {
            const auto& impl = *reinterpret_cast <const HostData*> (self->host_data);
            if (impl.callbacks.onRequestFlushParameters)
                impl.callbacks.onRequestFlushParameters();
        };

        latencyExtension.changed = [] (const auto* self)
        {
            const auto& impl = *reinterpret_cast <const HostData*> (self->host_data);
            if (impl.callbacks.onLatencyChanged)
                impl.callbacks.onLatencyChanged();
        };
    }

    const void* getExtension (const char* id) const
    {
        if (std::string_view (id) == CLAP_EXT_NOTE_PORTS)
            return std::addressof (notePortsExtension);

        if (std::string_view (id) == CLAP_EXT_AUDIO_PORTS)
            return std::addressof (audioPortsExtension);

        if (std::string_view (id) == CLAP_EXT_PARAMS)
            return std::addressof (parametersExtension);

        if (std::string_view (id) == CLAP_EXT_LATENCY)
            return std::addressof (latencyExtension);

        return nullptr;
    }

    clap_plugin_t* plugin = nullptr;

    uint32_t supportedNotePortDialects = 0;
    Callbacks callbacks;

    clap_host_note_ports_t notePortsExtension;
    clap_host_audio_ports_t audioPortsExtension;
    clap_host_params_t parametersExtension;
    clap_host_latency_t latencyExtension;
};

struct StubHostConfig
{
    uint32_t supportedNotePortDialects = {};
    HostData::OnRequestRestartFn onRestartRequested;
};

struct StubHost : public clap_host_t
{
    StubHost (const StubHostConfig& config = {})
        : data (config.supportedNotePortDialects, {
            [this] { ++latencyChangedCallCount; },
            [this] (auto flags) { rescanNotePortsCallArgs.push_back (flags); },
            [this] (auto flags) { rescanAudioPortsCallArgs.push_back (flags); },
            [this] (auto flags) { rescanParametersCallArgs.push_back (flags); },
            [this] (const auto& args) { clearParameterCallArgs.push_back (args); },
            [this] { ++requestFlushParametersCallCount; },
            [this, onRestartRequested = config.onRestartRequested] (auto* p)
            {
                if (onRestartRequested)
                    onRestartRequested (p);

                ++requestRestartCallCount;
            }
        })
    {
        clap_version = CLAP_VERSION;
        host_data = std::addressof (data);
        get_extension = [] (const auto* self, const auto* id) -> const void*
        {
            const auto& impl = *reinterpret_cast <const HostData*> (self->host_data);
            return impl.getExtension (id);
        };
        request_restart = [] (const auto* self)
        {
            const auto& impl = *reinterpret_cast <const HostData*> (self->host_data);
            if (impl.callbacks.onRequestRestart)
                impl.callbacks.onRequestRestart (impl.plugin);
            // N.B. for now it is up to the test code to deactivate / activate the plugin
        };
    }

    ~StubHost() = default;

    StubHost (StubHost&&) = delete;
    StubHost (const StubHost&) = delete;
    StubHost& operator= (StubHost&&) = delete;
    StubHost& operator= (const StubHost&) = delete;

    HostData data;

    void clearCounts()
    {
        rescanAudioPortsCallArgs.clear();
        rescanNotePortsCallArgs.clear();
        rescanParametersCallArgs.clear();
        clearParameterCallArgs.clear();

        requestFlushParametersCallCount = 0;
        latencyChangedCallCount = 0;
        requestRestartCallCount = 0;
    }

    std::vector<uint32_t> rescanAudioPortsCallArgs {};
    std::vector<uint32_t> rescanNotePortsCallArgs {};
    std::vector<clap_param_rescan_flags> rescanParametersCallArgs {};
    std::vector<HostData::ClearParameterArgs> clearParameterCallArgs {};
    size_t requestFlushParametersCallCount {};
    size_t latencyChangedCallCount {};
    size_t requestRestartCallCount {};
};

template <typename T>
auto getExtension (const clap_plugin* plugin, const char* extension)
{
    return reinterpret_cast<const T*> (plugin->get_extension (plugin, extension));
}

inline std::optional<clap_audio_port_info_t> getPortInfo (const clap_plugin& plugin, uint32_t index, bool isInput)
{
    clap_audio_port_info_t out;

    if (auto* ext = getExtension<clap_plugin_audio_ports_t> (std::addressof (plugin), CLAP_EXT_AUDIO_PORTS))
        if (ext->get (std::addressof (plugin), index, isInput, std::addressof (out)))
            return out;

    return {};
}

template <typename StringType>
StringType toString (const char* bytes, size_t size)
{
    const auto length = std::distance (bytes, std::find (bytes, bytes + size, '\0'));
    return { bytes, static_cast<size_t> (length) };
}

struct InMemoryFile
{
    std::filesystem::path path;
    std::string content;
};

using InMemoryFiles = std::vector<InMemoryFile>;

inline Environment::VirtualFileSystem createInMemoryFileSystem (const InMemoryFiles& files)
{
    const auto findFile = [] (const auto& path, const auto& all)
    {
        return std::find_if (all.begin(), all.end(), [&] (const auto& f) { return f.path == path; });
    };

    const auto allFiles = std::make_shared<InMemoryFiles> (files);

    const auto toSource = [=](const auto& path) -> std::string
    {
        if (const auto it = findFile (path, *allFiles); it != allFiles->end())
            return it->content;

        return {};
    };

    return
    {
        [=] (const auto& path) -> std::unique_ptr<std::istream>
        {
            if (const auto source = toSource (path); ! source.empty())
                return std::make_unique<std::istringstream> (source);

            return {};
        },
        [] (const auto& path) { return path; },
        [] (const auto&) -> std::filesystem::file_time_type { return {}; },
        [=] (const auto& path)
        {
            return findFile (path, *allFiles) != allFiles->end();
        }
    };
}

inline cmaj::plugin::Environment createJITEnvironmentWithInMemoryFileSystem (const InMemoryFiles& files)
{
    return
    {
        cmaj::plugin::Environment::EngineType::JIT,
        [] { return Engine::create(); },
        createInMemoryFileSystem (files)
    };
}

// N.B. this uses static storage for the descriptor. be careful not to construct more than one in the same scope.
template <typename PatchClass>
auto makeGeneratedCppPlugin (const clap_host_t& host)
{
    clap_plugin_entry_t entry =  cmaj::plugin::clap::createGeneratedCppPluginEntryPoint<PatchClass> ();
    auto* factory = reinterpret_cast<const clap_plugin_factory_t*> (entry.get_factory (CLAP_PLUGIN_FACTORY_ID));
    CHOC_ASSERT (factory->get_plugin_count (factory) == 1);

    const auto toOwned = [] (auto* p)
    {
        const auto deleter = [] (auto* ptr)
        {
            if (ptr)
            {
                ptr->destroy (ptr);
                delete ptr;
            }
        };
        return std::unique_ptr<const clap_plugin, decltype (deleter)> (p, deleter);
    };

    const auto* descriptor = factory->get_plugin_descriptor (factory, 0);

    return toOwned (factory->create_plugin (factory, std::addressof (host), descriptor->id));
}

struct StubGeneratedCppPerformer
{
    StubGeneratedCppPerformer() = default;
    ~StubGeneratedCppPerformer() = default;

    static constexpr uint32_t maxFramesPerBlock  = 512;
    static constexpr uint32_t eventBufferSize    = 32;
    static constexpr uint32_t maxOutputEventSize = 0;
    static constexpr double   latency            = 0.000000;

    static constexpr uint32_t getEndpointHandleForName (std::string_view endpointName)
    {
        (void) endpointName;
        return 0;
    }

    static constexpr const char* programDetailsJSON = R"JSON({
      "mainProcessor": "Test",
      "inputs": [
        {
          "endpointID": "in",
          "endpointType": "stream",
          "dataType": {
            "type": "vector",
            "element": {
              "type": "float32"
            },
            "size": 2
          },
          "purpose": "audio in",
          "numAudioChannels": 2
        },
        {
          "endpointID": "gain",
          "endpointType": "value",
          "dataType": {
            "type": "float32"
          },
          "annotation": {
            "name": "Gain",
            "min": 0,
            "max": 1,
            "init": 0.75
          },
          "purpose": "parameter"
        }
      ],
      "outputs": [
        {
          "endpointID": "out",
          "endpointType": "stream",
          "dataType": {
            "type": "vector",
            "element": {
              "type": "float32"
            },
            "size": 2
          },
          "purpose": "audio out",
          "numAudioChannels": 2
        }
      ]
    })JSON";

    void initialise (int32_t sessionID, double frequency)
    {
        (void) sessionID;
        (void) frequency;
    }

    void advance (int32_t frames)
    {
        (void) frames;
    }

    void copyOutputValue (uint32_t endpointHandle, void* dest)
    {
        (void) endpointHandle; (void) dest;
    }

    void copyOutputFrames (uint32_t endpointHandle, void* dest, uint32_t numFramesToCopy)
    {
        (void) endpointHandle;
        (void) dest;
        (void) numFramesToCopy;
    }

    uint32_t getNumOutputEvents (uint32_t endpointHandle)
    {
        (void) endpointHandle;

        return {};
    }

    void resetOutputEventCount (uint32_t endpointHandle)
    {
        (void) endpointHandle;
    }

    uint32_t getOutputEventType (uint32_t endpointHandle, uint32_t index)
    {
        (void) endpointHandle; (void) index;

        return {};
    }

    static uint32_t getOutputEventDataSize (uint32_t endpointHandle, uint32_t typeIndex)
    {
        (void) endpointHandle; (void) typeIndex;

        return 0;
    }

    uint32_t readOutputEvent (uint32_t endpointHandle, uint32_t index, void* dest)
    {
        (void) endpointHandle; (void) index; (void) dest;

        return {};
    }

    void addEvent (uint32_t endpointHandle, uint32_t typeIndex, const void* eventData)
    {
        (void) endpointHandle; (void) typeIndex; (void) eventData;
    }

    void setValue (uint32_t endpointHandle, const void* value, int32_t frames)
    {
        (void) endpointHandle;
        (void) value;
        (void) frames;
    }

    void setInputFrames (uint32_t endpointHandle, const void* frameData, uint32_t numFrames, uint32_t numTrailingFramesToClear)
    {
        (void) endpointHandle;
        (void) frameData;
        (void) numFrames;
        (void) numTrailingFramesToClear;
    }

    const char* getStringForHandle (uint32_t handle, size_t& stringLength)
    {
        (void) handle; (void) stringLength;
        return "";
    }
};

struct StubGeneratedCppPatch
{
    using PerformerClass = StubGeneratedCppPerformer;
    static constexpr auto filename = "test.cmajorpatch";

    struct File { std::string_view name, content; };

    static constexpr const char* test_cmajorpatch =
        R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,
            "source": ["test.cmajor"]
        })";

    static constexpr std::array files =
    {
        File { "test.cmajorpatch", test_cmajorpatch }
    };
};

//==============================================================================

inline void runUnitTests (choc::test::TestProgress& progress)
{
    constexpr auto hasStaticallyLinkedPerformer = []
    {
      #ifdef CMAJOR_DLL
        return CMAJOR_DLL == 0;
      #else
        return false;
      #endif
    };
    static_assert (hasStaticallyLinkedPerformer());

    CHOC_CATEGORY (CLAP);

    {
        CHOC_TEST (LatencyReporting)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,
            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph G [[ main ]]
            {
                input stream float in;
                output stream float out;
                connection in -> P.in;
                connection P.out -> out;
            }

            processor P
            {
                input stream float in;
                output stream float out;
                processor.latency = 32;
                void main() { loop { out <- in; advance(); } }
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        // execute
        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_EXPECT_TRUE (deactivateOnExit.activated);

        // verify
        auto* maybeLatencyExtension = plugin->get_extension (plugin.get(), CLAP_EXT_LATENCY);
        CHOC_ASSERT (maybeLatencyExtension != nullptr);

        const auto& latencyExtension = *reinterpret_cast<const clap_plugin_latency_t*> (maybeLatencyExtension);

        CHOC_EXPECT_EQ (host.latencyChangedCallCount, static_cast<size_t> (1));
        CHOC_EXPECT_EQ (latencyExtension.get (plugin.get()), static_cast<uint32_t> (32));
    }

    {
        CHOC_TEST (ParameterAnnotationMapping)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "synth",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                input event float defaultRangeContinuousWithInitialValue [[ name: "Default Range Continuous w/ initial value", init: 0.5 ]];
                input event float customRangeFromZeroContinuous [[ name: "Custom Range (from zero) Continuous", min: 0, max: 100 ]];
                input event float customRangeFromNonZeroContinuous [[ name: "Custom Range (from non-zero) Continuous", min: 10, max: 20 ]];
                input event float booleanSwitch [[ name: "Boolean Switch", boolean ]];
                input event float defaultRangeTextOptions [[ name: "Default Range Text Options", text: "I|II|III" ]];
                input event float customRangeFromZeroTextOptions [[ name: "Custom Range (from zero) Text Options", text: "I|II|III", min: 0, max: 100 ]];
                input event float customRangeFromNonZeroTextOptions [[ name: "Custom Range (from non-zero) Text Options", text: "I|II|III", min: 25, max: 75 ]];
                input event float explicitlyDiscreteFromZero [[ name: "Explicitly Discrete Range (from zero) w/ initial value", max: 4, step: 1, init: 2, discrete ]];

                input event float nonParameter;

                output event float out;
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        // execute
        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_EXPECT_TRUE (deactivateOnExit.activated);

        // verify
        CHOC_ASSERT (! host.rescanParametersCallArgs.empty());
        CHOC_EXPECT_EQ (host.rescanParametersCallArgs.size(), static_cast<size_t> (1));
        CHOC_EXPECT_EQ (host.rescanParametersCallArgs.front(), static_cast<clap_param_rescan_flags> (CLAP_PARAM_RESCAN_ALL));

        auto* maybeParametersExtension = plugin->get_extension (plugin.get(), CLAP_EXT_PARAMS);
        CHOC_ASSERT (maybeParametersExtension != nullptr);

        const auto& parametersExtension = *reinterpret_cast<const clap_plugin_params_t*> (maybeParametersExtension);

        CHOC_EXPECT_EQ (parametersExtension.count (plugin.get()), static_cast<uint32_t> (8));

        // N.B. currently not checking the ID. IDs are currently the endpoint handles, which is brittle. improve this.
        {
            clap_param_info_t out {};
            CHOC_EXPECT_TRUE (parametersExtension.get_info (plugin.get(), 0, std::addressof (out)));
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_AUTOMATABLE);
            CHOC_EXPECT_EQ (toString<std::string_view> (out.name, CLAP_NAME_SIZE), std::string_view ("Default Range Continuous w/ initial value"));
            CHOC_EXPECT_NEAR (out.min_value, 0.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.max_value, 1.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.default_value, 0.5, 0.0001f);
        }

        {
            clap_param_info_t out {};
            CHOC_EXPECT_TRUE (parametersExtension.get_info (plugin.get(), 1, std::addressof (out)));
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_AUTOMATABLE);
            CHOC_EXPECT_EQ (toString<std::string_view> (out.name, CLAP_NAME_SIZE), std::string_view ("Custom Range (from zero) Continuous"));
            CHOC_EXPECT_NEAR (out.min_value, 0.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.max_value, 100.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.default_value, 0.0, 0.0001f);
        }

        {
            clap_param_info_t out {};
            CHOC_EXPECT_TRUE (parametersExtension.get_info (plugin.get(), 2, std::addressof (out)));
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_AUTOMATABLE);
            CHOC_EXPECT_EQ (toString<std::string_view> (out.name, CLAP_NAME_SIZE), std::string_view ("Custom Range (from non-zero) Continuous"));
            CHOC_EXPECT_NEAR (out.min_value, 10.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.max_value, 20.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.default_value, 10.0, 0.0001f);
        }

        {
            clap_param_info_t out {};
            CHOC_EXPECT_TRUE (parametersExtension.get_info (plugin.get(), 3, std::addressof (out)));
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_AUTOMATABLE);
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_STEPPED);
            CHOC_EXPECT_EQ (toString<std::string_view> (out.name, CLAP_NAME_SIZE), std::string_view ("Boolean Switch"));
            CHOC_EXPECT_NEAR (out.min_value, 0.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.max_value, 1.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.default_value, 0.0, 0.0001f);
        }

        {
            clap_param_info_t out {};
            CHOC_EXPECT_TRUE (parametersExtension.get_info (plugin.get(), 4, std::addressof (out)));
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_AUTOMATABLE);
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_STEPPED);
            CHOC_EXPECT_EQ (toString<std::string_view> (out.name, CLAP_NAME_SIZE), std::string_view ("Default Range Text Options"));
            CHOC_EXPECT_NEAR (out.min_value, 0.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.max_value, 2.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.default_value, 0.0, 0.0001f);
        }

        {
            clap_param_info_t out {};
            CHOC_EXPECT_TRUE (parametersExtension.get_info (plugin.get(), 5, std::addressof (out)));
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_AUTOMATABLE);
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_STEPPED);
            CHOC_EXPECT_EQ (toString<std::string_view> (out.name, CLAP_NAME_SIZE), std::string_view ("Custom Range (from zero) Text Options"));
            CHOC_EXPECT_NEAR (out.min_value, 0.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.max_value, 2.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.default_value, 0.0, 0.0001f);
        }

        {
            clap_param_info_t out {};
            CHOC_EXPECT_TRUE (parametersExtension.get_info (plugin.get(), 6, std::addressof (out)));
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_AUTOMATABLE);
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_STEPPED);
            CHOC_EXPECT_EQ (toString<std::string_view> (out.name, CLAP_NAME_SIZE), std::string_view ("Custom Range (from non-zero) Text Options"));
            CHOC_EXPECT_NEAR (out.min_value, 0.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.max_value, 2.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.default_value, 0.0, 0.0001f);
        }

        {
            clap_param_info_t out {};
            CHOC_EXPECT_TRUE (parametersExtension.get_info (plugin.get(), 7, std::addressof (out)));
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_AUTOMATABLE);
            CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_STEPPED);
            CHOC_EXPECT_EQ (toString<std::string_view> (out.name, CLAP_NAME_SIZE), std::string_view ("Explicitly Discrete Range (from zero) w/ initial value"));
            CHOC_EXPECT_NEAR (out.min_value, 0.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.max_value, 4.0, 0.0001f);
            CHOC_EXPECT_NEAR (out.default_value, 2.0, 0.0001f);
        }
    }

    {
        CHOC_TEST (SingleParameterValueEventAtStartOfBlock)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                input stream float32<2> in;
                output stream float32<2> out;

                input value float32 gain [[ name: "Gain", min: 0, max: 1, init: 0.75 ]];

                connection in * gain -> out;
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_ASSERT (deactivateOnExit.activated);

        StubStereoAudioPortBackingData<minBlockSize> inputBacking;
        StubStereoAudioPortBackingData<minBlockSize> outputBacking;

        auto inputs = toClapAudioBuffer (inputBacking);
        std::fill (inputBacking.left.begin(), inputBacking.left.end(), 1.0f);
        std::fill (inputBacking.right.begin(), inputBacking.right.end(), 1.0f);

        auto outputs = toClapAudioBuffer (outputBacking);

        const clap_id gainEventId = 2; // N.B. using endpoint handles is brittle

        using EventQueueContext = std::vector<clap_event_param_value_t>;

        EventQueueContext inputEventQueueContext
        {
            makeParameterEvent (0, gainEventId, 0.5f),
        };
        const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

        // execute
        {
            CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
            const clap_process_t process
            {
                /*.steady_time = */-1,
                /*.frames_count = */minBlockSize,
                /*.transport = */nullptr, // free-running
                /*.audio_inputs = */std::addressof (inputs),
                /*.audio_outputs = */std::addressof (outputs),
                /*.audio_inputs_count = */1,
                /*.audio_outputs_count = */1,
                /*.in_events = */std::addressof (inputEventQueue),
                /*.out_events = */nullptr
            };

            CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

            plugin->stop_processing (plugin.get()); // N.B. audio-thread
        }

        // verify
        CHOC_EXPECT_NEAR (outputBacking.left[0], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[1], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[2], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[3], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[0], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[1], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[2], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[3], 0.5f, 0.0001f);
    }

    {
        CHOC_TEST (ParameterValueEventsAtEverySampleOfBlock)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                input stream float32<2> in;
                output stream float32<2> out;

                input value float32 gain [[ name: "Gain", min: 0, max: 1, init: 0.75 ]];

                connection in * gain -> out;
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_ASSERT (deactivateOnExit.activated);

        StubStereoAudioPortBackingData<minBlockSize> inputBacking;
        StubStereoAudioPortBackingData<minBlockSize> outputBacking;

        auto inputs = toClapAudioBuffer (inputBacking);
        std::fill (inputBacking.left.begin(), inputBacking.left.end(), 1.0f);
        std::fill (inputBacking.right.begin(), inputBacking.right.end(), 1.0f);

        auto outputs = toClapAudioBuffer (outputBacking);

        const clap_id gainEventId = 2; // N.B. using endpoint handles is brittle

        using EventQueueContext = std::vector<clap_event_param_value_t>;

        EventQueueContext inputEventQueueContext
        {
            makeParameterEvent (0, gainEventId, 0.0f),
            makeParameterEvent (1, gainEventId, 0.5f),
            makeParameterEvent (2, gainEventId, 0.25f),
            makeParameterEvent (3, gainEventId, 0.0f),
        };
        const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

        // execute
        {
            CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
            const clap_process_t process
            {
                /*.steady_time = */-1,
                /*.frames_count = */minBlockSize,
                /*.transport = */nullptr, // free-running
                /*.audio_inputs = */std::addressof (inputs),
                /*.audio_outputs = */std::addressof (outputs),
                /*.audio_inputs_count = */1,
                /*.audio_outputs_count = */1,
                /*.in_events = */std::addressof (inputEventQueue),
                /*.out_events = */nullptr
            };

            CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

            plugin->stop_processing (plugin.get()); // N.B. audio-thread
        }

        // verify
        CHOC_EXPECT_NEAR (outputBacking.left[0], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[1], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[2], 0.25f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[3], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[0], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[1], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[2], 0.25f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[3], 0.0f, 0.0001f);
    }

    {
        CHOC_TEST (SingleMidiEventAtStartOfBlock)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                output stream float<2> out;

                input event std::midi::Message midiInput;

                node pulse = UnitPulseOnNoteOn;

                connection
                {
                    pulse -> std::mixers::MonoToStereo (float) -> out;
                    midiInput -> std::midi::MPEConverter -> pulse.noteOn;
                }
            }

            processor UnitPulseOnNoteOn
            {
                output stream float out;

                input event std::notes::NoteOn noteOn;

                event noteOn (std::notes::NoteOn e)
                {
                    pulse = ! pulse;
                }

                var pulse = false;

                void main()
                {
                    loop
                    {
                        if (pulse)
                        {
                            out <- 1.0f;
                            pulse = false;
                        }
                        else
                        {
                            out <- 0.0f;
                        }

                        advance();
                    }
                }
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_ASSERT (deactivateOnExit.activated);

        StubStereoAudioPortBackingData<minBlockSize> outputBacking;

        const clap_audio_buffer_t inputs {};
        auto outputs = toClapAudioBuffer (outputBacking);

        using EventQueueContext = std::vector<clap_event_midi_t>;
        EventQueueContext inputEventQueueContext
        {
            makeMidiEvent (0, 0, { 0x90, 0x3C, 0x7F }),
        };
        const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

        // execute
        {
            CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
            const clap_process_t process
            {
                /*.steady_time = */-1,
                /*.frames_count = */minBlockSize,
                /*.transport = */nullptr, // free-running
                /*.audio_inputs = */std::addressof (inputs),
                /*.audio_outputs = */std::addressof (outputs),
                /*.audio_inputs_count = */0,
                /*.audio_outputs_count = */1,
                /*.in_events = */std::addressof (inputEventQueue),
                /*.out_events = */nullptr
            };

            CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

            plugin->stop_processing (plugin.get()); // N.B. audio-thread
        }

        // verify
        CHOC_EXPECT_NEAR (outputBacking.left[0], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[1], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[2], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[3], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[0], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[1], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[2], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[3], 0.0f, 0.0001f);
    }

    {
        CHOC_TEST (MidiEventsAtStartAndEndOfBlock)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                output stream float<2> out;

                input event std::midi::Message midiInput;

                node pulse = UnitPulseOnNoteOn;

                connection
                {
                    pulse -> std::mixers::MonoToStereo (float) -> out;
                    midiInput -> std::midi::MPEConverter -> pulse.noteOn;
                }
            }

            processor UnitPulseOnNoteOn
            {
                output stream float out;

                input event std::notes::NoteOn noteOn;

                event noteOn (std::notes::NoteOn e)
                {
                    pulse = ! pulse;
                }

                var pulse = false;

                void main()
                {
                    loop
                    {
                        if (pulse)
                        {
                            out <- 1.0f;
                            pulse = false;
                        }
                        else
                        {
                            out <- 0.0f;
                        }

                        advance();
                    }
                }
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_ASSERT (deactivateOnExit.activated);

        StubStereoAudioPortBackingData<minBlockSize> inputBacking;
        StubStereoAudioPortBackingData<minBlockSize> outputBacking;

        const clap_audio_buffer_t inputs {};
        auto outputs = toClapAudioBuffer (outputBacking);

        using EventQueueContext = std::vector<clap_event_midi_t>;

        EventQueueContext inputEventQueueContext
        {
            makeMidiEvent (0, 0, { 0x90, 0x3C, 0x7F }),
            makeMidiEvent (3, 0, { 0x90, 0x3C, 0x7F }),
        };
        const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

        // execute
        {
            CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
            const clap_process_t process
            {
                /*.steady_time = */-1,
                /*.frames_count = */minBlockSize,
                /*.transport = */nullptr, // free-running
                /*.audio_inputs = */std::addressof (inputs),
                /*.audio_outputs = */std::addressof (outputs),
                /*.audio_inputs_count = */0,
                /*.audio_outputs_count = */1,
                /*.in_events = */std::addressof (inputEventQueue),
                /*.out_events = */nullptr
            };

            CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

            plugin->stop_processing (plugin.get()); // N.B. audio-thread
        }

        // verify
        CHOC_EXPECT_NEAR (outputBacking.left[0], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[1], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[2], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[3], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[0], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[1], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[2], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[3], 1.0f, 0.0001f);
    }

    {
        CHOC_TEST (NoteEventsWithinBlock)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                output stream float<2> out;

                input event std::midi::Message midiInput;

                node step = UnitStep;

                connection
                {
                    step -> std::mixers::MonoToStereo (float) -> out;
                    midiInput -> std::midi::MPEConverter -> step.note;
                }
            }

            processor UnitStep
            {
                output stream float out;

                input event (std::notes::NoteOn, std::notes::NoteOff) note;

                event note (std::notes::NoteOn e)
                {
                    pulse = true;
                }

                event note (std::notes::NoteOff e)
                {
                    pulse = false;
                }

                var pulse = false;

                void main()
                {
                    loop
                    {
                        out <- float32 (pulse);

                        advance();
                    }
                }
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_ASSERT (deactivateOnExit.activated);

        StubStereoAudioPortBackingData<minBlockSize> inputBacking;
        StubStereoAudioPortBackingData<minBlockSize> outputBacking;

        const clap_audio_buffer_t inputs {};
        auto outputs = toClapAudioBuffer (outputBacking);

        using EventQueueContext = std::vector<clap_event_note_t>;

        const auto toNonClapEventSpaceNoteEvent = [] (clap_event_note_t noteEvent)
        {
            noteEvent.header.space_id = 42;
            return noteEvent;
        };

        EventQueueContext inputEventQueueContext
        {
            toNonClapEventSpaceNoteEvent (makeNoteEvent (0, CLAP_EVENT_NOTE_ON, 0, 0, 60, 1.0)), // N.B. ensure non clap events are ignored
            makeNoteEvent (1, CLAP_EVENT_NOTE_ON, 0, 0, 60, 1.0),
            makeNoteEvent (3, CLAP_EVENT_NOTE_OFF, 0, 0, 60, 1.0),
        };
        const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

        // execute
        {
            CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
            const clap_process_t process
            {
                /*.steady_time = */-1,
                /*.frames_count = */minBlockSize,
                /*.transport = */nullptr, // free-running
                /*.audio_inputs = */std::addressof (inputs),
                /*.audio_outputs = */std::addressof (outputs),
                /*.audio_inputs_count = */0,
                /*.audio_outputs_count = */1,
                /*.in_events = */std::addressof (inputEventQueue),
                /*.out_events = */nullptr
            };

            CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

            plugin->stop_processing (plugin.get()); // N.B. audio-thread
        }

        // verify
        CHOC_EXPECT_NEAR (outputBacking.left[0], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[1], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[2], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[3], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[0], 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[1], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[2], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[3], 0.0f, 0.0001f);
    }

    {
        CHOC_TEST (MidiOutputEvents)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                input event std::midi::Message midiInput;
                output event std::midi::Message midiOutput;

                connection midiInput -> midiOutput;
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_ASSERT (deactivateOnExit.activated);

        using EventQueueContext = std::vector<clap_event_midi_t>;

        EventQueueContext inputEventQueueContext
        {
            makeMidiEvent (0, 0, { 0x90, 0x3C, 0x7F }),
            makeMidiEvent (0, 0, { 0x90, 0x3E, 0x7F }),
            makeMidiEvent (2, 0, { 0x80, 0x3C, 0x7F }),
            makeMidiEvent (3, 0, { 0x80, 0x3E, 0x7F }),
        };

        const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

        EventQueueContext outputEventQueueContext;
        const clap_output_events_t outputEventQueue
        {
            std::addressof (outputEventQueueContext),
            [](const auto* self, const clap_event_header_t* header) -> bool
            {
                auto& context = *reinterpret_cast <EventQueueContext*> (self->ctx);

                if (header->type != CLAP_EVENT_MIDI)
                    return false;

                const auto& event = reinterpret_cast<const clap_event_midi_t&> (*header);

                context.push_back (event);

                return true;
            }
        };

        // execute
        {
            CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
            const clap_process_t process
            {
                /*.steady_time = */-1,
                /*.frames_count = */minBlockSize,
                /*.transport = */nullptr, // free-running
                /*.audio_inputs = */nullptr,
                /*.audio_outputs = */nullptr,
                /*.audio_inputs_count = */0,
                /*.audio_outputs_count = */0,
                /*.in_events = */std::addressof (inputEventQueue),
                /*.out_events = */std::addressof (outputEventQueue)
            };

            CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

            plugin->stop_processing (plugin.get()); // N.B. audio-thread
        }

        // verify
        CHOC_EXPECT_EQ (outputEventQueueContext.size(), inputEventQueueContext.size());
        for (size_t i = 0; i < outputEventQueueContext.size(); ++i)
            CHOC_EXPECT_TRUE (std::memcmp (std::addressof (outputEventQueueContext[i]), std::addressof (inputEventQueueContext[i]), sizeof (clap_event_midi_t)) == 0);
    }

    {
        CHOC_TEST (MidiOutputEventsAreSentAsClapNoteEventsIfHostSupportsNoteDialect)

        // setup
        StubHost host ({ CLAP_NOTE_DIALECT_CLAP });

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                input event std::midi::Message midiInput;
                output event std::midi::Message midiOutput;

                connection midiInput -> midiOutput;
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_ASSERT (deactivateOnExit.activated);

        using EventQueueContext = std::vector<clap_event_note_t>;

        EventQueueContext inputEventQueueContext
        {
            makeNoteEvent (1, CLAP_EVENT_NOTE_ON, 0, 0, 60, 1.0),
            makeNoteEvent (3, CLAP_EVENT_NOTE_OFF, 0, 0, 60, 1.0),
        };

        const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

        EventQueueContext outputEventQueueContext;
        const clap_output_events_t outputEventQueue
        {
            std::addressof (outputEventQueueContext),
            [](const auto* self, const clap_event_header_t* header) -> bool
            {
                auto& context = *reinterpret_cast <EventQueueContext*> (self->ctx);

                if (! (header->type == CLAP_EVENT_NOTE_ON || header->type == CLAP_EVENT_NOTE_OFF))
                    return false;

                const auto& event = reinterpret_cast<const clap_event_note_t&> (*header);

                context.push_back (event);

                return true;
            }
        };

        // execute
        {
            CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
            const clap_process_t process
            {
                /*.steady_time = */-1,
                /*.frames_count = */minBlockSize,
                /*.transport = */nullptr, // free-running
                /*.audio_inputs = */nullptr,
                /*.audio_outputs = */nullptr,
                /*.audio_inputs_count = */0,
                /*.audio_outputs_count = */0,
                /*.in_events = */std::addressof (inputEventQueue),
                /*.out_events = */std::addressof (outputEventQueue)
            };

            CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

            plugin->stop_processing (plugin.get()); // N.B. audio-thread
        }

        // verify
        CHOC_EXPECT_EQ (outputEventQueueContext.size(), static_cast<size_t> (2));
        for (size_t i = 0; i < outputEventQueueContext.size(); ++i)
            CHOC_EXPECT_TRUE (std::memcmp (std::addressof (outputEventQueueContext[i]), std::addressof (inputEventQueueContext[i]), sizeof (clap_event_midi_t)) == 0);
    }

    {
        CHOC_TEST (InactiveJITPluginHasNoNotePortsForPatchWithMidiInput)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "synth",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                output stream float<2> out;

                input event std::midi::Message midiInput;
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        // execute
        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        // verify
        auto* maybeNotePortExtension = plugin->get_extension (plugin.get(), CLAP_EXT_NOTE_PORTS);
        CHOC_ASSERT (maybeNotePortExtension != nullptr);

        const auto& notePortExtension = *reinterpret_cast<const clap_plugin_note_ports_t*> (maybeNotePortExtension);

        CHOC_EXPECT_EQ (notePortExtension.count (plugin.get(), true), static_cast<uint32_t> (0));
        CHOC_EXPECT_EQ (notePortExtension.count (plugin.get(), false), static_cast<uint32_t> (0));

        clap_note_port_info_t out {};
        CHOC_EXPECT_FALSE (notePortExtension.get (plugin.get(), 0, true, std::addressof (out)));
        CHOC_EXPECT_FALSE (notePortExtension.get (plugin.get(), 0, false, std::addressof (out)));
    }

    {
        CHOC_TEST (ActivatedPluginHasNotePortsForPatchWithRespectiveMidiEndpoints)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "synth",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                output stream float<2> out;

                input event std::midi::Message midiInput;
                output event std::midi::Message midiOutput;
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        // execute
        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_EXPECT_TRUE (deactivateOnExit.activated);
        //

        // verify
        CHOC_ASSERT (! host.rescanNotePortsCallArgs.empty());
        CHOC_EXPECT_EQ (host.rescanNotePortsCallArgs.size(), static_cast<size_t> (1));
        CHOC_EXPECT_EQ (host.rescanNotePortsCallArgs.front(), static_cast<uint32_t> (CLAP_NOTE_PORTS_RESCAN_ALL));

        auto* maybeNotePortExtension = plugin->get_extension (plugin.get(), CLAP_EXT_NOTE_PORTS);
        CHOC_ASSERT (maybeNotePortExtension != nullptr);

        const auto& notePortExtension = *reinterpret_cast<const clap_plugin_note_ports_t*> (maybeNotePortExtension);

        CHOC_EXPECT_EQ (notePortExtension.count (plugin.get(), true), static_cast<uint32_t> (1));
        CHOC_EXPECT_EQ (notePortExtension.count (plugin.get(), false), static_cast<uint32_t> (1));

        {
            clap_note_port_info_t out {};
            CHOC_EXPECT_TRUE (notePortExtension.get (plugin.get(), 0, true, std::addressof (out)));
            CHOC_EXPECT_TRUE (out.supported_dialects & CLAP_NOTE_DIALECT_MIDI);
            CHOC_EXPECT_TRUE (out.supported_dialects & CLAP_NOTE_DIALECT_CLAP);
            CHOC_EXPECT_FALSE (out.supported_dialects & CLAP_NOTE_DIALECT_MIDI_MPE);
            CHOC_EXPECT_FALSE (out.supported_dialects & CLAP_NOTE_DIALECT_MIDI2);
            CHOC_EXPECT_EQ (out.preferred_dialect, static_cast<uint32_t> (CLAP_NOTE_DIALECT_MIDI));
            CHOC_EXPECT_EQ (std::string_view (out.name), "midiInput");
            // N.B. not currently checking the id. need a better mapping strategy than using endpoint handles
        }

        {
            clap_note_port_info_t out {};
            CHOC_EXPECT_TRUE (notePortExtension.get (plugin.get(), 0, false, std::addressof (out)));
            CHOC_EXPECT_TRUE (out.supported_dialects & CLAP_NOTE_DIALECT_MIDI);
            CHOC_EXPECT_TRUE (out.supported_dialects & CLAP_NOTE_DIALECT_CLAP);
            CHOC_EXPECT_FALSE (out.supported_dialects & CLAP_NOTE_DIALECT_MIDI_MPE);
            CHOC_EXPECT_FALSE (out.supported_dialects & CLAP_NOTE_DIALECT_MIDI2);
            CHOC_EXPECT_EQ (out.preferred_dialect, static_cast<uint32_t> (CLAP_NOTE_DIALECT_MIDI));
            CHOC_EXPECT_EQ (std::string_view (out.name), "midiOutput");
            // N.B. not currently checking the id. need a better mapping strategy than using endpoint handles
        }
    }

    {
        CHOC_TEST (ActivatedPluginHasNoNotePortsForPatchWithoutMidiInput)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "synth",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                output stream float<2> out;
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        // execute
        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_EXPECT_TRUE (deactivateOnExit.activated);
        //

        // verify
        auto* maybeNotePortExtension = plugin->get_extension (plugin.get(), CLAP_EXT_NOTE_PORTS);
        CHOC_ASSERT (maybeNotePortExtension != nullptr);

        const auto& notePortExtension = *reinterpret_cast<const clap_plugin_note_ports_t*> (maybeNotePortExtension);

        CHOC_EXPECT_EQ (notePortExtension.count (plugin.get(), true), static_cast<uint32_t> (0));
        CHOC_EXPECT_EQ (notePortExtension.count (plugin.get(), false), static_cast<uint32_t> (0));

        clap_note_port_info_t out {};
        CHOC_EXPECT_FALSE (notePortExtension.get (plugin.get(), 0, true, std::addressof (out)));
        CHOC_EXPECT_FALSE (notePortExtension.get (plugin.get(), 0, false, std::addressof (out)));
    }

    {
        CHOC_TEST (ActivatedPluginHasAudioPortsForEachEndpoint)

        // setup

        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "effect",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            processor Test [[ main ]]
            {
                input stream float32 monoIn;
                input stream float32<2> stereoIn;
                input stream float32<3> threeIn;

                output stream float32 monoOut;
                output stream float32<2> stereoOut;
                output stream float32<3> threeOut;

                void main()
                {
                    loop
                    {
                        monoOut <- monoIn;
                        stereoOut <- stereoIn;
                        threeOut <- threeIn;

                        advance();
                    }
                }
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        // execute
        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_EXPECT_TRUE (deactivateOnExit.activated);
        //

        // verify
        CHOC_ASSERT (! host.rescanAudioPortsCallArgs.empty());
        CHOC_EXPECT_EQ (host.rescanAudioPortsCallArgs.size(), static_cast<size_t> (1));
        CHOC_EXPECT_EQ (host.rescanAudioPortsCallArgs.front(), static_cast<uint32_t> (CLAP_AUDIO_PORTS_RESCAN_LIST));

        auto* maybeAudioPortsExtension = plugin->get_extension (plugin.get(), CLAP_EXT_AUDIO_PORTS);
        CHOC_ASSERT (maybeAudioPortsExtension != nullptr);

        [[maybe_unused]] const auto& audioPortsExtension = *reinterpret_cast<const clap_plugin_audio_ports_t*> (maybeAudioPortsExtension);

        CHOC_ASSERT (audioPortsExtension.count (plugin.get(), true) == static_cast<uint32_t> (3));
        CHOC_ASSERT (audioPortsExtension.count (plugin.get(), false) == static_cast<uint32_t> (3));

        {
            const auto port = getPortInfo (*plugin, 0, true);
            CHOC_ASSERT (port);
            CHOC_EXPECT_EQ (port->id, static_cast<uint32_t> (0));
            CHOC_EXPECT_EQ (std::string_view (port->name), std::string_view ("monoIn"));
            CHOC_EXPECT_TRUE (port->flags & CLAP_AUDIO_PORT_IS_MAIN);
            CHOC_EXPECT_EQ (port->channel_count, static_cast<uint32_t> (1));
            CHOC_EXPECT_EQ (std::string_view (port->port_type), std::string_view (CLAP_PORT_MONO));
            CHOC_EXPECT_EQ (port->in_place_pair, CLAP_INVALID_ID);
        }

        {
            const auto port = getPortInfo (*plugin, 1, true);
            CHOC_ASSERT (port);
            CHOC_EXPECT_EQ (port->id, static_cast<uint32_t> (1));
            CHOC_EXPECT_EQ (std::string_view (port->name), std::string_view ("stereoIn"));
            CHOC_EXPECT_FALSE (port->flags & CLAP_AUDIO_PORT_IS_MAIN);
            CHOC_EXPECT_EQ (port->channel_count, static_cast<uint32_t> (2));
            CHOC_EXPECT_EQ (std::string_view (port->port_type), std::string_view (CLAP_PORT_STEREO));
            CHOC_EXPECT_EQ (port->in_place_pair, CLAP_INVALID_ID);
        }

        {
            const auto port = getPortInfo (*plugin, 2, true);
            CHOC_ASSERT (port);
            CHOC_EXPECT_EQ (port->id, static_cast<uint32_t> (2));
            CHOC_EXPECT_EQ (std::string_view (port->name), std::string_view ("threeIn"));
            CHOC_EXPECT_FALSE (port->flags & CLAP_AUDIO_PORT_IS_MAIN);
            CHOC_EXPECT_EQ (port->channel_count, static_cast<uint32_t> (3));
            CHOC_EXPECT_TRUE (port->port_type == nullptr);
            CHOC_EXPECT_EQ (port->in_place_pair, CLAP_INVALID_ID);
        }

        {
            const auto port = getPortInfo (*plugin, 0, false);
            CHOC_ASSERT (port);
            CHOC_EXPECT_EQ (port->id, static_cast<uint32_t> (0));
            CHOC_EXPECT_EQ (std::string_view (port->name), std::string_view ("monoOut"));
            CHOC_EXPECT_TRUE (port->flags & CLAP_AUDIO_PORT_IS_MAIN);
            CHOC_EXPECT_EQ (port->channel_count, static_cast<uint32_t> (1));
            CHOC_EXPECT_EQ (std::string_view (port->port_type), std::string_view (CLAP_PORT_MONO));
            CHOC_EXPECT_EQ (port->in_place_pair, CLAP_INVALID_ID);
        }

        {
            const auto port = getPortInfo (*plugin, 1, false);
            CHOC_ASSERT (port);
            CHOC_EXPECT_EQ (port->id, static_cast<uint32_t> (1));
            CHOC_EXPECT_EQ (std::string_view (port->name), std::string_view ("stereoOut"));
            CHOC_EXPECT_FALSE (port->flags & CLAP_AUDIO_PORT_IS_MAIN);
            CHOC_EXPECT_EQ (port->channel_count, static_cast<uint32_t> (2));
            CHOC_EXPECT_EQ (std::string_view (port->port_type), std::string_view (CLAP_PORT_STEREO));
            CHOC_EXPECT_EQ (port->in_place_pair, CLAP_INVALID_ID);
        }

        {
            const auto port = getPortInfo (*plugin, 2, false);
            CHOC_ASSERT (port);
            CHOC_EXPECT_EQ (port->id, static_cast<uint32_t> (2));
            CHOC_EXPECT_EQ (std::string_view (port->name), std::string_view ("threeOut"));
            CHOC_EXPECT_FALSE (port->flags & CLAP_AUDIO_PORT_IS_MAIN);
            CHOC_EXPECT_EQ (port->channel_count, static_cast<uint32_t> (3));
            CHOC_EXPECT_TRUE (port->port_type == nullptr);
            CHOC_EXPECT_EQ (port->in_place_pair, CLAP_INVALID_ID);
        }
    }


    {
        CHOC_TEST (ProcessPluginWithinOneInputAndOneOutputAudioPort)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                input stream float32<2> in;
                output stream float32<2> out;

                input value float32 gain [[ name: "Gain", min: 0, max: 1, init: 0.75 ]];

                connection in * gain -> out;
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_ASSERT (deactivateOnExit.activated);

        StubStereoAudioPortBackingData<minBlockSize> inputBacking;
        StubStereoAudioPortBackingData<minBlockSize> outputBacking;

        auto inputs = toClapAudioBuffer (inputBacking);
        std::fill (inputBacking.left.begin(), inputBacking.left.end(), 1.0f);
        std::fill (inputBacking.right.begin(), inputBacking.right.end(), 1.0f);

        auto outputs = toClapAudioBuffer (outputBacking);

        using EventQueueContext = std::vector<clap_event_param_value_t>;

        EventQueueContext inputEventQueueContext {};
        const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

        // execute
        {
            CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
            const clap_process_t process
            {
                /*.steady_time = */-1,
                /*.frames_count = */minBlockSize,
                /*.transport = */nullptr, // free-running
                /*.audio_inputs = */std::addressof (inputs),
                /*.audio_outputs = */std::addressof (outputs),
                /*.audio_inputs_count = */1,
                /*.audio_outputs_count = */1,
                /*.in_events = */std::addressof (inputEventQueue),
                /*.out_events = */nullptr
            };

            CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

            plugin->stop_processing (plugin.get()); // N.B. audio-thread
        }

        // verify
        CHOC_EXPECT_NEAR (outputBacking.left[0], 0.75f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[1], 0.75f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[2], 0.75f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[3], 0.75f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[0], 0.75f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[1], 0.75f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[2], 0.75f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[3], 0.75f, 0.0001f);
    }

    {
        CHOC_TEST (ProcessPluginWithMultipleInputAndOutputAudioPorts)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "effect",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            processor Test [[ main ]]
            {
                input stream float32 monoIn;
                input stream float32<2> stereoIn;
                input stream float32<3> threeIn;

                output stream float32 monoOut;
                output stream float32<2> stereoOut;
                output stream float32<3> threeOut;

                void main()
                {
                    loop
                    {
                        monoOut <- monoIn;
                        stereoOut <- stereoIn;
                        threeOut <- threeIn;

                        advance();
                    }
                }
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_ASSERT (deactivateOnExit.activated);

        StubAudioPortBackingData<1, minBlockSize> inputMonoBacking;
        StubAudioPortBackingData<2, minBlockSize> inputStereoBacking;
        StubAudioPortBackingData<3, minBlockSize> inputThreeBacking;

        std::array<clap_audio_buffer_t, 3> inputs
        {{
            toClapAudioBuffer (inputMonoBacking),
            toClapAudioBuffer (inputStereoBacking),
            toClapAudioBuffer (inputThreeBacking),
        }};
        std::fill (inputMonoBacking.channels[0].begin(), inputMonoBacking.channels[0].end(), 0.1f);
        std::fill (inputStereoBacking.channels[0].begin(), inputStereoBacking.channels[0].end(), 0.21f);
        std::fill (inputStereoBacking.channels[1].begin(), inputStereoBacking.channels[1].end(), 0.22f);
        std::fill (inputThreeBacking.channels[0].begin(), inputThreeBacking.channels[0].end(), 0.31f);
        std::fill (inputThreeBacking.channels[1].begin(), inputThreeBacking.channels[1].end(), 0.32f);
        std::fill (inputThreeBacking.channels[2].begin(), inputThreeBacking.channels[2].end(), 0.33f);

        StubAudioPortBackingData<1, minBlockSize> outputMonoBacking;
        StubAudioPortBackingData<2, minBlockSize> outputStereoBacking;
        StubAudioPortBackingData<3, minBlockSize> outputThreeBacking;

        std::array<clap_audio_buffer_t, 3> outputs
        {{
            toClapAudioBuffer (outputMonoBacking),
            toClapAudioBuffer (outputStereoBacking),
            toClapAudioBuffer (outputThreeBacking),
        }};

        using EventQueueContext = std::vector<clap_event_param_value_t>;

        EventQueueContext inputEventQueueContext {};
        const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

        // execute
        {
            CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
            const clap_process_t process
            {
                /*.steady_time = */-1,
                /*.frames_count = */minBlockSize,
                /*.transport = */nullptr, // free-running
                /*.audio_inputs = */inputs.data(),
                /*.audio_outputs = */outputs.data(),
                /*.audio_inputs_count = */static_cast<uint32_t> (inputs.size()),
                /*.audio_outputs_count = */static_cast<uint32_t> (outputs.size()),
                /*.in_events = */std::addressof (inputEventQueue),
                /*.out_events = */nullptr
            };

            CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

            plugin->stop_processing (plugin.get()); // N.B. audio-thread
        }

        // verify
        CHOC_EXPECT_NEAR (outputMonoBacking.channels[0][0], 0.1f, 0.0001f);
        CHOC_EXPECT_NEAR (outputMonoBacking.channels[0][1], 0.1f, 0.0001f);
        CHOC_EXPECT_NEAR (outputMonoBacking.channels[0][2], 0.1f, 0.0001f);
        CHOC_EXPECT_NEAR (outputMonoBacking.channels[0][3], 0.1f, 0.0001f);
        CHOC_EXPECT_NEAR (outputStereoBacking.channels[0][0], 0.21f, 0.0001f);
        CHOC_EXPECT_NEAR (outputStereoBacking.channels[0][1], 0.21f, 0.0001f);
        CHOC_EXPECT_NEAR (outputStereoBacking.channels[0][2], 0.21f, 0.0001f);
        CHOC_EXPECT_NEAR (outputStereoBacking.channels[0][3], 0.21f, 0.0001f);
        CHOC_EXPECT_NEAR (outputStereoBacking.channels[1][0], 0.22f, 0.0001f);
        CHOC_EXPECT_NEAR (outputStereoBacking.channels[1][1], 0.22f, 0.0001f);
        CHOC_EXPECT_NEAR (outputStereoBacking.channels[1][2], 0.22f, 0.0001f);
        CHOC_EXPECT_NEAR (outputStereoBacking.channels[1][3], 0.22f, 0.0001f);
        CHOC_EXPECT_NEAR (outputThreeBacking.channels[0][0], 0.31f, 0.0001f);
        CHOC_EXPECT_NEAR (outputThreeBacking.channels[0][1], 0.31f, 0.0001f);
        CHOC_EXPECT_NEAR (outputThreeBacking.channels[0][2], 0.31f, 0.0001f);
        CHOC_EXPECT_NEAR (outputThreeBacking.channels[0][3], 0.31f, 0.0001f);
        CHOC_EXPECT_NEAR (outputThreeBacking.channels[1][0], 0.32f, 0.0001f);
        CHOC_EXPECT_NEAR (outputThreeBacking.channels[1][1], 0.32f, 0.0001f);
        CHOC_EXPECT_NEAR (outputThreeBacking.channels[1][2], 0.32f, 0.0001f);
        CHOC_EXPECT_NEAR (outputThreeBacking.channels[1][3], 0.32f, 0.0001f);
        CHOC_EXPECT_NEAR (outputThreeBacking.channels[2][0], 0.33f, 0.0001f);
        CHOC_EXPECT_NEAR (outputThreeBacking.channels[2][1], 0.33f, 0.0001f);
        CHOC_EXPECT_NEAR (outputThreeBacking.channels[2][2], 0.33f, 0.0001f);
        CHOC_EXPECT_NEAR (outputThreeBacking.channels[2][3], 0.33f, 0.0001f);
    }

    {
        CHOC_TEST (ProcessPluginWithNoInputsAndMultipleOutputAudioPorts)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "effect",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            processor Test [[ main ]]
            {
                output stream float32<2> mainOut;
                output stream float32 otherOut;

                void main()
                {
                    loop
                    {
                        mainOut <- float32<2> (-0.5f, 0.5f);
                        otherOut <- (1.0f);

                        advance();
                    }
                }
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_ASSERT (deactivateOnExit.activated);

        StubAudioPortBackingData<2, minBlockSize> mainOutputBacking;
        StubAudioPortBackingData<1, minBlockSize> otherOutputBacking;

        std::array<clap_audio_buffer_t, 0> inputs {};
        std::array<clap_audio_buffer_t, 2> outputs
        {{
            toClapAudioBuffer (mainOutputBacking),
            toClapAudioBuffer (otherOutputBacking),
        }};

        using EventQueueContext = std::vector<clap_event_param_value_t>;

        EventQueueContext inputEventQueueContext {};
        const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

        // execute
        {
            CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
            const clap_process_t process
            {
                /*.steady_time = */-1,
                /*.frames_count = */minBlockSize,
                /*.transport = */nullptr, // free-running
                /*.audio_inputs = */inputs.data(),
                /*.audio_outputs = */outputs.data(),
                /*.audio_inputs_count = */static_cast<uint32_t> (inputs.size()),
                /*.audio_outputs_count = */static_cast<uint32_t> (outputs.size()),
                /*.in_events = */std::addressof (inputEventQueue),
                /*.out_events = */nullptr
            };

            CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

            plugin->stop_processing (plugin.get()); // N.B. audio-thread
        }

        // verify
        CHOC_EXPECT_NEAR (mainOutputBacking.channels[0][0], -0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (mainOutputBacking.channels[0][1], -0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (mainOutputBacking.channels[0][2], -0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (mainOutputBacking.channels[0][3], -0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (mainOutputBacking.channels[1][0], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (mainOutputBacking.channels[1][1], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (mainOutputBacking.channels[1][2], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (mainOutputBacking.channels[1][3], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (otherOutputBacking.channels[0][0], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (otherOutputBacking.channels[0][1], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (otherOutputBacking.channels[0][2], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (otherOutputBacking.channels[0][3], 1.0f, 0.0001f);
    }

    {
        CHOC_TEST (ProcessPluginWithMultipleInputsAndOneOutputAudioPorts)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "effect",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            processor Test [[ main ]]
            {
                input stream float32<2> mainIn;
                input stream float32 sidechain;

                output stream float32 out;

                void main()
                {
                    loop
                    {
                        out <- (mainIn[0] + mainIn[1]) * 0.5f * sidechain;

                        advance();
                    }
                }
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_ASSERT (deactivateOnExit.activated);

        StubAudioPortBackingData<2, minBlockSize> mainInputBacking {};
        mainInputBacking.channels[0] = {{ 0.1f, 0.2f, 0.3f, 0.4f }};
        mainInputBacking.channels[1] = {{ 0.9f, 0.8f, 0.7f, 0.6f }};
        StubAudioPortBackingData<1, minBlockSize> sidechainInputBacking;
        sidechainInputBacking.channels[0] = {{ 1.0f, 0.75f, 0.5f, 0.25f }};

        std::array<clap_audio_buffer_t, 2> inputs
        {{
            toClapAudioBuffer (mainInputBacking),
            toClapAudioBuffer (sidechainInputBacking),
        }};

        StubAudioPortBackingData<1, minBlockSize> mainOutputBacking;

        std::array<clap_audio_buffer_t, 1> outputs
        {{
            toClapAudioBuffer (mainOutputBacking),
        }};

        using EventQueueContext = std::vector<clap_event_param_value_t>;

        EventQueueContext inputEventQueueContext {};
        const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

        // execute
        {
            CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
            const clap_process_t process
            {
                /*.steady_time = */-1,
                /*.frames_count = */minBlockSize,
                /*.transport = */nullptr, // free-running
                /*.audio_inputs = */inputs.data(),
                /*.audio_outputs = */outputs.data(),
                /*.audio_inputs_count = */static_cast<uint32_t> (inputs.size()),
                /*.audio_outputs_count = */static_cast<uint32_t> (outputs.size()),
                /*.in_events = */std::addressof (inputEventQueue),
                /*.out_events = */nullptr
            };

            CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

            plugin->stop_processing (plugin.get()); // N.B. audio-thread
        }

        // verify
        CHOC_EXPECT_NEAR (mainOutputBacking.channels[0][0], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (mainOutputBacking.channels[0][1], 0.375f, 0.0001f);
        CHOC_EXPECT_NEAR (mainOutputBacking.channels[0][2], 0.25f, 0.0001f);
        CHOC_EXPECT_NEAR (mainOutputBacking.channels[0][3], 0.125f, 0.0001f);
    }

    {
        CHOC_TEST (TransportFlagsSetForBlock)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                output stream float<2> out;

                input event std::timeline::TransportState transportStateIn;

                // N.B. work around `std::timeline::TransportState` not working as a value
                node cachedEvent = EventToValue (std::timeline::TransportState, std::timeline::TransportState ());
                node upmix = std::mixers::MonoToStereo (float);

                connection
                {
                    transportStateIn -> cachedEvent;
                    float32 (cachedEvent.out.isPlaying()) * 0.25f -> upmix;
                    float32 (cachedEvent.out.isRecording()) * 0.55f -> upmix;
                    float32 (cachedEvent.out.isLooping()) * 0.2f -> upmix;
                    upmix -> out;
                }
            }

            processor EventToValue (using T, T initialValue)
            {
                input event T in;
                output value T out;

                T cached = initialValue;

                event in (T e)
                {
                    cached = e;
                }

                void main()
                {
                    loop
                    {
                        out <- cached;
                        advance();
                    }
                }
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);

        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        StubStereoAudioPortBackingData<minBlockSize> outputBacking;

        const clap_audio_buffer_t inputs {};
        auto outputs = toClapAudioBuffer (outputBacking);

        using EventQueueContext = std::vector<clap_event_transport_t>;

        const auto reset = [&]
        {
            outputBacking.left.fill (0.0f);
            outputBacking.right.fill (0.0f);
        };

        {
            // setup
            reset();

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_IS_RECORDING));

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */0,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 0.55, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 0.55f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 0.55f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 0.55, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 0.55, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 0.55f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 0.55f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 0.55, 0.0001f);
        }

        {
            // setup
            reset();

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_IS_PLAYING));

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */1,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 0.25f, 0.0001f);
        }

        {
            // setup
            reset();

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_IS_LOOP_ACTIVE));

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */1,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 0.2f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 0.2f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 0.2f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 0.2f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 0.2f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 0.2f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 0.2f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 0.2f, 0.0001f);
        }

        {
            // setup
            reset();

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_IS_RECORDING | CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_IS_LOOP_ACTIVE));

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */1,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 1.0f, 0.0001f);
        }

        {
            // setup
            reset();

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_IS_LOOP_ACTIVE));

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */1,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 0.45f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 0.45f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 0.45f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 0.45f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 0.45f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 0.45f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 0.45f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 0.45f, 0.0001f);
        }

        {
            // setup
            reset();

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_IS_RECORDING | CLAP_TRANSPORT_IS_LOOP_ACTIVE));

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */1,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 0.75f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 0.75f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 0.75f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 0.75f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 0.75f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 0.75f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 0.75f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 0.75f, 0.0001f);
        }

        {
            // setup
            reset();

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_IS_RECORDING | CLAP_TRANSPORT_IS_PLAYING));

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */1,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 0.8f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 0.8f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 0.8f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 0.8f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 0.8f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 0.8f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 0.8f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 0.8f, 0.0001f);
        }
    }

    // N.B. if hosts actually send sub-block timeline events in the event queue, we don't currently handle that case. do hosts do that?
    {
        CHOC_TEST (Timeline)

        // setup
        StubHost host {};

        const clap_plugin_descriptor_t descriptor {};

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your-name.your-patch-id",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "synth",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["test.cmajor"]
        })";

        const auto cmajorSource = R"(
            processor Test [[ main ]]
            {
                input
                {
                    event std::timeline::TimeSignature timeSigIn;
                    event std::timeline::Tempo tempoIn;
                    event std::timeline::Position positionIn;
                }

                output stream float32<2> out;

                event timeSigIn (std::timeline::TimeSignature e)
                {
                    cachedTimeSignature = e;
                }

                event tempoIn (std::timeline::Tempo e)
                {
                    cachedTempo = e;
                }

                event positionIn (std::timeline::Position e)
                {
                    cachedPosition = e;
                }

                std::timeline::TimeSignature cachedTimeSignature;
                std::timeline::Tempo cachedTempo;
                std::timeline::Position cachedPosition;

                int64 frameCount = 0;

                void main()
                {
                    let numeratorToLeftChannelDenominatorToRightChannel = 0;
                    let tempoToBothChannels = 1;
                    let frameIndexToBothChannels = 2;
                    let quarterNoteToLeftChannelBarStartQuarterNoteToRightChannel = 3;

                    for (int32 eventIndex = 0; eventIndex < 4; ++eventIndex)
                    {
                        if (eventIndex == numeratorToLeftChannelDenominatorToRightChannel)
                            out <- float32<2> (float32 (cachedTimeSignature.numerator), float32 (cachedTimeSignature.denominator));

                        if (eventIndex == tempoToBothChannels)
                        {
                            let bpm = float32 (cachedTempo.bpm);
                            out <- float32<2> (bpm, bpm);
                        }

                        if (eventIndex == frameIndexToBothChannels)
                        {
                            let frameIndex = float32 (cachedPosition.frameIndex);
                            out <- float32<2> (frameIndex, frameIndex);
                        }

                        if (eventIndex == quarterNoteToLeftChannelBarStartQuarterNoteToRightChannel)
                            out <- float32<2> (float32 (cachedPosition.quarterNote), float32 (cachedPosition.barStartQuarterNote));

                        advance();
                    }
                }
            }
        )";

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
            { "test.cmajorpatch", manifestSource },
            { "test.cmajor", cmajorSource }
        });

        // host timeline beat time 0, with tempo and time signature
        {
            // setup
            auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

            CHOC_ASSERT (plugin != nullptr);

            plugin->init (plugin.get());

            const double frequency = 4;
            constexpr uint32_t minBlockSize = 4;
            constexpr uint32_t maxBlockSize = 4;

            StubStereoAudioPortBackingData<minBlockSize> outputBacking;

            const clap_audio_buffer_t inputs {};
            auto outputs = toClapAudioBuffer (outputBacking);

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            using EventQueueContext = std::vector<clap_event_transport_t>;

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = ([]
            {
                auto event = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING));
                event.tempo = 120.0;
                event.tsig_num = 3;
                event.tsig_denom = 4;
                return event;
            })();

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */0,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 3.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 4.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 120.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 120.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 0.0f, 0.0001f);
        }

        // host timeline in 3/8, within second bar. tempo at 20bpm. times specified in beat time.
        {
            // setup
            auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

            CHOC_ASSERT (plugin != nullptr);

            plugin->init (plugin.get());

            const double frequency = 4;
            constexpr uint32_t minBlockSize = 4;
            constexpr uint32_t maxBlockSize = 4;

            StubStereoAudioPortBackingData<minBlockSize> outputBacking;

            const clap_audio_buffer_t inputs {};
            auto outputs = toClapAudioBuffer (outputBacking);

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            using EventQueueContext = std::vector<clap_event_transport_t>;

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = ([]
            {
                auto event = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_HAS_BEATS_TIMELINE));
                event.song_pos_beats = toFixedPointBeatTime (2.0);
                event.bar_start = toFixedPointBeatTime (1.5);
                event.tempo = 20.0;
                event.tsig_num = 3;
                event.tsig_denom = 8;

                return event;
            })();

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */0,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 3.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 8.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 20.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 20.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 24.0f, 0.0001f); // N.B. 2.0 * (60.0 / 20) * frequency
            CHOC_EXPECT_NEAR (outputBacking.right[2], 24.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 2.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 1.5f, 0.0001f);
        }

        // host timeline in 3/8, within second bar. tempo not given. times specified in beat time.
        {
            // setup
            auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

            CHOC_ASSERT (plugin != nullptr);

            plugin->init (plugin.get());

            const double frequency = 4;
            constexpr uint32_t minBlockSize = 4;
            constexpr uint32_t maxBlockSize = 4;

            StubStereoAudioPortBackingData<minBlockSize> outputBacking;

            const clap_audio_buffer_t inputs {};
            auto outputs = toClapAudioBuffer (outputBacking);

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            using EventQueueContext = std::vector<clap_event_transport_t>;

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = ([]
            {
                auto event = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_HAS_BEATS_TIMELINE));
                event.song_pos_beats = toFixedPointBeatTime (2.0);
                event.bar_start = toFixedPointBeatTime (1.5);
                event.tsig_num = 3;
                event.tsig_denom = 8;

                return event;
            })();

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */0,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 3.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 8.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 0.0f, 0.0001f);
        }

        // host timeline in 3/8, within second bar. tempo at 20bpm. times specified in seconds.
        {
            // setup
            auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

            CHOC_ASSERT (plugin != nullptr);

            plugin->init (plugin.get());

            const double frequency = 4;
            constexpr uint32_t minBlockSize = 4;
            constexpr uint32_t maxBlockSize = 4;

            StubStereoAudioPortBackingData<minBlockSize> outputBacking;

            const clap_audio_buffer_t inputs {};
            auto outputs = toClapAudioBuffer (outputBacking);

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            using EventQueueContext = std::vector<clap_event_transport_t>;

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = ([]
            {
                auto event = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_HAS_SECONDS_TIMELINE));
                event.song_pos_seconds = toFixedPointSeconds (6.0);
                event.bar_number = 1;
                event.tempo = 20.0;
                event.tsig_num = 3;
                event.tsig_denom = 8;

                return event;
            })();

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */0,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 3.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 8.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 20.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 20.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 24.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 24.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 2.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 1.5f, 0.0001f);
        }

        // host timeline in 3/8, within second bar. tempo at 20bpm. times specified in seconds and beats.
        {
            // setup
            auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

            CHOC_ASSERT (plugin != nullptr);

            plugin->init (plugin.get());

            const double frequency = 4;
            constexpr uint32_t minBlockSize = 4;
            constexpr uint32_t maxBlockSize = 4;

            StubStereoAudioPortBackingData<minBlockSize> outputBacking;

            const clap_audio_buffer_t inputs {};
            auto outputs = toClapAudioBuffer (outputBacking);

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            using EventQueueContext = std::vector<clap_event_transport_t>;

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = ([]
            {
                auto event = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_HAS_SECONDS_TIMELINE));
                event.song_pos_seconds = toFixedPointSeconds (6.0);
                event.song_pos_beats = toFixedPointBeatTime (2.0);
                event.bar_start = toFixedPointBeatTime (1.5);
                event.bar_number = 1;
                event.tempo = 20.0;
                event.tsig_num = 3;
                event.tsig_denom = 8;

                return event;
            })();

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */0,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 3.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 8.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 20.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 20.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 24.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 24.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 2.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 1.5f, 0.0001f);
        }

        // host timeline in unspecified time signature. tempo at 20bpm. times specified in seconds.
        {
            // setup
            auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

            CHOC_ASSERT (plugin != nullptr);

            plugin->init (plugin.get());

            const double frequency = 4;
            constexpr uint32_t minBlockSize = 4;
            constexpr uint32_t maxBlockSize = 4;

            StubStereoAudioPortBackingData<minBlockSize> outputBacking;

            const clap_audio_buffer_t inputs {};
            auto outputs = toClapAudioBuffer (outputBacking);

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            using EventQueueContext = std::vector<clap_event_transport_t>;

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = ([]
            {
                auto event = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_HAS_SECONDS_TIMELINE));
                event.song_pos_seconds = toFixedPointSeconds (6.0);
                event.tempo = 20.0;

                return event;
            })();

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */0,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 20.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 20.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 24.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 24.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 2.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 0.0f, 0.0001f);
        }

        // host timeline in 3/8, within second bar. tempo not given. times specified in seconds.
        {
            // setup
            auto plugin = cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);

            CHOC_ASSERT (plugin != nullptr);

            plugin->init (plugin.get());

            const double frequency = 4;
            constexpr uint32_t minBlockSize = 4;
            constexpr uint32_t maxBlockSize = 4;

            StubStereoAudioPortBackingData<minBlockSize> outputBacking;

            const clap_audio_buffer_t inputs {};
            auto outputs = toClapAudioBuffer (outputBacking);

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            using EventQueueContext = std::vector<clap_event_transport_t>;

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            const auto transportForBlock = ([]
            {
                auto event = makeTransportStateEventWithFlags (0, static_cast<uint32_t> (CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_HAS_SECONDS_TIMELINE));
                event.song_pos_seconds = toFixedPointSeconds (6.0);
                event.bar_number = 1;
                event.tsig_num = 3;
                event.tsig_denom = 8;

                return event;
            })();

            // execute
            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */std::addressof (transportForBlock),
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */0,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            // verify
            CHOC_EXPECT_NEAR (outputBacking.left[0], 3.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[0], 8.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[1], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[1], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[2], 24.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[2], 24.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.left[3], 0.0f, 0.0001f);
            CHOC_EXPECT_NEAR (outputBacking.right[3], 1.5f, 0.0001f);
        }
    }

    {
        CHOC_TEST (ExplicitlyChangingPatchToOneWithMoreChannels)

        // setup

        const auto onRestartRequested = [] (auto* plugin) {
            // N.B. imitate Bitwig (i.e. non spec compliant)
            plugin->stop_processing (plugin); // N.B. audio-thread

            if (auto* ext = getExtension<clap_plugin_audio_ports_t> (plugin, CLAP_EXT_AUDIO_PORTS))
            {
                [[maybe_unused]] const auto outputs = ext->count (plugin, false);
                [[maybe_unused]] const auto inputs = ext->count (plugin, true);
            }

            if (auto* ext = getExtension<clap_plugin_latency_t> (plugin, CLAP_EXT_LATENCY))
            {
                [[maybe_unused]] const auto latency = ext->get (plugin);
            }

            plugin->start_processing (plugin); // N.B. audio-thread
        };

        StubHost host ({ {}, onRestartRequested });

        const clap_plugin_descriptor_t descriptor {};

        std::vector<InMemoryFile> files {};

        {
            const auto manifestSource = R"({
                "CmajorVersion": 1,
                "ID": "com.your-name.your-patch-id",
                "version": "1.0",
                "name": "MidiEcho",
                "description": "Test",
                "category": "midi",
                "manufacturer": "Your Company Goes Here",
                "isInstrument": false,

                "source": ["echo.cmajor"]
            })";

            const auto cmajorSource = R"(
                graph Test [[ main ]]
                {
                    input event std::midi::Message midiIn;
                    output event std::midi::Message midiOut;

                    connection midiIn -> midiOut;
                }
            )";

            files.push_back ({ "echo.cmajorpatch", manifestSource });
            files.push_back ({ "echo.cmajor", cmajorSource });
        }

        {
            const auto manifestSource = R"({
                "CmajorVersion": 1,
                "ID": "com.your-name.your-patch-id",
                "version": "1.0",
                "name": "NaiveStereoGainWithLatency",
                "description": "Test",
                "category": "effect",
                "manufacturer": "Your Company Goes Here",
                "isInstrument": false,

                "source": ["stereo.cmajor"]
            })";

            const auto cmajorSource = R"(
                graph Test [[ main ]]
                {
                    input stream float32<2> in;
                    output stream float32<2> out;

                    input value float32 gain [[ name: "Gain", init: 0.5 ]];

                    node p = PassThruWithLatency;

                    connection in * gain -> p -> out;
                }

                processor PassThruWithLatency
                {
                    input stream float32<2> in;
                    output stream float32<2> out;

                    processor.latency = 32;

                    void main()
                    {
                        loop
                        {
                            out <- in;
                            advance();
                        }
                    }
                }
            )";

            files.push_back ({ "stereo.cmajorpatch", manifestSource });
            files.push_back ({ "stereo.cmajor", cmajorSource });
        }

        const auto vfs = createJITEnvironmentWithInMemoryFileSystem (files);

        auto [plugin, updatePatch] = cmaj::plugin::clap::createUpdatablePlugin (descriptor, host, "echo.cmajorpatch", vfs);

        CHOC_ASSERT (plugin != nullptr);
        CHOC_ASSERT (updatePatch != nullptr);

        host.data.plugin = plugin.get();
        plugin->init (plugin.get());

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
        CHOC_ASSERT (deactivateOnExit.activated);

        auto* maybeNotePortExtension = plugin->get_extension (plugin.get(), CLAP_EXT_NOTE_PORTS);
        CHOC_ASSERT (maybeNotePortExtension != nullptr);

        const auto& notePortExtension = *reinterpret_cast<const clap_plugin_note_ports_t*> (maybeNotePortExtension);

        {
            CHOC_ASSERT (notePortExtension.count (plugin.get(), true) == static_cast<uint32_t> (1));
            CHOC_ASSERT (notePortExtension.count (plugin.get(), false) == static_cast<uint32_t> (1));
        }

        auto* maybeAudioPortsExtension = plugin->get_extension (plugin.get(), CLAP_EXT_AUDIO_PORTS);
        CHOC_ASSERT (maybeAudioPortsExtension != nullptr);

        const auto& audioPortsExtension = *reinterpret_cast<const clap_plugin_audio_ports_t*> (maybeAudioPortsExtension);

        CHOC_ASSERT (audioPortsExtension.count (plugin.get(), true) == static_cast<uint32_t> (0));
        CHOC_ASSERT (audioPortsExtension.count (plugin.get(), false) == static_cast<uint32_t> (0));

        auto* maybeLatencyExtension = plugin->get_extension (plugin.get(), CLAP_EXT_LATENCY);
        CHOC_ASSERT (maybeLatencyExtension != nullptr);

        const auto& latencyExtension = *reinterpret_cast<const clap_plugin_latency_t*> (maybeLatencyExtension);

        CHOC_ASSERT (latencyExtension.get (plugin.get()) == static_cast<uint32_t> (0));

        auto* maybeParametersExtension = plugin->get_extension (plugin.get(), CLAP_EXT_PARAMS);
        CHOC_ASSERT (maybeParametersExtension != nullptr);

        const auto& parametersExtension = *reinterpret_cast<const clap_plugin_params_t*> (maybeParametersExtension);

        CHOC_ASSERT (parametersExtension.count (plugin.get()) == static_cast<uint32_t> (0));

        host.clearCounts();

        StubStereoAudioPortBackingData<minBlockSize> inputBacking;
        StubStereoAudioPortBackingData<minBlockSize> outputBacking;

        auto inputs = toClapAudioBuffer (inputBacking);
        std::fill (inputBacking.left.begin(), inputBacking.left.end(), -1.0f);
        std::fill (inputBacking.right.begin(), inputBacking.right.end(), 1.0f);

        auto outputs = toClapAudioBuffer (outputBacking);

        using EventQueueContext = std::vector<clap_event_note_t>;
        EventQueueContext inputEventQueueContext;
        const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

        // execute
        updatePatch ("stereo.cmajorpatch");

        {
            CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
            const clap_process_t process
            {
                /*.steady_time = */-1,
                /*.frames_count = */minBlockSize,
                /*.transport = */nullptr, // free-running
                /*.audio_inputs = */std::addressof (inputs),
                /*.audio_outputs = */std::addressof (outputs),
                /*.audio_inputs_count = */1,
                /*.audio_outputs_count = */1,
                /*.in_events = */std::addressof (inputEventQueue),
                /*.out_events = */nullptr
            };

            CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

            plugin->stop_processing (plugin.get()); // N.B. audio-thread
        }

        // verify
        CHOC_EXPECT_NEAR (outputBacking.left[0], -0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[1], -0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[2], -0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[3], -0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[0], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[1], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[2], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[3], 0.5f, 0.0001f);

        {
            CHOC_ASSERT (! host.rescanNotePortsCallArgs.empty());
            CHOC_EXPECT_EQ (host.rescanNotePortsCallArgs.size(), static_cast<size_t> (1));
            CHOC_EXPECT_EQ (host.rescanNotePortsCallArgs.front(), static_cast<uint32_t> (CLAP_NOTE_PORTS_RESCAN_ALL));

            CHOC_EXPECT_EQ (notePortExtension.count (plugin.get(), true), static_cast<uint32_t> (0));
            CHOC_EXPECT_EQ (notePortExtension.count (plugin.get(), false), static_cast<uint32_t> (0));
        }

        {
            CHOC_EXPECT_EQ (host.requestRestartCallCount, static_cast<size_t> (1));
            CHOC_ASSERT (! host.rescanAudioPortsCallArgs.empty());
            CHOC_EXPECT_EQ (host.rescanAudioPortsCallArgs.size(), static_cast<size_t> (1));
            CHOC_EXPECT_EQ (host.rescanAudioPortsCallArgs.front(), static_cast<uint32_t> (CLAP_AUDIO_PORTS_RESCAN_LIST));

            const auto inputPortCount = audioPortsExtension.count (plugin.get(), true);
            const auto outputPortCount = audioPortsExtension.count (plugin.get(), false);

            CHOC_EXPECT_EQ (inputPortCount, static_cast<uint32_t> (1));
            CHOC_EXPECT_EQ (outputPortCount, static_cast<uint32_t> (1));

            {
                const auto port = getPortInfo (*plugin, 0, true);
                CHOC_ASSERT (port);
                CHOC_EXPECT_EQ (port->id, static_cast<uint32_t> (0));
                CHOC_EXPECT_EQ (std::string_view (port->name), std::string_view ("in"));
                CHOC_EXPECT_TRUE (port->flags & CLAP_AUDIO_PORT_IS_MAIN);
                CHOC_EXPECT_EQ (port->channel_count, static_cast<uint32_t> (2));
                CHOC_EXPECT_EQ (std::string_view (port->port_type), std::string_view (CLAP_PORT_STEREO));
                CHOC_EXPECT_EQ (port->in_place_pair, CLAP_INVALID_ID);
            }

            {
                const auto port = getPortInfo (*plugin, 0, false);
                CHOC_ASSERT (port);
                CHOC_EXPECT_EQ (port->id, static_cast<uint32_t> (0));
                CHOC_EXPECT_EQ (std::string_view (port->name), std::string_view ("out"));
                CHOC_EXPECT_TRUE (port->flags & CLAP_AUDIO_PORT_IS_MAIN);
                CHOC_EXPECT_EQ (port->channel_count, static_cast<uint32_t> (2));
                CHOC_EXPECT_EQ (std::string_view (port->port_type), std::string_view (CLAP_PORT_STEREO));
                CHOC_EXPECT_EQ (port->in_place_pair, CLAP_INVALID_ID);
            }
        }

        {
            CHOC_ASSERT (! host.rescanParametersCallArgs.empty());
            CHOC_EXPECT_EQ (host.rescanParametersCallArgs.size(), static_cast<size_t> (1));
            CHOC_EXPECT_EQ (host.rescanParametersCallArgs.front(), static_cast<clap_param_rescan_flags> (CLAP_PARAM_RESCAN_ALL));

            CHOC_EXPECT_EQ (parametersExtension.count (plugin.get()), static_cast<uint32_t> (1));

            {
                clap_param_info_t out {};
                CHOC_EXPECT_TRUE (parametersExtension.get_info (plugin.get(), 0, std::addressof (out)));
                // N.B. currently not checking the ID. IDs are currently the endpoint handles, which is brittle. improve this.
                CHOC_EXPECT_TRUE (out.flags & CLAP_PARAM_IS_AUTOMATABLE);
                CHOC_EXPECT_EQ (toString<std::string_view> (out.name, CLAP_NAME_SIZE), std::string_view ("Gain"));
                CHOC_EXPECT_NEAR (out.min_value, 0.0, 0.0001f);
                CHOC_EXPECT_NEAR (out.max_value, 1.0, 0.0001f);
                CHOC_EXPECT_NEAR (out.default_value, 0.5, 0.0001f);
            }
        }

        {
            CHOC_EXPECT_EQ (host.latencyChangedCallCount, static_cast<size_t> (1));

            CHOC_EXPECT_EQ (latencyExtension.get (plugin.get()), static_cast<uint32_t> (32));
        }
    }

    // state
    const auto testStateSerialisationRoundTrip = [&progress] (const auto& createPlugin)
    {
        // setup
        StubHost host {};

        const double frequency = 4;
        constexpr uint32_t minBlockSize = 4;
        constexpr uint32_t maxBlockSize = 4;

        StubStereoAudioPortBackingData<minBlockSize> inputBacking;
        StubStereoAudioPortBackingData<minBlockSize> outputBacking;

        auto inputs = toClapAudioBuffer (inputBacking);
        std::fill (inputBacking.left.begin(), inputBacking.left.end(), 1.0f);
        std::fill (inputBacking.right.begin(), inputBacking.right.end(), 1.0f);

        auto outputs = toClapAudioBuffer (outputBacking);

        const clap_id gainEventId = 2; // N.B. using endpoint handles is brittle

        std::vector<uint8_t> savedState;

        // execute
        {
            auto plugin = createPlugin (host);

            CHOC_ASSERT (plugin != nullptr);

            plugin->init (plugin.get());

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            using EventQueueContext = std::vector<clap_event_param_value_t>;

            EventQueueContext inputEventQueueContext
            {
                makeParameterEvent (0, gainEventId, 0.5f),
            };
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */nullptr, // free-running
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */1,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }

            auto* maybeStateExtension = plugin->get_extension (plugin.get(), CLAP_EXT_STATE);
            CHOC_ASSERT (maybeStateExtension != nullptr);

            const auto& stateExtension = *reinterpret_cast<const clap_plugin_state_t*> (maybeStateExtension);

            struct Writer
            {
                std::function<int64_t(const uint8_t*, uint64_t)> write;
            };

            Writer writerImpl
            {
                [&] (const auto* source, auto)
                {
                    savedState.push_back (*static_cast<const uint8_t*> (source));
                    return 1;
                }
            };

            clap_ostream_t writer {};
            writer.ctx = std::addressof (writerImpl);
            writer.write = [] (const auto* stream, const void* source, uint64_t size) -> int64_t
            {
                const auto& impl = *reinterpret_cast<const Writer*> (stream->ctx);
                return impl.write (reinterpret_cast<const uint8_t*> (source), size);
            };

            CHOC_EXPECT_TRUE (stateExtension.save (plugin.get(), std::addressof (writer)));
        }

        {
            auto plugin = createPlugin (host);

            CHOC_ASSERT (plugin != nullptr);

            plugin->init (plugin.get());

            auto* maybeStateExtension = plugin->get_extension (plugin.get(), CLAP_EXT_STATE);
            CHOC_ASSERT (maybeStateExtension != nullptr);

            const auto& stateExtension = *reinterpret_cast<const clap_plugin_state_t*> (maybeStateExtension);

            struct Reader
            {
                std::function<int64_t(uint8_t*, uint64_t)> read;
            };

            size_t numBytesRead { 0 };

            Reader readerImpl
            {
                [&] (auto* out, auto) -> int64_t
                {
                    const auto totalBytes = savedState.size();
                    const auto remainingBytes = totalBytes - numBytesRead;

                    if (remainingBytes == 0)
                        return 0;

                    const auto chunkSize = std::min (remainingBytes, static_cast<size_t> (1));

                    const auto begin = savedState.data() + numBytesRead;
                    std::copy (begin, begin + chunkSize, reinterpret_cast<uint8_t*> (out));

                    numBytesRead += chunkSize;

                    return static_cast<int64_t> (chunkSize);  // todo: error case, -1
                }
            };

            clap_istream_t reader {};
            reader.ctx = std::addressof (readerImpl);
            reader.read = [] (const auto* stream, void* sink, uint64_t size) -> int64_t
            {
                const auto& impl = *reinterpret_cast<const Reader*> (stream->ctx);
                return impl.read (reinterpret_cast<uint8_t*> (sink), size);
            };

            // N.B. load before activation on purpose, as that is what happens in Bitwig, for example
            CHOC_EXPECT_TRUE (stateExtension.load (plugin.get(), std::addressof (reader)));

            ScopedActivator deactivateOnExit { *plugin, frequency, minBlockSize, maxBlockSize }; // N.B. main-thread
            CHOC_ASSERT (deactivateOnExit.activated);

            using EventQueueContext = std::vector<clap_event_param_value_t>;

            EventQueueContext inputEventQueueContext {};
            const auto inputEventQueue = toInputEventQueue<EventQueueContext> (inputEventQueueContext);

            {
                CHOC_EXPECT_TRUE (plugin->start_processing (plugin.get())); // N.B. audio-thread
                const clap_process_t process
                {
                    /*.steady_time = */-1,
                    /*.frames_count = */minBlockSize,
                    /*.transport = */nullptr, // free-running
                    /*.audio_inputs = */std::addressof (inputs),
                    /*.audio_outputs = */std::addressof (outputs),
                    /*.audio_inputs_count = */1,
                    /*.audio_outputs_count = */1,
                    /*.in_events = */std::addressof (inputEventQueue),
                    /*.out_events = */nullptr
                };

                CHOC_EXPECT_EQ (plugin->process (plugin.get(), std::addressof (process)), CLAP_PROCESS_CONTINUE);

                plugin->stop_processing (plugin.get()); // N.B. audio-thread
            }
        }

        // verify
        CHOC_EXPECT_NEAR (outputBacking.left[0], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[1], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[2], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.left[3], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[0], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[1], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[2], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBacking.right[3], 0.5f, 0.0001f);
    };

    {
        CHOC_TEST (StateSerialisationRoundTrip)

        testStateSerialisationRoundTrip ([] (const auto& host)
        {
            const clap_plugin_descriptor_t descriptor {};

            const auto manifestSource = R"({
                "CmajorVersion": 1,
                "ID": "com.your-name.your-patch-id",
                "version": "1.0",
                "name": "Test",
                "description": "Test",
                "category": "generator",
                "manufacturer": "Your Company Goes Here",
                "isInstrument": false,

                "source": ["test.cmajor"]
            })";

            const auto cmajorSource = R"(
                graph Test [[ main ]]
                {
                    input stream float32<2> in;
                    output stream float32<2> out;

                    input value float32 gain [[ name: "Gain", min: 0, max: 1, init: 0.75 ]];

                    connection in * gain -> out;
                }
            )";

            const auto vfs = createJITEnvironmentWithInMemoryFileSystem ({
                { "test.cmajorpatch", manifestSource },
                { "test.cmajor", cmajorSource }
            });

            return cmaj::plugin::clap::create (descriptor, host, "test.cmajorpatch", vfs);
        });
    }

    // generated descriptor
    {
        const auto toManifest = [] (const auto& manifestJson)
        {
            const auto vfs = createInMemoryFileSystem ({
                { "manifest.cmajorpatch", manifestJson }
            });

            cmaj::PatchManifest manifest;
            manifest.initialiseWithVirtualFile (
                "manifest.cmajorpatch",
                vfs.createFileReader,
                [getFullPathForFile = vfs.getFullPathForFile] (const auto& path) { return getFullPathForFile (path).string(); },
                vfs.getFileModificationTime,
                vfs.fileExists
            );

            return manifest;
        };

        const auto count = [] (const auto* features)
        {
            size_t length = 0;
            for (auto* ptr = features; *ptr != nullptr; ++ptr)
                ++length;

            return length;
        };

        {
            CHOC_TEST (GenerateClapDescriptorBasicMetadata)

            // setup
            const auto manifest = toManifest (R"({
                "CmajorVersion": 1,
                "ID": "com.your-name.your-patch-id",
                "version": "1.0",
                "name": "Name",
                "description": "Description",
                "category": "synth",
                "manufacturer": "Your Company Goes Here",
                "URL": "https://cmajor.dev",
                "isInstrument": true,
                "source": ["test.cmajor"]
            })");

            // execute
            const auto storedDescriptor = cmaj::plugin::clap::detail::toStoredDescriptor (manifest, {}, {});
            const auto descriptor = toClapDescriptor (storedDescriptor);

            // verify
            CHOC_EXPECT_TRUE (clap_version_is_compatible (descriptor.clap_version));
            CHOC_EXPECT_TRUE (descriptor.id && std::string_view (descriptor.id) == "com.your-name.your-patch-id");
            CHOC_EXPECT_TRUE (descriptor.version && std::string_view (descriptor.version) == "1.0");
            CHOC_EXPECT_TRUE (descriptor.name && std::string_view (descriptor.name) == "Name");
            CHOC_EXPECT_TRUE (descriptor.description && std::string_view (descriptor.description) == "Description");
            CHOC_EXPECT_TRUE (descriptor.vendor && std::string_view (descriptor.vendor) == "Your Company Goes Here");
            CHOC_EXPECT_TRUE (descriptor.url && std::string_view (descriptor.url) == "https://cmajor.dev");
            CHOC_EXPECT_TRUE (descriptor.manual_url && std::string_view (descriptor.manual_url).empty());
            CHOC_EXPECT_TRUE (descriptor.support_url && std::string_view (descriptor.support_url).empty());
            CHOC_EXPECT_TRUE (descriptor.features && count (descriptor.features) == 1);
            CHOC_EXPECT_TRUE (descriptor.features[1] == nullptr);
        }

        {
            CHOC_TEST (GenerateClapDescriptorInstrument)

            // setup
            const auto manifest = toManifest (R"({
                "CmajorVersion": 1,
                "ID": "com.your-name.your-patch-id",
                "version": "1.0",
                "name": "Name",
                "isInstrument": true,
                "source": ["test.cmajor"]
            })");

            // execute
            const auto storedDescriptor = cmaj::plugin::clap::detail::toStoredDescriptor (manifest, {}, {});
            const auto descriptor = toClapDescriptor (storedDescriptor);

            // verify
            CHOC_ASSERT (descriptor.features && count (descriptor.features) == 1);
            CHOC_EXPECT_EQ (std::string_view (descriptor.features[0]), std::string_view (CLAP_PLUGIN_FEATURE_INSTRUMENT));
        }

        {
            CHOC_TEST (GenerateClapDescriptorForEffect)

            // setup
            const auto manifest = toManifest (R"({
                "CmajorVersion": 1,
                "ID": "com.your-name.your-patch-id",
                "version": "1.0",
                "name": "Name",
                "source": ["test.cmajor"]
            })");

            // execute
            const auto storedDescriptor = cmaj::plugin::clap::detail::toStoredDescriptor (manifest, {}, {});
            const auto descriptor = toClapDescriptor (storedDescriptor);

            // verify
            CHOC_ASSERT (descriptor.features && count (descriptor.features) == 1);
            CHOC_EXPECT_EQ (std::string_view (descriptor.features[0]), std::string_view (CLAP_PLUGIN_FEATURE_AUDIO_EFFECT));
        }

        {
            CHOC_TEST (GenerateClapDescriptorForNoteEffectDeducedFromEndpoints)

            // setup
            const auto manifest = toManifest (R"({
                "CmajorVersion": 1,
                "ID": "com.your-name.your-patch-id",
                "version": "1.0",
                "name": "Name",
                "source": ["test.cmajor"]
            })");

            const auto createMidiEndpoint = [] (const std::string& name, auto isInput)
            {
                cmaj::EndpointDetails endpoint;
                endpoint.endpointID = cmaj::EndpointID::create (name);
                endpoint.endpointType = cmaj::EndpointType::event;
                endpoint.isInput = isInput;
                endpoint.dataTypes.push_back (cmaj::MIDIEvents::createMIDIMessageObject ({}).getType());
                return endpoint;
            };

            // execute
            const auto storedDescriptor = cmaj::plugin::clap::detail::toStoredDescriptor (
                manifest,
                {{ createMidiEndpoint ("midiIn", true) }},
                {{ createMidiEndpoint ("midiOut", false) }}
            );
            const auto descriptor = toClapDescriptor (storedDescriptor);

            // verify
            CHOC_ASSERT (descriptor.features && count (descriptor.features) == 1);
            CHOC_EXPECT_EQ (std::string_view (descriptor.features[0]), std::string_view (CLAP_PLUGIN_FEATURE_NOTE_EFFECT));
        }
    }

    {
        CHOC_TEST (PluginCreatedWithGeneratedCppHasParameters)

        // setup
        StubHost host {};

        auto plugin = cmaj::plugin::clap::test::makeGeneratedCppPlugin<StubGeneratedCppPatch> (host);

        CHOC_ASSERT (plugin != nullptr);

        // execute
        plugin->init (plugin.get());

        // verify
        auto* maybeParametersExtension = plugin->get_extension (plugin.get(), CLAP_EXT_PARAMS);
        CHOC_ASSERT (maybeParametersExtension != nullptr);

        const auto& parametersExtension = *reinterpret_cast<const clap_plugin_params_t*> (maybeParametersExtension);

        CHOC_EXPECT_EQ (parametersExtension.count (plugin.get()), static_cast<uint32_t> (1));
    }
}

} // namespace cmaj::plugin::clap
