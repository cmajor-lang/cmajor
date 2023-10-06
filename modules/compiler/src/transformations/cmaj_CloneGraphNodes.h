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

static inline void cloneGraphNodes (AST::Program& program)
{
    struct CloneGraphNodes  : public passes::PassAvoidingGenericFunctionsAndModules
    {
        using super = PassAvoidingGenericFunctionsAndModules;
        using super::visit;

        CloneGraphNodes (AST::Program& p) : super (p) {}

        void visit (AST::GraphNode& node) override
        {
            auto oldMultiplier = nodeMultiplier;
            nodeMultiplier *= node.getClockMultiplier();

            super::visit (node);

            auto processorWithRate = findOrCreateProcessorWithRate (node.getProcessorType(), nodeMultiplier);

            if (processorWithRate != node.getProcessorType())
            {
                node.processorType.createReferenceTo (processorWithRate);

                node.getParentModule().visitObjectsInScope ([&node, processorWithRate] (AST::Object& s)
                {
                    if (auto e = s.getAsEndpointInstance())
                        if (! e->node.hasDefaultValue())
                            if (std::addressof (e->getNode()) == std::addressof (node))
                                if (! e->endpoint.hasDefaultValue())
                                    e->endpoint.replaceWith (*processorWithRate->findEndpointWithName (e->getResolvedEndpoint().getName()));
                });
            }

            nodeMultiplier = oldMultiplier;
        }

        ptr<AST::ProcessorBase> findOrCreateProcessorWithRate (ptr<AST::ProcessorBase> processor, double multiplier)
        {
            auto& processorInstances = processorMultipliers[processor];

            if (processorInstances.empty())
            {
                processorInstances[multiplier] = processor;
                return processor;
            }

            auto processorInstance = processorInstances.find (multiplier);

            if (processorInstance != processorInstances.end())
                return processorInstance->second;

            auto& clone = AST::createClonedSiblingModule (*processor, false);
            processorInstances[multiplier] = clone;

            return clone;
        }

        double nodeMultiplier = 1.0;

        using ProcessorInstancesMapType = std::map<double, ptr<AST::ProcessorBase>>;

        std::map<ptr<AST::ProcessorBase>, ProcessorInstancesMapType> processorMultipliers;
    };

    CloneGraphNodes visitor (program);
    visitor.visitObject (program.getMainProcessor());
}

}
