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

static void mergeNamespaces (AST::Namespace& target, AST::Namespace& source)
{
    target.subModules.moveListItems (source.subModules);
    target.constants.moveListItems (source.constants);
    target.imports.moveListItems (source.imports);
    target.aliases.moveListItems (source.aliases);
    target.functions.moveListItems (source.functions);
    target.structures.moveListItems (source.structures);
    target.enums.moveListItems (source.enums);
    target.staticAssertions.moveListItems (source.staticAssertions);

    if (source.isSystem)
        target.isSystem = true;

    if (source.comment != nullptr)
        if (target.getComment() == nullptr || target.getComment()->getContent().empty())
            target.comment.referTo (source.comment.get());

    if (auto sourceAnnotation = AST::castTo<AST::Annotation> (source.annotation))
    {
        if (auto destAnnotation = AST::castTo<AST::Annotation> (target.annotation))
            destAnnotation->mergeFrom (*sourceAnnotation, false);
        else
            target.annotation.setChildObject (*sourceAnnotation);
    }
}

static bool mergeFirstPairOfDuplicateNamespaces (AST::Namespace& ns)
{
    bool anyDone = false;

    for (size_t i = 0; i < ns.subModules.size(); ++i)
    {
        if (auto ns1 = AST::castTo<AST::Namespace> (ns.subModules[i]))
        {
            if (! ns1->isGenericOrParameterised())
            {
                anyDone = mergeFirstPairOfDuplicateNamespaces (*ns1) || anyDone;
                const auto& name1 = ns1->name.get();

                for (size_t j = i + 1; j < ns.subModules.size(); ++j)
                {
                    if (ns.subModules[j].hasName (name1))
                    {
                        if (auto ns2 = AST::castTo<AST::Namespace> (ns.subModules[j]))
                        {
                            if (! ns2->isGenericOrParameterised())
                            {
                                mergeNamespaces (*ns1, *ns2);
                                ns.subModules.remove (j);

                                if (ns2->hasAnyReferrers())
                                    ns2->replaceWith (*ns1);

                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    return anyDone;
}

void mergeDuplicateNamespaces (AST::Namespace& parentNamespace)
{
    while (mergeFirstPairOfDuplicateNamespaces (parentNamespace))
    {}
}

}
