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

#if CMAJ_ENABLE_CODEGEN_CPP || CMAJ_ENABLE_PERFORMER_CPP

#include "../../../include/cmaj_ErrorHandling.h"
#include "choc/text/choc_TextTable.h"

#include "../../../../include/cmajor/COM/cmaj_EngineFactoryInterface.h"
#include "../../codegen/cmaj_CodeGenerator.h"
#include "../../transformations/cmaj_Transformations.h"

#include "cmaj_CPlusPlus.h"
#include "../../../include/cmaj_CppGenerationUtils.h"

namespace cmaj::cplusplus
{

//==============================================================================
struct CPlusPlusCodeGenerator
{
    CPlusPlusCodeGenerator (const AST::Program& p, std::string_view opts,
                            double maxFreq, uint32_t maxFrames, uint32_t eventBuffer,
                            const std::function<EndpointHandle(const EndpointID&)> h)
        : program (p), mainProcessor (program.getMainProcessor()),
          maxFrequency (maxFreq), maxNumFramesPerBlock (maxFrames), eventBufferSize (eventBuffer),
          getEndpointHandleFn (std::move (h))
    {
        try
        {
            options = choc::json::parse (opts);
        }
        catch (...)
        {}

        if (! options.isObject())
            options = choc::value::createObject({});
    }

    ~CPlusPlusCodeGenerator() = default;

    bool generate()
    {
        mainClassName = options["classname"].toString();

        if (mainClassName.empty())
            mainClassName = makeSafeIdentifier (mainProcessor.name.get());

        return generateFromLinkedProgram();
    }

    GeneratedCPP getResult()
    {
        GeneratedCPP result;
        result.code = out.toString();
        result.mainClassName = mainClassName;
        return result;
    }

    //==============================================================================
    const AST::Program& program;
    choc::value::Value options;
    AST::ProcessorBase& mainProcessor;
    double maxFrequency;
    const uint32_t maxNumFramesPerBlock, eventBufferSize;
    std::function<EndpointHandle(const EndpointID&)> getEndpointHandleFn;

    std::string mainClassName;

    choc::text::CodePrinter out, functionOut, functionLocalVariables;
    choc::memory::Pool pool;
    choc::value::SimpleStringDictionary stringDictionary;
    std::vector<std::string> stringHandleMatchers;
    std::unordered_set<uint32_t> stringHandlesDone;
    std::unordered_map<std::string, std::string> upcastFunctions;

    using Builder = CodeGenerator<CPlusPlusCodeGenerator>;
    ptr<Builder> codeGenerator;

    ptr<const AST::LoopStatement> currentLoop;
    size_t functionBlockIndentDepth = 0,
           breakLabelIndex = 0,
           tempVariableIndex = 0,
           branchIndex = 0,
           numActiveForwardBranches = 0,
           constantAggregateIndex = 0;

    std::vector<std::string> globalConstants;

    DuckTypedStructMappings<std::string, true> structTypeNames;
    std::vector<std::string> emittedStructNames;

    EndpointHandle getEndpointHandle (const AST::EndpointDeclaration& e)
    {
        return getEndpointHandleFn (e.getEndpointID());
    }

    //==============================================================================
    bool generateFromLinkedProgram()
    {
        if (! options["bare"].getWithDefault<bool> (false))
            out << choc::text::replace (getFileHeaderBoilerplate(), "NAME", mainProcessor.getFullyQualifiedReadableName());

        printMainClass (makeSafeIdentifier (options["namespace"].toString()));
        return true;
    }

    void printMainClass (const std::string& outerNamespace)
    {
        if (! outerNamespace.empty())
        {
            {
                out << "namespace " << outerNamespace << newLine;
                auto indent = out.createIndentWithBraces();
                printMainClass();
            }

            out << " // namespace " << outerNamespace << blankLine;
        }
        else
        {
            printMainClass();
        }
    }

    void printMainClass()
    {
        out << "struct " << mainClassName << newLine;

        {
            auto indent = out.createIndentWithBraces();
            out << mainClassName << "() = default;" << newLine
                << "~" << mainClassName << "() = default;" << blankLine;

            printProcessorDescription();

            out << sectionBreak;
            printEndpointProperties();

            out << sectionBreak
                << choc::text::trim (getHelperClassDefinitions())
                << sectionBreak
                << choc::text::trim (getWarningDisableFlags())
                << sectionBreak;

            Builder codeGen (*this, mainProcessor);
            codeGenerator = codeGen;

            codeGen.emitTypes();
            out << sectionBreak;
            printPublicMethods();
            out << sectionBreak;
            codeGen.emitFunctions();
            out << sectionBreak;
            printGetStringForHandleFunction();
            out << sectionBreak;
            printUpcastFunctions();
            out << sectionBreak;
            codeGen.emitGlobals();
            out << sectionBreak;
            printGlobalConstants();

            out << sectionBreak
                << choc::text::trim (getIntrinsicFunctions())
                << sectionBreak
                << choc::text::trim (getWarningReenableFlags())
                << newLine;

            codeGenerator = {};
        }

        out << ";" << blankLine;
    }

    void printProcessorDescription()
    {
        out << "static constexpr std::string_view name = "
            << cpp_utils::createStringLiteral (mainProcessor.name.toString()) << ";" << newLine
            << blankLine;
    }

    struct TypeNameList : public AST::UniqueNameList<AST::Object, TypeNameList>
    {
        std::string getRootName (const AST::Object& t)
        {
            return AST::SignatureBuilder::removeDuplicateUnderscores (makeSafeIdentifier (t.getFullyQualifiedNameWithoutRoot()));
        }
    };

    TypeNameList typeNames;

    std::string getTypeName (const AST::Object& t, bool ignoreConst)
    {
        if (auto alias = t.getAsAlias())
            return typeNames.getName (*alias);

        auto& type = AST::castToTypeBaseRef (t);

        if (auto str = type.getAsStructType())
        {
            return structTypeNames.getOrCreate (*str, [this] (const AST::StructType& s)
            {
                return typeNames.getName (s);
            });
        }

        if (auto p = type.getAsPrimitiveType())
        {
            if (p->isVoid())                 return "void";
            if (p->isPrimitiveBool())        return "bool";
            if (p->isPrimitiveInt32())       return "int32_t";
            if (p->isPrimitiveInt64())       return "int64_t";
            if (p->isPrimitiveFloat32())     return "float";
            if (p->isPrimitiveFloat64())     return "double";
            if (p->isPrimitiveString())      return "StringHandle";
            if (p->isPrimitiveComplex32())   { CMAJ_ASSERT_FALSE; return {}; } // should have been removed
            if (p->isPrimitiveComplex64())   { CMAJ_ASSERT_FALSE; return {}; } // should have been removed
        }

        if (auto r = type.getAsMakeConstOrRef())
        {
            auto result = getTypeName (r->getSourceRef(), true);

            if (r->isConst() && ! ignoreConst)    result = "const " + result;
            if (r->isReference())                 result += "&";

            return result;
        }

        if (auto a = type.getAsArrayType())
        {
            auto elementType = getTypeName (a->getInnermostElementTypeRef(), ignoreConst);

            if (a->isFixedSizeArray())
                return "Array<" + elementType + ", " + std::to_string (a->resolveSize()) + ">";

            if (a->isSlice())
                return "Slice<" + elementType + ">";
        }

        if (auto v = type.getAsVectorType())
            return "Vector<" + getTypeName (v->getElementType(), true) + ", " + std::to_string (v->resolveSize()) + ">";

        if (type.getAsEnumType())
            return "int32_t";

        CMAJ_ASSERT_FALSE;
        return {};
    }

    //==============================================================================
    void printPublicMethods()
    {
        printMaxFrequencyFunction();
        printInitialiseFunction();
        printResetFunction();
        printAdvanceFunction();
        printGetEndpointAddressesFunction();
        printIterateOutputEventsFunctions();
        printIncomingEventHandlerFunctions();
        printIncomingValueHandlerFunctions();
        printIncomingStreamHandlerFunctions();

        std::string properties = R"CPPGEN(
// Rendering state values
int32_t initSessionID;
double initFrequency;
STATE_STRUCT state = {};
IO_STRUCT io = {};
)CPPGEN";

        out << sectionBreak
            << choc::text::replace (choc::text::trim (properties),
                                    "STATE_STRUCT", getTypeName (*mainProcessor.findStruct (mainProcessor.getStrings().stateStructName), false),
                                    "IO_STRUCT",    getTypeName (*mainProcessor.findStruct (mainProcessor.getStrings().ioStructName), false))
            << blankLine;
    }

    void printMaxFrequencyFunction()
    {
        out << "double getMaxFrequency() const" << newLine;

        {
            auto indent = out.createIndentWithBraces();

            out << "return " << maxFrequency << ";" << newLine;
        }

        out << blankLine;
    }

    void printInitialiseFunction()
    {
        out << "void initialise (int32_t sessionID, double frequency)" << newLine;

        {
            auto indent = out.createIndentWithBraces();

            out << "assert (frequency <= getMaxFrequency());" << newLine;
            out << "initSessionID = sessionID;" << newLine;
            out << "initFrequency = frequency;" << newLine;
            out << "reset();" << newLine;
        }

        out << blankLine;
    }

