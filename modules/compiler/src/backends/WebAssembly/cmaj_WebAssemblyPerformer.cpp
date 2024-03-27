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

#if CMAJ_ENABLE_PERFORMER_WEBVIEW

#include <thread>
#include <condition_variable>
#include "../cmaj_EngineBase.h"
#include "cmaj_JavascriptClassGenerator.h"
#include "choc/gui/choc_MessageLoop.h"
#include "choc/text/choc_Files.h"
#include "choc/gui/choc_WebView.h"
#include "choc/gui/choc_DesktopWindow.h"
#include "../../../modules/playback/include/cmaj_AllocationChecker.h"


namespace cmaj::webassembly
{

//==============================================================================
template <bool useBinaryen>
struct WebAssemblyEngine
{
    WebAssemblyEngine (EngineBase<WebAssemblyEngine>& e) : engine (e)
    {
    }

    EngineBase<WebAssemblyEngine>& engine;

    static std::string getEngineVersion()   { return std::string ("wasm1"); }

    struct ExtraEndpointInfo
    {
        choc::value::ValueView outputValue, eventCount, eventsList, streamData, inputValue;
        uint32_t frameSizeBytes = 0;
    };

    static constexpr bool canUseForwardBranches = ! useBinaryen;
    static constexpr bool usesDynamicRateAndSessionID = false;
    static constexpr bool allowTopLevelSlices = false;
    static constexpr bool supportsExternalFunctions = false;
    static bool engineSupportsIntrinsic (AST::Intrinsic::Type) { return false; }

    //==============================================================================
    struct LinkedCode
    {
        LinkedCode (WebAssemblyEngine& wasmEngine, bool, double latencyToUse, CacheDatabaseInterface*, const char*)
            : latency (latencyToUse)
        {
            JavascriptClassGenerator gen (AST::getProgram (*wasmEngine.engine.program),
                                          wasmEngine.engine.buildSettings, {}, useBinaryen,
                                          SIMDMode (wasmEngine.engine.options));

            wrapperJavascript = gen.generate();
            mainClassName = gen.mainClassName;
        }

        std::string wrapperJavascript, mainClassName;
        double latency;
    };

    //==============================================================================
    struct JITInstance
    {
        JITInstance (std::shared_ptr<LinkedCode> cc, int32_t sessionID, double frequency)
            : code (std::move (cc))
        {
            try
            {
                instanceName = "window." + code->mainClassName + "_instance";

                std::condition_variable cv;
                std::mutex m;

                context.initFinished = [&, this] (const choc::value::ValueView& args)
                {
                    if (args.isArray() && args.size() > 0 && ! args[0].isVoid())
                        initError = choc::json::toString (args[0]);

                    std::unique_lock<std::mutex> l (m);
                    cv.notify_all();
                    return choc::value::Value();
                };

                std::unique_lock<std::mutex> lock (m);
                context.loadMainModuleScript (createJavascript (sessionID, frequency));
                cv.wait (lock);

                if (! initError.empty())
                    throw choc::javascript::Error (initError);
            }
            catch (const std::runtime_error& e)
            {
                std::cout << e.what() << std::endl;
                CMAJ_ASSERT_FALSE;
            }
        }

        std::string createJavascript (int32_t sessionID, double frequency)
        {
            std::ostringstream out;

            out << code->wrapperJavascript;

            out << choc::text::replace (R"(

//==============================================================================
async function initialisePatch()
{
    try
    {
        INSTANCE = new MAIN_CLASS();
        const result = await INSTANCE.initialise (SESSION_ID, FREQUENCY);
        initFinished();
    }
    catch (e)
    {
        initFinished (e);
    }
}

initialisePatch();

)",
                "MAIN_CLASS", code->mainClassName,
                "INSTANCE", instanceName,
                "SESSION_ID", std::to_string (sessionID),
                "FREQUENCY", std::to_string (frequency));

            return out.str();
        }

        void reset()
        {
            ScopedDisableAllocationTracking disableTracking;
            context.evaluate (instanceName + ".reset()");
        }

