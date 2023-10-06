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

#include "../COM/cmaj_PerformerInterface.h"

namespace cmaj
{

//==============================================================================
/// A helper class that can be used if you need to wrap an Engine
/// and intercept some of the calls it makes.
struct PerformerProxy  : public PerformerInterface
{
    virtual ~PerformerProxy() = default;

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
