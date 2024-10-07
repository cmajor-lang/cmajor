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

#include "../../../include/cmajor/API/cmaj_Engine.h"
#include "../../../include/cmajor/helpers/cmaj_PatchWorker_WebView.h"
#include "../../../include/cmajor/helpers/cmaj_EndpointTypeCoercion.h"
#include "../../../modules/playback/include/cmaj_AllocationChecker.h"
#include "../../../modules/compiler/src/transformations/cmaj_Transformations.h"

namespace cmaj::javascript
{

//==============================================================================
struct PerformerLibrary
{
    PerformerLibrary (const cmaj::BuildSettings& bs) : buildSettings (bs)
    {
        setEngineType ({});
    }

    void setEngineType (std::string_view newName)
    {
        std::string name (newName);

        if (! isValidEngineType (name.c_str()))
            throw std::runtime_error ("No such engine: " + name);

        engineTypeName = name;
        actualEngineName = {};
    }

    bool isValidEngineType (std::string_view name)
    {
        if (name.empty())
            return true;

        for (const auto& type : choc::text::splitAtWhitespace (cmaj::Library::getEngineTypes()))
            if (type == name)
                return true;

        return false;
    }

    std::string getEngineTypeName()
    {
        if (actualEngineName.empty())
        {
            auto f = cmaj::Library::createEngineFactory (engineTypeName.c_str());
            CMAJ_ASSERT (f != nullptr);
            actualEngineName = f->getName();
        }

        return actualEngineName;
    }

    void bind (choc::javascript::Context& context)
    {
        context.run (getWrapperScript());

        CMAJ_JAVASCRIPT_BINDING_METHOD (programNew)
        CMAJ_JAVASCRIPT_BINDING_METHOD (programRelease)
        CMAJ_JAVASCRIPT_BINDING_METHOD (programReset)
        CMAJ_JAVASCRIPT_BINDING_METHOD (programParse)
        CMAJ_JAVASCRIPT_BINDING_METHOD (programGetSyntaxTree)
        CMAJ_JAVASCRIPT_BINDING_METHOD (programGetBinaryModule)

        CMAJ_JAVASCRIPT_BINDING_METHOD (engineNew)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineRelease)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineIsValid)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineGetBuildSettings)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineSetBuildSettings)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineLoad)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineUnload)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineGetInputEndpoints)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineGetOutputEndpoints)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineGetEndpointHandle)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineLink)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineIsLoaded)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineIsLinked)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineCreatePerformer)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineGetAvailableCodeGenTargetTypes)
        CMAJ_JAVASCRIPT_BINDING_METHOD (engineGenerateCode)

        CMAJ_JAVASCRIPT_BINDING_METHOD (performerRelease)
        CMAJ_JAVASCRIPT_BINDING_METHOD (performerSetBlockSize)
        CMAJ_JAVASCRIPT_BINDING_METHOD (performerAdvance)
        CMAJ_JAVASCRIPT_BINDING_METHOD (performerReset)
        CMAJ_JAVASCRIPT_BINDING_METHOD (performerGetOutputFrames)
        CMAJ_JAVASCRIPT_BINDING_METHOD (performerGetOutputEvents)
        CMAJ_JAVASCRIPT_BINDING_METHOD (performerGetOutputValue)
        CMAJ_JAVASCRIPT_BINDING_METHOD (performerSetInputFrames)
        CMAJ_JAVASCRIPT_BINDING_METHOD (performerSetInputValue)
        CMAJ_JAVASCRIPT_BINDING_METHOD (performerAddInputEvent)
        CMAJ_JAVASCRIPT_BINDING_METHOD (performerGetXRuns)
        CMAJ_JAVASCRIPT_BINDING_METHOD (performerCalculateRenderPerformance)

        CMAJ_JAVASCRIPT_BINDING_METHOD (sourceTransformerNew)
        CMAJ_JAVASCRIPT_BINDING_METHOD (sourceTransformerRelease)
        CMAJ_JAVASCRIPT_BINDING_METHOD (sourceTransformerTransform)
    }

    void reset()
    {
        programs.clear();
        performers.clear();
    }

    cmaj::Program* getProgram (choc::javascript::ArgumentList args, size_t index)
    {
        return programs.getObject (args, index);
    }

    std::string getEngineTypeName (choc::javascript::ArgumentList args) const
    {
        auto engineName = engineTypeName;

        if (auto engine = args[0])
        {
            if (engine->isObject() && engine->hasObjectMember ("type"))
                engineName = (*engine)["type"].getString();
        }

        return engineName;
    }

    cmaj::Engine createEngine (const std::string& engineType, choc::javascript::ArgumentList args)
    {
        auto engine = cmaj::Engine::create (engineType, args[0]);
        engine.setBuildSettings (buildSettings);
        return engine;
    }

