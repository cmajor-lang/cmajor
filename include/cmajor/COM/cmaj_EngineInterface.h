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

#include "cmaj_PerformerInterface.h"
#include "cmaj_CacheDatabaseInterface.h"


namespace cmaj
{

//==============================================================================
/** This is the basic COM API class for an instance of an engine.

    The job of an engine is to compile and link a program with some settings, and
    to produce PerformerInstance objects which can then be used to render it.

    Note that the cmaj::Engine helper class provides a much nicer-to-use wrapper
    around this class, to avoid you needing to deal with all the COM nastiness!

    EngineInterface objects can be created by EngineFactoryInterface, but an
    easier way to get one is to use the helper function cmaj::Engine::create().
*/
struct EngineInterface   : public choc::com::Object
{
    EngineInterface() = default;

    //==============================================================================
    /// Returns a JSON string which can be parsed into a BuildSettings object
    [[nodiscard]] virtual choc::com::String* getBuildSettings() = 0;

    /// Applies some new build settings for the engine to use.
    /// Takes a JSON string which was created by a BuildSettings object.
    /// The engine maintains a current copy of its settings, and they're used by
    /// various functions like load(), link() and createPerformer() as required.
    virtual void setBuildSettings (const char*) = 0;

    //==============================================================================
    /// Attempts to load a program, returning either a nullptr or a JSON error string
    /// which can be parsed into something more useful with DiagnosticMessageList::fromJSONString()
    [[nodiscard]] virtual choc::com::String* load (ProgramInterface*) = 0;

    /// Unloads the current program and completely resets the state of the engine.
    virtual void unload() = 0;

    //==============================================================================
    /// If a program has been successfully loaded, this returns a JSON object with
    /// information about its properties.
    /// This may be called after successfully loading a program.
    [[nodiscard]] virtual choc::com::String* getProgramDetails() = 0;

    /// Returns a handle which can be used to communicate with an input or output endpoint.
    /// This may be called after successfully loading a program, and before linking has happened.
    /// If the ID isn't found, this will return an invalid handle.
    virtual EndpointHandle getEndpointHandle (const char* endpointID) = 0;

    //==============================================================================
    /// Sets the value of an external variable.
    /// This may be called after successfully loading a program, and before linking.
    /// If the type of object provided doesn't fit, the engine may return true here but
    /// emit an error about the problem later on during the linking process. If there's no
    /// such variable or other problems, then you can expect this method to return false.
    virtual bool setExternalVariable (const char* name,
                                      const void* serialisedValueData,
                                      size_t serialisedValueDataSize) = 0;

    //==============================================================================
    /// Attempts to link the currently-loaded program into a state that can be executed.
    /// After loading and before linking, the caller must:
    ///  - Resolve any external variables
    ///  - Connect any endpoints that it needs to send or receive from
    ///
    /// On failure, the errors are returned as a JSON string which can be parsed with
    /// DiagnosticMessageList::fromJSONString()
    ///
    /// If all goes well, this returns nullptr, after which the caller can call
    /// createPerformer() to create performer instances to use for rendering.
    ///
    /// If a non-null CacheDatabaseInterface object is supplied, it may be used to save
    /// and restore previously-compiled binary and skip the need to link.
    [[nodiscard]] virtual choc::com::String* link (CacheDatabaseInterface*) = 0;

    /// When a program has been successfully linked, calling this will return a new
    /// instance of a PerformerInterface which can be used to render the program.
    /// You can create multiple performer instances and they will each have their own
    /// independent state.
    /// If the engine isn't linked or in a state where a valid performer can be
    /// created, this will just return nullptr.
    [[nodiscard]] virtual PerformerInterface* createPerformer() = 0;

    //==============================================================================
    /// Returns true if a program has been successfully loaded, but not yet linked.
    virtual bool isLoaded() = 0;

    /// Returns true if a program has been successfully linked and can be run.
    virtual bool isLinked() = 0;

    //==============================================================================
    using HandleCodeGenOutput = void(*)(void* context,
                                        const char* generatedCode,
                                        size_t generatedCodeSize,
                                        const char* mainClassName,
                                        const char* messageListJSON);

    /// Attempts to generate some code from the currently-loaded program, producing
    /// output in the specified format.
    /// This can only be called after a successful load(), and before link().
    /// After calling this method, the engine may be reset to an unloaded state.
    virtual void generateCode (const char* targetType,
                               const char* options,
                               void* callbackContext,
                               HandleCodeGenOutput) = 0;

    /// Returns a space-separated list of available code-gen targets
    virtual const char* getAvailableCodeGenTargetTypes() = 0;
};

using EnginePtr = choc::com::Ptr<EngineInterface>;


} // namespace cmaj
