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

#include <sstream>
#include "../../include/cmaj_ErrorHandling.h"

namespace cmaj
{

//==============================================================================
static thread_local DiagnosticMessageHandler* activeHandler = nullptr;

DiagnosticMessageHandler::DiagnosticMessageHandler()
   : previousHandler (activeHandler)
{
    activeHandler = this;
}

DiagnosticMessageHandler::DiagnosticMessageHandler (DiagnosticMessageList& list)
   : listToAddMessagesTo (std::addressof (list)), previousHandler (activeHandler)
{
    activeHandler = this;
}

DiagnosticMessageHandler::~DiagnosticMessageHandler()
{
    activeHandler = previousHandler;
}

void DiagnosticMessageHandler::handleMessages (const DiagnosticMessageList& list)
{
    if (listToAddMessagesTo != nullptr)
    {
        listToAddMessagesTo->add (list);
    }
    else
    {
        if (! list.hasInternalCompilerErrors())
            throw IgnoredErrorException();

        if (previousHandler != nullptr)
            previousHandler->handleMessages (list);
    }
}

bool DiagnosticMessageHandler::currentThreadHasMessageHandler()
{
    return activeHandler != nullptr;
}

//==============================================================================
void emitMessage (const DiagnosticMessageList& list)
{
    if (activeHandler != nullptr)
        activeHandler->handleMessages (list);
}

void emitMessage (DiagnosticMessage m)
{
    DiagnosticMessageList list;
    list.messages.push_back (std::move (m));
    emitMessage (list);
}

[[noreturn]] void abortCompilation()
{
    throw AbortCompilationException();
}

[[noreturn]] void throwError (const DiagnosticMessageList& list)
{
    emitMessage (list);
    abortCompilation();
}

[[noreturn]] void throwError (DiagnosticMessage m)
{
    DiagnosticMessageList list;
    list.messages.push_back (std::move (m));
    throwError (list);
}

[[noreturn]] void fatalError (const std::string& description)
{
    throwError (DiagnosticMessage::create ("Internal compiler error: " + choc::text::addDoubleQuotes (description),
                                           {},
                                           DiagnosticMessage::Type::internalCompilerError,
                                           DiagnosticMessage::Category::none));
}

[[noreturn]] void fatalError (const char* location, int line)
{
    fatalError (std::string (location) + ":" + std::to_string (line));
}

} // namespace cmaj