    void printResetFunction()
    {
        out << "void reset()" << newLine;

        {
            auto indent = out.createIndentWithBraces();

            if (auto f = mainProcessor.findSystemInitFunction())
            {
                out << "std::memset (&state, 0, sizeof (state));" << newLine;
                out << "int32_t processorID = 0;" << newLine;
                out << codeGenerator->getFunctionName (*f) + " (state, processorID, initSessionID, initFrequency);" << newLine;
            }
        }

        out << blankLine;
    }

    void printAdvanceFunction()
    {
        bool isSingleFrame = false;

        if (auto f = mainProcessor.findMainFunction())
        {
            isSingleFrame = true;
            out << "void advanceOneFrame() { " << codeGenerator->getFunctionName (*f) << " (state, io); }" << blankLine;
        }

        out << "void advance (int32_t frames)" << newLine;
        {
            auto indent = out.createIndentWithBraces();

            for (auto& output : mainProcessor.getOutputEndpoints (true))
            {
                if (output->isStream())
                {
                    out << "io." << StreamUtilities::getEndpointStateMemberName (output);

                    if (isSingleFrame)
                        out << " = Null();" << newLine;
                    else
                        out << ".clear (static_cast<SizeType> (frames));" << newLine;
                }
            }

            if (auto f = mainProcessor.findSystemAdvanceFunction())
                out << codeGenerator->getFunctionName (*f) << " (state, io, frames);" << newLine;
            else
                out << "assert (frames == 1); advanceOneFrame();" << newLine;
        }

        out << blankLine;
    }

    void printGetEndpointAddressesFunction()
    {
        out << "void copyOutputValue (EndpointHandle endpointHandle, void* dest)" << newLine;

        {
            auto indent = out.createIndentWithBraces();
            bool anyDone = false;

            for (auto& e : mainProcessor.getAllEndpoints())
            {
                if (e->isOutput() && e->isValue())
                {
                    anyDone = true;
                    auto name = "state." + StreamUtilities::getEndpointStateMemberName (e);
                    auto details = AST::createEndpointDetails (e);
                    auto size = std::to_string (details.dataTypes.front().getValueDataSize());

                    out << "if (endpointHandle == " << std::to_string (getEndpointHandle (e))
                        << ") { std::memcpy (dest, std::addressof (" << name << "), " << size << "); return; }" << newLine;
                }
            }

            if (! anyDone)
                out << "(void) endpointHandle; (void) dest;" << blankLine;

            out << "assert (false);" << newLine;
        }

        out << blankLine;

        out << "void copyOutputFrames (EndpointHandle endpointHandle, void* dest, uint32_t numFramesToCopy)" << newLine;
        {
            auto indent = out.createIndentWithBraces();
            bool anyDone = false;

            for (auto& e : mainProcessor.getAllEndpoints())
            {
                if (e->isOutput() && e->isStream())
                {
                    anyDone = true;
                    auto name = "io." + StreamUtilities::getEndpointStateMemberName (e);
                    auto details = AST::createEndpointDetails (e);
                    auto size = std::to_string (details.dataTypes.front().getValueDataSize()) + " * numFramesToCopy";

                    out << "if (endpointHandle == " << std::to_string (getEndpointHandle (e))
                        << ") { std::memcpy (dest, std::addressof (" << name << "), " << size
                        << "); std::memset (std::addressof (" << name << "), 0, " << size << "); return; }" << newLine;
                }
            }

            if (! anyDone)
                out << "(void) endpointHandle; (void) dest; (void) numFramesToCopy;" << blankLine;

            out << "assert (false);" << newLine;
        }

        out << blankLine;
    }

    void printIterateOutputEventsFunctions()
    {
        AST::ObjectRefVector<const AST::EndpointDeclaration> eventOutputs;

        for (auto& output : mainProcessor.getOutputEndpoints (true))
            if (output->isEvent())
                eventOutputs.push_back (output);

        out << blankLine;

        out << "uint32_t getNumOutputEvents (EndpointHandle endpointHandle)" << newLine;
        {
            auto indent = out.createIndentWithBraces();

            for (auto& output : eventOutputs)
                out << "if (endpointHandle == " << getEndpointHandle (output)
                    << ") return state." << EventHandlerUtilities::getEventCountStateMemberName (output) << ";" << newLine;

            if (eventOutputs.empty())
                out << "(void) endpointHandle;" << blankLine;

            out << "assert (false); return {};" << newLine;
        }

        out << blankLine;

        out << "void resetOutputEventCount (EndpointHandle endpointHandle)" << newLine;
        {
            auto indent = out.createIndentWithBraces();

            for (auto& output : eventOutputs)
                out << "if (endpointHandle == " << getEndpointHandle (output)
                    << ") { state." << EventHandlerUtilities::getEventCountStateMemberName (output) << " = 0; return; }" << newLine;

            if (eventOutputs.empty())
                out << "(void) endpointHandle;" << blankLine;
        }

        out << blankLine;

        out << "uint32_t getOutputEventType (EndpointHandle endpointHandle, uint32_t index)" << newLine;
        {
            auto indent = out.createIndentWithBraces();

            for (auto& output : eventOutputs)
                out << "if (endpointHandle == " << getEndpointHandle (output)
                    << ") return state." << output->getName().get() << "[index].type;" << newLine;

            if (eventOutputs.empty())
                out << "(void) endpointHandle; (void) index;" << blankLine;

            out << "assert (false); return {};" << newLine;
        }

        out << blankLine;

        out << "static uint32_t getOutputEventDataSize (EndpointHandle endpointHandle, uint32_t typeIndex)" << newLine;
        {
            auto indent = out.createIndentWithBraces();
            out << "(void) endpointHandle; (void) typeIndex;" << blankLine;

            for (auto& output : eventOutputs)
            {
                auto details = AST::createEndpointDetails (output);
                uint32_t typeIndex = 0;

                for (auto& dataType : details.dataTypes)
                    out << "if (endpointHandle == " << getEndpointHandle (output) << " && typeIndex == " << typeIndex++
                        << ") return " << static_cast<uint32_t> (dataType.getValueDataSize()) << ";" << newLine;
            }

            out << "assert (false); return 0;" << newLine;
        }

        out << blankLine;

        out << "uint32_t readOutputEvent (EndpointHandle endpointHandle, uint32_t index, unsigned char* dest)" << newLine;
        {
            auto indent1 = out.createIndentWithBraces();

            for (auto& output : eventOutputs)
            {
                auto dataTypes = output->dataTypes.getAsObjectTypeList<AST::TypeBase>();
                auto details = AST::createEndpointDetails (output);

                out << "if (endpointHandle == " << getEndpointHandle (output) << ")" << newLine;

                {
                    auto indent2 = out.createIndentWithBraces();
                    out << "auto& event = state." << output->getName().get() << "[index];" << newLine;

                    for (size_t type = 0; type < dataTypes.size(); ++type)
                    {
                        out << "if (event.type == " << type << ")" << newLine;

                        {
                            auto indent3 = out.createIndentWithBraces();

                            if (details.dataTypes[type].getValueDataSize() > 0)
                                printPackValue ("dest", "event.value_" + std::to_string (type), details.dataTypes[type]);

                            out << "return event.frame;" << newLine;
                        }

                        out << newLine;
                    }

                    out << newLine;
                }

                out << newLine;
            }

            out << newLine;

            if (eventOutputs.empty())
                out << "(void) endpointHandle; (void) index; (void) dest;" << blankLine;

            out << "assert (false);" << newLine << "return {};" << newLine;
        }

        out << blankLine;
    }

    void printPackValue (const std::string& dest, const std::string& source, const choc::value::Type& type)
    {
        if (packedSizeMatchesNativeSize (type))
        {
            auto valueSize = type.getValueDataSize();
            out << "memcpy (" << dest << ", std::addressof (" << source << "), " << valueSize << ");" << newLine;
            out << "dest += " << valueSize << ";" << newLine;
            return;
        }

        if (type.isBool())
        {
            out << "int32_t t = " << source << ";" << newLine;
            printPackValue (dest, "t", choc::value::Type::createInt32());
            return;
        }

        if (type.isArray() || type.isVector())
        {
            std::string loopVar = "i" + std::to_string (out.getTotalIndent());
            out << "for (int " << loopVar << " = 0; " << loopVar << " < " << type.getNumElements() << "; " << loopVar << "++)" << newLine;

            {
                auto indent = out.createIndentWithBraces();
                printPackValue (dest, source + "[" + loopVar + "]", type.getElementType());
            }
            out << newLine;
            return;
        }

        if (type.isObject())
        {
            for (uint32_t i=0; i < type.getNumElements(); i++)
            {
                auto member = type.getObjectMember (i);
                printPackValue (dest, source + "." + std::string (member.name), member.type);
            }
            return;
        }
    }

    void printUnpackValue (const std::string& dest, const std::string& source, const choc::value::Type& type)
    {
        if (packedSizeMatchesNativeSize (type))
        {
            auto valueSize = type.getValueDataSize();
            out << "memcpy (&" << dest << ", " << source << ", " << valueSize << ");" << newLine;
            out << source << " += " << valueSize << ";" << newLine;
            return;
        }

        if (type.isBool())
        {
            out << dest << " = *(bool *) " << source << ";" << newLine;
            out << source << " += 4;" << newLine;
            return;
        }

        if (type.isArray() || type.isVector())
        {
            std::string loopVar = "i" + std::to_string (out.getTotalIndent());
            out << "for (int " << loopVar << " = 0; " << loopVar << " < " << type.getNumElements() << "; " << loopVar << "++)" << newLine;

            {
                auto indent = out.createIndentWithBraces();
                printUnpackValue (dest + "[" + loopVar + "]", source, type.getElementType());
            }
            out << newLine;
            return;
        }

        if (type.isObject())
        {
            for (uint32_t i=0; i < type.getNumElements(); i++)
            {
                auto member = type.getObjectMember (i);
                printUnpackValue (dest + "." + std::string (member.name), source, member.type);
            }
            return;
        }
    }

