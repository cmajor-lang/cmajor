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

#include "cmaj_Performer.h"

#include <functional>
#include <vector>

namespace cmaj
{

/**
    This class that acts as a wrapper around an EngineInterface object, replacing
    its clunky COM API with nicer, idiomatic C++ methods.

    This is basically a smart-pointer to an EngineInterface object, so bear in mind
    that copying an Engine is just copying a (ref-counted) pointer - it won't make a
    copy of the underlying engine itself.
*/
struct Engine
{
    /// This creates an empty engine, which is basically a null pointer.
    /// To create a usable Engine, use the Engine::create() function.
    Engine() = default;
    ~Engine();

    Engine (const Engine&) = default;
    Engine (Engine&&) = default;
    Engine& operator= (const Engine&) = default;
    Engine& operator= (Engine&&) = default;

    Engine (EnginePtr);

    /// Returns true if this is a valid engine.
    operator bool() const                           { return engine; }

    bool operator!= (decltype (nullptr)) const      { return engine; }
    bool operator== (decltype (nullptr)) const      { return ! engine; }

    //==============================================================================
    /// Tries to create a engine, returning a null object if it fails for some reason.
    ///
    /// For a list of the available strings that you can pass to engineType, see
    /// the getAvailableEngineTypes() function. For a default (LLVM JIT) engine,
    /// just leave this empty.
    ///
    /// The engineCreationOptions parameter is used for supplying obscure
    /// implementation-specific flags to the underlying engine factory, so unlikely
    /// to be needed by most users.
    static Engine create (const std::string& engineType = {},
                          const choc::value::Value* engineCreationOptions = nullptr);

    /// Returns a list of the engine types that are available for use in the
    /// create() function.
    static std::vector<std::string> getAvailableEngineTypes();

    //==============================================================================
    /// Returns the engine's current build settings
    BuildSettings getBuildSettings() const;

    /// Applies some new build settings for the engine to use.
    /// The engine maintains a current copy of its settings, and they're used by
    /// various functions like load(), link() and createPerformer() as required.
    void setBuildSettings (const BuildSettings& newSettings);


    //==============================================================================
    using ExternalVariableProviderFn = std::function<choc::value::Value (const cmaj::ExternalVariable&)>;
    using ExternalFunctionProviderFn = std::function<void*(const char* functionName, choc::span<choc::value::Type> parameterTypes)>;

    /// Attempts to load a program into this engine.
    /// Returns true if it succeeds, and adds any errors or messages to the list provided.
    /// The ExternalVariableProviderFn and ExternalFunctionProviderFn are used to resolve
    /// any external variables or functions that the program may contain. If your program
    /// doesn't have any externals, you can pass null functors for these parameters.
    bool load (DiagnosticMessageList& messages,
               const Program& programToLoad,
               ExternalVariableProviderFn getExternalVariable,
               ExternalFunctionProviderFn getExternalFunction);

    /// Unloads the current program and completely resets the state of the engine.
    void unload();

    //==============================================================================
    /// Returns a list of the input endpoints that the loaded program provides.
    /// This may be called after successfully loading a program.
    EndpointDetailsList getInputEndpoints() const;

    /// Returns a JSON list of the output endpoints that the loaded program provides.
    /// This may be called after successfully loading a program.
    EndpointDetailsList getOutputEndpoints() const;

    /// Returns a handle which can be used to communicate with an input or output endpoint.
    /// This may be called after successfully loading a program, and before linking has happened.
    /// If the ID isn't found, this will return an invalid handle.
    EndpointHandle getEndpointHandle (const char* endpointID) const;

    /// If a program has been successfully loaded, this returns a JSON object with
    /// information about its properties.
    /// This may be called after successfully loading a program.
    choc::value::Value getProgramDetails() const;

    //==============================================================================
    /// Attempts to link the currently-loaded program into a state that can be executed.
    /// After loading and before linking, the caller must:
    ///  - Resolve any external variables
    ///  - Connect any endpoints that it needs to send or receive from
    ///
    /// If all goes well, this returns true, and the caller can call createPerformer()
    /// to get a Performer to use for rendering. Any link errors are added to the
    /// DiagnosticMessageList that is passed in.
    bool link (DiagnosticMessageList&, CacheDatabaseInterface* optionalCache = nullptr);

