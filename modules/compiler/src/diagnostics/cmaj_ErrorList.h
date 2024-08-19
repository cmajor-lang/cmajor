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


//==============================================================================
#define DECLARE_MESSAGE(type, category, name, errorText) \
    template <typename... Substrings> static DiagnosticMessage name (Substrings&&... substrings) \
    { \
        static_assert (sizeof...(substrings) == countSubstrings (errorText), "wrong number of strings"); \
        return createMessage (category, type, errorText, std::forward<Substrings> (substrings)...); \
    }

#define DECL_COMPILE_ERROR(name, messageText)     DECLARE_MESSAGE (DiagnosticMessage::Type::error,   DiagnosticMessage::Category::compile, name, messageText)
#define DECL_RUNTIME_ERROR(name, messageText)     DECLARE_MESSAGE (DiagnosticMessage::Type::error,   DiagnosticMessage::Category::runtime, name, messageText)
#define DECL_WARNING(name, messageText)           DECLARE_MESSAGE (DiagnosticMessage::Type::warning, DiagnosticMessage::Category::compile, name, messageText)
#define DECL_NOTE(name, messageText)              DECLARE_MESSAGE (DiagnosticMessage::Type::note,    DiagnosticMessage::Category::compile, name, messageText)

namespace cmaj
{
struct Errors
{

//==============================================================================
DECL_COMPILE_ERROR (staticAssertionFailure,                 "Static assertion failure")
DECL_COMPILE_ERROR (staticAssertionFailureWithMessage,      "{0}")
DECL_COMPILE_ERROR (unimplementedFeature,                   "Language feature not yet implemented: {0}!")

// Low-level lexer errors
DECL_COMPILE_ERROR (identifierTooLong,                      "Identifier too long")
DECL_COMPILE_ERROR (illegalCharacter,                       "Unexpected character '{0}' in source")
DECL_COMPILE_ERROR (endOfInputInStringConstant,             "Unexpected end-of-input in string constant")
DECL_COMPILE_ERROR (unterminatedComment,                    "Unterminated '/*' comment")
DECL_COMPILE_ERROR (invalidUTF8,                            "Invalid UTF8 data")
DECL_COMPILE_ERROR (errorInEscapeCode,                      "Syntax error in unicode escape sequence")
DECL_COMPILE_ERROR (unrecognisedLiteralSuffix,              "Unrecognised suffix on literal")
DECL_COMPILE_ERROR (complexNumberSuffixNeedsFloat,          "The 'i' suffix for complex numbers requires a floating-point literal")
DECL_COMPILE_ERROR (errorInNumericLiteral,                  "Syntax error in numeric constant")
DECL_COMPILE_ERROR (noOctalLiterals,                        "Octal literals are not supported")
DECL_COMPILE_ERROR (integerLiteralTooLarge,                 "Integer literal is too large to be represented")
DECL_COMPILE_ERROR (integerLiteralNeedsSuffix,              "This value is too large to fit into an int32, did you mean to add an 'i64' suffix?")
DECL_COMPILE_ERROR (noLeadingUnderscoreAllowed,             "Identifiers beginning with an underscore are reserved for system use")

// Errors where the parser expects a particular type of object
DECL_COMPILE_ERROR (foundWhenExpecting,                     "Found {0} when expecting {1}")
DECL_COMPILE_ERROR (expectedStatement,                      "Expected a statement")
DECL_COMPILE_ERROR (expectedType,                           "Expected a type")
DECL_COMPILE_ERROR (expectedValue,                          "Expected a value")
DECL_COMPILE_ERROR (expectedConstant,                       "Expected a constant value")
DECL_COMPILE_ERROR (expectedVariableDecl,                   "Expected a variable declaration")
DECL_COMPILE_ERROR (expectedSingleVariableDeclaration,      "Expected a single variable declaraion")
DECL_COMPILE_ERROR (expectedFunctionOrVariable,             "Expected a function or variable declaration")
DECL_COMPILE_ERROR (expectedNamespaceName,                  "Expected a namespace name")
DECL_COMPILE_ERROR (expectedProcessorName,                  "Expected a processor name")
DECL_COMPILE_ERROR (didNotExpectFunctionName,               "Did not expect a function name in this context")
DECL_COMPILE_ERROR (useProcessorForGraphAlias,              "If you're trying to declare an alias for a graph, use the 'processor' keyword")
DECL_COMPILE_ERROR (expectedEndpoint,                       "Expected an endpoint")
DECL_COMPILE_ERROR (expectedProcessorOrEndpoint,            "Expected a processor name or endpoint")
DECL_COMPILE_ERROR (expectedNode,                           "Expected a node name")
DECL_COMPILE_ERROR (expectedGenericWildcardName,            "Expected a generic function wildcard name")
DECL_COMPILE_ERROR (expectedTopLevelDecl,                   "Expected a graph, processor or namespace declaration")
DECL_COMPILE_ERROR (expectedStreamType,                     "Expected a stream type specifier")
DECL_COMPILE_ERROR (expectedStreamTypeOrEndpoint,           "Expected a stream type specifier or endpoint")
DECL_COMPILE_ERROR (expectedInterpolationType,              "Expected an interpolation type")
DECL_COMPILE_ERROR (invalidInterpolationType,               "Invalid interpolation type for non-float endpoint")
DECL_COMPILE_ERROR (expectedImportModule,                   "Expected a module identifier")
DECL_COMPILE_ERROR (expectedBlockAfterLabel,                "Expected a label name to be followed by a loop statement or braced block")
DECL_COMPILE_ERROR (expectedBlockLabel,                     "Expected the name of a block label")
DECL_COMPILE_ERROR (expected1or2Args,                       "Expected 1 or 2 arguments")

// Errors where things are being done in the wrong place
DECL_COMPILE_ERROR (namespaceMustBeInsideNamespace,         "A namespace can only be defined inside a namespace")
DECL_COMPILE_ERROR (processorMustBeInsideNamespace,         "A processor can only be defined inside a namespace")
DECL_COMPILE_ERROR (graphMustBeInsideNamespace,             "A graph can only be defined inside a namespace")
DECL_COMPILE_ERROR (graphCannotContainMainOrInitFunctions,  "The main() and init() functions may only be declared inside a processor")
DECL_COMPILE_ERROR (onlyMemberFunctionsCanBeConst,          "Only struct member functions can be declared `const`")
DECL_COMPILE_ERROR (namespaceCannotContainEndpoints,        "A namespace cannot contain endpoint declarations")
DECL_COMPILE_ERROR (importsMustBeAtStart,                   "Import statements can only be declared at the start of a namespace")
DECL_COMPILE_ERROR (noEventFunctionsAllowed,                "Event handlers can only be declared inside a processor or graph")
DECL_COMPILE_ERROR (cannotMixEventFunctionsAndConnections,  "Graphs cannot contain both event handlers and connections for the same endpoint")
DECL_COMPILE_ERROR (endpointDeclsMustBeFirst,               "Endpoint declarations must all appear at the start of the processor")
DECL_COMPILE_ERROR (processorSpecialisationNotAllowed,      "Processor specialisations may only be used in graphs")
DECL_COMPILE_ERROR (namespaceSpecialisationNotAllowed,      "Namespace specialisations may only be used in namespaces")
DECL_COMPILE_ERROR (processorReferenceNotAllowed,           "Processor references are not allowed in this context")
DECL_COMPILE_ERROR (nonConstInGraph,                        "Only constant variables can be declared inside a graph")
DECL_COMPILE_ERROR (nonConstInNamespace,                    "Only constant variables can be declared inside a namespace")
DECL_COMPILE_ERROR (namespaceNotAllowed,                    "Namespace names are not allowed in this context")
DECL_COMPILE_ERROR (semicolonAfterBrace,                    "A brace-enclosed declaration should not be followed by a semicolon")
DECL_COMPILE_ERROR (continueMustBeInsideALoop,              "The 'continue' statement can only be used inside a loop")
DECL_COMPILE_ERROR (breakMustBeInsideALoop,                 "The 'break' statement can only be used inside a loop")
DECL_COMPILE_ERROR (recursiveReference,                     "'{0}' cannot refer to itself recursively")
DECL_COMPILE_ERROR (recursiveReferences,                    "'{0}' and '{1}' reference each other recursively")
DECL_COMPILE_ERROR (recursiveExpression,                    "This expression contains a recursive reference")
DECL_COMPILE_ERROR (tooManyNamespaceInstances,              "Exceeded the maximum number of specialised namespace instances ({0}) - possible namespace recursion")
DECL_COMPILE_ERROR (nodesCannotBeDeclaredWithinNamespaces,  "Nodes cannot be declared within namespaces")

// Unresolved/duplicate/ambiguous name errors
DECL_COMPILE_ERROR (nameInUse,                              "The name '{0}' is already in use")
DECL_COMPILE_ERROR (invalidEndpointName,                    "The name '{0}' is not a valid endpoint name")
DECL_COMPILE_ERROR (unresolvedSymbol,                       "Cannot find symbol '{0}'")
DECL_COMPILE_ERROR (ambiguousSymbol,                        "Multiple matches found when looking for '{0}'")
DECL_COMPILE_ERROR (unknownFunction,                        "Unknown function: '{0}'")
DECL_COMPILE_ERROR (cannotResolveFunction,                  "Cannot resolve target function")
DECL_COMPILE_ERROR (noMatchForFunctionCall,                 "No suitable override found for function call: {0}")
DECL_COMPILE_ERROR (ambiguousFunctionCall,                  "Ambiguous function call: {0}")
DECL_COMPILE_ERROR (noFunctionWithNumberOfArgs,             "Can't find a function '{0}' with {1} argument(s)")
DECL_COMPILE_ERROR (unknownFunctionWithSuggestion,          "Unknown function: '{0}' (did you mean '{1}'?)")
DECL_COMPILE_ERROR (unknownMemberInStruct,                  "No member called '{0}' found in struct '{1}'")
DECL_COMPILE_ERROR (unknownMemberInComplex,                 "{1} has no member called '{0}'")
DECL_COMPILE_ERROR (expectedStructForDotOperator,           "Expected a struct type to the left of the dot operator")
DECL_COMPILE_ERROR (invalidDotArguments,                    "Invalid arguments for the dot operator")
DECL_COMPILE_ERROR (cannotResolveBracketedExpression,       "Cannot resolve bracketed expression in this context")
DECL_COMPILE_ERROR (initialiserRefersToTarget,              "The variable '{0}' cannot recursively refer to itself in its initial value")
DECL_COMPILE_ERROR (cannotCastType,                         "Cannot convert type '{0}' to '{1}'")
DECL_COMPILE_ERROR (cannotImplicitlyCastType,               "Cannot implicitly convert '{0}' to '{1}'")
DECL_COMPILE_ERROR (cannotCastValue,                        "Cannot convert {0} ('{1}') to '{2}'")
DECL_COMPILE_ERROR (cannotImplicitlyCastValue,              "Cannot implicitly convert {0} ('{1}') to '{2}'")
DECL_COMPILE_ERROR (cannotCastListToType,                   "Cannot convert comma-separated list to type '{0}'")
DECL_COMPILE_ERROR (ambiguousCastBetween,                   "Ambiguous cast from type '{0}' to '{1}'")
DECL_COMPILE_ERROR (wrongNumArgsForAggregate,               "Wrong number of values to create a type '{0}'")
DECL_COMPILE_ERROR (cannotUseProcessorAsValue,              "Cannot use a processor name as a value")
DECL_COMPILE_ERROR (cannotUseProcessorAsType,               "Cannot use a processor name as a type")
DECL_COMPILE_ERROR (cannotUseProcessorAsFunction,           "Cannot use a processor name as a function call")
DECL_COMPILE_ERROR (processorNameAlreadyUsed,               "A processor called '{0}' is already visible in this scope")
DECL_COMPILE_ERROR (expectedArrayOrVectorForBracketOp,      "Expected a vector or array to the left of the bracket operator")
DECL_COMPILE_ERROR (cannotFindBlockLabel,                   "This statement does not have a parent block or loop labelled '{0}'")
DECL_COMPILE_ERROR (cannotFindForwardJumpTarget,            "Cannot find a scoped block labelled '{0}'")

// Type definition errors
DECL_COMPILE_ERROR (variableCannotBeVoid,                   "A variable type cannot be 'void'")
DECL_COMPILE_ERROR (parameterCannotBeVoid,                  "Function parameters cannot be void")
DECL_COMPILE_ERROR (arrayElementCannotBeVoid,               "Array elements cannot be void")
DECL_COMPILE_ERROR (vectorElementCannotBeVoid,              "Vector elements cannot be void")
DECL_COMPILE_ERROR (targetCannotBeVoid,                     "Cannot cast to a void type")
DECL_COMPILE_ERROR (variableTypeCannotBeReference,          "Only parameter variables can be given reference types")
DECL_COMPILE_ERROR (tooManyConsts,                          "The 'const' keyword cannot be applied to a type that is already const")
DECL_COMPILE_ERROR (memberCannotBeConst,                    "Struct members cannot be declared const")
DECL_COMPILE_ERROR (memberCannotBeReference,                "Struct members cannot be references")
DECL_COMPILE_ERROR (memberCannotBeVoid,                     "Structure members cannot be void")
DECL_COMPILE_ERROR (cannotPassConstAsNonConstRef,           "Cannot pass a const value as a non-const reference")
DECL_COMPILE_ERROR (processorParamsCannotBeReference,       "Processor parameter types cannot be references")
DECL_COMPILE_ERROR (badTypeForPrimitiveType,                "'primitiveType' can only be applied to a vector or primitive type")
DECL_COMPILE_ERROR (badTypeForElementType,                  "'elementType' can only be applied to an array or vector type")
DECL_COMPILE_ERROR (badTypeForMetafunction,                 "'{0}' cannot be applied to an argument of type '{1}'")
DECL_COMPILE_ERROR (wrongTypeForAllocOp,                    "The alloc() function can only be applied to a slice type (e.g. `int[]`)")
DECL_COMPILE_ERROR (tooManyElements,                        "Too many elements")
DECL_COMPILE_ERROR (illegalVectorSize,                      "Illegal vector size")
DECL_COMPILE_ERROR (illegalArraySize,                       "Illegal array size")
DECL_COMPILE_ERROR (nonIntegerArraySize,                    "Array or vector size must be an integer")
DECL_COMPILE_ERROR (nonIntegerArrayIndex,                   "An array index must be an integer type")
DECL_COMPILE_ERROR (arraySizeMustBeConstant,                "An array size must be a constant")
DECL_COMPILE_ERROR (arrayTypeCannotBeReference,             "Array elements cannot be references")
DECL_COMPILE_ERROR (illegalTypeForVectorElement,            "Cannot create a vector with elements that are not primitive types")
DECL_COMPILE_ERROR (wrapOrClampSizeMustBeConstant,          "The size of a 'wrap' or 'clamp' type must be a constant")
DECL_COMPILE_ERROR (wrapOrClampSizeHasOneArgument,          "The size of a 'wrap' or 'clamp' type must be a single value")
DECL_COMPILE_ERROR (illegalSliceSize,                       "Invalid array slice range")
DECL_COMPILE_ERROR (cannotReturnLocalReference,             "Only slices which refer to global data can be returned from functions")
DECL_COMPILE_ERROR (cannotAssignSliceToWiderScope,          "Values which may contain slices of local data cannot be assigned to variables which may outlive them")
DECL_COMPILE_ERROR (cannotSendLocalReference,               "Only source values which have global scope may be written to an endpoint")
DECL_COMPILE_ERROR (cannotAssignToSliceElement,             "Cannot assign to an element of a slice")
DECL_COMPILE_ERROR (unexpectedSliceRange,                   "Expected an array size, not a range")
DECL_COMPILE_ERROR (wrongNumberOfArrayIndexes,              "Number of indexes supplied does not match the type: expected {0}, but got {1}")
DECL_COMPILE_ERROR (wrongNumArgsForAtMethod,                "The 'at' method for this type expects {0} argument(s)")
DECL_COMPILE_ERROR (wrongTypeForAtMethod,                   "The 'at' method can only be applied to a vector or array")
DECL_COMPILE_ERROR (wrongNumArgsForMetafunction,            "Wrong number of arguments for the '{0}' metafunction")
DECL_COMPILE_ERROR (typeReferenceNotAllowed,                "Type references are not allowed in this context")
DECL_COMPILE_ERROR (cannotResolveVectorSize,                "Cannot resolve vector size expression in this context")
DECL_COMPILE_ERROR (cannotResolveTypeOfExpression,          "Cannot resolve the type of this expression")
DECL_COMPILE_ERROR (wrongNumberOfComplexInitialisers,       "Too many initialisers for complex number")
DECL_COMPILE_ERROR (usingCannotBeReference,                 "Using declarations cannot be references")
DECL_COMPILE_ERROR (enumCannotBeEmpty,                      "An enum declaration must contain at least one item")
DECL_COMPILE_ERROR (expectedEnumMember,                     "Expected the name of one of the member of this enum")
DECL_COMPILE_ERROR (unknownEnumMember,                      "The enum '{0}' does not contain a member called '{1}'")
DECL_COMPILE_ERROR (useFloat64InsteadOfDouble,              "'double' is not a type name in Cmaj - did you mean `float64`?")

// Externals
DECL_COMPILE_ERROR (externalCannotHaveInitialiser,          "External variables cannot be given an initialiser value")
DECL_COMPILE_ERROR (externalNotAllowedInFunction,           "External constants cannot be declared inside a function")
DECL_COMPILE_ERROR (unresolvedExternal,                     "Failed to resolve external variable '{0}'")
DECL_COMPILE_ERROR (cannotApplyExternalVariableValue,       "Cannot apply value of type '{0}' to external variable '{1}'")
DECL_COMPILE_ERROR (externalOnlyAllowedOnStateVars,         "The 'external' flag can only be applied to state variables")
DECL_COMPILE_ERROR (missingExternalGeneratorProperty,       "The wave-generation annotation must provide a property '{0}'")
DECL_COMPILE_ERROR (illegalExternalGeneratorProperty,       "The value of annotation property '{0}' on variable '{1}' was out of range")
DECL_COMPILE_ERROR (noConstOnExternals,                     "External declarations do not require the 'const' keyword")
DECL_COMPILE_ERROR (unresolvedExternalFunction,             "Failed to resolve external function '{0}'")
DECL_COMPILE_ERROR (externalFunctionCannotHaveBody,         "An external function declaration cannot have a body")
DECL_COMPILE_ERROR (externalFunctionCannotBeGeneric,        "An external function cannot use generics")
DECL_COMPILE_ERROR (externalFunctionCannotUseParamType,     "An external function can only take primitive parameter types")
DECL_COMPILE_ERROR (externalFunctionsNotSupported,          "The target back-end does not support external functions")

// Function-related errors
DECL_COMPILE_ERROR (tooManyParameters,                      "Too many function parameters")
DECL_COMPILE_ERROR (duplicateFunction,                      "A function with matching parameters has already been defined")
DECL_COMPILE_ERROR (functionHasParams,                      "The {0}() function must not have any parameters")
DECL_COMPILE_ERROR (functionMustBeVoid,                     "The {0}() function must return 'void'")
DECL_COMPILE_ERROR (cannotCallFunction,                     "The {0}() function cannot be called from user code")
DECL_COMPILE_ERROR (cannotResolveFunctionOrCast,            "Could not resolve function or cast")
DECL_COMPILE_ERROR (voidFunctionCannotReturnValue,          "A void function cannot return a value")
DECL_COMPILE_ERROR (nonVoidFunctionMustReturnValue,         "This function must return a value of type '{0}'")
DECL_COMPILE_ERROR (functionReturnTypeCannotBeConst,        "Function return type cannot be const")
DECL_COMPILE_ERROR (cannotReturnReferenceType,              "Cannot return reference type")
DECL_COMPILE_ERROR (functionContainsAnInfiniteLoop,         "The function '{0}' contains at least one infinite loop")
DECL_COMPILE_ERROR (notAllControlPathsReturnAValue,         "Not all control paths in the function '{0}' return a value")
DECL_COMPILE_ERROR (functionCallsItselfRecursively,         "The function {0} calls itself recursively")
DECL_COMPILE_ERROR (functionsCallEachOtherRecursively,      "The functions {0} and {1} call each other recursively")
DECL_COMPILE_ERROR (recursiveFunctionCallSequence,          "Recursive call sequence via functions: {0}")
DECL_COMPILE_ERROR (cannotResolveGenericArgs,               "Could not resolve argument types for function call {0}")
DECL_COMPILE_ERROR (cannotResolveGenericParameter,          "Could not resolve generic parameter '{0}'")
DECL_COMPILE_ERROR (cannotResolveGenericFunction,           "Failed to resolve generic function call {0}")
DECL_COMPILE_ERROR (cannotResolveGenericWildcard,           "Could not find a value for '{0}' that satisfies all argument types")
DECL_COMPILE_ERROR (unresolvedAnnotation,                   "Cannot resolve annotation value as a compile-time constant")
DECL_COMPILE_ERROR (functionHasNoImplementation,            "This function has no implementation")

// Expression and statement errors
DECL_COMPILE_ERROR (wrongTypeForUnary,                      "Illegal type for unary operator")
DECL_COMPILE_ERROR (illegalTypeForOperator,                 "Illegal type for the '{0}' operator")
DECL_COMPILE_ERROR (illegalTypesForBinaryOperator,          "Illegal types for binary operator '{0}' ('{1}' and '{2}')")
DECL_COMPILE_ERROR (illegalOperatorForArray,                "The '{0}' operator can be applied to vector types, but not arrays")
DECL_COMPILE_ERROR (ternaryCannotBeVoid,                    "The ternary operator must return non-void values")
DECL_COMPILE_ERROR (ternaryTypesMustMatch,                  "Ternary operator branches have different types ('{0}' and '{1}')")
DECL_COMPILE_ERROR (ternaryCannotBeStatement,               "A ternary operator cannot be used as a statement")
DECL_COMPILE_ERROR (indexOutOfRange,                        "Index is out of range")
DECL_COMPILE_ERROR (expectedArrayIndex,                     "Expected an array index")
DECL_COMPILE_ERROR (loopCountMustBePositive,                "Number of loop iterations must be a positive integer")
DECL_COMPILE_ERROR (operatorNeedsAssignableTarget,          "The '{0}' operator must be given an assignable variable")
DECL_COMPILE_ERROR (assignmentToNonAssignableTarget,        "Expected an assignable target on the left of the '{0}' operator")
DECL_COMPILE_ERROR (cannotUseBracketOnEndpoint,             "Cannot use the bracket operator on this endpoint")
DECL_COMPILE_ERROR (rangeBasedForMustBeWrapType,            "A range-based-for loop must declare a variable with a 'wrap' type")
DECL_COMPILE_ERROR (preIncDecCollision,                     "Variables which have the ++ or -- operator applied can not be used twice within the same statement")
DECL_COMPILE_ERROR (initialiserListTooLong,                 "Initialiser list exceeds max length limit")
DECL_COMPILE_ERROR (identifierMustBeUnqualified,            "This identifier cannot have a namespace qualifier")
DECL_COMPILE_ERROR (assignmentInsideExpression,             "Assignment is not allowed inside an expression")
DECL_COMPILE_ERROR (endpointWriteInsideExpression,          "Writing to an endpoint is not allowed inside an expression")
DECL_COMPILE_ERROR (expressionHasNoEffect,                  "This constant expression will have no effect")
DECL_COMPILE_ERROR (resultOfOperatorIsUnused,               "The result of the '{0}' operator is unused")
DECL_COMPILE_ERROR (resultOfCastIsUnused,                   "The result of this cast is unused")
DECL_COMPILE_ERROR (comparisonAlwaysFalse,                  "Comparison is always false")
DECL_COMPILE_ERROR (comparisonAlwaysTrue,                   "Comparison is always true")
DECL_COMPILE_ERROR (divideByZero,                           "Divide-by zero is undefined behaviour")
DECL_COMPILE_ERROR (moduloZero,                             "Modulo zero is undefined behaviour")
DECL_COMPILE_ERROR (expectedStringLiteralAsArg2,            "Expected a string literal error message as the second argument")
DECL_COMPILE_ERROR (forwardBranchMustBeFirstStatement,      "Forward branches must be the first statement in the block")
DECL_COMPILE_ERROR (forwardBranchMustBeInt32,               "Forward branch condition must be an int32 value")

// Endpoint and processor definitions
DECL_COMPILE_ERROR (invalidEndpointSpecifier,               "Invalid endpoint specifier")
DECL_COMPILE_ERROR (cannotUseOutputAsConnectionSource,      "Cannot use graph output endpoint '{0}' as a connection source")
DECL_COMPILE_ERROR (cannotUseInputAsConnectionDestination,  "Cannot use graph input endpoint '{0}' as a connection destination")
DECL_COMPILE_ERROR (cannotConnectFromAnInput,               "The endpoint '{0}' is an input, so cannot be connected to '{1}'")
DECL_COMPILE_ERROR (cannotConnectToAnOutput,                "The endpoint '{1}' is an output, so cannot take an input from '{0}'")
DECL_COMPILE_ERROR (cannotConnect,                          "Cannot connect {0} ({1}) to {2} ({3})")
DECL_COMPILE_ERROR (cannotConnectExpressionToEventHandler,  "Expressions can only be connected to a stream or value endpoint")
DECL_COMPILE_ERROR (cannotConnectEndpointArrays,            "Cannot connect two endpoint arrays with different sizes")
DECL_COMPILE_ERROR (cannotHoistInputEndpoint,               "Cannot expose a child's input endpoint as an output")
DECL_COMPILE_ERROR (cannotHoistOutputEndpoint,              "Cannot expose a child's output endpoint as an input")
DECL_COMPILE_ERROR (incompatibleInputInterpolationTypes,    "The inputs to processor '{0}' must all use the same interpolation type")
DECL_COMPILE_ERROR (incompatibleOutputInterpolationTypes,   "The outputs from processor '{0}' must all use the same interpolation type")
DECL_COMPILE_ERROR (processorHasNoSuitableInputs,           "This processor has no suitable input endpoints")
DECL_COMPILE_ERROR (processorHasNoSuitableOutputs,          "This processor has no suitable output endpoints")
DECL_COMPILE_ERROR (processorNeedsAnOutput,                 "A processor must declare at least one output")
DECL_COMPILE_ERROR (mustBeOnlyOneEndpoint,                  "A processor can only be placed inside a chain if it has exactly one input and one output")
DECL_COMPILE_ERROR (cannotNameEndpointInChain,              "A processor that is chained between two others cannot specify an endpoint name")
DECL_COMPILE_ERROR (cannotChainConnectionWithMultiple,      "Cannot create a chained sequence of connections when multiple endpoints are specified")
DECL_COMPILE_ERROR (noSuchOperationOnEndpoint,              "No such operation is supported on an endpoint")
DECL_COMPILE_ERROR (noSuchOperationOnProcessor,             "No such operation is supported on a processor")
DECL_COMPILE_ERROR (feedbackInGraph,                        "Feedback cycle in graph: {0}")
DECL_COMPILE_ERROR (cannotFindMainProcessor,                "Cannot find a main processor or graph to use")
DECL_COMPILE_ERROR (multipleProcessorsMarkedAsMain,         "Multiple processors were marked as 'main'")
DECL_COMPILE_ERROR (cannotFindProcessor,                    "Cannot find processor: {0}")
DECL_COMPILE_ERROR (multipleSuitableMainCandidates,         "Cannot choose between multiple candidates as the main processor")
DECL_COMPILE_ERROR (onlyValueSpecialisationsSupported,      "Only value main processor specialisations are supported")

DECL_COMPILE_ERROR (endpointHasMultipleTypes,               "This endpoint has more than one type")
DECL_COMPILE_ERROR (noMatchForWildcardInput,                "No inputs were found that matched the wildcard '{0}'")
DECL_COMPILE_ERROR (noMatchForWildcardOutput,               "No outputs were found that matched the wildcard '{0}'")
DECL_COMPILE_ERROR (duplicateTypesInList,                   "Duplicate types found in type list: {0} and {1}")
DECL_COMPILE_ERROR (illegalTypeForEndpoint,                 "'{0}' is not a valid data type for this endpoint type")
DECL_COMPILE_ERROR (illegalTypeForTopLevelEndpoint,         "'{0}' is not a valid data type for a top level processor endpoint")
DECL_COMPILE_ERROR (noArraysOnArrayEndpoint,                "Endpoint arrays cannot use array data types")
DECL_COMPILE_ERROR (noMultipleTypesOnEndpoint,              "Multiple data types not supported by this endpoint type")
DECL_COMPILE_ERROR (cannotUseSlicesInTopLevel,              "Top level endpoints cannot include slices")
DECL_COMPILE_ERROR (notAnEndpointArray,                     "This endpoint is not an array")
DECL_COMPILE_ERROR (notANodeArray,                          "This node is not an array")
DECL_COMPILE_ERROR (cannotReadFromOutput,                   "Cannot read from an output")
DECL_COMPILE_ERROR (cannotReadFromEventInput,               "Event inputs must be handled in event callback functions, they cannot be read as expressions")
DECL_COMPILE_ERROR (cannotReadFromArrayEndpoint,            "Cannot read an array endpoint from a processor array - you must specify a processor array instance")
DECL_COMPILE_ERROR (delayLineMustBeConstant,                "A delay line length must be a constant")
DECL_COMPILE_ERROR (delayLineMustHaveIntLength,             "A delay line length must be an integer")
DECL_COMPILE_ERROR (delayLineTooShort,                      "A delay line length must be greater than zero")
DECL_COMPILE_ERROR (delayLineTooLong,                       "This exceeds the maximum delay line length ({0})")
DECL_COMPILE_ERROR (latencyMustBeConstantIntOrFloat,        "The processor.latency value must be declared as a constant integer or float")
DECL_COMPILE_ERROR (latencyOutOfRange,                      "This latency value is out of range")
DECL_COMPILE_ERROR (latencyOnlyForProcessor,                "The processor.latency value can only be declared in a processor")
DECL_COMPILE_ERROR (latencyAlreadyDeclared,                 "The processor.latency value must not be set more than once")
DECL_COMPILE_ERROR (ratioMustBeInteger,                     "Clock ratio must be an integer constant")
DECL_COMPILE_ERROR (ratioOutOfRange,                        "Clock ratio out of range")
DECL_COMPILE_ERROR (ratioMustBePowerOf2,                    "Clock ratio must be a power of 2")
DECL_COMPILE_ERROR (processorNeedsMainFunction,             "A processor must contain a main() function")
DECL_COMPILE_ERROR (isRunFunctionSupposedToBeMain,          "A processor must contain a main() function. Perhaps this 'run' function was intended to be 'main'?")
DECL_COMPILE_ERROR (multipleMainFunctions,                  "A processor cannot contain more than one main() function")
DECL_COMPILE_ERROR (mainFunctionMustCallAdvance,            "The main() function must call advance()")
DECL_COMPILE_ERROR (advanceIsNotAMethod,                    "The advance() function cannot be used as a method call")
DECL_COMPILE_ERROR (advanceHasNoArgs,                       "The advance() function does not take any arguments")
DECL_COMPILE_ERROR (invalidAdvanceArgumentType,             "The advance() function argument must be a node type")
DECL_COMPILE_ERROR (advanceCannotBeCalledHere,              "The advance() function cannot be called inside this function")
DECL_COMPILE_ERROR (resetWrongArguments,                    "The reset() function does not take any arguments")
DECL_COMPILE_ERROR (paramCannotContainSlice,                "functions that call advance() do not support parameters containing slices")
DECL_COMPILE_ERROR (endpointsCanOnlyBeUsedInMain,           "Endpoints can only be read or written by code that is called from the main() function")
DECL_COMPILE_ERROR (endpointsCannotBeUsedDuringInit,        "Endpoints cannot be read or written during init()")
DECL_COMPILE_ERROR (streamsCannotBeUsedInEventCallbacks,    "Streams cannot be used in event callback functions")
DECL_COMPILE_ERROR (valuesCannotBeUsedInEventCallbacks,     "Values cannot be used in event callback functions")
DECL_COMPILE_ERROR (noSuchInputEvent,                       "Event handler '{0}' does not match an event input")
DECL_COMPILE_ERROR (cannotUseVarFromOtherProcessor,         "Cannot reference a mutable variable belonging to another processor")
DECL_COMPILE_ERROR (propertiesOutsideProcessor,             "Processor properties are only valid inside a processor declaration")
DECL_COMPILE_ERROR (cannotAssignToProcessorProperties,      "Processor properties are constants, and cannot be modified")
DECL_COMPILE_ERROR (unknownProcessorProperty,               "Unknown processor property")
DECL_COMPILE_ERROR (endpointTypeCannotBeReference,          "Endpoint types cannot be references")
DECL_COMPILE_ERROR (endpointTypeCannotBeConst,              "Endpoint types cannot be const")
DECL_COMPILE_ERROR (endpointTypeNotResolved,                "Endpoint type not yet resolved")
DECL_COMPILE_ERROR (writeValueTypeNotResolved,              "Cannot determine the type of the value to be written to this endpoint")
DECL_COMPILE_ERROR (externalTypeNotResolved,                "Cannot determine the type at load time")
DECL_COMPILE_ERROR (eventFunctionInvalidType,               "Event '{0}' does not support type '{1}'")
DECL_COMPILE_ERROR (eventFunctionInvalidArguments,          "Event function arguments invalid")
DECL_COMPILE_ERROR (eventParamsCannotBeNonConstReference,   "Event parameters cannot be non-const references")
DECL_COMPILE_ERROR (inPlaceOperatorMustBeStatement,         "The in-place operator '{0}' must be used as a statement, not an expression")
DECL_COMPILE_ERROR (eventFunctionIndexInvalid,              "Event Handlers for event arrays need a first argument index integer type")
DECL_COMPILE_ERROR (cannotUseInputAsFunction,               "Cannot use an input as a function call")
DECL_COMPILE_ERROR (cannotUseOutputAsFunction,              "Cannot use an output as a function call")
DECL_COMPILE_ERROR (defaultParamsMustBeAtTheEnd,            "When a default parameter has been declared, all subsequent parameters must also have default values")
DECL_COMPILE_ERROR (wrongNumArgsForProcessor,               "Wrong number of arguments to instantiate processor '{0}'")
DECL_COMPILE_ERROR (wrongNumArgsForNamespace,               "Wrong number of arguments to instantiate namespace '{0}'")
DECL_COMPILE_ERROR (cannotUseProcessorWithoutArgs,          "The processor '{0}' cannot be used without providing arguments for its parameters")
DECL_COMPILE_ERROR (cannotUseNamespaceWithoutArgs,          "The namespace '{0}' cannot be used without providing arguments for its parameters")
DECL_COMPILE_ERROR (cannotUseProcessorInLet,                "The processor '{0}' cannot be used in a 'node' statement if it is also used directly")
DECL_COMPILE_ERROR (cannotReuseImplicitNode,                "An implicitly-created graph node cannot be used more than once: create a named instance instead")
DECL_COMPILE_ERROR (seemsToBeOldStyleEndpointWrite,         "Cannot perform a left-shift on an output - did you mean to use the '<-' operator instead of '<<'?")
DECL_COMPILE_ERROR (rightArrowInStatement,                  "The '->' operator can only be used in connection declarations - did you mean to use the '<-' operator instead?")
DECL_COMPILE_ERROR (functionOutOfScope,                     "Function is not visible from this scope")
DECL_COMPILE_ERROR (cannotWriteToOwnInput,                  "A module cannot write to its own input endpoints")
DECL_COMPILE_ERROR (nestedGraphNodesAreNotSupported,        "A connection cannot refer to nested graph nodes")


// Compiler-level and runtime errors
DECL_COMPILE_ERROR (emptyProgram,                           "Program is empty")
DECL_COMPILE_ERROR (invalidProgram,                         "Program is invalid")
DECL_COMPILE_ERROR (noProgramLoaded,                        "No program loaded")
DECL_COMPILE_ERROR (cannotGenerateIfLinked,                 "Cannot generate once a program has been linked")
DECL_COMPILE_ERROR (maximumStackSizeExceeded,               "Stack size limit exceeded - program requires {0}, maximum allowed is {1}")
DECL_COMPILE_ERROR (unsupportedSampleRate,                  "Unsupported sample rate")
DECL_COMPILE_ERROR (failedToCompile,                        "Failed to compile {0}")
DECL_COMPILE_ERROR (failedToLink,                           "Failed to link {0}")
DECL_COMPILE_ERROR (failedToJit,                            "Failed to construct jit {0}")

// Warnings
DECL_WARNING (indexHasRuntimeOverhead,                      "Performance warning: using an array index of type 'int' will add a runtime range check. To avoid this, use a `wrap<>` or `clamp<>` type for the index. To hide this warning, use .at() instead of []")
DECL_WARNING (localVariableShadow,                          "'{0}' shadows another declaration with the same name")

DECL_NOTE (seePreviousDeclaration,                          "See previous declaration")
DECL_NOTE (seeSourceOfLocalData,                            "See context where local data is assigned")
DECL_NOTE (seePossibleCandidate,                            "See possible candidate")


//==============================================================================
//==============================================================================
#undef DECLARE_MESSAGE
#undef DECL_COMPILE_ERROR
#undef DECL_RUNTIME_ERROR
#undef DECL_WARNING
#undef DECL_NOTE

//==============================================================================
private:
    template <typename... Substrings>
    static DiagnosticMessage createMessage (DiagnosticMessage::Category category, FullCodeLocation location,
                                            DiagnosticMessage::Type type, const char* text, Substrings&&... substrings)
    {
        std::string description (text);
        std::vector<std::string> asStrings { std::string (substrings)... };

        for (size_t i = 0; i < asStrings.size(); ++i)
        {
            auto s = asStrings[i];

            if (s.length() > 128)
                s = s.substr (0, 128) + "...";

            description = choc::text::replace (description, "{" + std::to_string (i) + "}", s);
        }

        return DiagnosticMessage::create (choc::text::trim (description), std::move (location), type, category);
    }

    template <typename... Substrings>
    static DiagnosticMessage createMessage (DiagnosticMessage::Category category, DiagnosticMessage::Type type,
                                            const char* text, Substrings&&... substrings)
    {
        return createMessage (category, FullCodeLocation(), type, text, std::forward<Substrings> (substrings)...);
    }

    static constexpr int countSubstrings (const char* text)
    {
        for (int num = 0;; ++num)
        {
            auto numMatches = numberOfMatchesOfArg (text, num);

            if (numMatches < 0)
                return -1;

            if (numMatches == 0)
                return num;
        }
    }

    static constexpr int numberOfMatchesOfArg (const char* text, int index)
    {
        int matches = 0;

        for (; *text != 0; ++text)
        {
            if (*text == '{')
            {
                ++text;
                if (*text < '0' || *text > '9')  return -1;
                if (index == (*text - '0'))      ++matches;
                if (*++text != '}')              return -1;
            }
        }

        return matches;
    }
};
} // namespace cmaj