    bool packedSizeMatchesNativeSize (const choc::value::Type& type)
    {
        if (type.isArray() || type.isVector())
        {
            if (type.getElementType().isPrimitive() && ! type.getElementType().isBool())
                return true;

            return false;
        }

        if (type.isObject())
            return false;

        if (type.isBool())
            return false;

        return true;
    }

    void printIncomingEventHandlerFunctions()
    {
        for (auto& input : mainProcessor.getInputEndpoints (true))
        {
            if (input->isEvent())
            {
                auto dataTypes = input->dataTypes.getAsObjectTypeList<AST::TypeBase>();

                for (auto& dataType : dataTypes)
                {
                    if (auto handlerFn = AST::findEventHandlerFunction (input, dataType))
                    {
                        auto functionName = "addEvent_" + std::string (input->getName());

                        out << "void " << functionName << " (";

                        if (dataType->isVoid())
                        {
                            out << ")" << newLine;
                        }
                        else
                        {
                            if (dataType->isPrimitive())
                                out << getTypeName (dataType, false);
                            else
                                out << "const " << getTypeName (dataType, false) << "&";

                            out << " event)" << newLine;
                        }

                        {
                            auto indent = out.createIndentWithBraces();

                            if (dataType->isVoid())
                                out << codeGenerator->getFunctionName (*handlerFn) << " (state);" << newLine;
                            else
                                out << codeGenerator->getFunctionName (*handlerFn) << " (state, event);" << newLine;
                        }

                        out << blankLine;
                    }
                }
            }
        }

        out << "void addEvent (EndpointHandle endpointHandle, uint32_t typeIndex, const unsigned char* eventData)" << newLine;

        {
            auto indent = out.createIndentWithBraces();

            out << "(void) endpointHandle; (void) typeIndex; (void) eventData;" << blankLine;

            for (auto& input : mainProcessor.getInputEndpoints (true))
            {
                if (input->isEvent())
                {
                    auto dataTypes = input->dataTypes.getAsObjectTypeList<AST::TypeBase>();
                    uint32_t index = 0;

                    for (auto& dataType : dataTypes)
                    {
                        if (auto handlerFn = AST::findEventHandlerFunction (input, dataType))
                        {
                            auto functionName = "addEvent_" + std::string (input->getName());

                            std::string compareTypeIndex;

                            if (dataTypes.size() > 1)
                            {
                                compareTypeIndex = " && typeIndex == " + std::to_string (index);
                            }

                            out << "if (endpointHandle == " << std::to_string (getEndpointHandle (input)) << compareTypeIndex << ")" << newLine;

                            {
                                auto indent2 = out.createIndentWithBraces();

                                if (dataType->isVoid())
                                {
                                    out << "return " + functionName + "();" << newLine;
                                }
                                else
                                {
                                    out << getTypeName (dataType, false) << " value;" << newLine;
                                    printUnpackValue ("value", "eventData", dataType->toChocType());
                                    out << "return " << functionName << " (value);" << newLine;
                                }
                            }

                            out << blankLine;
                        }

                        ++index;
                    }
                }
            }

        }

        out << blankLine;
    }

    void printIncomingValueHandlerFunctions()
    {
        out << "void setValue (EndpointHandle endpointHandle, const void* value, int32_t frames)" << newLine;
        {
            auto indent = out.createIndentWithBraces();
            bool anyDone = false;

            for (auto& input : mainProcessor.getInputEndpoints (true))
            {
                if (input->isValue())
                {
                    out << "if (endpointHandle == " << getEndpointHandle (input)
                        << ") return " << AST::getSetValueFunctionName (input) << " (state, *("
                        << getTypeName (AST::castToTypeBaseRef (input->dataTypes[0]), true)
                        << "*) value, frames);" << newLine;

                    anyDone = true;
                }
            }

            if (! anyDone)
                out << "(void) endpointHandle; (void) value; (void) frames;" << blankLine;
        }

        out << blankLine;
    }

    void printIncomingStreamHandlerFunctions()
    {
        std::vector<std::string> dispatchers;

        for (auto& input : mainProcessor.getInputEndpoints (true))
        {
            if (input->isStream())
            {
                auto details = AST::createEndpointDetails (input);
                auto& frameType = details.dataTypes.front();
                auto frameStride = frameType.getValueDataSize();

                auto functionName = "setInputFrames_" + std::string (input->getName());

                out << "void " << functionName << " (const void* data, uint32_t numFrames, uint32_t numTrailingFramesToClear)" << newLine;
                {
                    auto indent = out.createIndentWithBraces();
                    auto buffer = "io." + std::string (input->getName());

                    if (maxNumFramesPerBlock == 1)
                    {
                        out << "(void) numTrailingFramesToClear;" << newLine
                            << "memcpy (&" << buffer << ", data, " << frameStride << ");" << newLine;
                    }
                    else
                    {
                        out << "memcpy (" << buffer << ".elements, data, numFrames * " << frameStride << ");" << newLine
                            << "if (numTrailingFramesToClear != 0) memset (" << buffer << ".elements + numFrames, 0, numTrailingFramesToClear * " << frameStride << ");" << newLine;
                    }

                    dispatchers.push_back ("if (endpointHandle == " + std::to_string (getEndpointHandle (input))
                                            + ") return " + functionName + " (frameData, numFrames, numTrailingFramesToClear);");
                }

                out << blankLine;
            }
        }

        out << "void setInputFrames (EndpointHandle endpointHandle, const void* frameData, uint32_t numFrames, uint32_t numTrailingFramesToClear)" << newLine;
        {
            auto indent = out.createIndentWithBraces();

            if (dispatchers.empty())
                out << "(void) endpointHandle; (void) frameData; (void) numFrames; (void) numTrailingFramesToClear;" << blankLine;

            for (auto& d : dispatchers)
                out << d << newLine;
        }

        out << blankLine;
    }

    void printGetStringForHandleFunction()
    {
        out << "const char* getStringForHandle (uint32_t handle, size_t& stringLength)" << newLine;
        {
            auto indent = out.createIndentWithBraces();

            if (stringHandleMatchers.empty())
            {
                out << "(void) handle; (void) stringLength;" << newLine
                    << "return \"\";" << blankLine;
            }
            else
            {
                out << "switch (handle)" << newLine;
                auto indent2 = out.createIndentWithBraces();

                for (auto& matcher : stringHandleMatchers)
                    out << matcher << newLine;

                out << "default:  stringLength = 0; return \"\";";
            }
        }

        out << blankLine;
    }

