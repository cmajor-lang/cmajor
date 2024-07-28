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

def hexDigitToInt (digit):
    if digit >= ord('0') and digit <= ord('9'): return digit - ord('0')
    if digit >= ord('a') and digit <= ord('f'): return digit - ord('a')
    if digit >= ord('A') and digit <= ord('F'): return digit - ord('A')
    return -1

def escapeCppStringLiteral (text):
    avoidHexDigit = False
    lastChar = 0
    result = ""

    for c in text.encode('utf-8'):
        if   c == ord('\t'): result += "\\t"
        elif c == ord('\n'): result += "\\n"
        elif c == ord('\r'): result += "\\r"
        elif c == ord('\"'): result += "\\\""
        elif c == ord('\\'): result += "\\\\"

        elif c == ord('?'):
            # NB: two consecutive unescaped question-marks would form a trigraph
            result += "\\?" if lastChar == ord('?') else "?"

        elif c >= ord(' ') and c < 127:
            if avoidHexDigit and hexDigitToInt (c) >= 0:
                result += "\"\""

            result += chr(c)
        else:
            result += f"\\{c:o}"
            avoidHexDigit = True
            lastChar = c
            continue

        avoidHexDigit = False
        lastChar = c

    return result

def createCppStringLiteral (text, indent):
    result = ""

    if text == "":
        result = '""'
    else:
        line = ""

        for c in text:
            line += c

            if c == '\n' or len (line) >= maxStringLiteralSize:
                result += indent + '"' + escapeCppStringLiteral (line) + '"\n'
                line = ""

        if line != "":
            result += indent + '"' + escapeCppStringLiteral (line) + '"\n'

    return result.strip()

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
            result += "    static constexpr const char* " + identifier + " = " + createCppStringLiteral (content.decode('utf-8'), "        ") + ";\n"
        else:
            result += "    static constexpr const char " + identifier + "[] = {\n        " + createCppDataLiteralFromData (content, True) + " };\n"

        asString = "std::string_view (" + identifier + ", " + str (len (content)) + ")"

        fileList += "        File { " + createCppStringLiteral (name, "            ") + ", " + asString + " }"

    return result + "\n" + fileList + "\n    };\n"
