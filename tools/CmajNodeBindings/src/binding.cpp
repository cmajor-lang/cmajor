#define CHOC_ASSERT(x) assert(x)
#define NAPI_EXPERIMENTAL

#include <napi.h>

#include <cassert>
#include <memory>
#include <unordered_map>

#include "../../../include/choc/text/choc_JSON.h"
#include "../../../include/cmajor/API/cmaj_Engine.h"
#include "../../../include/cmajor/helpers/cmaj_EndpointTypeCoercion.h"
#include "../../../modules/compiler/src/AST/cmaj_AST.h"
#include "../../../modules/compiler/src/transformations/cmaj_Transformations.h"

using namespace cmaj;
namespace {
inline std::string toJSON(const choc::value::Value &v, bool pretty = false) {
	return choc::json::toString(v, pretty);
}

inline choc::value::Value fromJSON(const Napi::Value &v) {
	return choc::json::parseValue(v.As<Napi::String>().Utf8Value());
}

template <typename Msgs>
void throwIfErrors(const Msgs &m, Napi::Env env) {
	if (!m.empty())
		throw Napi::Error::New(env, m.toString());
}
} // namespace

class ProgramWrap : public Napi::ObjectWrap<ProgramWrap> {
  public:
	static Napi::Function Init(Napi::Env env) {
		return DefineClass(
			env, "Program",
			{InstanceMethod<&ProgramWrap::Parse>("parse"),
			 InstanceMethod<&ProgramWrap::ParseAsync>("parseAsync"),
			 InstanceMethod<&ProgramWrap::Reset>("reset"),
			 InstanceMethod<&ProgramWrap::GetSyntaxTree>("getSyntaxTree"),
			 InstanceMethod<&ProgramWrap::GetBinaryModule>("getBinaryModule")});
	}

	ProgramWrap(const Napi::CallbackInfo &info)
		: Napi::ObjectWrap<ProgramWrap>(info),
		  program(std::make_unique<Program>()) {}

	Napi::Value GetBinaryModule(const Napi::CallbackInfo &info) {
		if (auto *p = dynamic_cast<cmaj::AST::Program *>(program->program.get())) {
			cmaj::transformations::runBasicResolutionPasses(*p);

			auto modules = p->rootNamespace.getSubModules();

			if (modules.empty())
				throw Napi::Error::New(info.Env(), "No modules in program");

			auto binaryData = cmaj::transformations::createBinaryModule(modules);

			auto arr = Napi::Array::New(info.Env(), binaryData.size());
			for (size_t i = 0; i < binaryData.size(); ++i) {
				arr.Set(i, Napi::Number::New(info.Env(), binaryData[i]));
			}
			return arr;
		}

		throw Napi::Error::New(info.Env(), "Cannot find program");
	}

	Napi::Value Parse(const Napi::CallbackInfo &info) {
		std::string src = info[0].As<Napi::String>().Utf8Value();

		DiagnosticMessageList msgs;
		bool ok = program->parse(msgs, "user", src);
		throwIfErrors(msgs, info.Env());

		return Napi::Boolean::New(info.Env(), ok);
	}

	class ParseWorker : public Napi::AsyncWorker {
	  public:
		ParseWorker(ProgramWrap &owner, std::string source, Napi::Promise::Deferred def)
			: Napi::AsyncWorker(owner.Env()),
			  program(*owner.program),
			  source(std::move(source)),
			  deferred(def) {}

		void Execute() override {
			DiagnosticMessageList msgs;
			ok = program.parse(msgs, "user", source);

			if (!msgs.empty())
				error = msgs.toString();
		}

		void OnOK() override {
			if (error.empty())
				deferred.Resolve(Napi::Boolean::New(Env(), ok));
			else
				deferred.Reject(Napi::Error::New(Env(), error).Value());
		}

	  private:
		Program &program;
		std::string source;
		Napi::Promise::Deferred deferred;
		std::string error;
		bool ok = false;
	};