    //==============================================================================
    void printEndpointProperties()
    {
        out << choc::text::trim (R"(
using EndpointHandle = uint32_t;

enum class EndpointType
{
    stream,
    event,
    value
};

struct EndpointInfo
{
    uint32_t handle;
    const char* name;
    EndpointType endpointType;
};
)") << sectionBreak;

        auto inputs  = mainProcessor.getInputEndpoints (true);
        auto outputs = mainProcessor.getOutputEndpoints (true);

        out << "static constexpr uint32_t numInputEndpoints  = " << std::to_string (inputs.size()) << ";" << newLine
            << "static constexpr uint32_t numOutputEndpoints = " << std::to_string (outputs.size()) << ";" << blankLine
            << "static constexpr uint32_t maxFramesPerBlock  = " << std::to_string (maxNumFramesPerBlock) << ";" << newLine
            << "static constexpr uint32_t eventBufferSize    = " << std::to_string (eventBufferSize) << ";" << newLine
            << "static constexpr uint32_t maxOutputEventSize = " << std::to_string (findMaxOutputEventSize()) << ";" << newLine
            << "static constexpr double   latency            = " << std::to_string (program.getMainProcessor().getLatency()) << ";" << blankLine;

        std::unordered_map<const AST::EndpointDeclaration*, std::string> endpointVariables;

        {
            choc::text::TextTable endpointHandles, nameMatches;

            for (auto& endpoint : mainProcessor.getAllEndpoints())
            {
                auto name = std::string (endpoint->getName());
                endpointHandles << name << std::to_string (getEndpointHandle (endpoint));
                endpointHandles.newRow();

                nameMatches << "if (endpointName == " + cpp_utils::createStringLiteral (name) + ")"
                            << "return static_cast<uint32_t> (EndpointHandles::" + name + ");";
                nameMatches.newRow();
            }

            out << blankLine
                << "enum class EndpointHandles";
            printBraceEnclosedList (out, endpointHandles.getRows ("", " = ", ""));

            out << blankLine;

            out << "static constexpr uint32_t getEndpointHandleForName (std::string_view endpointName)" << newLine;
            {
                auto indent = out.createIndentWithBraces();

                for (auto& row : nameMatches.getRows ({}, "  ", {}))
                    out << row << newLine;

                out << "return 0;" << newLine;
            }
        }

        if (! inputs.empty())
        {
            choc::text::TextTable inputTable;
            uint32_t index = 0;

            for (auto& input : inputs)
            {
                inputTable << std::to_string (getEndpointHandle (input)) + ","
                           << cpp_utils::createStringLiteral (input->getName()) + ","
                           << getEndpointTypeDescription (input);

                inputTable.newRow();

                endpointVariables[input.getPointer()] = "inputEndpoints[" + std::to_string (index++) + "]";
            }

            out << blankLine;
            out << "static constexpr EndpointInfo inputEndpoints[] = ";
            printBraceEnclosedList (out, inputTable.getRows ("{ ", "  ", " }"));
        }

        if (! outputs.empty())
        {
            choc::text::TextTable outputTable;
            uint32_t index = 0;

            for (auto& output : outputs)
            {
                outputTable << std::to_string (getEndpointHandle (output)) + ","
                            << cpp_utils::createStringLiteral (output->getName()) + ","
                            << getEndpointTypeDescription (output);

                outputTable.newRow();

                endpointVariables[output.getPointer()] = "outputEndpoints[" + std::to_string (index++) + "]";
            }

            out << blankLine
                << "static constexpr EndpointInfo outputEndpoints[] = ";
            printBraceEnclosedList (out, outputTable.getRows ("{ ", "  ", " }"));
        }

        out << sectionBreak
            << "static constexpr uint32_t numAudioInputChannels  = " << std::to_string (countTotalAudioChannels (true)) << ";" << newLine
            << "static constexpr uint32_t numAudioOutputChannels = " << std::to_string (countTotalAudioChannels (false)) << ";" << blankLine;

        printEndpointArray ("outputAudioStreams", endpointVariables, false, [] (const EndpointDetails& e) { return e.getNumAudioChannels() != 0; });
        printEndpointArray ("outputEvents",       endpointVariables, false, [] (const EndpointDetails& e) { return e.isEvent(); });
        printEndpointArray ("outputMIDIEvents",   endpointVariables, false, [] (const EndpointDetails& e) { return e.isMIDI(); });
        printEndpointArray ("inputAudioStreams",  endpointVariables, true,  [] (const EndpointDetails& e) { return e.getNumAudioChannels() != 0; });
        printEndpointArray ("inputEvents",        endpointVariables, true,  [] (const EndpointDetails& e) { return e.isEvent(); });
        printEndpointArray ("inputMIDIEvents",    endpointVariables, true,  [] (const EndpointDetails& e) { return e.isMIDI(); });
        printEndpointArray ("inputParameters",    endpointVariables, true,  [] (const EndpointDetails& e) { return e.isParameter(); });

        auto programDetails = choc::value::createObject ({});
        programDetails.setMember ("mainProcessor", mainProcessor.getFullyQualifiedReadableName());
        programDetails.setMember ("inputs",  program.endpointList.inputEndpointDetails.toJSON (false));
        programDetails.setMember ("outputs", program.endpointList.outputEndpointDetails.toJSON (false));

        out << "static constexpr const char* programDetailsJSON = "
            << cpp_utils::createMultiLineStringLiteral (choc::json::toString (programDetails, true), "    ") << ";" << blankLine;
    }

    template <typename Pred>
    void printEndpointArray (std::string_view name, std::unordered_map<const AST::EndpointDeclaration*, std::string>& variableNames, bool isInput, Pred&& pred)
    {
        auto matches = mainProcessor.findEndpoints ([&] (const AST::EndpointDeclaration& e)
        {
            return e.isInput == isInput && pred (AST::createEndpointDetails (e));
        });

        std::vector<std::string> items;

        for (auto& e : matches)
            items.push_back (variableNames[e.getPointer()]);

        if (items.empty())
        {
            out << "static constexpr std::array<EndpointInfo, 0> " << name << " {};";
        }
        else
        {
            out << "static constexpr std::array " << name;
            printBraceEnclosedList (out, items);
        }

        out << blankLine;
    }

    uint32_t countTotalAudioChannels (bool isInput)
    {
        uint32_t total = 0;

        for (auto& e : mainProcessor.getAllEndpoints())
            if (e->isInput == isInput)
                total += AST::createEndpointDetails (e).getNumAudioChannels();

        return total;
    }

    uint32_t findMaxOutputEventSize()
    {
        uint32_t size = 0;

        for (auto& output : mainProcessor.getOutputEndpoints (true))
        {
            if (output->isEvent())
            {
                auto details = AST::createEndpointDetails (output);

                for (auto& dataType : details.dataTypes)
                    size = std::max (size, static_cast<uint32_t> (dataType.getValueDataSize()));
            }
        }

        return (size + 7u) & ~7u;
    }

    static void printBraceEnclosedList (choc::text::CodePrinter& out, choc::span<std::string> lines)
    {
        if (lines.empty())
        {
            out << "{};" << newLine;
            return;
        }

        out << newLine;
        {
            auto indent = out.createIndentWithBraces();
            printLinesWithCommas (out, lines);
        }
        out << ";" << newLine;
    }

    static void printLinesWithCommas (choc::text::CodePrinter& out, choc::span<std::string> lines)
    {
        for (size_t i = 0; i < lines.size(); ++i)
        {
            out << lines[i];

            if (i != lines.size() - 1)
                out << ",";

            out << newLine;
        }
    }

    static std::string getEndpointTypeDescription (const AST::EndpointDeclaration& e)
    {
        if (e.isStream()) return "EndpointType::stream";
        if (e.isEvent())  return "EndpointType::event";
        CMAJ_ASSERT (e.isValue());
        return "EndpointType::value";
    }

    //==============================================================================
    std::string createUpcastFunction (const AST::StructType& parentType, const AST::StructType& childType)
    {
        AST::SignatureBuilder sig;
        sig << "state_upcast" << parentType << childType;
        auto name = sig.toString (40);

        auto& function = upcastFunctions[name];

        if (function.empty())
        {
            auto path = AST::getPathToChildOfStruct (parentType, childType);
            CMAJ_ASSERT (path.size() == 1);
            auto index = path.front();

            auto indexedName = std::string (parentType.getMemberName (index));
            auto parentTypeName = getTypeName (parentType, true);

            auto offset = std::string ("OFFSETOF (") + parentTypeName + ", " + indexedName;

            if (AST::castToTypeBaseRef (parentType.memberTypes[index]).isArray())
                offset += ".elements[child._instanceIndex]";

            function = "static " + parentTypeName + "& " + name + " (" + getTypeName (childType, true) + "& child) "
                         + "{ return *reinterpret_cast<" + parentTypeName + "*> (reinterpret_cast<char*> (std::addressof (child)) - (" + offset + "))); }";
        }

        return name;
    }

    void printUpcastFunctions()
    {
        if (! upcastFunctions.empty())
        {
            out << "#define OFFSETOF(type, member)   ((size_t)((char *)&(*(type *)nullptr).member))" << newLine;

            for (auto& f : upcastFunctions)
                out << f.second << newLine;
        }
    }

    //==============================================================================
    using ParenStatus = SourceCodeFormattingHelper::ParenStatus;

    struct ExpressionString
    {
        ExpressionString() = default;
        ExpressionString (std::string_view s, ParenStatus p) : text (s), parenStatus (p) {}
        ExpressionString (std::string s, ParenStatus p)      : text (std::move (s)), parenStatus (p) {}

        std::string getWithoutParens() const
        {
            return text;
        }

        std::string getWithParensAlways() const
        {
            if (parenStatus == ParenStatus::present)
                return text;

            return "(" + text + ")";
        }

        std::string getWithParensIfNeeded() const
        {
            if (parenStatus == ParenStatus::needed)
                return getWithParensAlways();

            return text;
        }

        std::string text;
        ParenStatus parenStatus = ParenStatus::notNeeded;
    };

    //==============================================================================
    using StatementLocation = size_t;

    struct ValueBase
    {
        ExpressionString* expression = nullptr;

        operator bool() const                       { return expression != nullptr; }
        std::string getWithoutParens() const        { CMAJ_ASSERT (expression != nullptr); return expression->getWithoutParens(); }
        std::string getWithParensAlways() const     { CMAJ_ASSERT (expression != nullptr); return expression->getWithParensAlways(); }
        std::string getWithParensIfNeeded() const   { CMAJ_ASSERT (expression != nullptr); return expression->getWithParensIfNeeded(); }
    };

    struct ValueReader     : public ValueBase {};

    struct ValueReference  : public ValueBase
    {
        bool isPointer() const   { return true; }
    };

    ValueReader createReaderForReference (ValueReference ref)
    {
        return { { ref.expression } };
    }

    //==============================================================================
    template <typename ValueType, typename StringType>
    ValueType createExpression (StringType s, ParenStatus parens)  { return ValueType {{ std::addressof (pool.allocate<ExpressionString> (std::move (s), parens)) }}; }

    template <typename StringType>
    ValueReader createReaderNoParensNeeded   (StringType s)   { return createExpression<ValueReader> (std::move (s), ParenStatus::notNeeded); }
    template <typename StringType>
    ValueReader createReaderParensNeeded     (StringType s)   { return createExpression<ValueReader> (std::move (s), ParenStatus::needed); }
    template <typename StringType>
    ValueReader createReaderAlreadyHasParens (StringType s)   { return createExpression<ValueReader> (std::move (s), ParenStatus::present); }