        void advance (uint32_t framesToAdvance)
        {
            ScopedDisableAllocationTracking disableTracking;
            context.evaluate (instanceName + ".advance (" + std::to_string (framesToAdvance) + ")");
        }

        std::function<void(void*, uint32_t)> createCopyOutputValueFunction (const EndpointInfo& e)
        {
            const auto& name = e.details.endpointID.toString();
            CMAJ_ASSERT (e.details.dataTypes.size() == 1);

            if (e.details.isStream())
            {
                context.evaluate (choc::text::replace (R"(
                        let tempOutputFrames_NAME = new Array (2048);

                        function returnOutputFrames_NAME (numFrames)
                        {
                            for (let frame = 0; frame < numFrames; ++frame)
                                tempOutputFrames_NAME[frame] = INSTANCE.getOutputFrame_NAME (frame);

                            return tempOutputFrames_NAME.slice (0, numFrames);
                        })",
                    "NAME", name,
                    "INSTANCE", instanceName));

                return [this,
                        frameType = e.details.dataTypes.front(),
                        functionName = "returnOutputFrames_" + name] (void* destBuffer, uint32_t numFrames)
                {
                    ScopedDisableAllocationTracking disableTracking;
                    auto result = context.evaluateWithResult (functionName + "(" + std::to_string (numFrames) + ")");
                    writeToValueWithType (destBuffer, choc::value::Type::createArray (frameType, numFrames), result);
                };
            }
            else
            {
                return [this,
                        command = instanceName + ".getOutputValue_" + name + "()",
                        endpointType = e.details.dataTypes.front()] (void* destBuffer, uint32_t)
                {
                    ScopedDisableAllocationTracking disableTracking;
                    auto result = context.evaluateWithResult (command);
                    writeToValueWithType (destBuffer, endpointType, result);
                };
            }
        }

        std::function<void(const void*, uint32_t, uint32_t)> createSetInputStreamFramesFunction (const EndpointInfo& e)
        {
            auto command = instanceName + ".setInputStreamFrames_" + e.details.endpointID.toString() + "([";
            CMAJ_ASSERT (e.details.dataTypes.size() == 1);
            auto elementType = e.details.dataTypes.front();
            uint32_t numChannels = 0;

            if (elementType.isArray() || elementType.isVector())
            {
                numChannels = elementType.getNumElements();
                elementType = elementType.getElementType();
            }

            if (elementType.isFloat32())
            {
                return [this, command, numChannels] (const void* sourceData, uint32_t numFrames, uint32_t numTrailingFramesToClear)
                {
                    ScopedDisableAllocationTracking disableTracking;
                    this->setInputStreamFrames<float> (command, sourceData, numChannels, numFrames, numTrailingFramesToClear);
                };
            }

            if (elementType.isFloat64())
            {
                return [this, command, numChannels] (const void* sourceData, uint32_t numFrames, uint32_t numTrailingFramesToClear)
                {
                    ScopedDisableAllocationTracking disableTracking;
                    this->setInputStreamFrames<double> (command, sourceData, numChannels, numFrames, numTrailingFramesToClear);
                };
            }

            CMAJ_ASSERT_FALSE;
            return {};
        }

        template <typename FloatType>
        void setInputStreamFrames (std::string_view command, const void* sourceData,
                                   uint32_t numChannels, uint32_t numFrames, uint32_t numTrailingFramesToClear)
        {
            std::ostringstream s;
            s << command;

            auto sourceFloats = static_cast<const FloatType*> (sourceData);

            if (numChannels == 0)
            {
                s << "[ ";

                for (uint32_t i = 0; i < numFrames + numTrailingFramesToClear; ++i)
                {
                    if (i != 0)
                        s << ",";

                    if (i < numFrames)
                        s << choc::text::floatToString (sourceFloats[i]);
                    else
                        s << "0";
                }

                s << " ]";
            }
            else
            {
                for (uint32_t chan = 0; chan < numChannels; ++chan)
                {
                    if (chan != 0)
                        s << ", ";

                    s << "[ ";

                    for (uint32_t i = 0; i < numFrames + numTrailingFramesToClear; ++i)
                    {
                        if (i != 0)
                            s << ",";

                        if (i < numFrames)
                            s << choc::text::floatToString (sourceFloats[chan + i * numChannels]);
                        else
                            s << "0";
                    }

                    s << " ]";
                }
            }

            s << "], " << numFrames << ", 0)";
            context.evaluate (s.str());
        }

