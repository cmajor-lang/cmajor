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

#include <filesystem>

#include "choc/javascript/choc_javascript.h"
#include "choc/containers/choc_COM.h"
#include "choc/audio/choc_SampleBufferUtilities.h"
#include "choc/audio/choc_SincInterpolator.h"

#include "../../../include/cmajor/API/cmaj_DiagnosticMessages.h"
#include "../../../include/cmajor/API/cmaj_ExternalVariables.h"
#include "../../playback/include/cmaj_AudioFileUtils.h"

namespace cmaj::javascript
{

static inline choc::value::Value createErrorObject (std::string_view message)
{
    return choc::value::createObject ("Error",
                                      "name", "Error",
                                      "message", message);
}

static inline choc::value::Value createErrorObject (const cmaj::DiagnosticMessageList& messageList)
{
    return messageList.toJSON();
}

template <typename Function>
static choc::value::Value catchAllErrors (bool ignoreWarnings, Function&& f)
{
    cmaj::DiagnosticMessageList messageList;

    try
    {
        f();
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

    if (messageList.hasErrors() || (! ignoreWarnings && messageList.hasWarnings()))
        return createErrorObject (messageList);

    return {};
}

} // namespace cmaj::javascript