	Napi::Value ParseAsync(const Napi::CallbackInfo &info) {
		std::string src = info[0].As<Napi::String>().Utf8Value();
		auto deferred = Napi::Promise::Deferred::New(info.Env());
		auto *worker = new ParseWorker(*this, std::move(src), deferred);
		worker->Queue();
		return deferred.Promise();
	}

	Napi::Value Reset(const Napi::CallbackInfo &info) {
		program->reset();
		return info.Env().Undefined();
	}

	Napi::Value GetSyntaxTree(const Napi::CallbackInfo &info) {
		auto tree = program->getSyntaxTree({});
		return tree.empty() ? info.Env().Null()
							: Napi::String::New(info.Env(), tree);
	}

	Program *get() const { return program.get(); }

  private:
	std::unique_ptr<Program> program;
};

class EngineWrap;
class PerformerWrap : public Napi::ObjectWrap<PerformerWrap> {
  public:
	static Napi::FunctionReference constructor;
	static Napi::Function Init(Napi::Env env);
	PerformerWrap(const Napi::CallbackInfo &info);
	~PerformerWrap() override = default;
	Napi::Value SetBlockSize(const Napi::CallbackInfo &info);
	Napi::Value Advance(const Napi::CallbackInfo &info);
	Napi::Value Reset(const Napi::CallbackInfo &info);
	Napi::Value GetOutputFrames(const Napi::CallbackInfo &info);
	Napi::Value GetOutputEvents(const Napi::CallbackInfo &info);
	Napi::Value GetOutputValue(const Napi::CallbackInfo &info);
	Napi::Value SetInputFrames(const Napi::CallbackInfo &info);
	Napi::Value SetInputValue(const Napi::CallbackInfo &info);
	Napi::Value AddInputEvent(const Napi::CallbackInfo &info);
	Napi::Value GetXRuns(const Napi::CallbackInfo &info);

  private:
	std::unique_ptr<cmaj::Performer> performer;
	cmaj::EndpointTypeCoercionHelperList helpers;
	uint32_t currentBlock = 0;
	EngineWrap &engineOwner;

	static EndpointHandle toHandle(const Napi::Value &v) {
		return static_cast<EndpointHandle>(v.As<Napi::Number>().Uint32Value());
	}
};
class EngineWrap : public Napi::ObjectWrap<EngineWrap> {
  public:
	static Napi::Function Init(Napi::Env env) {
		return DefineClass(
			env, "Engine",
			{InstanceMethod<&EngineWrap::SetBuildSettings>("setBuildSettings"),
			 InstanceMethod<&EngineWrap::GetBuildSettings>("getBuildSettings"),
			 InstanceMethod<&EngineWrap::Load>("load"),
			 InstanceMethod<&EngineWrap::LoadAsync>("loadAsync"),
			 InstanceMethod<&EngineWrap::Unload>("unload"),
			 InstanceMethod<&EngineWrap::Link>("link"),
			 InstanceMethod<&EngineWrap::LinkAsync>("linkAsync"),
			 InstanceMethod<&EngineWrap::IsLoaded>("isLoaded"),
			 InstanceMethod<&EngineWrap::IsLinked>("isLinked"),
			 InstanceMethod<&EngineWrap::GetInputEndpoints>("getInputEndpoints"),
			 InstanceMethod<&EngineWrap::GetOutputEndpoints>("getOutputEndpoints"),
			 InstanceMethod<&EngineWrap::GetEndpointHandle>("getEndpointHandle"),
			 InstanceMethod<&EngineWrap::CreatePerformer>("createPerformer"),
			 InstanceMethod<&EngineWrap::GenerateCode>("generateCode")});
	}

	EngineWrap(const Napi::CallbackInfo &info)
		: Napi::ObjectWrap<EngineWrap>(info), engine(Engine::create()) {}