    template <typename StringType>
    ValueReference createReferenceNoParensNeeded   (StringType s)   { return createExpression<ValueReference> (std::move (s), ParenStatus::notNeeded); }
    template <typename StringType>
    ValueReference createReferenceParensNeeded     (StringType s)   { return createExpression<ValueReference> (std::move (s), ParenStatus::needed); }
    template <typename StringType>
    ValueReference createReferenceAlreadyHasParens (StringType s)   { return createExpression<ValueReference> (std::move (s), ParenStatus::present); }

    //==============================================================================
    void addStruct (const AST::StructType& s)
    {
        auto name = getTypeName (s, true);

        if (std::find (emittedStructNames.begin(), emittedStructNames.end(), name) != emittedStructNames.end())
            return;

        emittedStructNames.push_back (name);
        out << "struct " << name << newLine;

        {
            auto indent = out.createIndentWithBraces();

            for (size_t i = 0; i < s.memberNames.size(); ++i)
            {
                auto& type = s.getMemberType(i);
                out << getTypeName (type, true) << " " << makeSafeIdentifier (s.getMemberName(i));

                if (type.isPrimitive())
                    out << " = {}";

                out << ";" << newLine;
            }
        }

        out << ";" << blankLine;
    }

    void addAlias (const AST::Alias& alias, const AST::TypeBase& targetType)
    {
        out << "using " << getTypeName (alias, false) << " = " << getTypeName (targetType, false) << ";" << newLine;
    }

    void addGlobalVariable (const AST::VariableDeclaration&, const AST::TypeBase& type,
                            std::string_view name, ValueReader constantValue)
    {
        CMAJ_ASSERT (! type.isReference());
        std::string decl = constantValue ? (type.isPrimitive() ? "static constexpr " : "const ") : "";

        decl += getTypeName (type, true) + " " + std::string (name) + " {";

        if (constantValue)
            decl += " " + constantValue.getWithoutParens() + " ";

        decl += "};";

        globalConstants.push_back (decl);
    }

    void beginFunction (const AST::Function& fn, std::string_view name, const AST::TypeBase& returnType)
    {
        breakLabelIndex = 0;
        tempVariableIndex = 0;
        branchIndex = 0;
        functionBlockIndentDepth = 0;
        functionOut.reset();
        functionLocalVariables.reset();

        choc::SmallVector<std::string, 8> args;

        for (auto& p : fn.iterateParameters())
            args.push_back (getTypeName (*p.getType(), false) + " " + codeGenerator->getVariableName (p));

        out << getTypeName (returnType, false) << " " << name
            << ProgramPrinter::createParenthesisedList (args) << " noexcept" << newLine
            << "{" << newLine;

        out.addIndent (4);
    }

    void endFunction()
    {
        if (! functionLocalVariables.empty())
            out << functionLocalVariables.toString() << blankLine;

        out << choc::text::trimEnd (functionOut.toString()) << newLine;

        out.addIndent (-4);
        out.trimTrailingBlankLines();
        out << "}" << blankLine;

        currentLoop = {};
    }

    //==============================================================================
    void beginBlock()
    {
        if (functionBlockIndentDepth++ != 0)
        {
            functionOut << "{" << newLine;
            functionOut.addIndent (4);
        }
    }

    void endBlock()
    {
        if (--functionBlockIndentDepth != 0)
        {
            functionOut.addIndent (-4);
            functionOut.trimTrailingBlankLines();
            functionOut << "}" << newLine;
        }
    }

    struct IfStatus
    {
        bool ignoreElse;
    };

    IfStatus beginIfStatement (ValueReader condition, bool hasTrueBranch, bool hasFalseBranch)
    {
        functionOut << "if ";

        // special case for empty true block
        if (hasFalseBranch && ! hasTrueBranch)
        {
            functionOut << " (! " << condition.getWithParensAlways() << ")" << newLine << "{" << newLine;
            functionOut.addIndent (4);
            return { true };
        }

        functionOut << condition.getWithParensAlways() << newLine << "{" << newLine;
        functionOut.addIndent (4);
        return { false };
    }

    void addElseStatement (IfStatus& status)
    {
        if (status.ignoreElse)
            return;

        functionOut.addIndent (-4);
        functionOut.trimTrailingBlankLines();
        functionOut << "}" << newLine << "else" << newLine << "{" << newLine;
        functionOut.addIndent (4);
    }

    void endIfStatement (IfStatus&)
    {
        functionOut.addIndent (-4);
        functionOut.trimTrailingBlankLines();
        functionOut << "}" << newLine;
    }

    bool addBreakFromCurrentLoop()
    {
        functionOut << "break;" << newLine;
        return true;
    }

    using BreakInstruction = std::string;

    BreakInstruction addBreak()
    {
        auto label = "_break_" + std::to_string (breakLabelIndex++);
        functionOut << "goto " << label << ";" << newLine;
        return label;
    }

    void resolveBreak (const BreakInstruction& label)
    {
        functionOut << label << ": {}" << newLine;
    }

    struct LoopStatus {};

    LoopStatus beginLoop()
    {
        functionOut << "for (;;)" << newLine
                    << "{" << newLine;
        functionOut.addIndent (4);
        return {};
    }

    void endLoop (LoopStatus&)
    {
        functionOut.addIndent (-4);
        functionOut.trimTrailingBlankLines();
        functionOut << "}" << newLine;
    }

    using ForwardBranchPlaceholder = std::string;
    struct ForwardBranchTarget {};

    ForwardBranchPlaceholder beginForwardBranch (ValueReader condition, size_t numBranches)
    {
        ++numActiveForwardBranches;
        auto branchID = "_branch" + std::to_string (branchIndex++) + "_";
        functionOut << "switch " << condition.getWithParensAlways() << newLine;

        {
            auto indent = functionOut.createIndentWithBraces();

            for (size_t i = 0; i < numBranches; ++i)
                functionOut << "case " << std::to_string (i + 1) << ": goto " << branchID << std::to_string (i) << ";" << newLine;

            functionOut << "default: break;" << newLine;
        }

        functionOut << blankLine;
        return branchID;
    }

    ForwardBranchTarget createForwardBranchTarget (const ForwardBranchPlaceholder& branchID, size_t index)
    {
        functionOut << branchID << std::to_string (index) << ":" << newLine;
        return {};
    }

    void resolveForwardBranch (const ForwardBranchPlaceholder&, const std::vector<ForwardBranchTarget>&)
    {
        --numActiveForwardBranches;
    }

    void addExpressionAsStatement (ValueReader expression)
    {
        functionOut << expression.getWithoutParens() << ";" << newLine;
    }

    void addLocalVariableDeclaration (const AST::VariableDeclaration& variable, ValueReader optionalInitialiser,
                                      bool ensureZeroInitialised, bool isTemp)
    {
        auto name = codeGenerator->getVariableName (variable);
        auto type = getTypeName (*variable.getType(), true) + " ";
        isTemp = isTemp || variable.isLocal();

        if (isTemp)
            functionLocalVariables << type << " " << name << ";" << newLine;
        else
            functionOut << type << " ";

        if (optionalInitialiser)
        {
            functionOut << name << " = " + optionalInitialiser.getWithoutParens() << ";" << newLine;
        }
        else if (ensureZeroInitialised)
        {
            if (variable.getType()->isPrimitive() || ! isTemp)
                functionOut << name << " = {};" << newLine;
            else
                functionOut << name << " = " << type << "{};" << newLine;
        }
        else if (! isTemp)
        {
            functionOut << name << ";" << newLine;
        }
    }

    ValueReference createLocalTempVariable (const AST::VariableDeclaration& variable, ValueReader optionalInitialiser, bool ensureZeroInitialised)
    {
        addLocalVariableDeclaration (variable, optionalInitialiser, ensureZeroInitialised, true);
        return createVariableReference (variable);
    }

    void addLocalVariableDeclaration (const AST::VariableDeclaration& variable, ValueReader optionalInitialiser, bool ensureZeroInitialised)
    {
        addLocalVariableDeclaration (variable, optionalInitialiser, ensureZeroInitialised, false);
    }

    void addAssignToReference (ValueReference targetRef, ValueReader value)
    {
        functionOut << targetRef.getWithParensIfNeeded();

        if (value)
            functionOut << " = " << value.getWithParensIfNeeded() << ";" << newLine;
        else
            functionOut << " = Null();" << newLine;
    }

    void addReturnValue (ValueReader value)
    {
        functionOut << "return " << value.getWithoutParens() << ";" << newLine;
    }

    void addReturnVoid()
    {
        functionOut << "return;" << newLine;
    }

    void addAddValueToInteger (ValueReference target, int32_t delta)
    {
        if (delta == 1)
            functionOut << "++" << target.getWithParensIfNeeded() << ";" << newLine;
        else if (delta == -1)
            functionOut << "--" << target.getWithParensIfNeeded() << ";" << newLine;
        else
            functionOut << target.getWithParensIfNeeded() << " += " << std::to_string (delta) << ";" << newLine;
    }

