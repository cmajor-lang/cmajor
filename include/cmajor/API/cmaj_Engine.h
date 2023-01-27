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

#include "cmaj_Performer.h"

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
    ~Engine() = default;

    Engine (EnginePtr p) : engine (p) {}

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
    /// Attempts to load a program into this engine.
    /// Returns true if it succeeds, and adds any errors or messages to the list provided.
    bool load (DiagnosticMessageList& messages, const Program& programToLoad);

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

    //==============================================================================
    /// Returns a list describing all the external variables in the loaded program.
    /// This may be called after successfully loading a program, at which point all these
    /// variables must be given a value with setExternalVariable() before the program can be linked.
    ExternalVariableList getExternalVariables() const;

    /// Sets the value of an external variable.
    /// This may be called after successfully loading a program, and before linking.
    /// If the type of object provided doesn't fit, the engine may return true here but
    /// emit an error about the problem later on during the linking process. If there's no
    /// such variable or other problems, then you can expect this method to return false.
    bool setExternalVariable (const char* name, const choc::value::ValueView& value);

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

inline bool Engine::load (DiagnosticMessageList& messages, const Program& programToLoad)
{
    // You need to create a valid Engine using Engine::create() before you can load things into it.
    if (engine == nullptr)
    {
        messages.add (DiagnosticMessage::createError ("missing engine", {}));
        return false;
    }

    if (auto result = choc::com::StringPtr (engine->load (programToLoad.program.get())))
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

inline ExternalVariableList Engine::getExternalVariables() const
{
    // This method is only valid on a loaded but not-yet-linked engine
    if (! isLoaded() || isLinked())
        return {};

    auto details = getProgramDetails();

    if (details.isObject())
        return ExternalVariableList::fromJSON (details["externals"]);

    return {};
}

inline bool Engine::setExternalVariable (const char* name, const choc::value::ValueView& value)
{
    // This method is only valid on a loaded but not-yet-linked engine
   if (! isLoaded() || isLinked())
       return false;

    struct Serialiser
    {
        void write (const void* d, size_t num)
        {
            data.insert (data.end(), static_cast<const char*> (d), static_cast<const char*> (d) + num);
        }

        std::vector<uint8_t> data;
    };

    Serialiser s;
    s.data.reserve (2048);
    value.serialise (s);
    return engine->setExternalVariable (name, s.data.data(), s.data.size());
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
        return messages.addFromJSONString (result);

    return true;
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

inline Engine::CodeGenOutput Engine::generateCode (const std::string& targetType, const std::string& options) const
{
    struct Callback
    {
        static void handleResult (void* context, const char* code, const char* mainClassName, const char* messages)
        {
            auto& o = *static_cast<CodeGenOutput*> (context);

            if (code != nullptr)
                o.generatedCode = code;

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