    /// When a program has been successfully linked, calling this will return a new
    /// instance of a Performer which can be used to render the program.
    /// You call this multiple times to create multiple independent instances of the
    /// program.
    Performer createPerformer();

    /// Returns true if a program has been successfully loaded, but not yet linked.
    bool isLoaded() const;

    /// Returns true if a program has been successfully linked and can be run.
    bool isLinked() const;

    /// Returns a string with any relevant logging output produced during the last
    /// load/link calls.
    std::string getLastBuildLog() const;

    //==============================================================================
    /// Holds the results created by the generateCode() method.
    struct CodeGenOutput
    {
        std::string generatedCode, mainClassName;
        DiagnosticMessageList messages;
    };

    /// Attempts to generate some code from the currently-loaded program, producing
    /// output in the specified format. Different formats may support their own
    /// options which are provided via the extraOptionsJSON parameter.
    /// This can only be called after a successful load(), and before link().
    /// After calling this method, the engine may be reset to an unloaded state.
    CodeGenOutput generateCode (const std::string& targetType,
                                const std::string& extraOptionsJSON) const;

    /// Returns a list of the available code-gen targets that the generateCode()
    /// method will accept.
    std::vector<std::string> getAvailableCodeGenTargetTypes() const;

    //==============================================================================
    /// The underlying COM engine object that this helper object is wrapping.
    EnginePtr engine;

private:
    Library::SharedLibraryPtr library;
};



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

inline Engine::Engine (EnginePtr p) : engine (p), library (Library::getSharedLibraryPtr()) {}

inline Engine::~Engine()
{
    engine = {};   // explicitly release the engine before the library
    library = {};
}

inline Engine Engine::create (const std::string& engineType, const choc::value::Value* engineCreationOptions)
{
    std::string options;

    if (engineCreationOptions != nullptr && ! engineCreationOptions->isVoid())
        options = choc::json::toString (*engineCreationOptions);

    if (auto factory = EngineFactoryPtr (Library::createEngineFactory (engineType.c_str())))
        if (auto e = EnginePtr (factory->createEngine (options.c_str())))
            return Engine (e);

    return {};
}

inline std::vector<std::string> Engine::getAvailableEngineTypes()
{
    return choc::text::splitAtWhitespace (Library::getEngineTypes());
}

inline BuildSettings Engine::getBuildSettings() const
{
    auto json = choc::com::StringPtr (engine->getBuildSettings());
    return BuildSettings::fromJSON (json);
}

inline void Engine::setBuildSettings (const BuildSettings& newSettings)
{
    if (engine != nullptr)
        engine->setBuildSettings (newSettings.toJSON().c_str());
}

inline bool Engine::load (DiagnosticMessageList& messages, const Program& programToLoad,
                          ExternalVariableProviderFn getExternalVariable,
                          ExternalFunctionProviderFn getExternalFunction)
{
    struct ExternalResolver
    {
        EngineInterface& engine;
        ExternalVariableProviderFn getVariable;
        ExternalFunctionProviderFn getFunction;

        static void resolveVariable (void* context, const char* ext)
        {
            try
            {
                auto instance = static_cast<ExternalResolver*> (context);

                if (instance->getVariable)
                {
                    auto externalVariable = ExternalVariable::fromJSON (choc::json::parse (ext));

                    if (auto v = instance->getVariable (externalVariable); ! v.isVoid())
                    {
                        auto s = v.serialise();
                        instance->engine.setExternalVariable (externalVariable.name.c_str(), s.data.data(), s.data.size());
                    }
                }
            }
            catch (const std::exception&) {}
        }

        static void* resolveFunction (void* context, const char* functionName, const char* parameterTypes)
        {
            auto instance = static_cast<ExternalResolver*> (context);

            if (instance->getFunction)
            {
                std::vector<choc::value::Type> types;

                if (parseJSONTypeList (types, parameterTypes))
                    return instance->getFunction (functionName, types);
            }

            return {};
        }

        static bool parseJSONTypeList (std::vector<choc::value::Type>& result, std::string_view json)
        {
            try
            {
                auto paramTypes = choc::json::parse (json);

                if (paramTypes.isArray())
                {
                    std::vector<choc::value::Type> typeArray;

                    for (auto typeValue : paramTypes)
                    {
                        auto type = choc::value::Type::fromValue (typeValue);

                        if (type.isVoid())
                            return false;

                        result.push_back (std::move (type));
                    }

                    return true;
                }
            }
            catch (...) {}

            return false;
        }
    };

    // You need to create a valid Engine using Engine::create() before you can load things into it.
    if (engine == nullptr)
    {
        messages.add (DiagnosticMessage::createError ("missing engine", {}));
        return false;
    }

    ExternalResolver externalResolver { *engine, std::move (getExternalVariable), std::move (getExternalFunction) };

    if (auto result = choc::com::StringPtr (engine->load (programToLoad.program.get(),
                                                          std::addressof (externalResolver), ExternalResolver::resolveVariable,
                                                          std::addressof (externalResolver), ExternalResolver::resolveFunction)))
        return messages.addFromJSONString (result);

    return true;
}

inline void Engine::unload()
{
    if (engine != nullptr)
        engine->unload();
}

inline EndpointDetailsList Engine::getInputEndpoints() const
{
    auto details = getProgramDetails();

    if (details.isObject())
        return EndpointDetailsList::fromJSON (details["inputs"], true);

    return {};
}

inline EndpointDetailsList Engine::getOutputEndpoints() const
{
    auto details = getProgramDetails();

    if (details.isObject())
        return EndpointDetailsList::fromJSON (details["outputs"], false);

    return {};
}

inline EndpointHandle Engine::getEndpointHandle (const char* endpointID) const
{
    // This method is only valid on a loaded engine
    if (! isLoaded())
        return {};

    return engine->getEndpointHandle (endpointID);
}

inline choc::value::Value Engine::getProgramDetails() const
{
    // This method is only valid on a loaded engine
    if (! isLoaded())
        return {};

    if (engine != nullptr)
    {
        if (auto details = engine->getProgramDetails())
        {
            try
            {
                return choc::json::parse (choc::com::StringPtr (details));
            }
            catch (...) {}
        }
    }

    return {};
}

inline bool Engine::link (DiagnosticMessageList& messages, CacheDatabaseInterface* cache)
{
    // This method is only valid on a loaded but not-yet-linked engine
   if (! isLoaded() || isLinked())
   {
       messages.add (DiagnosticMessage::createError ("Program must be loaded but not linked", {}));
       return false;
   }

    if (auto result = choc::com::StringPtr (engine->link (cache)))
        messages.addFromJSONString (result);

    return ! messages.hasErrors();
}

inline Performer Engine::createPerformer()
{
    // This method is only valid on a fully-linked engine
    if (! isLinked())
        return {};

    if (auto perf = PerformerPtr (engine->createPerformer()))
        return Performer (perf);

    return {};
}

inline bool Engine::isLoaded() const    { return engine != nullptr && engine->isLoaded(); }
inline bool Engine::isLinked() const    { return engine != nullptr && engine->isLinked(); }

inline std::string Engine::getLastBuildLog() const
{
    if (engine != nullptr)
        if (auto result = choc::com::StringPtr (engine->getLastBuildLog()))
            return result;

    return {};
}

inline Engine::CodeGenOutput Engine::generateCode (const std::string& targetType, const std::string& options) const
{
    struct Callback
    {
        static void handleResult (void* context, const char* code, size_t codeSize, const char* mainClassName, const char* messages)
        {
            auto& o = *static_cast<CodeGenOutput*> (context);

            if (codeSize > 0)
                o.generatedCode = std::string (code, codeSize);

            if (messages != nullptr)
                o.messages = cmaj::DiagnosticMessageList::fromJSONString (messages);

            if (mainClassName != nullptr)
                o.mainClassName = mainClassName;
        }
    };

    CodeGenOutput output;
    engine->generateCode (targetType.c_str(), options.c_str(),
                          std::addressof (output), Callback::handleResult);
    return output;
}

inline std::vector<std::string> Engine::getAvailableCodeGenTargetTypes() const
{
    return choc::text::splitAtWhitespace (engine->getAvailableCodeGenTargetTypes());
}


} // namespace cmaj
