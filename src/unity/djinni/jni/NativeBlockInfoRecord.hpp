// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

#pragma once

#include "block_info_record.hpp"
#include "djinni_support.hpp"

namespace djinni_generated {

class NativeBlockInfoRecord final {
public:
    using CppType = ::BlockInfoRecord;
    using JniType = jobject;

    using Boxed = NativeBlockInfoRecord;

    ~NativeBlockInfoRecord();

    static CppType toCpp(JNIEnv* jniEnv, JniType j);
    static ::djinni::LocalRef<JniType> fromCpp(JNIEnv* jniEnv, const CppType& c);

private:
    NativeBlockInfoRecord();
    friend ::djinni::JniClass<NativeBlockInfoRecord>;

    const ::djinni::GlobalRef<jclass> clazz { ::djinni::jniFindClass("com/florin/jniunifiedbackend/BlockInfoRecord") };
    const jmethodID jconstructor { ::djinni::jniGetMethodID(clazz.get(), "<init>", "(IJLjava/lang/String;)V") };
    const jfieldID field_mHeight { ::djinni::jniGetFieldID(clazz.get(), "mHeight", "I") };
    const jfieldID field_mTimeStamp { ::djinni::jniGetFieldID(clazz.get(), "mTimeStamp", "J") };
    const jfieldID field_mBlockHash { ::djinni::jniGetFieldID(clazz.get(), "mBlockHash", "Ljava/lang/String;") };
};

}  // namespace djinni_generated