    //==============================================================================
    ValueReader createConstantInt32   (int32_t v)           { return createReaderNoParensNeeded (std::string ("int32_t {") + ProgramPrinter::formatInt32 (v) + "}"); }
    ValueReader createConstantInt64   (int64_t v)           { return createReaderNoParensNeeded (std::string ("int64_t {") + ProgramPrinter::formatInt64 (v, "L}")); }
    ValueReader createConstantFloat32 (float v)             { return createReaderNoParensNeeded (ProgramPrinter::formatFloat (v)); }
    ValueReader createConstantFloat64 (double v)            { return createReaderNoParensNeeded (ProgramPrinter::formatFloat (v)); }
    ValueReader createConstantBool    (bool b)              { return createReaderNoParensNeeded (b ? std::string_view ("true") : std::string_view ("false")); }

    ValueReader createConstantString (std::string_view s)
    {
        auto handle = stringDictionary.getHandleForString (s).handle;

        if (stringHandlesDone.find (handle) == stringHandlesDone.end())
        {
            stringHandlesDone.insert (handle);
            stringHandleMatchers.push_back ("case " + std::to_string (handle) + ":   stringLength = "
                                              + std::to_string (s.length()) + "; return " + cpp_utils::createStringLiteral (s) + ";");
        }

        return createConstantInt32 (static_cast<int32_t> (handle));
    }

    std::string getNextConstantName()   { return "_constant_" + std::to_string (constantAggregateIndex++); }

    ValueReader createConstantAggregate (const AST::ConstantAggregate& agg, bool mustHaveAddress = false)
    {
        auto elements = agg.values.getAsObjectList();
        auto& type = AST::castToTypeBaseRef (agg.type);
        auto typeName = getTypeName (type, true);

        if (elements.empty())
            return createReaderParensNeeded (typeName + " {}");

        if (type.isSlice())
        {
            auto& fixedSizeVersion = agg.context.allocator.createDeepClone (agg);
            AST::applySizeIfSlice (fixedSizeVersion.type, agg.getNumElements());
            auto sourceArray = createConstantAggregate (fixedSizeVersion, true);
            return createReaderNoParensNeeded (typeName + " { " + sourceArray.getWithParensIfNeeded() + " }");
        }

        std::string elementDecl;

        if (! agg.isZero())
        {
            bool first = true;

            for (auto& element : elements)
            {
                if (first)
                    first = false;
                else
                    elementDecl += ", ";

                elementDecl += codeGenerator->createConstant (AST::castToRef<AST::ConstantValueBase> (element)).getWithoutParens();
            }
        }

        // avoid using the variadic template constructor for larger arrays
        if (elements.size() > 32 && type.isArray())
        {
            auto rawArrayName = getNextConstantName();
            auto rawArrayDecl = "const " + getTypeName (*type.getArrayOrVectorElementType(), true) + " " + rawArrayName
                                  + "[" + std::to_string (elements.size()) + "] = { " + elementDecl + " };";

            globalConstants.push_back (rawArrayDecl);

            elementDecl = rawArrayName + ", " + std::to_string (elements.size()) + "u";
        }

        if (mustHaveAddress)
        {
            auto name = getNextConstantName();
            auto decl = "const " + typeName + " " + name + " = { " + elementDecl + " };";
            globalConstants.push_back (decl);
            return createReaderNoParensNeeded (name);
        }

        return createReaderNoParensNeeded (typeName + " { " + elementDecl + " }");
    }

    void printGlobalConstants()
    {
        for (auto& decl : globalConstants)
            out << decl << newLine;
    }

    ValueReader createNullConstant (const AST::TypeBase& t)
    {
        if (t.isVectorType())
            return createReaderNoParensNeeded (getTypeName (t, true) + "()");

        return createReaderNoParensNeeded (std::string ("Null()"));
    }

    ValueReader createValueCast (const AST::TypeBase& targetType, const AST::TypeBase&, ValueReader source)
    {
        auto typeName = getTypeName (targetType, true);
        auto sourceExpression = source.getWithoutParens();

        if (targetType.isPrimitive())
            return createReaderNoParensNeeded ("static_cast<" + typeName + "> (" + sourceExpression + ")");

        return createReaderNoParensNeeded (typeName + " { " + sourceExpression + " }");
    }

    ValueReader createSliceOfSlice (const AST::TypeBase& elementType, ValueReader sourceSlice, ValueReader start, ValueReader end)
    {
        (void) elementType;

        return createReaderNoParensNeeded (sourceSlice.getWithoutParens() + ".slice ("
                                           + start.getWithoutParens() + ", "
                                           + end.getWithoutParens() + ")");
    }

    ValueReader createSliceFromArray (const AST::TypeBase& elementType, ValueReader sourceArray,
                                      uint32_t offset, uint32_t numElements)
    {
        return createReaderNoParensNeeded ("Slice<" + getTypeName (elementType, true)
                                            + "> (" + sourceArray.getWithoutParens() + ", "
                                            + std::to_string (offset) + ", "
                                            + std::to_string (numElements) + ")");
    }

    ValueReference createStateUpcast (const AST::StructType& parentType, const AST::StructType& childType, ValueReference value)
    {
        auto name = createUpcastFunction (parentType, childType);
        return createReferenceNoParensNeeded (name + value.getWithParensAlways());
    }

    ValueReader createCall (std::string functionName,
                            const std::vector<Builder::FunctionCallArgValue>& argValues)
    {
        choc::SmallVector<std::string, 8> args;

        if (! argValues.empty())
        {
            choc::SmallVector<std::string, 8> argText;

            for (auto& arg : argValues)
                argText.push_back (arg.valueReader ? arg.valueReader.getWithoutParens()
                                                   : arg.valueReference.getWithoutParens());

            auto seemsToBeFunctionCall = [] (const std::string& code)
            {
                // this is a bit of a bodge which will have false positives,
                // but should catch all the cases we care about..
                return code.find ('(') != std::string::npos;
            };

            // This is an ugly workaround to enforce left-to-right order of arg evaluation.
            // If there's more than one arg which seems to be a function call, stash some of them
            // into temp variables..
            size_t numArgsWithPossibleSideEffects = 0;
            std::vector<bool> shouldUseTempVariable;
            shouldUseTempVariable.resize (argText.size());

            for (size_t i = 0; i < argValues.size(); ++i)
                if (argValues[i].valueReader && seemsToBeFunctionCall (argText[i]))
                    ++numArgsWithPossibleSideEffects;

            if (numArgsWithPossibleSideEffects > 1)
            {
                size_t numUsingTempVariable = 0;

                for (size_t i = 0; i < argValues.size(); ++i)
                {
                    if (argValues[i].valueReader && seemsToBeFunctionCall (argText[i]))
                    {
                        shouldUseTempVariable[i] = true;

                        if (++numUsingTempVariable == numArgsWithPossibleSideEffects - 1)
                            break; // (no need to use a temp for the last expression)
                    }
                }
            }

            for (size_t i = 0; i < argValues.size(); ++i)
            {
                auto& arg = argValues[i];

                if (shouldUseTempVariable[i])
                {
                    auto tempName = "_tempArg_" + std::to_string (tempVariableIndex++);

                    functionOut << "auto " << tempName << " = "
                                << arg.valueReader.getWithoutParens()
                                << ";" << newLine;

                    args.push_back (tempName);
                }
                else
                {
                    args.push_back (argText[i]);
                }
            }
        }

        return createReaderNoParensNeeded (functionName + ProgramPrinter::createParenthesisedList (args));
    }

    template <typename ArgValueList>
    ValueReader createIntrinsicCall (AST::Intrinsic::Type intrinsic, const ArgValueList& argValues, const AST::TypeBase&)
    {
        if (intrinsic == AST::Intrinsic::Type::exp && ! argValues.front().paramType.isPrimitiveFloat())
            return {};

        bool isVectorOp = ! argValues.empty() && argValues.front().paramType.isVector();

        if (isVectorOp
             && intrinsic != AST::Intrinsic::Type::abs    && intrinsic != AST::Intrinsic::Type::min
             && intrinsic != AST::Intrinsic::Type::max    && intrinsic != AST::Intrinsic::Type::sqrt
             && intrinsic != AST::Intrinsic::Type::log    && intrinsic != AST::Intrinsic::Type::log10
             && intrinsic != AST::Intrinsic::Type::sin    && intrinsic != AST::Intrinsic::Type::cos
             && intrinsic != AST::Intrinsic::Type::tan    && intrinsic != AST::Intrinsic::Type::sinh
             && intrinsic != AST::Intrinsic::Type::cosh   && intrinsic != AST::Intrinsic::Type::tanh
             && intrinsic != AST::Intrinsic::Type::asinh  && intrinsic != AST::Intrinsic::Type::acosh
             && intrinsic != AST::Intrinsic::Type::atanh  && intrinsic != AST::Intrinsic::Type::asin
             && intrinsic != AST::Intrinsic::Type::acos   && intrinsic != AST::Intrinsic::Type::atan
             && intrinsic != AST::Intrinsic::Type::atan2  && intrinsic != AST::Intrinsic::Type::pow
             && intrinsic != AST::Intrinsic::Type::exp)
            return {};

        return createCall ((isVectorOp ? "intrinsics::VectorOps::"
                                       : "intrinsics::")
                            + std::string (AST::Intrinsic::getIntrinsicName (intrinsic)), argValues);
    }

    template <typename ArgValueList>
    ValueReader createFunctionCall (const AST::Function&, std::string_view functionName, const ArgValueList& argValues)
    {
        return createCall (std::string (functionName), argValues);
    }

