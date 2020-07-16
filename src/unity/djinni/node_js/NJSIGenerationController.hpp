// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

#ifndef DJINNI_GENERATED_NJSIGENERATIONCONTROLLER_HPP
#define DJINNI_GENERATED_NJSIGENERATIONCONTROLLER_HPP


#include "NJSIGenerationListener.hpp"
#include <cstdint>
#include <memory>
#include <string>

#include <napi.h>
#include <uv.h>
#include <i_generation_controller.hpp>

using namespace std;

class NJSIGenerationController: public Napi::ObjectWrap<NJSIGenerationController> {
public:

    static Napi::FunctionReference constructor;
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    NJSIGenerationController(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NJSIGenerationController>(info){};

private:
    /** Register listener to be notified of generation related events */
    void setListener(const Napi::CallbackInfo& info);

    /**
     * Activate block generation (proof of work)
     * Number of threads should not exceed physical threads, memory limit is a string specifier in the form of #B/#K/#M/#G (e.g. 102400B, 10240K, 1024M, 1G)
     */
    Napi::Value startGeneration(const Napi::CallbackInfo& info);

    /** Stop any active block generation (proof of work) */
    Napi::Value stopGeneration(const Napi::CallbackInfo& info);

};
#endif //DJINNI_GENERATED_NJSIGENERATIONCONTROLLER_HPP