        auto createSetInputValueFunction (const EndpointInfo& e)
        {
            auto command = instanceName + ".setInputValue_" + e.details.endpointID.toString() + "(";
            CMAJ_ASSERT (e.details.dataTypes.size() == 1);
            auto temp = choc::value::Value (e.details.dataTypes.front());

            return [this, command, temp = std::move (temp)] (const void* valueData, uint32_t numFramesToReachValue) mutable
            {
                ScopedDisableAllocationTracking disableTracking;
                memcpy (temp.getRawData(), valueData, temp.getRawDataSize());
                context.evaluate (command + choc::json::toString (temp) + ", " + std::to_string (numFramesToReachValue) + ")");
            };
        }

        std::function<void(const void*)> createSendEventFunction (const EndpointInfo&, const AST::TypeBase& type, const AST::Function& f)
        {
            auto eventType = type.toChocType();
            auto command = instanceName + "." + AST::getEventHandlerFunctionName (f, "sendInputEvent_") + "(";
            auto temp = choc::value::Value (eventType);

            return [this, command, temp = std::move (temp)] (const void* eventData) mutable
            {
                ScopedDisableAllocationTracking disableTracking;
                memcpy (temp.getRawData(), eventData, temp.getRawDataSize());
                context.evaluate (command + choc::json::toString (temp) + ")");
            };
        }

        auto createGetNumOutputEventsFunction (const EndpointInfo& e)
        {
            auto command = instanceName + ".getOutputEventCount_" + e.details.endpointID.toString() + "()";

            return [this, command]
            {
                ScopedDisableAllocationTracking disableTracking;
                return static_cast<uint32_t> (context.evaluateWithResult (command).template getWithDefault<int32_t> (0));
            };
        }

        auto createResetEventCountFunction (const EndpointInfo& e)
        {
            auto command = instanceName + ".resetOutputEventCount_" + e.details.endpointID.toString() + "()";
            return [this, command]
            {
                ScopedDisableAllocationTracking disableTracking;
                context.evaluate (command);
            };
        }

        auto createGetEventTypeIndexFunction (const EndpointInfo& e)
        {
            auto command = instanceName + ".getOutputEvent_" + e.details.endpointID.toString() + "(";

            return [this, command] (uint32_t index)
            {
                ScopedDisableAllocationTracking disableTracking;
                return static_cast<uint32_t> (context.evaluateWithResult (command + std::to_string (index) + ").typeIndex").template getWithDefault<int32_t> (0));
            };
        }

        auto createReadOutputEventFunction (const EndpointInfo& e)
        {
            auto command = instanceName + ".getOutputEvent_" + e.details.endpointID.toString() + "(";

            return [this, command, dataTypes = e.details.dataTypes] (uint32_t index, void* dataBuffer)
            {
                ScopedDisableAllocationTracking disableTracking;
                auto result = context.evaluateWithResult (command + std::to_string (index) + ")");
                auto typeIndex = static_cast<uint32_t> (result["typeIndex"].template getWithDefault<int32_t> (0));
                writeToValueWithType (dataBuffer, dataTypes[typeIndex], result["event"]);
                return static_cast<uint32_t> (result["frame"].template get<int32_t>());
            };
        }

        struct Dictionary  : public choc::value::StringDictionary
        {
            Dictionary (JITInstance& j) : owner (j) {}

            Handle getHandleForString (std::string_view) override   { CMAJ_ASSERT_FALSE; }

