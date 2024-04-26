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

#include "../../include/cmaj_ErrorHandling.h"
#include "../../../../include/cmajor/COM/cmaj_Library.h"

#include "../backends/cmaj_EngineBase.h"
#include "CPlusPlus/cmaj_CPlusPlus.h"
#include "WebAssembly/cmaj_WebAssembly.h"
#include "LLVM/cmaj_LLVM.h"

namespace cmaj
{

using CreateEngineFactoryFn = EngineFactoryPtr(*)();

struct BackEnd
{
    const char* name = {};
    CreateEngineFactoryFn createFactory = {};
};

static BackEnd backends[] =
{
    // This order determines which one is the default engine

   #if CMAJ_ENABLE_PERFORMER_LLVM
    { cmaj::llvm::backendName, cmaj::llvm::createEngineFactory },
   #endif

   #if CMAJ_ENABLE_PERFORMER_WEBVIEW
    { cmaj::webassembly::backendName, cmaj::webassembly::createEngineFactory },
   #endif

   #if CMAJ_ENABLE_PERFORMER_CPP
    { cmaj::cplusplus::backendName, cmaj::cplusplus::createEngineFactory },
   #endif

    {}
};

static constexpr size_t numBackends = sizeof (backends) / sizeof (*backends) - 1;

static std::string getBackEndList()
{
    std::string list;

    for (size_t i = 0; i < numBackends; ++i)
    {
        if (i != 0)
            list += " ";

        list += backends[i].name;
    }

    return list;
}

const char* Library::getEngineTypes()
{
    static std::string list (getBackEndList());
    return list.c_str();
}

//==============================================================================
/// If building a library that only does code-gen, without any actual engine implementations,
/// it still needs an engine object to perform the code-gen process, so this is a fallback for that..
struct DummyEngineFactory  : public choc::com::ObjectWithAtomicRefCount<EngineFactoryInterface, DummyEngineFactory>
{
    virtual ~DummyEngineFactory() = default;

    struct DummyEngine
    {
        DummyEngine (EngineBase<DummyEngine>&) {}

        static constexpr bool canUseForwardBranches = false;
        static constexpr bool usesDynamicRateAndSessionID = true;
        static constexpr bool allowTopLevelSlices = false;
        static constexpr bool supportsExternalFunctions = true;
        static bool engineSupportsIntrinsic (AST::Intrinsic::Type) { return true; }

        static std::string getEngineVersion()   { return "dummy"; }

        struct LinkedCode { LinkedCode (const DummyEngine&, uint32_t, double, CacheDatabaseInterface*, const char*) {} static constexpr double latency = 0; };
        struct JITInstance { JITInstance (std::shared_ptr<LinkedCode>, int32_t, double) {} };

        PerformerInterface* createPerformer (std::shared_ptr<LinkedCode>) { return {}; }
    };

    const char* getName() override      { return "dummy"; }

    EngineInterface* createEngine (const char* engineCreationOptions) override
    {
        try
        {
            return choc::com::create<EngineBase<DummyEngine>> (engineCreationOptions).getWithIncrementedRefCount();
        }
        catch (...) {}

        return {};
    }
};

//==============================================================================
EngineFactoryPtr Library::createEngineFactory (const char* name)
{
    if constexpr (numBackends == 0)
    {
        (void) name;
        return choc::com::create<DummyEngineFactory>();
    }
    else
    {
        auto findBackEnd = [] (const char* nameToFind) -> BackEnd*
        {
            for (size_t i = 0; i < numBackends; ++i)
                if (nameToFind == nullptr
                     || std::string_view (nameToFind).empty()
                     || std::string_view (nameToFind) == backends[i].name)
                    return std::addressof (backends[i]);

            return {};
        };

        if (auto b = findBackEnd (name))
            return b->createFactory();

        return {};
    }
}

}
