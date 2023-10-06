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

#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <complex>
#include <random>
#include <optional>
#include <functional>

#include "../../include/cmaj_ErrorHandling.h"

#include "choc/memory/choc_ObjectPointer.h"
#include "choc/memory/choc_ObjectReference.h"
#include "choc/memory/choc_PoolAllocator.h"
#include "choc/memory/choc_VariableLengthEncoding.h"
#include "choc/memory/choc_Endianness.h"
#include "choc/memory/choc_xxHash.h"
#include "choc/audio/choc_Oscillators.h"
#include "choc/audio/choc_SincInterpolator.h"
#include "choc/audio/choc_SampleBufferUtilities.h"

#include "../diagnostics/cmaj_ErrorList.h"

#include "../../../../cmajor/include/cmajor/API/cmaj_Program.h"
#include "../../../../cmajor/include/cmajor/API/cmaj_Endpoints.h"
#include "../../../../cmajor/include/cmajor/API/cmaj_ExternalVariables.h"
#include "../../../../cmajor/include/cmajor/API/cmaj_SourceFiles.h"
#include "../../../../cmajor/include/cmajor/API/cmaj_DiagnosticMessages.h"
#include "../../../../cmajor/include/cmajor/API/cmaj_BuildSettings.h"

#include "cmaj_EnumList.h"
#include "cmaj_IdentifierPath.h"

namespace cmaj
{

static constexpr double pi = 3.141592653589793238;
static constexpr double twoPi = 2 * pi;

template <typename Type>
using ptr = choc::ObjectPointer<Type>;

template <typename Type>
using ref = choc::ObjectReference<Type>;


#define CMAJ_AST_CLASSES(X) \
  X(Advance) \
  X(Alias) \
  X(Annotation) \
  X(ArrayType) \
  X(Assignment) \
  X(BinaryOperator) \
  X(BoundedType) \
  X(BracketedSuffix) \
  X(BracketedSuffixTerm) \
  X(BreakStatement) \
  X(CallOrCast) \
  X(Cast) \
  X(ChevronedSuffix) \
  X(Comment) \
  X(Connection) \
  X(ConnectionIf) \
  X(ConnectionList) \
  X(ConstantAggregate) \
  X(ConstantBool) \
  X(ConstantComplex32) \
  X(ConstantComplex64) \
  X(ConstantEnum) \
  X(ConstantFloat32) \
  X(ConstantFloat64) \
  X(ConstantInt32) \
  X(ConstantInt64) \
  X(ConstantString) \
  X(ContinueStatement) \
  X(DotOperator) \
  X(EndpointDeclaration) \
  X(EndpointInstance) \
  X(EnumType) \
  X(ExpressionList) \
  X(ForwardBranch) \
  X(Function) \
  X(FunctionCall) \
  X(GetArraySlice) \
  X(GetElement) \
  X(GetStructMember) \
  X(Graph) \
  X(GraphNode) \
  X(HoistedEndpointPath) \
  X(Identifier) \
  X(IfStatement) \
  X(InPlaceOperator) \
  X(LoopStatement) \
  X(MakeConstOrRef) \
  X(NamedReference) \
  X(Namespace) \
  X(NamespaceSeparator) \
  X(NoopStatement) \
  X(PreOrPostIncOrDec) \
  X(PrimitiveType) \
  X(Processor) \
  X(ProcessorProperty) \
  X(ReadFromEndpoint) \
  X(Reset) \
  X(ReturnStatement) \
  X(ScopeBlock) \
  X(StateUpcast) \
  X(StaticAssertion) \
  X(StructType) \
  X(TernaryOperator) \
  X(TypeMetaFunction) \
  X(UnaryOperator) \
  X(ValueMetaFunction) \
  X(VariableDeclaration) \
  X(VariableReference) \
  X(VectorType) \
  X(WriteToEndpoint) \

#define CMAJ_CLASSES_NEEDING_CAST(X) \
  CMAJ_AST_CLASSES(X) \
  X(ConstantValueBase) \
  X(Expression) \
  X(ModuleBase) \
  X(ProcessorBase) \
  X(Statement) \
  X(TypeBase) \
  X(ValueBase) \

template <typename Target, typename Source> Target* castObject (Source&);
template <typename Target, typename Source> const Target* castObject (const Source&);


//==============================================================================
// Tons of cross-references between the classes in here make it much easier to put them
// all inside a massive struct rather than a namespace, to avoid loads of pre-declarations
struct AST
{
    //==============================================================================
    struct Allocator;
    struct IntegerRange;

    struct Property;
    struct Object;
    struct Expression;
    struct ModuleBase;
    struct Program;

    struct ObjectContext;
    struct ValueBase;
    struct ConstantValueBase;
    struct ProcessorBase;
    struct TypeBase;
    struct Statement;
    struct Visitor;
    struct SignatureBuilder;
    struct SideEffects;

    template <typename ObjectType>
    using ObjectRefVector = choc::SmallVector<ref<ObjectType>, 8>;

    using RemappedObjects = std::unordered_map<const Object*, Object*>;

    #define CMAJ_PREDECLARE_CLASS(name)   struct name;
    CMAJ_AST_CLASSES(CMAJ_PREDECLARE_CLASS)
    #undef CMAJ_PREDECLARE_CLASS

    struct SyntaxTreeOptions
    {
        cmaj::SyntaxTreeOptions options;
        std::unordered_map<const AST::Object*, int32_t> objectIDs;

