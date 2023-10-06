#
#     ,ad888ba,                              88
#    d8"'    "8b
#   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
#   Y8,           88    88    88  88     88  88
#    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
#     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
#                                           ,88
#                                        888P"
#
#  The Cmajor project is subject to commercial or open-source licensing.
#  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
#  visit https://cmajor.dev to learn about our commercial licence options.
#
#  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
#  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
#  DISCLAIMED.

import os.path
import struct

# MSVC struggles with longer literals than this
maxStringLiteralSize = 2040

def createCppStringLiteral (text, guard = ""):

    if len (text) > maxStringLiteralSize:
        split = getSplitPoint (text)
        return createCppStringLiteral (text[:split], guard) + "\n" + createCppStringLiteral (text[split:], guard)

    isAscii = True

    for c in text:
        if not isNonEscapedAsciiStringChar (c):
            isAscii = False
            break

    if isAscii:
        return "\"" + text + "\""

    i = 0
    originalGuard = guard

    while text.find (")" + guard + "\"") >= 0:
        if originalGuard == "":
            originalGuard = "TEXT"
            guard = originalGuard
        else:
            i = i + 1
            guard = originalGuard + str (i)

    return "R\"" + guard + "(" + text + ")" + guard + "\""


def createCppDataLiteralFromFile (file, indent = "    "):
    with open (file, "rb") as f:
        return createCppDataLiteralFromData (f.read(), False, indent)


def createCppDataLiteralFromData (data, isSigned, indent = "    "):
    result = ""
    currentLine = ""
    firstInLine = True

    for byte in struct.iter_unpack ("b" if isSigned else "B", data):
        if firstInLine:
            firstInLine = False
        elif len (currentLine) > 200:
            result += currentLine + ",\n"
            currentLine = indent
        else:
            currentLine += ","

        if isSigned and byte[0] < 0:
            currentLine += "(char)"

        currentLine += str (byte[0])

    return result + currentLine


def replaceFileIfDifferent (file, newContent):
    oldContent = ""

    if (os.path.exists (file)):
        with open (file, 'r') as f:
            oldContent = f.read()

    if (oldContent != newContent):
        print ("Updating " + file)

        with open (file, "w") as f:
            f.write (newContent)
    else:
        print ("Skipping unchanged file: " + file)


def getSplitPoint (text):
    pos = text[:maxStringLiteralSize].rfind ('\n\n')

    if pos > maxStringLiteralSize / 2:
        return pos

    pos = text[:maxStringLiteralSize].rfind ('\n')

    if pos > maxStringLiteralSize / 2:
        return pos

    return maxStringLiteralSize


def isDigit (c):
    return (c >= '0' and c <= '9')

def isLetterOrDigit (c):
    return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z') or isDigit (c)

def isSafeIdentifierChar (c):
    return isLetterOrDigit (c) or c == '_'

def isNonEscapedAsciiStringChar (c):
    return isLetterOrDigit (c) or " _,./?[]{}+-&()*^%$!@'<>:;".find (c) >= 0

def makeSafeIdentifier (s):
    result = ""

    for c in s:
        if " ,./;:".find (c) >= 0:
            result += "_"
        elif isSafeIdentifierChar (c):
            result += c

    if len (s) > 0 and (s[0] >= '0' and s[0] <= '9'):
        return "_" + result

    return result

def isValidUTF8 (data):
    try:
        data.decode('utf-8')
    except UnicodeError:
        return False

    return True


def createCppFileData (files):
    result = ""
    fileList = "\n    static constexpr std::array files =\n    {\n"
    first = True

    for f in files:
        name = f[0]
        identifier = makeSafeIdentifier (name)
        content = f[1]

        if first:
            first = False
        else:
            fileList += ",\n"

        if (len (content) < 65536 and content.find (0) < 0 and isValidUTF8 (content)):
            result += "    static constexpr const char* " + identifier + " =\n        " + createCppStringLiteral (content.decode('utf-8')) + ";\n"
        else:
            result += "    static constexpr const char " + identifier + "[] = {\n        " + createCppDataLiteralFromData (content, True) + " };\n"

        asString = "std::string_view (" + identifier + ", " + str (len (content)) + ")"

        fileList += "        File { " + createCppStringLiteral (name) + ", " + asString + " }"

    return result + "\n" + fileList + "\n    };\n"
