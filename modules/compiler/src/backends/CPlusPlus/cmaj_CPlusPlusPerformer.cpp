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

#include "../../../include/cmaj_DefaultFlags.h"

#if CMAJ_ENABLE_PERFORMER_CPP

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

#include "../../../include/cmaj_ErrorHandling.h"
#include "choc/platform/choc_DynamicLibrary.h"
#include "choc/text/choc_Files.h"

#include "../../../../../include/cmajor/API/cmaj_Engine.h"
#include "../../../../../include/cmajor/helpers/cmaj_PerformerProxy.h"
#include "../cmaj_EngineBase.h"
#include "../../AST/cmaj_AST.h"

#include "cmaj_CPlusPlus.h"


namespace cmaj::cplusplus
{

//==============================================================================
struct TemporaryCompiledDLL
{
    TemporaryCompiledDLL (const std::string& cppContent, cmaj::BuildSettings settings, const std::string& extraCompileArgs, const std::string& extraLinkerArgs)
        : buildSettings (settings)
    {
        try
        {
            std::ofstream cpp (tmpFolder.file.string() + "/" + cppFilename, std::ios::binary);
            cpp << cppContent;

            if (buildSettings.shouldDumpDebugInfo())
                std::cout << cppContent << std::endl;
        }
        catch (...)
        {
            CMAJ_ASSERT_FALSE;
        }

        auto getOptimisationFlag = [] (int level) -> std::string
        {
            switch (level)
            {
                case 0:     return "-O0 -g";
                case 1:     return "-O1";
                case 2:     return "-O2";
                case 3:     return "-O3";
                case 4:     return "-O3 -ffast-math";
                default:    return "-O3";
            }
        };

        auto cmajorFolder = std::filesystem::path (__FILE__);

        while (! cmajorFolder.filename().empty() && cmajorFolder.filename() != "cmajor-dev")
            cmajorFolder = cmajorFolder.parent_path();

        cmajorFolder.append ("cmajor").append ("include");

        build (getOptimisationFlag (buildSettings.getOptimisationLevel())
               + " -I" + cmajorFolder.string()
               + " -DMAX_BLOCK_SIZE=" + std::to_string (buildSettings.getMaxBlockSize()) +
               + " -std=c++17 -fPIC -Wno-#pragma-messages -Wno-parentheses-equality -Wno-deprecated-declarations -Wno-tautological-compare -Werror -Wall -Wextra " + extraCompileArgs,
               extraLinkerArgs);
    }

    ~TemporaryCompiledDLL()
    {
        library.reset();

        try
        {
            std::string sym = tmpFolder.file.string() + "/cmaj.dSYM";
            remove_all (std::filesystem::path (sym));
        }
        catch (...) {}
    }

    void build (const std::string& compilerFlags, const std::string& extraLinkerArgs)
    {
       #ifdef WIN32
        (void) compilerFlags;
        (void) extraLinkerArgs;
        throwError (Errors::unimplementedFeature ("cpp performer on windows"));
       #else
        auto compileCommand = "cd " + tmpFolder.file.string()
                            + "&& g++ " + compilerFlags + " -c -o " + objFilename + " " + cppFilename
                            + "&& g++ -shared -o " + libFilename + " " + objFilename + " " + extraLinkerArgs;

        auto* p = ::popen ((compileCommand + " 2>&1").c_str(), "r");

        char errorText[1024];
        size_t errorSize = fread (errorText, 1, sizeof (errorText), p);
        auto errorString = std::string (errorText, errorSize);

        auto statusCode = ::pclose (p);

        if (statusCode == 0 && ! choc::text::contains (errorString, "error:"))
        {
            library = std::make_unique<choc::file::DynamicLibrary> (tmpFolder.file.string() + "/" + libFilename);
        }
        else
        {
            std::cerr << std::endl << compileCommand << std::endl << errorString << std::endl;
            throwError (Errors::failedToCompile (errorString));
        }

       #endif
    }

    choc::file::TempFile tmpFolder { choc::file::TempFile::createRandomFilename("cmaj_temp", "d") };
    std::string cppFilename = "cmaj.cpp";
    std::string objFilename = "cmaj.o";
    std::string libFilename = "cmaj.so";

    std::unique_ptr<choc::file::DynamicLibrary> library;
    cmaj::BuildSettings buildSettings;
};


//==============================================================================
struct CPlusPlusEngine
{
    CPlusPlusEngine (EngineBase<CPlusPlusEngine>& e) : engine (e) {}

    EngineBase<CPlusPlusEngine>& engine;

    static std::string getEngineVersion()   { return "cpp1"; }

    static constexpr bool canUseForwardBranches = true;
    static constexpr bool usesDynamicRateAndSessionID = true;
    static constexpr bool allowTopLevelSlices = false;
    static constexpr bool supportsExternalFunctions = false;
    static bool engineSupportsIntrinsic (AST::Intrinsic::Type) { return true; }

