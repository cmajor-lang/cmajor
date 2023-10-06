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


struct Program  : public choc::com::ObjectWithAtomicRefCount<cmaj::ProgramInterface, Program>
{
    Program (bool parseComments = false)
         : rootNamespace (allocator.createNamespace (allocator.strings.rootNamespaceName)),
           parsingComments (parseComments)
    {
        rootNamespace.isSystem = true;
    }

    virtual ~Program() = default;

    void parse (const SourceFile&, bool isSystemModule);

    choc::com::String* parse (const char* filename, const char* fileContent, size_t fileContentSize) override
    {
        return catchAllErrorsAsJSON (false, [&]
        {
            auto& code = allocator.sourceFileList.add (filename != nullptr ? std::string (filename) : std::string(),
                                                       fileContent != nullptr && fileContentSize != 0 ? std::string (fileContent, fileContentSize) : std::string(),
                                                       false);
            parse (code, false);
            codeHash.addInput (code.content);

        });
    }

    /// Adds the standard library, and if the program has already been loaded and is
    /// mashed-up, reparses it from the original source files
    bool prepareForLoading()
    {
        if (needsReparsing)
            if (! reparse())
                return false;

        addStandardLibraryCode();
        needsReparsing = true;
        return true;
    }

    auto getTopLevelModules() const         { return rootNamespace.getSubModules(); }

    std::vector<ref<AST::ProcessorBase>> getAllProcessors() const
    {
        std::vector<ref<AST::ProcessorBase>> result;

        rootNamespace.visitAllModules (false, [&] (AST::ModuleBase& m)
        {
            if (auto p = m.getAsProcessorBase())
                result.push_back (*p);
        });

        return result;
    }

    template <typename Visitor>
    void visitAllModules (bool avoidGenericsAndParameterised, Visitor&& visitor) const
    {
        rootNamespace.visitAllModules (avoidGenericsAndParameterised, visitor);
    }

    template <typename Visitor>
    void visitAllFunctions (bool avoidGenericsAndParameterised, Visitor&& visitor) const
    {
        rootNamespace.visitAllFunctions (avoidGenericsAndParameterised, visitor);
    }

    ptr<AST::ProcessorBase> findMainProcessor() const
    {
        return mainProcessor;
    }

    AST::ProcessorBase& getMainProcessor() const
    {
        return *findMainProcessor();
    }

    void setMainProcessor (AST::ProcessorBase& p)
    {
        CMAJ_ASSERT (p.isChildOf (rootNamespace));
        mainProcessor = p;
    }

    void resetMainProcessor()
    {
        mainProcessor = nullptr;
    }

    std::vector<ref<AST::VariableDeclaration>> getAllExternals()
    {
        std::vector<ref<AST::VariableDeclaration>> results;

        rootNamespace.visitAllModules (true, [&results] (const AST::ModuleBase& m)
        {
            if (auto ns = m.getAsNamespace())
                for (auto& c : ns->constants)
                    if (auto v = AST::castToVariableDeclaration (c))
                        if (v->isExternal)
                            results.push_back (*v);

            if (auto ns = m.getAsProcessorBase())
                for (auto& c : ns->stateVariables)
                    if (auto v = AST::castToVariableDeclaration (c))
                        if (v->isExternal)
                            results.push_back (*v);
        });

        return results;
    }

    choc::com::String* getSyntaxTree (const cmaj::SyntaxTreeOptions& options) override
    {
        ptr<AST::ModuleBase> mod = rootNamespace;

        if (options.namespaceOrModule != nullptr && ! std::string_view (options.namespaceOrModule).empty())
        {
            mod = rootNamespace.findChildModule (rootNamespace.getStringPool().get (options.namespaceOrModule));

            if (mod == nullptr)
                return {};
        }

        if (parsingComments != options.includeComments)
        {
            parsingComments = options.includeComments;
            reparse();
        }

        AST::SyntaxTreeOptions opts;
        opts.options = options;
        opts.generateIDs (*mod);

        return choc::com::createRawString (choc::json::toString (mod->toSyntaxTree (opts), true));
    }

    bool reparse()
    {
        needsReparsing = false;
        rootNamespace.clear();
        endpointList.clear();
        mainProcessor = {};

        DiagnosticMessageList messageList;

        cmaj::catchAllErrors (messageList, [&]
        {
            for (auto& sourceFile : allocator.sourceFileList.sourceFiles)
                parse (*sourceFile, false);
        });

        return ! messageList.hasErrors();
    }

    ptr<AST::ProcessorBase> findMainProcessorCandidate (const std::string& processorName)
    {
        auto requestedProcessorName = allocator.strings.stringPool.get (processorName);

        auto isProcessorSuitable = [] (const AST::ProcessorBase& p)
        {
            return ! (p.isSystemModule());
        };

        auto allProcessors = getAllProcessors();

        if (! requestedProcessorName.empty())
        {
            for (auto& processor : allProcessors)
                if (processor->hasName (requestedProcessorName))
                    if (isProcessorSuitable (processor.get()))
                        return processor.get();

            cmaj::throwError (Errors::cannotFindProcessor (requestedProcessorName));
        }

        std::vector<ref<AST::ProcessorBase>> candidateProcessors;

        for (auto& processor : allProcessors)
            if (isProcessorSuitable (processor.get()))
                if (auto annotation = AST::castTo<AST::Annotation> (processor->annotation))
                    if (annotation->getPropertyAs<bool> ("main"))
                        candidateProcessors.push_back (processor.get());

        if (candidateProcessors.empty())
        {
            for (auto& processor : allProcessors)
            {
                if (isProcessorSuitable (processor.get()) && ! processor->isSpecialised())
                {
                    auto annotation = AST::castTo<AST::Annotation> (processor->annotation);

                    if (annotation == nullptr || annotation->findProperty ("main") == nullptr)
                        candidateProcessors.push_back (processor.get());
                }
            }
        }

        if (candidateProcessors.size() == 1)
            return candidateProcessors.front().get();

        if (candidateProcessors.size() > 1)
        {
            cmaj::DiagnosticMessageList messages;

            for (auto p : candidateProcessors)
                messages.add (p.get(), Errors::multipleSuitableMainCandidates());

            cmaj::throwError (messages);
        }

        cmaj::throwError (Errors::cannotFindMainProcessor());
        return {};
    }

    AST::Allocator allocator;
    AST::Namespace& rootNamespace;
    AST::EndpointList endpointList;
    choc::hash::xxHash64 codeHash;
    bool parsingComments;
    AST::ExternalVariableManager externalVariableManager;
    AST::ExternalFunctionManager externalFunctionManager;


private:
    mutable ptr<AST::ProcessorBase> mainProcessor;
    bool needsReparsing = false;

    void addStandardLibraryCode();
};

static Program& getProgram (cmaj::ProgramInterface& p)
{
    auto imp = dynamic_cast<Program*> (std::addressof (p));
    CMAJ_ASSERT (imp != nullptr);
    return *imp;
}

static const Program& getProgram (const cmaj::ProgramInterface& p)
{
    auto imp = dynamic_cast<const Program*> (std::addressof (p));
    CMAJ_ASSERT (imp != nullptr);
    return *imp;
}
