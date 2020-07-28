// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

#include "i_wallet_listener.hpp"
#include <memory>

static_assert(__has_feature(objc_arc), "Djinni requires ARC to be enabled for this file");

@protocol DBIWalletListener;

namespace djinni_generated {

class IWalletListener
{
public:
    using CppType = std::shared_ptr<::IWalletListener>;
    using CppOptType = std::shared_ptr<::IWalletListener>;
    using ObjcType = id<DBIWalletListener>;

    using Boxed = IWalletListener;

    static CppType toCpp(ObjcType objc);
    static ObjcType fromCppOpt(const CppOptType& cpp);
    static ObjcType fromCpp(const CppType& cpp) { return fromCppOpt(cpp); }

private:
    class ObjcProxy;
};

}  // namespace djinni_generated