    //==============================================================================
    struct LinkedCode
    {
        LinkedCode (CPlusPlusEngine& cppEngine, bool, double latencyToUse, CacheDatabaseInterface*, const char*)
            : latency (latencyToUse)
        {
            buildSettings = cppEngine.engine.buildSettings;

            if (buildSettings.getMaxBlockSize() == 0)
                buildSettings.setMaxBlockSize (1024);

            auto code = generateCPPClass (*cppEngine.engine.program, {},
                                          buildSettings.getMaxFrequency(),
                                          buildSettings.getMaxBlockSize(),
                                          buildSettings.getEventBufferSize(),
                                          [&] (const EndpointID& e) { return cppEngine.engine.getEndpointHandle (e); });

            if (code.code.empty())
                return;

            std::string extraCompileArgs, extraLinkerArgs;

            if (cppEngine.engine.options.isObject())
            {
                if (cppEngine.engine.options.hasObjectMember ("overrideSource"))
                {
                    code.code = choc::file::loadFileAsString (std::string (cppEngine.engine.options["overrideSource"].getString()));
                    code.mainClassName = "test";
                }

                if (cppEngine.engine.options.hasObjectMember ("extraCompileArgs"))
                    extraCompileArgs = cppEngine.engine.options["extraCompileArgs"].getString();

                if (cppEngine.engine.options.hasObjectMember ("extraLinkerArgs"))
                    extraLinkerArgs = cppEngine.engine.options["extraLinkerArgs"].getString();
            }

            code.code += getWrapperCode (code.mainClassName);

            dll = std::make_unique<TemporaryCompiledDLL> (code.code,
                                                          buildSettings,
                                                          extraCompileArgs,
                                                          extraLinkerArgs);

            CMAJ_ASSERT (dll->library != nullptr);

            loadFunction (createEngineFn, "createEngine");
        }

        //==============================================================================
        BuildSettings buildSettings;
        std::unique_ptr<TemporaryCompiledDLL> dll;

        using CreateEngineFn = cmaj::EngineInterface*(*)();
        CreateEngineFn createEngineFn = {};

        double latency;

        template <typename Fn>
        void loadFunction (Fn& f, std::string_view name)
        {
            f = reinterpret_cast<Fn> (dll->library->findFunction (name));
            CMAJ_ASSERT (f != nullptr);
        }

        static std::string getWrapperCode (std::string_view className)
        {
            std::string fns = R"CPPGEN(

#include "cmajor/helpers/cmaj_GeneratedCppEngine.h"

#ifdef _MSC_VER
 #define CMAJ_DLL_EXPORT __declspec (dllexport)
#else
 #define CMAJ_DLL_EXPORT __attribute__ ((visibility("default")))
#endif

extern "C" CMAJ_DLL_EXPORT cmaj::EngineInterface* createEngine()
{
    return choc::com::create<cmaj::GeneratedCppEngine<CLASS>>().getWithIncrementedRefCount();
}

)CPPGEN";

            fns = choc::text::replace (fns, "CLASS", className);

            return fns;
        }
    };

    // This is just needed to manage the COM object lifetimes
    struct Proxy  : public choc::com::ObjectWithAtomicRefCount<cmaj::PerformerProxy, Proxy>
    {
        Proxy (std::shared_ptr<LinkedCode> c, cmaj::EnginePtr e)  : engine (e), code (c)
        {
            target = PerformerPtr (e->createPerformer());
        }

        ~Proxy()
        {
            target = {};
            engine = {};
        }

        cmaj::EnginePtr engine;
        std::shared_ptr<LinkedCode> code;
    };

    PerformerInterface* createPerformer (std::shared_ptr<LinkedCode> code)
    {
        cmaj::Engine e;
        e.engine = EnginePtr (code->createEngineFn());
        e.setBuildSettings (code->buildSettings);
        return choc::com::create<Proxy> (code, e.engine).getWithIncrementedRefCount();
    }
};

//==============================================================================
struct Factory : public choc::com::ObjectWithAtomicRefCount<EngineFactoryInterface, Factory>
{
    virtual ~Factory() = default;
    const char* getName() override      { return "cpp"; }

    EngineInterface* createEngine (const char* engineCreationOptions) override
    {
        try
        {
            return choc::com::create<EngineBase<CPlusPlusEngine>> (engineCreationOptions).getWithIncrementedRefCount();
        }
        catch (...) {}

        return {};
    }
};

EngineFactoryPtr createEngineFactory()  { return choc::com::create<Factory>(); }


} // namespace cmaj::cplusplus

#endif // CMAJ_ENABLE_PERFORMER_CPP