        int32_t getObjectID (const AST::Object& o) const
        {
            auto found = objectIDs.find (std::addressof (o));
            CMAJ_ASSERT (found != objectIDs.end());
            return found->second;
        }

        void generateIDs (AST::Object& root)
        {
            struct IDGenerator  : public Visitor
            {
                IDGenerator (SyntaxTreeOptions& o, Allocator& a) : Visitor (a), opts (o) {}

                void visitObject (AST::Object& o) override
                {
                    opts.objectIDs[std::addressof (o)] = ++nextID;
                    Visitor::visitObject (o);
                }

                SyntaxTreeOptions& opts;
                int32_t nextID = 0;
            };

            IDGenerator gen (*this, root.context.allocator);
            gen.visitObject (root);
        }
    };

    #include "cmaj_AST_StringPool.h"
    #include "cmaj_AST_Properties.h"
    #include "cmaj_AST_Casts.h"
    #include "cmaj_AST_Utilities.h"
    #include "cmaj_AST_Intrinsics.h"
    #include "cmaj_AST_NameSearch.h"
    #include "cmaj_AST_Object.h"
    #include "cmaj_AST_Allocator.h"
    #include "cmaj_AST_Classes.h"
    #include "cmaj_AST_Classes_Syntax.h"
    #include "cmaj_AST_Classes_Processors.h"
    #include "cmaj_AST_Classes_Types.h"
    #include "cmaj_AST_Classes_Values.h"
    #include "cmaj_AST_Classes_Constants.h"
    #include "cmaj_AST_Visitor.h"
    #include "cmaj_AST_TypeRules.h"
    #include "cmaj_AST_Builder.h"
    #include "cmaj_AST_Externals.h"
    #include "cmaj_AST_Program.h"
};

inline AST::Allocator& AST::getAllocator (Object& o)    { return o.context.allocator; }

template <> inline std::optional<int32_t>              AST::ConstantValueBase::castToPrimitive<int32_t>() const              { return getAsInt32(); }
template <> inline std::optional<int64_t>              AST::ConstantValueBase::castToPrimitive<int64_t>() const              { return getAsInt64(); }
template <> inline std::optional<bool>                 AST::ConstantValueBase::castToPrimitive<bool>() const                 { return getAsBool(); }
template <> inline std::optional<float>                AST::ConstantValueBase::castToPrimitive<float>() const                { return getAsFloat32(); }
template <> inline std::optional<double>               AST::ConstantValueBase::castToPrimitive<double>() const               { return getAsFloat64(); }
template <> inline std::optional<std::complex<float>>  AST::ConstantValueBase::castToPrimitive<std::complex<float>>() const  { return getAsComplex32(); }
template <> inline std::optional<std::complex<double>> AST::ConstantValueBase::castToPrimitive<std::complex<double>>() const { return getAsComplex64(); }

//==============================================================================
#define CMAJ_DECLARE_CASTS(Class) \
  template <> inline AST::Class*       castObject <AST::Class,       AST::Object>       (AST::Object& o)         { return o.getAs ## Class(); } \
  template <> inline const AST::Class* castObject <AST::Class,       AST::Object>       (const AST::Object& o)   { return o.getAs ## Class(); } \
  template <> inline const AST::Class* castObject <AST::Class,       const AST::Object> (const AST::Object& o)   { return o.getAs ## Class(); } \
  template <> inline const AST::Class* castObject <const AST::Class, const AST::Object> (const AST::Object& o)   { return o.getAs ## Class(); } \
  template <> inline const AST::Class* castObject <const AST::Class, AST::Object>       (const AST::Object& o)   { return o.getAs ## Class(); } \
  template <> inline const AST::Class* castObject <const AST::Class, AST::Object>       (AST::Object& o)         { return o.getAs ## Class(); }

CMAJ_CLASSES_NEEDING_CAST(CMAJ_DECLARE_CASTS)
#undef CMAJ_DECLARE_CASTS

template <> inline AST::Object*       castObject <AST::Object,       AST::Object>       (AST::Object& o)         { return std::addressof (o); }
template <> inline const AST::Object* castObject <AST::Object,       AST::Object>       (const AST::Object& o)   { return std::addressof (o); }
template <> inline const AST::Object* castObject <AST::Object,       const AST::Object> (const AST::Object& o)   { return std::addressof (o); }
template <> inline const AST::Object* castObject <const AST::Object, const AST::Object> (const AST::Object& o)   { return std::addressof (o); }
template <> inline const AST::Object* castObject <const AST::Object, AST::Object>       (const AST::Object& o)   { return std::addressof (o); }
template <> inline const AST::Object* castObject <const AST::Object, AST::Object>       (AST::Object& o)         { return std::addressof (o); }

//==============================================================================
template <typename ObjectOrContext>
inline DiagnosticMessage DiagnosticMessage::withContext (const ObjectOrContext& c) const
{
    return withLocation (AST::getContext (c).getFullLocation());
}

template <typename ObjectOrContext>
[[noreturn]] inline void throwError (const ObjectOrContext& errorContext, DiagnosticMessage m, bool isStaticAssertion)
{
    AST::throwError (AST::getContext (errorContext), std::move (m), isStaticAssertion);
}

} // namespace cmaj

namespace std
{
    template <> struct hash<cmaj::AST::PooledString>
    {
        size_t operator() (cmaj::AST::PooledString s) const noexcept    { return s.hash(); }
    };
}

#include "../codegen/cmaj_ProgramPrinter.h"
