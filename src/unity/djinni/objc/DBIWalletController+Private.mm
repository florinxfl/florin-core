// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

#import "DBIWalletController+Private.h"
#import "DBIWalletController.h"
#import "DBBalanceRecord+Private.h"
#import "DBIWalletListener+Private.h"
#import "DJICppWrapperCache+Private.h"
#import "DJIError.h"
#import "DJIMarshal+Private.h"
#include <exception>
#include <stdexcept>
#include <utility>

static_assert(__has_feature(objc_arc), "Djinni requires ARC to be enabled for this file");

@interface DBIWalletController ()

- (id)initWithCpp:(const std::shared_ptr<::IWalletController>&)cppRef;

@end

@implementation DBIWalletController {
    ::djinni::CppProxyCache::Handle<std::shared_ptr<::IWalletController>> _cppRefHandle;
}

- (id)initWithCpp:(const std::shared_ptr<::IWalletController>&)cppRef
{
    if (self = [super init]) {
        _cppRefHandle.assign(cppRef);
    }
    return self;
}

+ (void)setListener:(nullable id<DBIWalletListener>)networklistener {
    try {
        ::IWalletController::setListener(::djinni_generated::IWalletListener::toCpp(networklistener));
    } DJINNI_TRANSLATE_EXCEPTIONS()
}

+ (BOOL)HaveUnconfirmedFunds {
    try {
        auto objcpp_result_ = ::IWalletController::HaveUnconfirmedFunds();
        return ::djinni::Bool::fromCpp(objcpp_result_);
    } DJINNI_TRANSLATE_EXCEPTIONS()
}

+ (int64_t)GetBalanceSimple {
    try {
        auto objcpp_result_ = ::IWalletController::GetBalanceSimple();
        return ::djinni::I64::fromCpp(objcpp_result_);
    } DJINNI_TRANSLATE_EXCEPTIONS()
}

+ (nonnull DBBalanceRecord *)GetBalance {
    try {
        auto objcpp_result_ = ::IWalletController::GetBalance();
        return ::djinni_generated::BalanceRecord::fromCpp(objcpp_result_);
    } DJINNI_TRANSLATE_EXCEPTIONS()
}

+ (BOOL)AbandonTransaction:(nonnull NSString *)txHash {
    try {
        auto objcpp_result_ = ::IWalletController::AbandonTransaction(::djinni::String::toCpp(txHash));
        return ::djinni::Bool::fromCpp(objcpp_result_);
    } DJINNI_TRANSLATE_EXCEPTIONS()
}

+ (nonnull NSString *)GetUUID {
    try {
        auto objcpp_result_ = ::IWalletController::GetUUID();
        return ::djinni::String::fromCpp(objcpp_result_);
    } DJINNI_TRANSLATE_EXCEPTIONS()
}

namespace djinni_generated {

auto IWalletController::toCpp(ObjcType objc) -> CppType
{
    if (!objc) {
        return nullptr;
    }
    return objc->_cppRefHandle.get();
}

auto IWalletController::fromCppOpt(const CppOptType& cpp) -> ObjcType
{
    if (!cpp) {
        return nil;
    }
    return ::djinni::get_cpp_proxy<DBIWalletController>(cpp);
}

}  // namespace djinni_generated

@end