	Napi::Value SetBuildSettings(const Napi::CallbackInfo &info) {
		if (!info[0].IsObject())
			throw Napi::TypeError::New(info.Env(),
									   "setBuildSettings expects an object");

		auto obj = info[0].As<Napi::Object>();
		BuildSettings bs = engine.getBuildSettings();

		if (obj.Has("frequency"))
			bs.setFrequency(obj.Get("frequency").As<Napi::Number>().Uint32Value());
		if (obj.Has("sessionID"))
			bs.setSessionID(obj.Get("sessionID").As<Napi::Number>().Uint32Value());

		engine.setBuildSettings(bs);
		return info.Env().Undefined();
	}

	Napi::Value GetBuildSettings(const Napi::CallbackInfo &info) {
		return Napi::String::New(info.Env(),
								 toJSON(engine.getBuildSettings().getValue()));
	}

	Napi::Value GenerateCode(const Napi::CallbackInfo &info) {
		if (!info[0].IsString())
			throw Napi::TypeError::New(info.Env(), "generateCode expects a target type string as first argument");
		if (!info[1].IsObject())
			throw Napi::TypeError::New(info.Env(), "generateCode expects an options object as second argument");

		std::string targetType = info[0].As<Napi::String>().Utf8Value();
		auto options = info[1].As<Napi::Object>();
		
		auto result = engine.generateCode(targetType, "");
		// return Napi::String::New(info.Env(), "1");

		// Convert CodeGenOutput to a value
		auto outputValue = choc::value::createObject({},
													 "generatedCode", choc::value::createString(result.generatedCode),
													 "mainClassName", choc::value::createString(result.mainClassName),
													 "messages", result.messages.toJSON());

		return Napi::String::New(info.Env(), toJSON(outputValue));
	}

	Napi::Value Load(const Napi::CallbackInfo &info) {
		auto prog =
			Napi::ObjectWrap<ProgramWrap>::Unwrap(info[0].As<Napi::Object>());
		DiagnosticMessageList msgs;
		bool ok = engine.load(msgs, *prog->get(), {}, {});
		throwIfErrors(msgs, info.Env());
		return Napi::Boolean::New(info.Env(), ok);
	}

	class LoadWorker : public Napi::AsyncWorker {
	  public:
		LoadWorker(EngineWrap &owner, Program *program, Napi::Promise::Deferred def)
			: Napi::AsyncWorker(owner.Env()),
			  engine(owner.engine),
			  program(program),
			  deferred(def) {}

		void Execute() override {
			DiagnosticMessageList msgs;
			ok = engine.load(msgs, *program, {}, {});

			if (!msgs.empty())
				error = msgs.toString();
		}

		void OnOK() override {
			if (error.empty())
				deferred.Resolve(Napi::Boolean::New(Env(), ok));
			else
				deferred.Reject(Napi::Error::New(Env(), error).Value());
		}

	  private:
		cmaj::Engine &engine;
		Program *program;
		Napi::Promise::Deferred deferred;
		std::string error;
		bool ok = false;
	};

	Napi::Value LoadAsync(const Napi::CallbackInfo &info) {
		auto prog = Napi::ObjectWrap<ProgramWrap>::Unwrap(info[0].As<Napi::Object>());
		auto deferred = Napi::Promise::Deferred::New(info.Env());
		auto *worker = new LoadWorker(*this, prog->get(), deferred);
		worker->Queue();
		return deferred.Promise();
	}

	Napi::Value Unload(const Napi::CallbackInfo &info) {
		engine.unload();
		return info.Env().Undefined();
	}

	class LinkWorker : public Napi::AsyncWorker {
	  public:
		LinkWorker(EngineWrap &owner,
				   Napi::Promise::Deferred def)
			: Napi::AsyncWorker(owner.Env()),
			  engine(owner.engine),
			  deferred(def) {}

		void Execute() override {
			DiagnosticMessageList msgs;
			engine.link(msgs);

			if (!msgs.empty())
				error = msgs.toString();
		}

		void OnOK() override {
			if (error.empty())
				deferred.Resolve(Env().Undefined());
			else
				deferred.Reject(Napi::Error::New(Env(), error).Value());
		}