private:
    //==============================================================================
    struct Performer
    {
        Performer() = default;
        ~Performer() = default;

        cmaj::Performer performer;
        EndpointTypeCoercionHelperList endpointTypeCoercionHelpers;
        uint32_t currentNumFrames = 0;

        choc::value::Value setBlockSize (uint32_t frames)
        {
            performer.setBlockSize (frames);
            currentNumFrames = frames;
            return {};
        }

        choc::value::Value reset()
        {
            try
            {
                performer.reset();
            }
            catch (const std::exception& e)
            {
                return createErrorObject (e.what());
            }
            catch (...)
            {
                return createErrorObject ("reset() failed");
            }

            return {};
        }

        choc::value::Value advance()
        {
            try
            {
                performer.advance();
            }
            catch (const std::exception& e)
            {
                return createErrorObject (e.what());
            }
            catch (...)
            {
                return createErrorObject ("advance() failed");
            }

            return {};
        }

        float getVectorElementAsFloat32 (choc::value::ValueView v, uint32_t index) const
        {
            if (index == 0 && ! v.isVector())
            {
                if (v.isFloat32())
                    return v.getFloat32();

                return static_cast<float> (v.getFloat64());
            }

            if (v[index].isFloat32())
                return v[index].getFloat32();

            return static_cast<float> (v[index].getFloat64());
        }

        choc::value::Value getOutputFrames (choc::javascript::ArgumentList args)
        {
            if (auto handle = getEndpointHandle (args, 1))
            {
                auto scratchView = endpointTypeCoercionHelpers.getViewForOutputArray (handle, cmaj::EndpointType::stream);

                if (scratchView.isVoid())
                    return createErrorObject ("Cannot get frames");

                performer.copyOutputFrames (handle, scratchView.getRawData(), currentNumFrames);
                scratchView.getMutableType().modifyNumElements (currentNumFrames);

                if (scratchView[0].isObject())
                {
                    auto frameSize = scratchView[0]["real"].size() * 2;

                    std::vector<float> f (frameSize);

                    return choc::value::createArray (currentNumFrames, [&] (uint32_t frame)
                    {
                        for (uint32_t i = 0; i < frameSize/2; i++)
                        {
                            f[i * 2]     = getVectorElementAsFloat32 (scratchView[frame]["real"], i);
                            f[i * 2 + 1] = getVectorElementAsFloat32 (scratchView[frame]["imag"], i);
                        }

                        return choc::value::createVector (f.data(), frameSize);
                    });
                }

                return choc::value::Value (scratchView);
            }

            return createErrorObject ("Cannot find endpoint");
        }

        choc::value::Value getOutputValue (choc::javascript::ArgumentList args)
        {
            if (auto handle = getEndpointHandle (args, 1))
            {
                auto scratchView = endpointTypeCoercionHelpers.getViewForOutputArray (handle, cmaj::EndpointType::value);

                if (scratchView.isVoid())
                    return createErrorObject ("Cannot get value");

                performer.copyOutputValue (handle, scratchView.getRawData());
                return choc::value::Value (scratchView);
            }

            return createErrorObject ("Cannot find endpoint");
        }

        choc::value::Value getOutputEvents (choc::javascript::ArgumentList args)
        {
            if (auto handle = getEndpointHandle (args, 1))
            {
                auto v = endpointTypeCoercionHelpers.getOutputEvents (handle, performer);

                if (v.isVoid())
                    return createErrorObject ("Cannot get value");

                return v;
            }

            return createErrorObject ("Cannot find endpoint");
        }

        choc::value::Value setInputFrames (choc::javascript::ArgumentList args)
        {
            if (auto handle = getEndpointHandle (args, 1))
            {
                if (auto data = args[2])
                {
                    if (auto coercedData = endpointTypeCoercionHelpers.coerceArray (handle, *data, cmaj::EndpointType::stream))
                    {
                        performer.setInputFrames (handle, coercedData.data, data->getType().getNumElements());
                        return {};
                    }
                }

                return createErrorObject ("Cannot convert to target type");
            }

            return createErrorObject ("Cannot find endpoint");
        }

        choc::value::Value setInputValue (choc::javascript::ArgumentList args)
        {
            if (auto handle = getEndpointHandle (args, 1))
            {
                if (auto value = args[2])
                {
                    if (auto coercedData = endpointTypeCoercionHelpers.coerceValue (handle, *value))
                    {
                        uint32_t framesToReachTarget = 0;

                        if (auto time = args[3])
                            framesToReachTarget = time->get<uint32_t>();

                        performer.setInputValue (handle, coercedData.data, framesToReachTarget);
                        return {};
                    }
                }

                return createErrorObject ("Cannot convert to target type");
            }

            return createErrorObject ("Cannot find endpoint");
        }

        choc::value::Value addInputEvent (choc::javascript::ArgumentList args)
        {
            if (auto handle = getEndpointHandle (args, 1))
            {
                if (auto value = args[2])
                {
                    if (auto coercedData = endpointTypeCoercionHelpers.coerceValueToMatchingType (handle, *value, cmaj::EndpointType::event))
                    {
                        performer.addInputEvent (handle, coercedData.typeIndex, coercedData.data.data);
                        return {};
                    }
                }

                return createErrorObject ("Cannot convert to target type");
            }

            return createErrorObject ("Cannot find endpoint");
        }

        choc::value::Value calculateRenderPerformance (choc::javascript::ArgumentList args)
        {
            auto blockSize  = args.get<uint32_t> (1);
            auto frames     = args.get<uint32_t> (2);
            auto blockCount = frames / blockSize;

            auto startTime = std::chrono::steady_clock::now();
            performer.setBlockSize (blockSize);

            for (uint32_t i = 0; i < blockCount; ++i)
                performer.advance();

            auto endTime = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = endTime - startTime;

            return choc::value::Value (elapsed.count());
        }

        static cmaj::EndpointHandle getEndpointHandle (choc::javascript::ArgumentList args, size_t index)
        {
            if (auto data = args[index])
                if (data->isInt() || data->isFloat())
                    return data->get<cmaj::EndpointHandle>();

            return {};
        }
    };

    //==============================================================================
    struct Engine
    {
        Engine (PerformerLibrary& l, choc::javascript::ArgumentList settings) : owner (l)
        {
            engineType = owner.getEngineTypeName (settings);
            engine = owner.createEngine (engineType, settings);
            engine.setBuildSettings (l.buildSettings);
        }

        PerformerLibrary& owner;
        std::string engineType;
        cmaj::Engine engine;
        std::unordered_map<std::string, EndpointHandle> endpointHandles;

        bool isValid() const
        {
            return engine;
        }

        //==============================================================================
        choc::value::Value getBuildSettings()
        {
            return engine.getBuildSettings().getValue();
        }

        choc::value::Value setBuildSettings (choc::javascript::ArgumentList args)
        {
            if (auto values = args[1])
                if (values->isObject())
                    engine.setBuildSettings (cmaj::BuildSettings::fromJSON (*values));

            return {};
        }

        choc::value::Value getEndpointHandle (choc::javascript::ArgumentList args)
        {
            if (auto endpointID = args.get<const char*> (1))
            {
                if (auto handle = engine.getEndpointHandle (endpointID))
                {
                    endpointHandles[endpointID] = handle;
                    return choc::value::createInt32 (static_cast<int32_t> (handle));
                }
            }

            return createErrorObject ("Cannot find endpoint");
        }

        choc::value::Value unload()
        {
            if (! engine)
                return createErrorObject ("Engine is not valid");

            engine.unload();
            return {};
        }

        std::unordered_map<std::string, choc::value::ValueView> parseExternals (const choc::value::Value* externals)
        {
            if (externals == nullptr)
                return {};

            std::unordered_map<std::string, choc::value::ValueView> result;

            if (externals->isObject())
            {
                for (uint32_t i = 0; i < externals->size(); i++)
                {
                    auto v = externals->getObjectMemberAt (i);
                    result[v.name] = v.value;
                }
            }

            return result;
        }

        choc::value::Value load (choc::javascript::ArgumentList args)
        {
            endpointHandles.clear();

            if (auto program = owner.programs.getObject (args, 1))
            {
                auto externals = parseExternals (args[2]);

                cmaj::DiagnosticMessageList messages;

                auto startTime = std::chrono::steady_clock::now();

                engine.load (messages, *program, [&] (const cmaj::ExternalVariable& v) -> choc::value::Value
                {
                    auto e = externals.find (v.name);

                   if (e != externals.end())
                       return choc::value::Value (e->second);

                    return {};
                },
                {});

                auto endTime = std::chrono::steady_clock::now();

                if (! messages.empty())
                    return messages.toJSON();

                std::chrono::duration<double> elapsed = endTime - startTime;
                return choc::value::Value (elapsed.count());
            }

            return createErrorObject ("Unknown program");
        }

        choc::value::Value link()
        {
            DiagnosticMessageList messages;

            auto startTime = std::chrono::steady_clock::now();
            engine.link (messages);
            auto endTime = std::chrono::steady_clock::now();

            if (! messages.empty())
                return messages.toJSON();

            std::chrono::duration<double> elapsed = endTime - startTime;
            return choc::value::Value (elapsed.count());
        }

        choc::value::Value isLoaded()
        {
            return choc::value::Value (engine.isLoaded());
        }

        choc::value::Value isLinked()
        {
            return choc::value::Value (engine.isLinked());
        }

        choc::value::Value createPerformer()
        {
            if (! engine.isLinked())
                return createErrorObject ("Engine not linked");

            auto perf = std::make_unique<Performer>();
            perf->endpointTypeCoercionHelpers.initialise (engine, 1024, false, false);

            if (auto p = engine.createPerformer())
            {
                p.performer = cmaj::createAllocationCheckingPerformerWrapper (p.performer);

                perf->performer = p;
                perf->endpointTypeCoercionHelpers.initialiseDictionary (p);

                for (auto& i : endpointHandles)
                    perf->endpointTypeCoercionHelpers.addMapping (i.first, i.second);

                return owner.performers.createNewObject (std::move (perf));
            }

            return createErrorObject ("Could not create performer");
        }

        choc::value::Value getAvailableCodeGenTargetTypes() const
        {
            if (! engine)
                return createErrorObject ("Engine is not valid");

            auto result = choc::value::createEmptyArray();

            for (auto& type : engine.getAvailableCodeGenTargetTypes())
                result.addArrayElement (choc::value::createString (type));

            return result;
        }

        choc::value::Value generateCode (choc::javascript::ArgumentList args)
        {
            if (! engine)
                return createErrorObject ("Engine is not valid");

            auto output = engine.generateCode (args.get<std::string> (1),
                                               args[2] != nullptr ? choc::json::toString (*args[2]) : std::string());

            return choc::json::create ("output", output.generatedCode,
                                       "messages", output.messages.toJSON(),
                                       "mainClass", output.mainClassName);
        }

        choc::value::Value getInputEndpoints() const
        {
            if (! engine)
                return createErrorObject ("Engine is not valid");

            return engine.getInputEndpoints().toJSON (true);
        }

        choc::value::Value getOutputEndpoints() const
        {
            if (! engine)
                return createErrorObject ("Engine is not valid");

            return engine.getOutputEndpoints().toJSON (true);
        }
    };

    struct SourceTransformer
    {
        SourceTransformer (std::filesystem::path source)
        {
            enableWebViewPatchWorker (patch);

            patch.lastLoadParams.manifest.createFileReaderFunctions (source);
            patch.lastLoadParams.manifest.sourceTransformer = source.filename().string();

            sourceTransformer = std::make_unique<cmaj::Patch::SourceTransformer> (patch, 5.0);
        }

        ~SourceTransformer() = default;

        choc::value::Value transform (choc::javascript::ArgumentList args)
        {
            auto filename  = args.get<std::string> (1);
            auto contents  = args.get<std::string> (2);

            DiagnosticMessageList errors;

            auto result = sourceTransformer->transform (errors, filename, contents);

            if (! errors.empty())
                return createErrorObject (errors);

            return choc::value::createString (result);
        }

        cmaj::Patch patch;
        std::unique_ptr<cmaj::Patch::SourceTransformer> sourceTransformer;
    };

    //==============================================================================
    std::string engineTypeName, actualEngineName;
    cmaj::BuildSettings buildSettings;

    ObjectHandleList<Performer, std::unique_ptr<Performer>> performers;
    ObjectHandleList<Engine, std::unique_ptr<Engine>> engines;
    ObjectHandleList<cmaj::Program, std::unique_ptr<cmaj::Program>> programs;
    ObjectHandleList<SourceTransformer, std::unique_ptr<SourceTransformer>> sourceTransformers;

    Engine* getEngine                       (choc::javascript::ArgumentList args) { return engines.getObject (args, 0); }
    Performer* getPerformer                 (choc::javascript::ArgumentList args) { return performers.getObject (args, 0); }
    SourceTransformer* getSourceTransformer (choc::javascript::ArgumentList args) { return sourceTransformers.getObject (args, 0); }

    //==============================================================================
    choc::value::Value programNew (choc::javascript::ArgumentList)
    {
        return programs.createNewObject (std::make_unique<cmaj::Program>());
    }

    choc::value::Value programRelease (choc::javascript::ArgumentList args)
    {
        programs.deleteObject (args, 0);
        return {};
    }

    choc::value::Value programReset (choc::javascript::ArgumentList args)
    {
        if (auto program = programs.getObject (args, 0))
        {
            program->reset();
            return {};
        }

        return createErrorObject ("Cannot find program");
    }

    choc::value::Value programParse (choc::javascript::ArgumentList args)
    {
        if (auto program = programs.getObject (args, 0))
        {
            const auto toContent = [] (const choc::value::Value* maybeContent) -> std::string
            {
                if (! maybeContent) return {};

                const auto& content = *maybeContent;

                if (content.isArray())
                {
                    std::vector<uint8_t> bytes;
                    bytes.reserve (content.size());

                    for (const auto& v : content)
                        bytes.push_back (static_cast<uint8_t> (v.get<int32_t>()));

                    return { bytes.begin(), bytes.end() };
                }

                return content.getWithDefault<std::string> ("");
            };

            auto filename = args.get<const char*> (1);
            auto content = toContent (args[2]);

            cmaj::DiagnosticMessageList messages;

            auto startTime = std::chrono::steady_clock::now();
            program->parse (messages, filename, content);
            auto endTime = std::chrono::steady_clock::now();

            if (! messages.empty())
                return messages.toJSON();

            std::chrono::duration<double> elapsed = endTime - startTime;
            return choc::value::Value (elapsed.count());
        }

        return createErrorObject ("Cannot find program");
    }

    choc::value::Value programGetSyntaxTree (choc::javascript::ArgumentList args)
    {
        if (auto program = programs.getObject (args, 0))
        {
            cmaj::SyntaxTreeOptions options;
            std::string module;

            if (args.size() > 1)
            {
                module = args.get<std::string> (1).c_str();
                options.namespaceOrModule = module.c_str();
            }

            auto tree = program->getSyntaxTree (options);

            if (! tree.empty())
                return choc::json::parseValue (tree);

            return createErrorObject ("Failed to generate syntax tree");
        }

        return createErrorObject ("Cannot find program");
    }

    choc::value::Value programGetBinaryModule (choc::javascript::ArgumentList args)
    {
        if (auto program = programs.getObject (args, 0))
        {
            if (auto* p = dynamic_cast<AST::Program*> (program->program.get()))
            {
                cmaj::transformations::runBasicResolutionPasses (*p);

                auto modules = p->rootNamespace.getSubModules();

                if (modules.empty())
                    return createErrorObject ("No modules in program");

                auto binaryData = cmaj::transformations::createBinaryModule (modules);

                return choc::value::createArray (static_cast<uint32_t> (binaryData.size()), [&] (size_t i)
                                                 {
                                                     return choc::value::createInt32 (binaryData[i]);
                                                 });
            }
        }

        return createErrorObject ("Cannot find program");
    }

    //==============================================================================
    choc::value::Value engineNew (choc::javascript::ArgumentList args)
    {
        return engines.createNewObject (std::make_unique<Engine> (*this, args));
    }

    choc::value::Value engineRelease (choc::javascript::ArgumentList args)
    {
        engines.deleteObject (args, 0);
        return {};
    }

    choc::value::Value engineIsValid (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return choc::value::createBool (engine->isValid());

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineGetBuildSettings (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return engine->getBuildSettings();

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineSetBuildSettings (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
        {
            engine->setBuildSettings (args);
            return {};
        }

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineGetInputEndpoints (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return engine->getInputEndpoints();

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineGetOutputEndpoints (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return engine->getOutputEndpoints();

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineGetEndpointHandle (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return engine->getEndpointHandle (args);

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineLoad (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return engine->load (args);

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineUnload (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return engine->unload();

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineLink (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return engine->link();

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineIsLoaded (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return engine->isLoaded();

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineIsLinked (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return engine->isLinked();

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineCreatePerformer (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return engine->createPerformer();

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineGetAvailableCodeGenTargetTypes (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return engine->getAvailableCodeGenTargetTypes();

        return createErrorObject ("Cannot find engine");
    }

    choc::value::Value engineGenerateCode (choc::javascript::ArgumentList args)
    {
        if (auto engine = getEngine (args))
            return engine->generateCode (args);

        return createErrorObject ("Cannot find engine");
    }

    //==============================================================================
    choc::value::Value performerRelease (choc::javascript::ArgumentList args)
    {
        performers.deleteObject (args, 0);
        return {};
    }

    choc::value::Value performerSetBlockSize (choc::javascript::ArgumentList args)
    {
        if (auto performer = getPerformer (args))
            return performer->setBlockSize (args.get<uint32_t> (1));

        return createErrorObject ("Cannot find performer");
    }

    choc::value::Value performerAdvance (choc::javascript::ArgumentList args)
    {
        if (auto performer = getPerformer (args))
            return performer->advance();

        return createErrorObject ("Cannot find performer");
    }

    choc::value::Value performerReset (choc::javascript::ArgumentList args)
    {
        if (auto performer = getPerformer (args))
            return performer->reset();

        return createErrorObject ("Cannot find performer");
    }

    choc::value::Value performerGetOutputFrames (choc::javascript::ArgumentList args)
    {
        if (auto performer = getPerformer (args))
            return performer->getOutputFrames (args);

        return createErrorObject ("Cannot find performer");
    }

    choc::value::Value performerGetOutputEvents (choc::javascript::ArgumentList args)
    {
        if (auto performer = getPerformer (args))
            return performer->getOutputEvents (args);

        return createErrorObject ("Cannot find performer");
    }

    choc::value::Value performerGetOutputValue (choc::javascript::ArgumentList args)
    {
        if (auto performer = getPerformer (args))
            return performer->getOutputValue (args);

        return createErrorObject ("Cannot find performer");
    }

    choc::value::Value performerSetInputFrames (choc::javascript::ArgumentList args)
    {
        if (auto performer = getPerformer (args))
            return performer->setInputFrames (args);

        return createErrorObject ("Cannot find performer");
    }

    choc::value::Value performerSetInputValue (choc::javascript::ArgumentList args)
    {
        if (auto performer = getPerformer (args))
            return performer->setInputValue (args);

        return createErrorObject ("Cannot find performer");
    }

    choc::value::Value performerAddInputEvent (choc::javascript::ArgumentList args)
    {
        if (auto performer = getPerformer (args))
            return performer->addInputEvent (args);

        return createErrorObject ("Cannot find performer");
    }

    choc::value::Value performerGetXRuns (choc::javascript::ArgumentList args)
    {
        if (auto performer = getPerformer (args))
            return choc::value::Value (static_cast<int32_t> (performer->performer.getXRuns()));

        return createErrorObject ("Cannot find performer");
    }

    choc::value::Value performerCalculateRenderPerformance (choc::javascript::ArgumentList args)
    {
        if (auto performer = getPerformer (args))
            return performer->calculateRenderPerformance (args);

        return createErrorObject ("Cannot find performer");
    }

    //==============================================================================
    choc::value::Value sourceTransformerNew (choc::javascript::ArgumentList args)
    {
        return sourceTransformers.createNewObject (std::make_unique<SourceTransformer> (args.get<std::string> (0)));
    }

    choc::value::Value sourceTransformerRelease (choc::javascript::ArgumentList args)
    {
        sourceTransformers.deleteObject (args, 0);
        return {};
    }

    choc::value::Value sourceTransformerTransform (choc::javascript::ArgumentList args)
    {
        if (auto sourceTransformer = getSourceTransformer (args))
            return sourceTransformer->transform (args);

        return createErrorObject ("Cannot find source transformer");
    }


    //==============================================================================
    static std::string getWrapperScript()
    {
        return R"WRAPPER_SCRIPT(

function isErrorOrWarning (response)
{
    if (response === undefined)
        return false;

    if (Array.isArray (response))
        return isErrorOrWarning (response[0]);

    return response.message !== undefined || response.fullDescription !== undefined;
}

function isError (response, options)
{
    if (response === undefined)
        return false;

    if (Array.isArray (response))
    {
        for (var i = 0; i < response.length; ++i)
        {
            if (isError (response[i], options))
                return true;
        }

        return false;
    }

    let failOnWarnings = true;

    if (options !== undefined && options.failOnWarnings !== undefined)
        failOnWarnings = options.failOnWarnings;

    if (failOnWarnings)
        return response.message !== undefined || response.fullDescription !== undefined;

    return (response.fullDescription !== undefined || response.message !== undefined)
        && (response.severity === undefined || response.severity == "error");
}

function getErrorDescription (error)
{
    if (Array.isArray (error))
    {
        var result = "";

        for (var i = 0; i < error.length; ++i)
        {
            if (i != 0)
                result += "\n";

            result += getErrorDescription (error[i]);
        }

        return result;
    }

    var desc = (error.fullDescription !== undefined) ?
                error.fullDescription : error.message;

    if (error.annotatedLine !== undefined && error.annotatedLine != "")
        desc += "\n" + error.annotatedLine;

    return desc;
}

function printError (error)
{
    const desc = getErrorDescription (error);
    console (desc + "\n");
}

class Engine
{
    constructor (engineArgs)            { this.id = _engineNew (engineArgs); }
    release()                           { _engineRelease (this.id); }

    isValid()                           { return _engineIsValid (this.id); }
    getBuildSettings()                  { return _engineGetBuildSettings (this.id); }
    setBuildSettings (settings)         { return _engineSetBuildSettings (this.id, settings); }
    load (program, callbackFn)          { return _engineLoad (this.id, program.id, callbackFn); }
    unload()                            { return _engineUnload (this.id); }
    getInputEndpoints()                 { return _engineGetInputEndpoints (this.id); }
    getOutputEndpoints()                { return _engineGetOutputEndpoints (this.id); }
    link()                              { return _engineLink (this.id); }
    isLoaded()                          { return _engineIsLoaded (this.id); }
    isLinked()                          { return _engineIsLinked (this.id); }
    createPerformer()                   { var result = _engineCreatePerformer (this.id); return isError (result) ? result : new Performer (result); }
    getEndpointHandle (id)              { return _engineGetEndpointHandle (this.id, id); }
    getAvailableCodeGenTargetTypes()    { return _engineGetAvailableCodeGenTargetTypes (this.id); }
    generateCode (target, options)      { return _engineGenerateCode (this.id, target, options); }
}

class Performer
{
    constructor (performerID)           { this.id = performerID; }
    release()                           { _performerRelease (this.id); }

    setBlockSize (frames)               { return _performerSetBlockSize (this.id, frames); }
    advance()                           { return _performerAdvance (this.id); }
    reset()                             { return _performerReset (this.id); }
    getOutputFrames (h)                 { return _performerGetOutputFrames (this.id, h); }
    getOutputEvents (h)                 { return _performerGetOutputEvents (this.id, h); }
    getOutputValue (h)                  { return _performerGetOutputValue (this.id, h); }
    setInputFrames (h, d)               { return _performerSetInputFrames (this.id, h, d); }
    setInputValue (h, d, f)             { return _performerSetInputValue (this.id, h, d, f); }
    addInputEvent (h, d)                { return _performerAddInputEvent (this.id, h, d); }
    getXRuns()                          { return _performerGetXRuns (this.id); }
    calculateRenderPerformance (bs, f)  { return _performerCalculateRenderPerformance (this.id, bs, f); }
}

class SourceTransformer
{
    constructor (engineArgs)            { this.id = _sourceTransformerNew (engineArgs); }
    release()                           { _sourceTransformerRelease (this.id); }

    transform (filename, contents)      { return _sourceTransformerTransform (this.id, filename, contents); }
}

class Program
{
    constructor()                       { this.id = _programNew(); }
    release()                           { return _programRelease (this.id); }
    reset()                             { return _programReset (this.id); }

    parse (source)
    {
        var filename = "";

        if (source.path !== undefined)
        {
            if (! source.exists())
                return {
                    severity: "error",
                    message: "Could not read file: '" + source.path + "'"
                };

            filename = source.path;
            source = source.read();
        }

        if (typeof source !== 'string' && ! (source instanceof Array))
            return {
                severity: "error",
                message: "Incorrect arguments to Program.parse()"
            };

        return _programParse (this.id, filename, source);
    }

    getSyntaxTree (moduleName)          { return _programGetSyntaxTree (this.id, moduleName); }
    getBinaryModule()                   { return _programGetBinaryModule (this.id); }
}

)WRAPPER_SCRIPT";
    }
};


}