            std::string_view getStringForHandle (Handle handle) const override
            {
                auto f = mappings.find (handle.handle);

                if (f != mappings.end())
                    return f->second;

                auto result = owner.context.evaluateWithResult (owner.instanceName + "._getStringForHandle (" + std::to_string (handle.handle) + ")");
                mappings[handle.handle] = result.toString();
                return mappings[handle.handle];
            }

            mutable std::unordered_map<decltype(Handle::handle), std::string> mappings;
            JITInstance& owner;
        };

        //==============================================================================
        struct WebViewInstance
        {
            //==============================================================================
            struct Context
            {
                Context (WebViewInstance& i) : instance (i), lock (i.mutex)
                {
                    instance.currentContext = this;
                }

                ~Context()
                {
                    instance.currentContext = {};
                }

                void loadMainModuleScript (const std::string& script)
                {
                    performOnMessageThread ([this, script]
                    {
                        instance.webview->setHTML (choc::text::replace (R"(
                        <!DOCTYPE html>
                        <html>
                            <head><title>Cmajor Test</title></head>
                            <body>
                            <p>Running test code...</p>
                            <p><pre>SCRIPT</pre></p>
                            </body>
                        </html>
                        <script type="module">SCRIPT</script>)",
                                        "SCRIPT", script));
                    });
                }

                void evaluate (const std::string& script)
                {
                    performOnMessageThread ([this, script]
                    {
                        instance.webview->evaluateJavascript (script);
                    });
                }

                choc::value::Value evaluateWithResult (const std::string& s)
                {
                    /// Can't trust the webview to handle bigint types
                    auto script = R"(JSON.stringify ()" + s + R"(,
                                     (key, value) => typeof value === 'bigint' ? value.toString() : value))";

                    std::unique_lock<std::mutex> l (instance.callMutex);
                    std::condition_variable callHappened;
                    std::string result;

                    choc::messageloop::postMessage ([&]
                    {
                        instance.webview->evaluateJavascript (script, [this, &callHappened, &result]
                                                                      (const std::string&, const choc::value::ValueView& value)
                        {
                            std::unique_lock<std::mutex> guard (instance.callMutex);
                            result = value.toString();
                            callHappened.notify_all();
                        });
                    });

