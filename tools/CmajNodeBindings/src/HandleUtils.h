#pragma once
#define NAPI_EXPERIMENTAL
#include <memory>
#include <napi.h>
#include <string>

template <class T>
using Ptr = std::unique_ptr<T>;

template <class T>
inline Napi::External<Ptr<T>> makeHandle(Napi::Env env, Ptr<T> &&p) {
    return Napi::External<Ptr<T>>::New(env, new Ptr<T>(std::move(p)),
                                       [](Napi::Env, Ptr<T> *pp) { delete pp; });
}
template <class T>
inline Napi::External<Ptr<T>> makeHandle(Napi::Env env, T &&value) {
    return makeHandle<T>(env,
                         std::make_unique<T>(std::move(value)));
}

template <class T>
inline T *get(const Napi::CallbackInfo &info, size_t index) {
    if (info.Length() <= index || !info[index].IsExternal())
        throw Napi::TypeError::New(info.Env(), "argument is not a Cmajor handle");

    auto ptr = info[index].As<Napi::External<Ptr<T>>>().Data();
    return ptr->get();
}

inline Napi::Object makeResult(Napi::Env env, bool ok, const std::string &msg = {}) {
    auto o = Napi::Object::New(env);
    o.Set("ok", Napi::Boolean::New(env, ok));
    if (!ok)
        o.Set("msg", Napi::String::New(env, msg));
    return o;
}