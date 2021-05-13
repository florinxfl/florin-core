// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

#pragma once

#include "djinni_support.hpp"
#include "i_p2p_network_listener.hpp"

namespace djinni_generated {

class NativeIP2pNetworkListener final : ::djinni::JniInterface<::IP2pNetworkListener, NativeIP2pNetworkListener> {
public:
    using CppType = std::shared_ptr<::IP2pNetworkListener>;
    using CppOptType = std::shared_ptr<::IP2pNetworkListener>;
    using JniType = jobject;

    using Boxed = NativeIP2pNetworkListener;

    ~NativeIP2pNetworkListener();

    static CppType toCpp(JNIEnv* jniEnv, JniType j) { return ::djinni::JniClass<NativeIP2pNetworkListener>::get()._fromJava(jniEnv, j); }
    static ::djinni::LocalRef<JniType> fromCppOpt(JNIEnv* jniEnv, const CppOptType& c) { return {jniEnv, ::djinni::JniClass<NativeIP2pNetworkListener>::get()._toJava(jniEnv, c)}; }
    static ::djinni::LocalRef<JniType> fromCpp(JNIEnv* jniEnv, const CppType& c) { return fromCppOpt(jniEnv, c); }

private:
    NativeIP2pNetworkListener();
    friend ::djinni::JniClass<NativeIP2pNetworkListener>;
    friend ::djinni::JniInterface<::IP2pNetworkListener, NativeIP2pNetworkListener>;

    class JavaProxy final : ::djinni::JavaProxyHandle<JavaProxy>, public ::IP2pNetworkListener
    {
    public:
        JavaProxy(JniType j);
        ~JavaProxy();

        void onNetworkEnabled() override;
        void onNetworkDisabled() override;
        void onConnectionCountChanged(int32_t numConnections) override;
        void onBytesChanged(int32_t totalRecv, int32_t totalSent) override;

    private:
        friend ::djinni::JniInterface<::IP2pNetworkListener, ::djinni_generated::NativeIP2pNetworkListener>;
    };

    const ::djinni::GlobalRef<jclass> clazz { ::djinni::jniFindClass("com/florin/jniunifiedbackend/IP2pNetworkListener") };
    const jmethodID method_onNetworkEnabled { ::djinni::jniGetMethodID(clazz.get(), "onNetworkEnabled", "()V") };
    const jmethodID method_onNetworkDisabled { ::djinni::jniGetMethodID(clazz.get(), "onNetworkDisabled", "()V") };
    const jmethodID method_onConnectionCountChanged { ::djinni::jniGetMethodID(clazz.get(), "onConnectionCountChanged", "(I)V") };
    const jmethodID method_onBytesChanged { ::djinni::jniGetMethodID(clazz.get(), "onBytesChanged", "(II)V") };
};

}  // namespace djinni_generated