    ValueReader createUnaryOp (AST::UnaryOpTypeEnum::Enum opType, const AST::TypeBase&, ValueReader input)
    {
        return createReaderParensNeeded (std::string (AST::UnaryOperator::getSymbolForOperator (opType))
                                            + " " + input.getWithParensIfNeeded());
    }

    static bool canPerformVectorUnaryOp()   { return true; }
    static bool canPerformVectorBinaryOp()  { return true; }

    ValueReader createAddInt32 (ValueReader lhs, int32_t rhs)
    {
        return createReaderParensNeeded (lhs.getWithParensIfNeeded() + " + " + std::to_string (rhs));
    }

    ValueReader createBinaryOp (AST::BinaryOpTypeEnum::Enum opType,
                                const AST::TypeRules::BinaryOperatorTypes& opTypes,
                                ValueReader lhs, ValueReader rhs)
    {
        if (opType == AST::BinaryOpTypeEnum::Enum::rightShiftUnsigned)
            return createReaderNoParensNeeded ("intrinsics::rightShiftUnsigned (" + lhs.getWithoutParens()
                                                 + ", " + rhs.getWithoutParens() + ")");

        if (opType == AST::BinaryOpTypeEnum::Enum::modulo && ! opTypes.operandType.isVector())
            return createReaderNoParensNeeded ("intrinsics::modulo (" + lhs.getWithoutParens()
                                                 + ", " + rhs.getWithoutParens() + ")");

        return createReaderParensNeeded (lhs.getWithParensIfNeeded()
                                         + " " + std::string (AST::BinaryOperator::getSymbolForOperator (opType))
                                         + " " + rhs.getWithParensIfNeeded());
    }

    ValueReader createTernaryOp (ValueReader condition, ValueReader trueValue, ValueReader falseValue)
    {
        return createReaderParensNeeded (condition.getWithParensIfNeeded()
                                         + " ? " + trueValue.getWithParensIfNeeded()
                                         + " : " + falseValue.getWithParensIfNeeded());
    }

    ValueReader createVariableReader (const AST::VariableDeclaration& v)
    {
        return createReaderNoParensNeeded (codeGenerator->getVariableName (v));
    }

    ValueReference createVariableReference (const AST::VariableDeclaration& v)
    {
        return createReferenceNoParensNeeded (codeGenerator->getVariableName (v));
    }

    ValueReader createElementReader (ValueReader parent, ValueReader index)
    {
        return createReaderNoParensNeeded (parent.getWithParensIfNeeded() + "[" + index.getWithoutParens() + "]");
    }

    ValueReference createElementReference (ValueReference parent, ValueReader index)
    {
        return createReferenceNoParensNeeded (parent.getWithParensIfNeeded() + "[" + index.getWithoutParens() + "]");
    }

    ValueReader createStructMemberReader (const AST::StructType&, ValueReader object, std::string_view memberName, int64_t)
    {
        return createReaderNoParensNeeded (object.getWithParensIfNeeded() + "." + makeSafeIdentifier (memberName));
    }

    ValueReference createStructMemberReference (const AST::StructType&, ValueReference object, std::string_view memberName, int64_t)
    {
        return createReferenceNoParensNeeded (object.getWithParensIfNeeded() + "." + makeSafeIdentifier (memberName));
    }

    ValueReader createGetSliceSize (ValueReader slice)
    {
        return createReaderNoParensNeeded (slice.getWithParensIfNeeded() + ".size()");
    }

    //==============================================================================
    static std::string makeSafeIdentifier (std::string_view name)   { return cmaj::cpp_utils::makeSafeIdentifier (name); }

    //==============================================================================
    static constexpr choc::text::CodePrinter::NewLine newLine = {};
    static constexpr choc::text::CodePrinter::BlankLine blankLine = {};
    static constexpr choc::text::CodePrinter::SectionBreak sectionBreak = {};

    static std::string_view getFileHeaderBoilerplate()
    {
        return R"CPPGEN(
#include <cstdint>
#include <cmath>
#include <cassert>
#include <string>
#include <cstring>
#include <array>

//==============================================================================
/// Auto-generated C++ class for the 'NAME' processor
///

#if ! (defined (__cplusplus) && (__cplusplus >= 201703L))
 #error "This code requires that your compiler is set to use C++17 or later!"
#endif

)CPPGEN";
    }

    static std::string_view getHelperClassDefinitions()
    {
        return R"CPPGEN(
struct intrinsics;

using SizeType = int32_t;
using IndexType = int32_t;
using StringHandle = uint32_t;

struct Null
{
    template <typename AnyType> operator AnyType() const    { return {}; }
    Null operator[] (IndexType) const                       { return {}; }
};

//==============================================================================
template <typename ElementType, SizeType numElements>
struct Array
{
    Array() = default;
    Array (Null) {}
    Array (const Array&) = default;

    template <typename ElementOrList>
    Array (const ElementOrList& value) noexcept
    {
        if constexpr (std::is_convertible<ElementOrList, ElementType>::value)
        {
            for (IndexType i = 0; i < numElements; ++i)
                this->elements[i] = static_cast<ElementType> (value);
        }
        else
        {
            for (IndexType i = 0; i < numElements; ++i)
                this->elements[i] = static_cast<ElementType> (value[i]);
        }
    }

    template <typename... Others>
    Array (ElementType e0, ElementType e1, Others... others) noexcept
    {
        this->elements[0] = static_cast<ElementType> (e0);
        this->elements[1] = static_cast<ElementType> (e1);

        if constexpr (numElements > 2)
        {
            const ElementType initialisers[] = { static_cast<ElementType> (others)... };

            for (size_t i = 0; i < sizeof...(others); ++i)
                this->elements[i + 2] = initialisers[i];
        }
    }

    Array (const ElementType* rawArray, size_t) noexcept
    {
        for (IndexType i = 0; i < numElements; ++i)
            this->elements[i] = rawArray[i];
    }

    Array& operator= (const Array&) noexcept = default;
    Array& operator= (Null) noexcept                 { this->clear(); return *this; }

    template <typename ElementOrList>
    Array& operator= (const ElementOrList& value) noexcept
    {
        if constexpr (std::is_convertible<ElementOrList, ElementType>::value)
        {
            for (IndexType i = 0; i < numElements; ++i)
                this->elements[i] = static_cast<ElementType> (value);
        }
        else
        {
            for (IndexType i = 0; i < numElements; ++i)
                this->elements[i] = static_cast<ElementType> (value[i]);
        }
    }

    static constexpr SizeType size()                                    { return numElements; }

    const ElementType& operator[] (IndexType index) const noexcept      { return this->elements[index]; }
    ElementType& operator[] (IndexType index) noexcept                  { return this->elements[index]; }

    void clear() noexcept
    {
        for (auto& element : elements)
            element = ElementType();
    }

    void clear (SizeType numElementsToClear) noexcept
    {
        for (SizeType i = 0; i < numElementsToClear; ++i)
            elements[i] = ElementType();
    }

    ElementType elements[numElements] = {};
};

//==============================================================================
template <typename ElementType, SizeType numElements>
struct Vector  : public Array<ElementType, numElements>
{
    Vector() = default;
    Vector (Null) {}

    template <typename ElementOrList>
    Vector (const ElementOrList& value) noexcept  : Array<ElementType, numElements> (value) {}

    template <typename... Others>
    Vector (ElementType e0, ElementType e1, Others... others) noexcept  : Array<ElementType, numElements> (e0, e1, others...) {}

    Vector (const ElementType* rawArray, size_t) noexcept  : Array<ElementType, numElements> (rawArray, size_t()) {}

    template <typename ElementOrList>
    Vector& operator= (const ElementOrList& value) noexcept { return Array<ElementType, numElements>::operator= (value); }

    Vector& operator= (Null) noexcept { this->clear(); return *this; }

    operator ElementType() const noexcept
    {
        static_assert (numElements == 1);
        return this->elements[0];
    }

    constexpr auto operator!() const noexcept     { return performUnaryOp ([] (ElementType n) { return ! n; }); }
    constexpr auto operator~() const noexcept     { return performUnaryOp ([] (ElementType n) { return ~n; }); }
    constexpr auto operator-() const noexcept     { return performUnaryOp ([] (ElementType n) { return -n; }); }

    constexpr auto operator+ (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a + b; }); }
    constexpr auto operator- (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a - b; }); }
    constexpr auto operator* (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a * b; }); }
    constexpr auto operator/ (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a / b; }); }
    constexpr auto operator% (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return intrinsics::modulo (a, b); }); }

    constexpr auto operator== (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a == b; }); }
    constexpr auto operator!= (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a != b; }); }
    constexpr auto operator<  (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a < b; }); }
    constexpr auto operator<= (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a <= b; }); }
    constexpr auto operator>  (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a > b; }); }
    constexpr auto operator>= (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a >= b; }); }

    template <typename Functor>
    constexpr Vector performUnaryOp (Functor&& f) const noexcept
    {
        Vector result;

        for (IndexType i = 0; i < numElements; ++i)
            result.elements[i] = f (this->elements[i]);

        return result;
    }

    template <typename Functor>
    constexpr Vector performBinaryOp (const Vector& rhs, Functor&& f) const noexcept
    {
        Vector result;

        for (IndexType i = 0; i < numElements; ++i)
            result.elements[i] = f (this->elements[i], rhs.elements[i]);

        return result;
    }

    template <typename Functor>
    constexpr Vector<bool, numElements> performComparison (const Vector& rhs, Functor&& f) const noexcept
    {
        Vector<bool, numElements> result;

        for (IndexType i = 0; i < numElements; ++i)
            result.elements[i] = f (this->elements[i], rhs.elements[i]);

        return result;
    }
};

