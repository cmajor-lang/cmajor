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

#include <string>
#include "cmaj_DefaultFlags.h"

#ifndef CMAJOR_DLL
 // When building the compiler library source directly, must set a CMAJOR_DLL=0 globally
 #error
#endif

//==============================================================================
#define CMAJ_ASSERT(x)        do { if (! (x)) cmaj::fatalError (__FUNCTION__, __LINE__); } while (false)
#define CMAJ_ASSERT_FALSE     cmaj::fatalError (__FUNCTION__, __LINE__)

#undef CHOC_ASSERT
#define CHOC_ASSERT(x) CMAJ_ASSERT(x)

namespace cmaj
{
    /// Throws an internal compiler exception
    [[noreturn]] void fatalError (const std::string& description);

    /// Throws an internal compiler exception
    [[noreturn]] void fatalError (const char* location, int line);
}

#include "../../../include/cmajor/API/cmaj_DiagnosticMessages.h"

namespace cmaj
{

//==============================================================================
/// Creating one of these on the stack will catch any errors that are
/// thrown by the current thread.
struct DiagnosticMessageHandler
{
    DiagnosticMessageHandler();
    DiagnosticMessageHandler (DiagnosticMessageList& listToAddMessagesTo);
    ~DiagnosticMessageHandler();

    static bool currentThreadHasMessageHandler();

    void handleMessages (const DiagnosticMessageList&);

    /// Thrown if the handler doesn't have a list to add the messages to
    struct IgnoredErrorException {};

    DiagnosticMessageList* listToAddMessagesTo = nullptr;
    DiagnosticMessageHandler* const previousHandler;
};

//==============================================================================
/// Sends an error or warning message to the current message handler.
void emitMessage (DiagnosticMessage);
/// Sends a set of error or warning messages to the current message handler.
void emitMessage (const DiagnosticMessageList&);

//==============================================================================
struct AbortCompilationException {};

/// Throws an AbortCompilationException to get out of any current parsing operation
[[noreturn]] void abortCompilation();

/// Emits an error message and calls abortCompilation()
[[noreturn]] void throwError (DiagnosticMessage);
/// Emits a group of messages and calls abortCompilation()
[[noreturn]] void throwError (const DiagnosticMessageList&);

template <typename ObjectOrContext>
[[noreturn]] static void throwError (const ObjectOrContext& errorContext, DiagnosticMessage, bool isStaticAssertion = false);

//==============================================================================
template <typename Function>
void catchAllErrors (DiagnosticMessageList& messageList, Function&& fn)
{
    DiagnosticMessageHandler handler (messageList);

    try
    {
        fn();
    }
    catch (AbortCompilationException)
    {
    }
    catch (const std::exception& e)
    {
        messageList.add (DiagnosticMessage::createError (e.what(), {}));
    }
    catch (...)
    {
        if (messageList.empty())
            messageList.add (DiagnosticMessage::createError ("Unknown internal error", {}));
    }
}


} // namespace cmaj
