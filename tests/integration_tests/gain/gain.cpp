#include <cstdint>
#include <cmath>
#include <cassert>
#include <string>
#include <cstring>

struct test
{
    test() = default;
    ~test() = default;

    //==============================================================================
    static constexpr uint32_t maxFramesPerBlock  = 1024;
    static constexpr uint32_t eventBufferSize    = 32;
    static constexpr uint32_t maxOutputEventSize = 0;
    static constexpr double   latency = 0;

    //==============================================================================
    int32_t _sessionID {};
    double _frequency {};

    float in[maxFramesPerBlock];
    float out[maxFramesPerBlock];

    void initialise (int32_t sessionID, double frequency)
    {
        _sessionID = sessionID;
        _frequency = frequency;
    }

    void advance (int32_t frames)
    {
        for (int i = 0; i < frames; i++)
            out[i] = in[i] * 0.5f;
    }

    //==============================================================================
    using EndpointHandle = uint32_t;

    enum class EndpointHandles
    {
        in  = 1,
        out = 2
    };

    static constexpr uint32_t getEndpointHandleForName (std::string_view name)
    {
        if (name == "in")   return static_cast<uint32_t> (EndpointHandles::in);
        if (name == "out")  return static_cast<uint32_t> (EndpointHandles::out);
        return 0;
    }

    static constexpr const char* programDetailsJSON = R"JSON({
      "outputs: [{
        "endpointID": "out",
        "endpointType": "stream",
        "dataType": {
          "type": "float32"
        },
        "purpose": "audio out",
        "numAudioChannels": 1
      }],
      "inputs": [{
        "endpointID": "in",
        "endpointType": "stream",
        "dataType": {
          "type": "float32"
        },
        "purpose": "audio in",
        "numAudioChannels": 1
      }]
    })JSON";

    //==============================================================================
    void setValue (EndpointHandle, const void*, int32_t)                { assert (false); }
    void copyOutputValue (EndpointHandle, void*)                        { assert (false); }

    //==============================================================================
    void addEvent (EndpointHandle, uint32_t, const void*)               { assert (false); }
    uint32_t getNumOutputEvents (EndpointHandle)                        { assert (false); return {}; }
    void resetOutputEventCount (EndpointHandle)                         { assert (false); }
    uint32_t getOutputEventType (EndpointHandle, uint32_t)              { assert (false); return {}; }
    static uint32_t getOutputEventDataSize (EndpointHandle, uint32_t)   { assert (false); return 0; }
    uint32_t readOutputEvent (EndpointHandle, uint32_t, void*)          { assert (false); return {}; }

    //==============================================================================
    void setInputFrames (EndpointHandle endpointHandle, const void* data, uint32_t numFrames, uint32_t numTrailingFramesToClear)
    {
        if (endpointHandle == 1) { std::memcpy (in, data, numFrames * 4); if (numTrailingFramesToClear != 0) std::memset (in + numFrames, 0, numTrailingFramesToClear * 4); return; }
        assert (false);
    }

    void copyOutputFrames (EndpointHandle endpointHandle, void* dest, uint32_t numFramesToCopy)
    {
        if (endpointHandle == 2) { std::memcpy (dest, std::addressof (out), 4 * numFramesToCopy); std::memset (std::addressof (out), 0, 4 * numFramesToCopy); return; }
        assert (false);
    }

    //==============================================================================
    const char* getStringForHandle (uint32_t handle, size_t& stringLength)
    {
        (void) handle; (void) stringLength;
        return "";
    }
};