                    callHappened.wait (l);
                    return choc::json::parseValue (result);
                }

                std::function<void(const choc::value::ValueView&)> initFinished;

            private:
                WebViewInstance& instance;
                std::unique_lock<std::recursive_mutex> lock;
            };

            //==============================================================================
            static Context get()
            {
                static WebViewInstance w;
                return Context (w);
            }

        private:
            WebViewInstance()
            {
                performOnMessageThread ([this]
                {
                    choc::ui::WebView::Options opts;
                    opts.enableDebugMode = true;
                    webview = std::make_unique<choc::ui::WebView> (opts);

                    CMAJ_ASSERT (webview->loadedOK());

                    webview->bind ("initFinished", [this] (const choc::value::ValueView& args)
                    {
                        if (currentContext && currentContext->initFinished)
                            currentContext->initFinished (args);

                        return choc::value::Value();
                    });
                });
            }

            template <typename F>
            static void performOnMessageThread (F&& f)
            {
                std::condition_variable cv;
                std::mutex m;

                std::unique_lock<std::mutex> lock (m);

                choc::messageloop::postMessage ([&]
                {
                    f();
                    std::unique_lock<std::mutex> l (m);
                    cv.notify_all();
                });

                cv.wait (lock);
            }

            //==============================================================================
            std::unique_ptr<choc::ui::WebView> webview;
            std::mutex callMutex;

            std::recursive_mutex mutex;
            Context* currentContext = nullptr;
        };

        //==============================================================================
        std::shared_ptr<LinkedCode> code;
        std::string instanceName, initError;
        typename WebViewInstance::Context context { WebViewInstance::get() };

        Dictionary dictionary { *this };
        choc::value::StringDictionary& getDictionary()  { return dictionary; }
    };


    //==============================================================================
    PerformerInterface* createPerformer (std::shared_ptr<LinkedCode> code)
    {
        return choc::com::create<PerformerBase<JITInstance>> (code, engine)
                 .getWithIncrementedRefCount();
    }

    static void writeToValueWithType (void* destData, const choc::value::Type& destType, const choc::value::ValueView& source)
    {
        auto coerced = coerceValueToType (destType, source);
        memcpy (destData, coerced.getRawData(), destType.getValueDataSize());
    }

    static choc::value::Value coerceValueToType (const choc::value::Type& destType, const choc::value::ValueView& v)
    {
        if (destType == v.getType())
            return choc::value::Value (v);

        if (destType.isBool())     return choc::value::Value (v.getWithDefault<bool> (false));
        if (destType.isInt32())    return choc::value::Value (v.getWithDefault<int32_t> (0));
        if (destType.isInt64())    return choc::value::Value (v.getWithDefault<int64_t> (0));
        if (destType.isFloat32())  return choc::value::Value (v.getWithDefault<float> (0));
        if (destType.isFloat64())  return choc::value::Value (v.getWithDefault<double> (0));
        if (destType.isString())   { CMAJ_ASSERT (v.isInt()); return choc::value::Value (v); }

        if (destType.isArray())
        {
            auto arraySize = destType.getNumElements();

            if (v.isArray() || v.isVector())
                return choc::value::createArray (arraySize, [&] (uint32_t index)
                                                 {
                                                     return coerceValueToType (destType.getArrayElementType (index), v[index]);
                                                 });

            return choc::value::createArray (arraySize, [&] (uint32_t index)
                                             {
                                                 return coerceValueToType (destType.getArrayElementType (index), v);
                                             });
        }

        if (destType.isVector())
        {
            auto result = choc::value::Value (destType);
            auto elementType = destType.getElementType();
            auto numElements = destType.getNumElements();

            if (v.isArray() || v.isVector())
            {
                for (uint32_t i = 0; i < numElements; ++i)
                    writeToValueWithType (result[i].getRawData(), elementType, v[i]);
            }
            else
            {
                for (uint32_t i = 0; i < numElements; ++i)
                    writeToValueWithType (result[i].getRawData(), elementType, v);
            }

            return result;
        }

        if (destType.isObject())
        {
            CMAJ_ASSERT (v.isObject());
            auto result = choc::value::Value (destType);

            for (uint32_t i = 0; i < destType.getNumElements(); ++i)
            {
                auto member = destType.getObjectMember (i);
                result.setMember (member.name, coerceValueToType (member.type, v[member.name]));
            }

            return result;
        }

        CMAJ_ASSERT_FALSE;
    }

    static uint32_t getFlattenedArraySize (const choc::value::Type& t)
    {
        if (t.isUniformArray() || t.isVector())
        {
            auto size = t.getNumElements();
            auto childSize = getFlattenedArraySize (t.getElementType());
            return childSize == 0 ? size : size * childSize;
        }

        return 0;
    }
};

//==============================================================================
struct Factory : public choc::com::ObjectWithAtomicRefCount<EngineFactoryInterface, Factory>
{
    virtual ~Factory() = default;
    const char* getName() override      { return "webview"; }

    EngineInterface* createEngine (const char* engineCreationOptions) override
    {
        try
        {
           #if CMAJ_ENABLE_CODEGEN_BINARYEN
            auto options = choc::json::parse (engineCreationOptions);
            bool useBinaryen = options.isObject() && options.hasObjectMember("binaryen");

            if (useBinaryen)
                return choc::com::create<EngineBase<WebAssemblyEngine<true>>> (engineCreationOptions).getWithIncrementedRefCount();
           #endif

            return choc::com::create<EngineBase<WebAssemblyEngine<false>>> (engineCreationOptions).getWithIncrementedRefCount();
        }
        catch (...) {}

        return {};
    }
};

EngineFactoryPtr createEngineFactory()              { return choc::com::create<Factory>(); }


} // namespace cmaj::webassembly

#endif
