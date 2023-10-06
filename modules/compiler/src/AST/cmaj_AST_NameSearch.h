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


struct NameSearch
{
    choc::SmallVector<ref<Object>, 8> itemsFound;
    PooledString nameToFind;
    bool stopAtFirstScopeWithResults = false;
    bool searchOnlySpecifiedScope = false;
    int requiredNumFunctionParams = -1;

    bool findVariables           = true,
         findTypes               = true,
         findFunctions           = true,
         findNamespaces          = true,
         findProcessors          = true,
         findNodes               = false,
         findEndpoints           = true,
         onlyFindLocalVariables  = false;

    void performSearch (Object& object, ptr<const Statement> statementToSearchUpTo)
    {
        CMAJ_ASSERT (! nameToFind.empty());
        auto originalNumItems = itemsFound.size();
        bool hasProcessedScopeBlockYet = false;

        for (ref<Object> s = object;;)
        {
            if (auto scopeBlock = s->getAsScopeBlock())
            {
                hasProcessedScopeBlockYet = true;
                scopeBlock->performLocalNameSearch (*this, statementToSearchUpTo);
            }
            else if (auto fn = s->getAsFunction())
            {
                if (! hasProcessedScopeBlockYet && fn->getMainBlock() != nullptr)
                {
                    hasProcessedScopeBlockYet = true;
                    s = *fn->getMainBlock();
                    s->performLocalNameSearch (*this, statementToSearchUpTo);
                }
                else
                {
                    fn->performLocalNameSearch (*this, {});
                }
            }
            else if (auto loop = s->getAsLoopStatement())
            {
                loop->performLocalNameSearch (*this, {});
            }
            else if (auto module = s->getAsModuleBase())
            {
                if (onlyFindLocalVariables)
                    break;

                if (module != nullptr)
                    module->performLocalNameSearch (*this, {});

                if (searchOnlySpecifiedScope)
                    break;
            }

            auto parent = s->getParentScope().get();

            if (parent == nullptr)
                break;

            if (stopAtFirstScopeWithResults && itemsFound.size() > originalNumItems)
                break;

            if (auto statement = s->getAsStatement())
                statementToSearchUpTo = *statement;

            s = *parent;
        }
    }

    void addResult (Object& o)
    {
        for (auto& i : itemsFound)
            if (i == o)
                return;

        itemsFound.push_back (o);
    }

    void addFirstMatching (ListProperty& list)
    {
        addFirstMatching (list, nameToFind);
    }

    void addFirstMatching (ListProperty& list, PooledString targetName)
    {
        if (auto o = list.findObjectWithName (targetName))
            addResult (*o);
    }
};