//==============================================================================
template <typename ElementType>
struct Slice
{
    Slice() = default;
    Slice (Null) {}
    Slice (ElementType* e, SizeType size) : elements (e), numElements (size) {}
    Slice (const Slice&) = default;
    Slice& operator= (const Slice&) = default;
    template <typename ArrayType> Slice (const ArrayType& a) : elements (const_cast<ArrayType&> (a).elements), numElements (a.size()) {}
    template <typename ArrayType> Slice (const ArrayType& a, SizeType offset, SizeType size) : elements (const_cast<ArrayType&> (a).elements + offset), numElements (size) {}

    constexpr SizeType size() const                                     { return numElements; }
    ElementType operator[] (IndexType index) const noexcept             { return numElements == 0 ? ElementType() : elements[index]; }
    ElementType& operator[] (IndexType index) noexcept                  { return numElements == 0 ? emptyValue : elements[index]; }

    Slice slice (IndexType start, IndexType end) noexcept               
    {
        if (numElements == 0) return {};
        if (start >= numElements) return {};

        return { elements + start, std::min (static_cast<SizeType> (end - start), numElements - start) };
    }

    ElementType* elements = nullptr;
    SizeType numElements = 0;

    static inline ElementType emptyValue {};
};
)CPPGEN";
    }

    static std::string_view getWarningDisableFlags()
    {
        return R"CPPGEN(
#if __clang__
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wunused-variable"
 #pragma clang diagnostic ignored "-Wunused-parameter"
 #pragma clang diagnostic ignored "-Wunused-label"

 #if __clang_major__ >= 14
  #pragma clang diagnostic ignored "-Wunused-but-set-variable"
 #endif

#elif __GNUC__
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wunused-variable"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wunused-but-set-variable"
 #pragma GCC diagnostic ignored "-Wunused-label"
#else
 #pragma warning (push, 0)
 #pragma warning (disable: 4702)
 #pragma warning (disable: 4706)
#endif
)CPPGEN";
    }

    static std::string_view getWarningReenableFlags()
    {
        return R"CPPGEN(
#if __clang__
 #pragma clang diagnostic pop
#elif __GNUC__
 #pragma GCC diagnostic pop
#else
 #pragma warning (pop)
#endif
)CPPGEN";
    }

    static std::string_view getIntrinsicFunctions()
    {
        return R"CPPGEN(
struct intrinsics
{
    template <typename T> static T modulo (T a, T b)
    {
        if constexpr (std::is_floating_point<T>::value)
            return std::fmod (a, b);
        else
            return a % b;
    }

    template <typename T> static T addModulo2Pi (T a, T b)
    {
        constexpr auto twoPi = static_cast<T> (3.141592653589793238 * 2);
        auto n = a + b;
        return n >= twoPi ? std::remainder (n, twoPi) : n;
    }

    template <typename T> static T abs           (T a)              { return std::abs (a); }
    template <typename T> static T min           (T a, T b)         { return std::min (a, b); }
    template <typename T> static T max           (T a, T b)         { return std::max (a, b); }
    template <typename T> static T clamp         (T a, T b, T c)    { return a < b ? b : (a > c ? c : a); }
    template <typename T> static T wrap          (T a, T b)         { if (b == 0) return 0; auto n = modulo (a, b); if (n < 0) n += b; return n; }
    template <typename T> static T fmod          (T a, T b)         { return b != 0 ? std::fmod (a, b) : 0; }
    template <typename T> static T remainder     (T a, T b)         { return b != 0 ? std::remainder (a, b) : 0; }
    template <typename T> static T floor         (T a)              { return std::floor (a); }
    template <typename T> static T ceil          (T a)              { return std::ceil (a); }
    template <typename T> static T rint          (T a)              { return std::rint (a); }
    template <typename T> static T sqrt          (T a)              { return std::sqrt (a); }
    template <typename T> static T pow           (T a, T b)         { return std::pow (a, b); }
    template <typename T> static T exp           (T a)              { return std::exp (a); }
    template <typename T> static T log           (T a)              { return std::log (a); }
    template <typename T> static T log10         (T a)              { return std::log10 (a); }
    template <typename T> static T sin           (T a)              { return std::sin (a); }
    template <typename T> static T cos           (T a)              { return std::cos (a); }
    template <typename T> static T tan           (T a)              { return std::tan (a); }
    template <typename T> static T sinh          (T a)              { return std::sinh (a); }
    template <typename T> static T cosh          (T a)              { return std::cosh (a); }
    template <typename T> static T tanh          (T a)              { return std::tanh (a); }
    template <typename T> static T asinh         (T a)              { return std::asinh (a); }
    template <typename T> static T acosh         (T a)              { return std::acosh (a); }
    template <typename T> static T atanh         (T a)              { return std::atanh (a); }
    template <typename T> static T asin          (T a)              { return std::asin (a); }
    template <typename T> static T acos          (T a)              { return std::acos (a); }
    template <typename T> static T atan          (T a)              { return std::atan (a); }
    template <typename T> static T atan2         (T a, T b)         { return std::atan2 (a, b); }
    template <typename T> static T isnan         (T a)              { return std::isnan (a) ? 1 : 0; }
    template <typename T> static T isinf         (T a)              { return std::isinf (a) ? 1 : 0; }

    static int32_t reinterpretFloatToInt (float   a)                { int32_t i; memcpy (std::addressof(i), std::addressof(a), sizeof(i)); return i; }
    static int64_t reinterpretFloatToInt (double  a)                { int64_t i; memcpy (std::addressof(i), std::addressof(a), sizeof(i)); return i; }
    static float   reinterpretIntToFloat (int32_t a)                { float   f; memcpy (std::addressof(f), std::addressof(a), sizeof(f)); return f; }
    static double  reinterpretIntToFloat (int64_t a)                { double  f; memcpy (std::addressof(f), std::addressof(a), sizeof(f)); return f; }

    static int32_t rightShiftUnsigned (int32_t a, int32_t b)        { return static_cast<int32_t> (static_cast<uint32_t> (a) >> b); }
    static int64_t rightShiftUnsigned (int64_t a, int64_t b)        { return static_cast<int64_t> (static_cast<uint64_t> (a) >> b); }

    struct VectorOps
    {
        template <typename Vec> static Vec abs     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::abs (x); }); }
        template <typename Vec> static Vec min     (Vec a, Vec b)     { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::min (x, y); }); }
        template <typename Vec> static Vec max     (Vec a, Vec b)     { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::max (x, y); }); }
        template <typename Vec> static Vec sqrt    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::sqrt (x); }); }
        template <typename Vec> static Vec log     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::log (x); }); }
        template <typename Vec> static Vec log10   (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::log10 (x); }); }
        template <typename Vec> static Vec sin     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::sin (x); }); }
        template <typename Vec> static Vec cos     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::cos (x); }); }
        template <typename Vec> static Vec tan     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::tan (x); }); }
        template <typename Vec> static Vec sinh    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::sinh (x); }); }
        template <typename Vec> static Vec cosh    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::cosh (x); }); }
        template <typename Vec> static Vec tanh    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::tanh (x); }); }
        template <typename Vec> static Vec asinh   (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::asinh (x); }); }
        template <typename Vec> static Vec acosh   (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::acosh (x); }); }
        template <typename Vec> static Vec atanh   (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::atanh (x); }); }
        template <typename Vec> static Vec asin    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::asin (x); }); }
        template <typename Vec> static Vec acos    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::acos (x); }); }
        template <typename Vec> static Vec atan    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::atan (x); }); }
        template <typename Vec> static Vec atan2   (Vec a, Vec b)     { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::atan2 (x, y); }); }
        template <typename Vec> static Vec pow     (Vec a, Vec b)     { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::pow (x, y); }); }
        template <typename Vec> static Vec exp     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::exp (x); }); }
    };
};

static constexpr float  _inf32  =  std::numeric_limits<float>::infinity();
static constexpr double _inf64  =  std::numeric_limits<double>::infinity();
static constexpr float  _ninf32 = -std::numeric_limits<float>::infinity();
static constexpr double _ninf64 = -std::numeric_limits<double>::infinity();
static constexpr float  _nan32  =  std::numeric_limits<float>::quiet_NaN();
static constexpr double _nan64  =  std::numeric_limits<double>::quiet_NaN();
)CPPGEN";
    }
};

GeneratedCPP generateCPPClass (const ProgramInterface& program, std::string_view options,
                               double maxFrequency, uint32_t maxNumFramesPerBlock, uint32_t eventBufferSize,
                               const std::function<EndpointHandle(const EndpointID&)>& getEndpointHandle)
{
    CPlusPlusCodeGenerator gen (AST::getProgram (program), options,
                                maxFrequency, maxNumFramesPerBlock, eventBufferSize,
                                getEndpointHandle);

    if (gen.generate())
        return gen.getResult();

    return {};
}


} // namespace cmaj::cplusplus

#endif // CMAJ_ENABLE_CODEGEN_CPP || CMAJ_ENABLE_PERFORMER_CPP
