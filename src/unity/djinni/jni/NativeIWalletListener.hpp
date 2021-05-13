// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

#pragma once

#include "djinni_support.hpp"
#include "i_wallet_listener.hpp"

namespace djinni_generated {

class NativeIWalletListener final : ::djinni::JniInterface<::IWalletListener, NativeIWalletListener> {
public:
    using CppType = std::shared_ptr<::IWalletListener>;
    using CppOptType = std::shared_ptr<::IWalletListener>;
    using JniType = jobject;

    using Boxed = NativeIWalletListener;

    ~NativeIWalletListener();

    static CppType toCpp(JNIEnv* jniEnv, JniType j) { return ::djinni::JniClass<NativeIWalletListener>::get()._fromJava(jniEnv, j); }
    static ::djinni::LocalRef<JniType> fromCppOpt(JNIEnv* jniEnv, const CppOptType& c) { return {jniEnv, ::djinni::JniClass<NativeIWalletListener>::get()._toJava(jniEnv, c)}; }
    static ::djinni::LocalRef<JniType> fromCpp(JNIEnv* jniEnv, const CppType& c) { return fromCppOpt(jniEnv, c); }

private:
    NativeIWalletListener();
    friend ::djinni::JniClass<NativeIWalletListener>;
    friend ::djinni::JniInterface<::IWalletListener, NativeIWalletListener>;

    class JavaProxy final : ::djinni::JavaProxyHandle<JavaProxy>, public ::IWalletListener
    {
    public:
        JavaProxy(JniType j);
        ~JavaProxy();

        void notifyBalanceChange(const ::BalanceRecord & new_balance) override;
        void notifyNewMutation(const ::MutationRecord & mutation, bool self_committed) override;
        void notifyUpdatedTransaction(const ::TransactionRecord & transaction) override;

    private:
        friend ::djinni::JniInterface<::IWalletListener, ::djinni_generated::NativeIWalletListener>;
    };

    const ::djinni::GlobalRef<jclass> clazz { ::djinni::jniFindClass("com/florin/jniunifiedbackend/IWalletListener") };
    const jmethodID method_notifyBalanceChange { ::djinni::jniGetMethodID(clazz.get(), "notifyBalanceChange", "(Lcom/florin/jniunifiedbackend/BalanceRecord;)V") };
    const jmethodID method_notifyNewMutation { ::djinni::jniGetMethodID(clazz.get(), "notifyNewMutation", "(Lcom/florin/jniunifiedbackend/MutationRecord;Z)V") };
    const jmethodID method_notifyUpdatedTransaction { ::djinni::jniGetMethodID(clazz.get(), "notifyUpdatedTransaction", "(Lcom/florin/jniunifiedbackend/TransactionRecord;)V") };
};

}  // namespace djinni_generated
