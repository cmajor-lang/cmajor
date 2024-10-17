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

namespace cmaj::transformations
{

//==============================================================================
static void createHoistedEndpointConnections (AST::Program& program)
{
    struct Hoister  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        Hoister (AST::Allocator& a) : super (a) {}

        CMAJ_DO_NOT_VISIT_CONSTANTS

        void visit (AST::EndpointDeclaration& e) override
        {
            super::visit (e);
            resolveHoistedEndpoint (e);
        }

        void resolveHoistedEndpoint (AST::EndpointDeclaration& outerEndpoint)
        {
            if (! outerEndpoint.isHoistedEndpoint())
                return;

            auto& path = AST::castToRef<AST::HoistedEndpointPath> (outerEndpoint.childPath);
            CMAJ_ASSERT (path.wildcardPattern.hasDefaultValue());

            if (path.pathSections.size() < 2)
                throwError (path, Errors::expectedStreamTypeOrEndpoint());

            resolveHoistedEndpoint (AST::castToRef<AST::Graph> (outerEndpoint.getParentProcessor()),
                                    outerEndpoint, path.pathSections.getAsObjectList());
        }

        void resolveHoistedEndpoint (AST::Graph& outerEndpointParentGraph,
                                     AST::EndpointDeclaration& outerEndpoint,
                                     choc::span<ref<AST::Object>> childPath)
        {
            CMAJ_ASSERT (! childPath.empty());

            if (AST::castTo<AST::GetElement> (childPath.front()) != nullptr)
                return;

            CMAJ_ASSERT (childPath.size() > 1);
            auto& topLevelNode = AST::castToRefSkippingReferences<AST::GraphNode> (childPath.front());

            if (childPath.size() == 2)
            {
                auto& childEndpoint = AST::castToRefSkippingReferences<AST::EndpointDeclaration> (childPath.back());
                createConnection (outerEndpointParentGraph, topLevelNode, outerEndpoint, childEndpoint);
            }
            else
            {
                auto& childNode = AST::castToRefSkippingReferences<AST::GraphNode> (childPath[1]);
                auto& intermediateParentGraph = childNode.getParentGraph();
                auto& intermediate = intermediateParentGraph.allocateChild<AST::EndpointDeclaration>();
                intermediate.setName (intermediate.getStringPool().get (AST::createUniqueName ("_hoisted", intermediateParentGraph.endpoints)));
                intermediate.isInput.set (outerEndpoint.isInput);
                intermediateParentGraph.endpoints.addReference (intermediate);

                resolveHoistedEndpoint (intermediateParentGraph, intermediate, childPath.tail());
                createConnection (outerEndpointParentGraph, topLevelNode, outerEndpoint, intermediate);
            }
        }

        void createConnection (AST::Graph& topLevelParentGraph,
                               AST::GraphNode& topLevelNode,
                               AST::EndpointDeclaration& outerEndpoint,
                               AST::EndpointDeclaration& childEndpoint)
        {
            outerEndpoint.childPath.reset();
            outerEndpoint.endpointType.set (childEndpoint.endpointType);

            for (auto& type : childEndpoint.dataTypes.getAsObjectList())
                outerEndpoint.dataTypes.addReference (type);

            if (auto a = AST::castTo<AST::Annotation> (childEndpoint.annotation))
            {
                if (outerEndpoint.annotation == nullptr)
                    outerEndpoint.annotation.referTo (*a);
                else
                    AST::castToRef<AST::Annotation> (outerEndpoint.annotation).mergeFrom (*a, false);
            }

            if (auto endpointSize = childEndpoint.arraySize.getObject())
                outerEndpoint.arraySize.referTo (*endpointSize);
            else if (auto nodeSize = topLevelNode.arraySize.getObject())
                outerEndpoint.arraySize.referTo (*nodeSize);

            if (! outerEndpoint.isInput)
                AST::addConnection (topLevelParentGraph, topLevelNode, childEndpoint, nullptr, outerEndpoint);
            else
                AST::addConnection (topLevelParentGraph, nullptr, outerEndpoint, topLevelNode, childEndpoint);
        }
    };

    Hoister (program.allocator).visitObject (program.rootNamespace);
}

}