	  private:
		cmaj::Engine &engine;
		Napi::Promise::Deferred deferred;
		std::string error;
	};

	Napi::Value Link(const Napi::CallbackInfo &info) {
		DiagnosticMessageList msgs;
		engine.link(msgs);
		throwIfErrors(msgs, info.Env());
		return info.Env().Undefined();
	}
	Napi::Value LinkAsync(const Napi::CallbackInfo &info) {
		auto deferred = Napi::Promise::Deferred::New(info.Env());
		auto *worker = new LinkWorker(*this, deferred);
		worker->Queue();
		return deferred.Promise();
	}
	Napi::Value IsLoaded(const Napi::CallbackInfo &info) {
		return Napi::Boolean::New(info.Env(), engine.isLoaded());
	}
	Napi::Value IsLinked(const Napi::CallbackInfo &info) {
		return Napi::Boolean::New(info.Env(), engine.isLinked());
	}

	Napi::Value GetInputEndpoints(const Napi::CallbackInfo &info) {
		return Napi::String::New(info.Env(),
								 toJSON(engine.getInputEndpoints().toJSON(true)));
	}
	Napi::Value GetOutputEndpoints(const Napi::CallbackInfo &info) {
		return Napi::String::New(info.Env(),
								 toJSON(engine.getOutputEndpoints().toJSON(true)));
	}

	Napi::Value GetEndpointHandle(const Napi::CallbackInfo &info) {
		std::string id = info[0].As<Napi::String>().Utf8Value();
		auto h = engine.getEndpointHandle(id.c_str());
		remembered[id] = h;
		return Napi::Number::New(info.Env(), h);
	}

	Napi::Value CreatePerformer(const Napi::CallbackInfo &info) {
		if (!engine.isLinked())
			throw Napi::Error::New(info.Env(), "Engine not linked");

		auto *perfPtr = new cmaj::Performer(engine.createPerformer());

		Napi::Object obj = PerformerWrap::constructor.New(
			{Napi::External<cmaj::Performer>::New(info.Env(), perfPtr),
			 Napi::External<EngineWrap>::New(info.Env(), this)});
		return obj;
	}

	const std::unordered_map<std::string, EndpointHandle> &getRemembered() const {
		return remembered;
	}

	Engine &get() { return engine; }

  private:
	Engine engine;
	std::unordered_map<std::string, EndpointHandle> remembered;

	friend class PerformerWrap;
};

Napi::FunctionReference PerformerWrap::constructor;

Napi::Function PerformerWrap::Init(Napi::Env env) {
	Napi::Function func = DefineClass(
		env, "Performer",
		{InstanceMethod<&PerformerWrap::SetBlockSize>("setBlockSize"),
		 InstanceMethod<&PerformerWrap::Advance>("advance"),
		 InstanceMethod<&PerformerWrap::Reset>("reset"),
		 InstanceMethod<&PerformerWrap::GetOutputFrames>("getOutputFrames"),
		 InstanceMethod<&PerformerWrap::GetOutputEvents>("getOutputEvents"),
		 InstanceMethod<&PerformerWrap::GetOutputValue>("getOutputValue"),
		 InstanceMethod<&PerformerWrap::SetInputFrames>("setInputFrames"),
		 InstanceMethod<&PerformerWrap::SetInputValue>("setInputValue"),
		 InstanceMethod<&PerformerWrap::AddInputEvent>("addInputEvent"),
		 InstanceMethod<&PerformerWrap::GetXRuns>("getXRuns")});

	constructor = Napi::Persistent(func);
	constructor.SuppressDestruct();
	return func;
}

PerformerWrap::PerformerWrap(const Napi::CallbackInfo &info)
	: Napi::ObjectWrap<PerformerWrap>(info),
	  performer(std::unique_ptr<cmaj::Performer>(
		  info[0].As<Napi::External<cmaj::Performer>>().Data())),
	  engineOwner(*info[1].As<Napi::External<EngineWrap>>().Data()) {
	helpers.initialise(engineOwner.get(), 1024, false, false);
	helpers.initialiseDictionary(*performer);

	for (auto &kv : engineOwner.getRemembered())
		helpers.addMapping(kv.first, kv.second);
}

