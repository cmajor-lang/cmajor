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

#include "../COM/cmaj_PerformerInterface.h"

namespace cmaj
{

//==============================================================================
/// A helper class that can be used if you need to wrap an Engine
/// and intercept some of the calls it makes.
struct PerformerProxy  : public PerformerInterface
{
    PerformerProxy (PerformerPtr targetPerformer) : target (std::move (targetPerformer)) {}
    ~PerformerProxy() override {}

    void setBlockSize (uint32_t numFramesForNextBlock) override                                     { target->setBlockSize (numFramesForNextBlock); }
    void setInputFrames (EndpointHandle e, const void* data, uint32_t numFrames) override           { target->setInputFrames (e, data, numFrames); }
    void setInputValue (EndpointHandle e, const void* data, uint32_t n) override                    { target->setInputValue (e, data, n); }
    void addInputEvent (EndpointHandle e, uint32_t index, const void* data) override                { target->addInputEvent (e, index, data); }
    void copyOutputValue (EndpointHandle e, void* dest) override                                    { target->copyOutputValue (e, dest); }
    void copyOutputFrames (EndpointHandle e, void* dest, uint32_t num) override                     { target->copyOutputFrames (e, dest, num); }
    void iterateOutputEvents (EndpointHandle e, void* c, HandleOutputEventCallback h) override      { return target->iterateOutputEvents (e, c, h); }
    void advance() override                                                                         { target->advance(); }
    const char* getStringForHandle (uint32_t h, size_t& len) override                               { return target->getStringForHandle (h, len); }
    uint32_t getXRuns() override                                                                    { return target->getXRuns(); }
    uint32_t getMaximumBlockSize() override                                                         { return target->getMaximumBlockSize(); }
    double getLatency() override                                                                    { return target->getLatency(); }
    uint32_t getEventBufferSize() override                                                          { return target->getEventBufferSize(); }
    const char* getRuntimeError() override                                                          { return target->getRuntimeError(); }

    PerformerPtr target;
};

}
