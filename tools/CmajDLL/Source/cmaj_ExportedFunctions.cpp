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

#include "../../../modules/compiler/include/cmaj_ErrorHandling.h"
#include "../../../include/cmajor/COM/cmaj_Library.h"

#ifdef _MSC_VER
 #define CMAJ_API_EXPORT extern "C" __declspec(dllexport)
#else
 #define CMAJ_API_EXPORT extern "C" __attribute__((visibility("default")))
#endif


CMAJ_API_EXPORT cmaj::Library::EntryPoints* cmajor_getEntryPointsV9()
{
    struct EntryPointsImpl  : public cmaj::Library::EntryPoints
    {
        EntryPointsImpl() = default;
        virtual ~EntryPointsImpl() = default;

        inline const char* getVersion() override
        {
             return CMAJ_VERSION;
        }

        cmaj::ProgramInterface* createProgram() override
        {
            return cmaj::Library::createProgram().getWithIncrementedRefCount();
        }

        const char* getEngineTypes() override
        {
            return cmaj::Library::getEngineTypes();
        }

        cmaj::EngineFactoryInterface* createEngineFactory (const char* name) override
        {
            return cmaj::Library::createEngineFactory (name).getWithIncrementedRefCount();
        }
    };

    static EntryPointsImpl ep;
    return std::addressof (ep);
}