Napi::Value PerformerWrap::SetBlockSize(const Napi::CallbackInfo &info) {
	currentBlock = info[0].As<Napi::Number>().Uint32Value();
	performer->setBlockSize(currentBlock);
	return info.Env().Undefined();
}

Napi::Value PerformerWrap::Advance(const Napi::CallbackInfo &info) {
	performer->advance();
	return info.Env().Undefined();
}

Napi::Value PerformerWrap::Reset(const Napi::CallbackInfo &info) {
	performer->reset();
	return info.Env().Undefined();
}

Napi::Value PerformerWrap::GetOutputFrames(const Napi::CallbackInfo &info) {
	auto h = toHandle(info[0]);
	auto ch = info[1].As<Napi::Number>().Uint32Value();
	auto nfrm =
		currentBlock ? currentBlock : performer->getMaximumBlockSize();
	auto count = static_cast<size_t>(ch) * nfrm;

	auto arr = (info.Length() > 2 && info[2].IsTypedArray())
				   ? info[2].As<Napi::Int32Array>()
				   : Napi::Int32Array::New(info.Env(), count);

	if (arr.ElementLength() < count)
		throw Napi::RangeError::New(info.Env(), "TypedArray too small");

	performer->copyOutputFrames(h, reinterpret_cast<int32_t *>(arr.Data()), nfrm);
	return arr;
}

Napi::Value PerformerWrap::GetOutputEvents(const Napi::CallbackInfo &info) {
	auto h = toHandle(info[0]);
	auto v = helpers.getOutputEvents(h, *performer);
	return Napi::String::New(info.Env(), toJSON(v));
}

Napi::Value PerformerWrap::GetOutputValue(const Napi::CallbackInfo &info) {
	auto h = toHandle(info[0]);
	auto view = helpers.getViewForOutputArray(h, EndpointType::value);
	performer->copyOutputValue(h, view.getRawData());
	return Napi::String::New(info.Env(), toJSON(choc::value::Value(view)));
}

Napi::Value PerformerWrap::SetInputFrames(const Napi::CallbackInfo &info) {
	auto h = toHandle(info[0]);
	auto jsArr = info[1].As<Napi::Int32Array>();

	choc::value::Value v =
		choc::value::createVector(jsArr.Data(), jsArr.ElementLength());
	auto coerced = helpers.coerceArray(h, v, EndpointType::stream);
	performer->setInputFrames(h, coerced.data, currentBlock);
	return info.Env().Undefined();
}

Napi::Value PerformerWrap::SetInputValue(const Napi::CallbackInfo &info) {
	auto h = toHandle(info[0]);
	auto json = fromJSON(info[1]);
	uint32_t fr =
		(info.Length() > 2) ? info[2].As<Napi::Number>().Uint32Value() : 0;

	auto coerced = helpers.coerceValue(h, json);
	performer->setInputValue(h, coerced.data, fr);
	return info.Env().Undefined();
}

Napi::Value PerformerWrap::AddInputEvent(const Napi::CallbackInfo &info) {
	auto h = toHandle(info[0]);
	auto json = fromJSON(info[1]);

	auto coerced =
		helpers.coerceValueToMatchingType(h, json, EndpointType::event);
	performer->addInputEvent(h, coerced.typeIndex, coerced.data.data);
	return info.Env().Undefined();
}

Napi::Value PerformerWrap::GetXRuns(const Napi::CallbackInfo &info) {
	return Napi::Number::New(info.Env(), performer->getXRuns());
}

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
	exports.Set("Program", ProgramWrap ::Init(env));
	exports.Set("Engine", EngineWrap ::Init(env));
	exports.Set("Performer", PerformerWrap::Init(env));
	return exports;
}

NODE_API_MODULE(cmajor_node, InitAll)