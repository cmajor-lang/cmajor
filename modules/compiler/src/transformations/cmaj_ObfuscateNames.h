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

/// Mangles the names of various types of object
void obfuscateNames (AST::Program& p)
{
    struct Obfuscator
    {
        Obfuscator (AST::Program& prog) : program (prog) {}

        void run()
        {
            if (auto mainProcessor = program.findMainProcessor())
            {
                addObjectToLeave (*mainProcessor);

                for (auto p = mainProcessor->findParentNamespace(); p != nullptr; p = p->findParentNamespace())
                    addObjectToLeave (*p);

                for (auto& f : mainProcessor->functions.iterateAs<AST::Function>())
                    if (f.isExportedFunction())
                        addObjectToLeave (f);

                for (auto& m : program.getTopLevelModules())
                {
                    m->visitObjectsInScope ([&] (AST::Object& o)
                    {
                        if (auto v = o.getAsVariableDeclaration())
                        {
                            if (v->isExternal)
                            {
                                addObjectToLeave (*v);

                                if (auto t = v->getType())
                                    addObjectToLeave (AST::castToRefSkippingReferences<AST::TypeBase> (t));
                            }
                        }
                    });
                }

                for (auto& e : mainProcessor->endpoints.iterateAs<AST::EndpointDeclaration>())
                {
                    addObjectToLeave (e);

                    for (auto& t : e.getDataTypes())
                        addObjectToLeave (AST::castToRefSkippingReferences<AST::TypeBase> (t));
                }

                for (auto& m : program.getTopLevelModules())
                    findAllNames (m);

                addNameToLeave (program.allocator.strings.mainFunctionName);
                addNameToLeave (program.allocator.strings.userInitFunctionName);
                addNameToLeave (program.allocator.strings.systemInitFunctionName);
                addNameToLeave (program.allocator.strings.systemAdvanceFunctionName);
                addNameToLeave (program.allocator.strings.rootNamespaceName);
                addNameToLeave (program.allocator.strings.consoleEndpointName);
                addNameToLeave (program.allocator.strings.rootNamespaceName);
                addNameToLeave (getStdLibraryNamespaceName());
                addNameToLeave (getIntrinsicsNamespaceName());
                addNameToLeave ("processor");
                addNameToLeave ("id");

                // Annoyingly, any unresolved identifiers in the code have to be
                // conservatively treated as unchangable, because we can't know whether
                // they're going to reference some non-obfuscated bit of the program
                for (auto& m : program.getTopLevelModules())
                {
                    m->visitObjectsInScope ([&] (AST::Object& o)
                    {
                        if (auto i = o.getAsIdentifier())
                            addNameToLeave (i->name.get());
                    });
                }

                for (auto& name : allNames)
                    newNames[name] = createNextName();

                for (auto& m : program.getTopLevelModules())
                    if (! m->isSystemModule())
                        replaceNames (m);
            }
        }

        void findAllNames (AST::ModuleBase& module)
        {
            module.visitObjectsInScope ([&] (AST::Object& o)
            {
                if (auto v = o.getAsVariableDeclaration())     addName (v->name);
                else if (auto f = o.getAsFunction())           addName (f->name);
                else if (auto a = o.getAsAlias())              addName (a->name);
                else if (auto gm = o.getAsGetStructMember())   addName (gm->member);
                else if (auto mb = o.getAsModuleBase())        addName (mb->name);
                else if (auto s = o.getAsStructType())
                {
                    addName (s->name);

                    for (auto& mn : s->memberNames)
                        if (auto sp = mn->getAsStringProperty())
                            addName (*sp);
                }
                else if (auto e = o.getAsEnumType())
                {
                    addName (e->name);

                    for (auto& mn : e->items)
                        if (auto sp = mn->getAsStringProperty())
                            addName (*sp);
                }
            });
        }

        void replaceNames (AST::ModuleBase& module)
        {
            module.visitObjectsInScope ([&] (AST::Object& o)
            {
                if (objectsToLeave.find (std::addressof (o)) != objectsToLeave.end())
                    return;

                if (auto v = o.getAsVariableDeclaration())
                {
                    if (v->isLocal() && ! v->isParameter())
                    {
                        if (newNames.find (std::string (v->name.get())) != newNames.end())
                            v->name.reset();
                    }
                    else
                    {
                        replaceName (v->name);
                    }
                }
                else if (auto f = o.getAsFunction())
                {
                    if (! f->isEventHandler)
                        replaceName (f->name);
                }
                else if (auto s = o.getAsStructType())
                {
                    if (replaceName (s->name)) // if the struct itself can't be renamed, we must also leave the members
                        for (auto& mn : s->memberNames)
                            if (auto sp = mn->getAsStringProperty())
                                replaceName (*sp);
                }
                else if (auto a = o.getAsAlias())        replaceName (a->name);
                else if (auto m = o.getAsModuleBase())   replaceName (m->name);
                else if (auto i = o.getAsIdentifier())   replaceName (i->name);
                else if (auto e = o.getAsEnumType())
                {
                    if (replaceName (e->name)) // if the enum itself can't be renamed, we must also leave the members
                        for (auto& mn : e->items)
                            if (auto sp = mn->getAsStringProperty())
                                replaceName (*sp);
                }
                else if (auto gm = o.getAsGetStructMember())
                {
                    if (auto structType = gm->getResultType())
                        if (objectsToLeave.find (structType.get()) == objectsToLeave.end())
                            if (! AST::getContext (structType).isInSystemModule())
                                replaceName (gm->member);
                }
            });
        }

        bool replaceName (AST::StringProperty& property)
        {
            if (property.get().empty())
                return false;

            auto found = newNames.find (std::string (property.get()));

            if (found != newNames.end())
            {
                property = property.getStringPool().get (found->second);
                return true;
            }

            return false;
        }

        std::string createNextName()
        {
            std::string name;
            static constexpr std::string_view lookup = "qwertyuiopasdfghjklzxcvbnmMNBVCXZASDFGHJKLPOIUYTREWQ";

            for (auto index = ++nextNameIndex; index != 0; index = index / lookup.length())
                name += lookup[index % lookup.length()];

            if (allNames.find (name) != allNames.end())
                return createNextName();

            return name;
        }

        void addName (AST::PooledString name)          { allNames.insert (std::string (name.get())); }
        void addName (std::string_view name)           { allNames.insert (std::string (name)); }
        void addNameToLeave (std::string_view name)    { allNames.erase (std::string (name)); }
        void addObjectToLeave (const AST::Object& o)   { objectsToLeave.insert (std::addressof (o)); }

        AST::Program& program;
        std::unordered_set<const AST::Object*> objectsToLeave;
        std::unordered_set<std::string> allNames;
        std::unordered_map<std::string, std::string> newNames;
        size_t nextNameIndex = 0;
    };

    Obfuscator ob (p);

    // obfuscation isn't "fit-for-purpose" yet.
    // The problem is that this algorithm tries to be conservative, and avoid changing any names
    // that are still in the code as unresolved Identifier objects. BUT... this fails in some cases
    // where the thing it refers to has other things dependent on it, but these dependent things
    // can't yet be identified because the program hasn't yet been fully linked...
    //
    // Really the only solution is to not attempt to obfuscate code with any unresolved symbols,
    // and instead only do this to a fully loaded program, where we can correctly identify all the
    // externally-visible objects whose names must be left
    CMAJ_ASSERT_FALSE;
    // ob.run();
}

}
