// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2016-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#include "base58.h"
#include "chain.h"
#include "clientversion.h"
#include "core_io.h"
#include "init.h"
#include "validation/validation.h"
#include "validation/witnessvalidation.h"
#include "httpserver.h"
#include "net.h"
#include "netbase.h"
#include "rpc/blockchain.h"
#include "rpc/server.h"
#include "timedata.h"
#include "util.h"
#include "util/strencodings.h"
#include "util/moneystr.h"
#ifdef ENABLE_WALLET
#include "wallet/rpcwallet.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif
#include "wallet/account.h"
#include "warnings.h"
#include "wallet/witness_operations.h"


#include <stdint.h>
#ifdef HAVE_MALLOC_INFO
#include <malloc.h>
#endif

#include <univalue.h>

/**
 * @note Do not add or change anything in the information returned by this
 * method. `getinfo` exists for backwards-compatibility only. It combines
 * information from wildly different sources in the program, which is a mess,
 * and is thus planned to be deprecated eventually.
 *
 * Based on the source of the information, new information should be added to:
 * - `getblockchaininfo`,
 * - `getnetworkinfo` or
 * - `getwalletinfo`
 *
 * Or alternatively, create a specific query method for the information.
 **/
static UniValue getinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getinfo\n"
            "\nDEPRECATED. Returns an object containing various state info.\n"
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,           (numeric) the server version\n"
            "  \"protocolversion\": xxxxx,   (numeric) the protocol version\n"
            "  \"walletversion\": xxxxx,     (numeric) the wallet version\n"
            "  \"balance\": xxxxxxx,         (numeric) the total " GLOBAL_APPNAME " balance of the wallet\n"
            "  \"blocks\": xxxxxx,           (numeric) the current number of blocks processed in the server\n"
            "  \"timeoffset\": xxxxx,        (numeric) the time offset\n"
            "  \"connections\": xxxxx,       (numeric) the number of connections\n"
            "  \"proxy\": \"host:port\",     (string, optional) the proxy used by the server\n"
            "  \"difficulty\": xxxxxx,       (numeric) the current difficulty\n"
            "  \"testnet\": true|false,      (boolean) if the server is using testnet or not\n"
            "  \"keypoololdest\": xxxxxx,    (numeric) the timestamp (seconds since Unix epoch) of the oldest pre-generated key in the key pool\n"
            "  \"keypoolsize\": xxxx,        (numeric) how many new keys are pre-generated\n"
            "  \"unlocked_until\": ttt,      (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "  \"paytxfee\": x.xxxx,         (numeric) the transaction fee set in " + CURRENCY_UNIT + "/kB\n"
            "  \"relayfee\": x.xxxx,         (numeric) minimum relay fee for transactions in " + CURRENCY_UNIT + "/kB\n"
            "  \"errors\": \"...\"           (string) any error messages\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getinfo", "")
            + HelpExampleRpc("getinfo", "")
        );

#ifdef ENABLE_WALLET
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);

    LOCK2(cs_main, pwallet ? &pwallet->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("version", CLIENT_VERSION);
    obj.pushKV("protocolversion", PROTOCOL_VERSION);
#ifdef ENABLE_WALLET
    if (pwallet) {
        obj.pushKV("walletversion", pwallet->GetVersion());
        obj.pushKV("balance",       ValueFromAmount(pwallet->GetBalance(nullptr, true, false, true)));
    }
#endif
    obj.pushKV("blocks",        (int)chainActive.Height());
    obj.pushKV("timeoffset",    GetTimeOffset());
    if(g_connman)
        obj.pushKV("connections",   (int)g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL));
    obj.pushKV("proxy",         (proxy.IsValid() ? proxy.proxy.ToStringIPPort() : std::string()));
    obj.pushKV("difficulty",    (double)GetDifficulty());
    obj.pushKV("testnet",       Params().NetworkIDString() == CBaseChainParams::TESTNET);
#ifdef ENABLE_WALLET
    if (pwallet) {
        obj.pushKV("keypoololdest", pwallet->GetOldestKeyPoolTime());
        //fixme: (FUT) (BIP44)
        {
            LOCK(pwallet->activeAccount->cs_keypool);
            obj.pushKV("keypoolsize",   (int)pwallet->activeAccount->GetKeyPoolSize());
        }
    }
    if (pwallet && pwallet->IsCrypted()) {
        obj.pushKV("unlocked_until", pwallet->nRelockTime);
    }
    obj.pushKV("mininput",      ValueFromAmount(nMinimumInputValue));
    obj.pushKV("paytxfee",      ValueFromAmount(payTxFee.GetFeePerK()));
#endif
    obj.pushKV("relayfee",      ValueFromAmount(::minRelayTxFee.GetFeePerK()));
    obj.pushKV("errors",        GetWarnings("statusbar"));
    return obj;
}

#ifdef ENABLE_WALLET
class DescribeAddressVisitor : public boost::static_visitor<UniValue>
{
public:
    CWallet * const pwallet;

    DescribeAddressVisitor(CWallet *_pwallet) : pwallet(_pwallet) {}

    UniValue operator()(const CNoDestination &dest) const { return UniValue(UniValue::VOBJ); }

    UniValue operator()(const CPoW2WitnessDestination &dest) const {
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        obj.pushKV("isscript", false);
        if (pwallet)
        {
            if (pwallet->GetPubKey(dest.spendingKey, vchPubKey))
            {
                obj.pushKV("spendingpubkey", HexStr(vchPubKey));
            }
            CKey privKey;
            bool ismine = pwallet->GetKey(dest.spendingKey, privKey);
            obj.pushKV("spendingprivkey_isavailable", ismine?"true":"false");
        }
        obj.pushKV("spendingpubkeyhash", dest.spendingKey.GetHex());
        if (pwallet)
        {
            if (pwallet->GetPubKey(dest.witnessKey, vchPubKey))
            {
                obj.pushKV("witnesspubkey", HexStr(vchPubKey));
            }
            CKey privKey;
            bool ismine = pwallet->GetKey(dest.witnessKey, privKey);
            obj.pushKV("witnessprivkey_isavailable", ismine?"true":"false");
        }
        obj.pushKV("witnesspubkeyhash", dest.witnessKey.GetHex());
        return obj;
    }

    UniValue operator()(const CKeyID &keyID) const {
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        obj.pushKV("isscript", false);
        if (pwallet && pwallet->GetPubKey(keyID, vchPubKey)) {
            obj.pushKV("pubkey", HexStr(vchPubKey));
            obj.pushKV("iscompressed", vchPubKey.IsCompressed());
        }
        return obj;
    }

    UniValue operator()(const CScriptID &scriptID) const {
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        obj.pushKV("isscript", true);
        if (pwallet && pwallet->GetCScript(scriptID, subscript)) {
            std::vector<CTxDestination> addresses;
            txnouttype whichType;
            int nRequired;
            ExtractDestinations(subscript, whichType, addresses, nRequired);
            obj.pushKV("script", GetTxnOutputType(whichType));
            obj.pushKV("hex", HexStr(subscript.begin(), subscript.end()));
            UniValue a(UniValue::VARR);
            for(const CTxDestination& addr : addresses)
                a.push_back(CNativeAddress(addr).ToString());
            obj.pushKV("addresses", a);
            if (whichType == TX_MULTISIG)
                obj.pushKV("sigsrequired", nRequired);
        }
        return obj;
    }
};
#endif

UniValue validateaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "validateaddress \"address\"\n"
            "\nReturn information about the given " GLOBAL_APPNAME " address.\n"
            "\nArguments:\n"
            "1. \"address\"     (string, required) The " GLOBAL_APPNAME " address to validate\n"
            "\nResult:\n"
            "{\n"
            "  \"isvalid\" : true|false,       (boolean) If the address is valid or not. If not, this is the only property returned.\n"
            "  \"address\" : \"address\", (string) The " GLOBAL_APPNAME " address validated\n"
            "  \"scriptPubKey\" : \"hex\",       (string) The hex encoded scriptPubKey generated by the address\n"
            "  \"ismine\" : true|false,        (boolean) If the address is yours or not\n"
            "  \"iswatchonly\" : true|false,   (boolean) If the address is watchonly\n"
            "  \"isscript\" : true|false,      (boolean) If the key is a script\n"
            "  \"pubkey\" : \"publickeyhex\",    (string) The hex value of the raw public key\n"
            "  \"iscompressed\" : true|false,  (boolean) If the address is compressed\n"
            "  \"account\" : \"account\"         (string) The account associated with the address, \n"
            "  \"accountlabel\" : \"accountlabel\" (string) Label of the account associated with the address, \n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("validateaddress", "\"GPSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")
            + HelpExampleRpc("validateaddress", "\"GPSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")
        );

#ifdef ENABLE_WALLET
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);

    LOCK2(cs_main, pwallet ? &pwallet->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    CNativeAddress address(request.params[0].get_str());
    bool isValid = address.IsValid();

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("isvalid", isValid);
    if (isValid)
    {
        CTxDestination dest = address.Get();
        std::string currentAddress = address.ToString();
        ret.pushKV("address", currentAddress);

        //fixme: (PHASE5) Add some segsig specific output here.
        CScript scriptPubKey = GetScriptForDestination(dest);
        ret.pushKV("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

#ifdef ENABLE_WALLET
        if (pwallet)
        {
            isminetype mine = IsMine(*pwallet, dest);
            ret.pushKV("ismine", (mine & ISMINE_SPENDABLE) ? true : false);
            ret.pushKV("iswatchonly", (mine & ISMINE_WATCH_ONLY) ? true: false);
            UniValue detail = boost::apply_visitor(DescribeAddressVisitor(pwallet), dest);
            ret.pushKVs(detail);
            for (const auto& accountIter : pwallet->mapAccounts)
            {
                if (IsMine(*accountIter.second, dest) > ISMINE_WATCH_ONLY )
                {
                    ret.pushKV("account", getUUIDAsString(accountIter.second->getUUID()));
                    ret.pushKV("accountlabel", accountIter.second->getLabel());
                }
            }
        }
#endif
    }
    return ret;
}

static UniValue getaddress(const JSONRPCRequest& request)
{
    LOCK(cs_main);

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaddress \"pubkey_or_script\" \n"
            "\nGet the address of a pubkey or script\n"
            "\nTo get the pubkey of an address use 'validateaddress'\n"
            "\nArguments:\n"
            "1. \"pubkey_or_script\"       (required) An hex encoded script or public key.\n"
            "\nResult:\n"
            "\nReturn an array of addresses on success\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddress \"Vd69eLAZ2r76C47xB3pDLa9Fx4Li8Xt5AHgzjJDuLbkP8eqUjToC\"", "")
            + HelpExampleRpc("getaddress \"Vd69eLAZ2r76C47xB3pDLa9Fx4Li8Xt5AHgzjJDuLbkP8eqUjToC\"", ""));

    std::string pubKeyOrScript = request.params[0].get_str();
    if (!IsHex(pubKeyOrScript))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Data is not hex encoded");

    UniValue result(UniValue::VARR);

    // Try public key first.
    std::vector<unsigned char> data(ParseHex(pubKeyOrScript));
    CPubKey pubKey(data.begin(), data.end());
    if (pubKey.IsFullyValid())
    {
        result.push_back(CNativeAddress(pubKey.GetID()).ToString());
    }
    else
    {
        // Not a public key so treat it as a script.
        CScript scriptPubKey(data.begin(), data.end());
        std::vector<CTxDestination> addresses;

        int nRequired;
        txnouttype type;
        if (ExtractDestinations(scriptPubKey, type, addresses, nRequired))
        {
            for(const CTxDestination& addr : addresses)
            {
                result.push_back(CNativeAddress(addr).ToString());
            }
        }
        //fixme: (PHASE5) Check that this handles p2sh correctly (handle ExtractDestinations failiure - look at decodescript to get an idea of what needs to be done)
    }

    return result;
}

// Needed even with !ENABLE_WALLET, to pass (ignored) pointers around
class CWallet;

/**
 * Used by addmultisigaddress / createmultisig:
 */
CScript _createmultisig_redeemScript(CAccount* const forAccount, const UniValue& params)
{
    int nRequired = params[0].get_int();
    const UniValue& keys = params[1].get_array();

    // Gather public keys
    if (nRequired < 1) { throw std::runtime_error("a multisignature address must require at least one key to redeem"); }
    if ((int)keys.size() < nRequired) { throw std::runtime_error(strprintf("not enough keys supplied (got %u keys, but need at least %d to redeem)", keys.size(), nRequired)); }
    if (keys.size() > 16) { throw std::runtime_error("Number of addresses involved in the multisignature address creation > 16\nReduce the number"); }
    
    std::vector<CPubKey> pubkeys;
    pubkeys.resize(keys.size());
    for (unsigned int i = 0; i < keys.size(); i++)
    {
        const std::string& ks = keys[i].get_str();
        #ifdef ENABLE_WALLET
        CNativeAddress address(ks);
        if (forAccount && address.IsValid())
        {
            // Case 1: Gulden address and we have full public key:
            CKeyID keyID;
            CPubKey vchPubKey;
            if (!address.GetKeyID(keyID)) { throw std::runtime_error(strprintf("%s does not refer to a key",ks)); }
            if (!forAccount->GetPubKey(keyID, vchPubKey)){ throw std::runtime_error(strprintf("no full public key for address %s",ks)); }
            if (!vchPubKey.IsFullyValid()) { throw std::runtime_error(" Invalid public key: "+ks); }
            pubkeys[i] = vchPubKey;
        }
        else
        #endif
        {
            // Case 2: hex public key
            bool validPublicKey=false;
            if (IsHex(ks))
            {
                CPubKey vchPubKey(ParseHex(ks));
                if (vchPubKey.IsFullyValid())
                {
                    validPublicKey = true;
                    pubkeys[i] = vchPubKey;
                }
            }
            if (!validPublicKey)
            {
                throw std::runtime_error(" Invalid public key: "+ks);
            }
        }
    }
    CScript result = GetScriptForMultisig(nRequired, pubkeys);

    if (result.size() > MAX_SCRIPT_ELEMENT_SIZE)
    { 
        throw std::runtime_error(strprintf("redeemScript exceeds size limit: %d > %d", result.size(), MAX_SCRIPT_ELEMENT_SIZE));
    }

    return result;
}

UniValue createmultisig(const JSONRPCRequest& request)
{
#ifdef ENABLE_WALLET
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
#endif

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 2)
    {
        std::string msg = "createmultisig num_required [\"key\",...]\n"
            "\nCreates a multi-signature address with n signature of m keys required.\n"
            "It returns a json object with the address and redeemScript.\n"

            "\nArguments:\n"
            "1. num_required   (numeric, required) The number of required signatures out of the n keys or addresses.\n"
            "2. \"keys\"       (string, required) A json array of keys which are " GLOBAL_APPNAME " addresses or hex-encoded public keys\n"
            "     [\n"
            "       \"key\"    (string) " GLOBAL_APPNAME " address or hex-encoded public key\n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "{\n"
            "  \"address\":\"multisigaddress\",  (string) The value of the new multisig address.\n"
            "  \"redeemScript\":\"script\"       (string) The string value of the hex-encoded redemption script.\n"
            "}\n"

            "\nExamples:\n"
            "\nCreate a multisig address from 2 addresses\n"
            + HelpExampleCli("createmultisig", "2 \"[\\\"G6sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"G71sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("createmultisig", "2, \"[\\\"G6sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"G71sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"")
        ;
        throw std::runtime_error(msg);
    }

    // Construct using pay-to-script-hash:
    #ifdef ENABLE_WALLET
    CScript inner = _createmultisig_redeemScript(pwallet->activeAccount, request.params);
    #else
    CScript inner = _createmultisig_redeemScript(nullptr, request.params);
    #endif
    CScriptID innerID(inner);
    CNativeAddress address(innerID);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", address.ToString());
    result.pushKV("redeemScript", HexStr(inner.begin(), inner.end()));

    return result;
}

UniValue verifymessage(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3)
        throw std::runtime_error(
            "verifymessage \"address\" \"signature\" \"message\"\n"
            "\nVerify a signed message\n"
            "\nArguments:\n"
            "1. \"address\"         (string, required) The " GLOBAL_APPNAME " address to use for the signature.\n"
            "2. \"signature\"       (string, required) The signature provided by the signer in base 64 encoding (see signmessage).\n"
            "3. \"message\"         (string, required) The message that was signed.\n"
            "\nResult:\n"
            "true|false   (boolean) If the signature is verified or not.\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n"
            + HelpExampleCli("signmessage", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"my message\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifymessage", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"signature\" \"my message\"") +
            "\nAs json rpc\n"
            + HelpExampleRpc("verifymessage", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\", \"signature\", \"my message\"")
        );

    LOCK(cs_main);

    std::string strAddress  = request.params[0].get_str();
    std::string strSign     = request.params[1].get_str();
    std::string strMessage  = request.params[2].get_str();

    CNativeAddress addr(strAddress);
    if (!addr.IsValid())
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");

    bool fInvalid = false;
    std::vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == keyID);
}

UniValue signmessagewithprivkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "signmessagewithprivkey \"privkey\" \"message\"\n"
            "\nSign a message with the private key of an address\n"
            "\nArguments:\n"
            "1. \"privkey\"         (string, required) The private key to sign the message with.\n"
            "2. \"message\"         (string, required) The message to create a signature of.\n"
            "\nResult:\n"
            "\"signature\"          (string) The signature of the message encoded in base 64\n"
            "\nExamples:\n"
            "\nCreate the signature\n"
            + HelpExampleCli("signmessagewithprivkey", "\"privkey\" \"my message\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"signature\" \"my message\"") +
            "\nAs json rpc\n"
            + HelpExampleRpc("signmessagewithprivkey", "\"privkey\", \"my message\"")
        );

    std::string strPrivkey = request.params[0].get_str();
    std::string strMessage = request.params[1].get_str();

    CEncodedSecretKey vchSecret;
    bool fGood = vchSecret.SetString(strPrivkey);
    if (!fGood)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
    CKey key = vchSecret.GetKey();
    if (!key.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");

    return EncodeBase64(&vchSig[0], vchSig.size());
}

static bool HashFromString(const std::string& strReq, uint256& v)
{
    if (!IsHex(strReq) || (strReq.size() != 64))
        return false;

    v.SetHex(strReq);
    return true;
}

UniValue verifyproofoffunds(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "verifymessage \"address\" \"signature\" \"message\"\n"
            "\nVerify a signed proof of funds message\n"
            "\nArguments:\n"
            "1. \"proof\"       (string, required) The proof provided by the signer (see generateproofoffunds).\n"
            "\nResult:\n"
            "true|false   (boolean) If the signature is verified or not.\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n"
            + HelpExampleCli("signmessage", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"my message\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifymessage", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"signature\" \"my message\"") +
            "\nAs json rpc\n"
            + HelpExampleRpc("verifymessage", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\", \"signature\", \"my message\"")
        );

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("isvalid", false);
    
    std::string strSign     = request.params[0].get_str();
    std::vector<std::string> vStrInputParts;
    boost::split(vStrInputParts, strSign, boost::is_any_of(":"));
    if (vStrInputParts.size() != 2)
    {
        ret.pushKV("info", "invalid proof");
        return ret;
    }
    
    std::string address = vStrInputParts[0];
    std::string strBlockHash = vStrInputParts[1];
    uint256 blockHash;
    if (!HashFromString(strBlockHash, blockHash))
    {
        ret.pushKV("info", "proof with invalid hash");
        return ret;
    }
    
    if (mapBlockIndex.count(blockHash) == 0)
    {
        ret.pushKV("info", "proof with invalid block");
        return ret;
    }
    
    CBlock block;
    CBlockIndex* signingChainTip = mapBlockIndex[blockHash];

    bool fInvalid = false;
    std::vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
    {
        ret.pushKV("info", "proof with malformed base64 encoding");
        return ret;
    }

    CHashWriter ss(SER_GETHASH, 0);
    ss << std::string("Florin Proof Of Funds:\n");
    // Introduce sufficient entropy such that it would be difficult to force signing of a weak message
    auto prev = signingChainTip;
    for (int i=0; i < 20; ++i)
    {
        prev->GetBlockHashPoW2().Serialize(ss);
        prev=prev->pprev;
    }

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
    {
        ret.pushKV("info", "unable to recover pubkey from proof");
        return ret;
    }
    
    std::map<COutPoint, Coin> allWitnessCoins;
    if (!getAllUnspentWitnessCoins(chainActive, Params(), chainActive.Tip(), allWitnessCoins))
    {
        ret.pushKV("info", "failed to enumerate all witness coins");
        return ret;
    }

    CAmount fundsForKey = 0;
    int partsForKey = 0;
    for (const auto& [outpoint, coin] : allWitnessCoins)
    {
        CTxDestination compareDestination;
        bool fValidAddress = ExtractDestination(coin.out, compareDestination);

        if (fValidAddress && boost::get<CPoW2WitnessDestination>(compareDestination).spendingKey == pubkey.GetID())
        {
            fundsForKey += coin.out.nValue;
            ++partsForKey;
        }
    }

    if (fundsForKey)
    {
        ret.pushKV("isvalid", true);
        ret.pushKV("info", "valid proof with funds");
        ret.pushKV("height", signingChainTip->nHeight);
        ret.pushKV("amount", FormatMoney(fundsForKey));
        ret.pushKV("parts", partsForKey);
        return ret;
    }
    else
    {
        ret.pushKV("info", "valid proof but no funds tied to key");
        return ret;
    }
}

UniValue generateproofoffunds(const JSONRPCRequest& request)
{
    #ifndef ENABLE_WALLET
    return "Command not supported without wallet";
    #else
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    LOCK2(cs_main, pwallet ? &pwallet->cs_wallet : NULL);

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "generateproofoffunds \"account\"\n"
            "\nSign a message that proves a holding account is yours\n"
            "\nAnd that can be used by others to see that you have control of the funds you claim\n"
            "\nArguments:\n"
            "1. \"account\"         (string, required) The private key to sign the message with.\n"
            "\nResult:\n"
            "\"signature\"          (string) The signature of the message encoded in base 64\n"
            "\nExamples:\n"
            "\nCreate the proof\n"
            + HelpExampleCli("generateproofoffunds", "\"my account\"") +
            "\nAs json rpc\n"
            + HelpExampleRpc("generateproofoffunds", "\"my account\"")
        );

    if (!pwallet)
        throw std::runtime_error("Cannot use command without an active wallet");

    EnsureWalletIsUnlocked(pwallet);
    
    CAccount* forAccount = AccountFromValue(pwallet, request.params[0], false);
    
    if (!forAccount->IsPoW2Witness())
        throw std::runtime_error("This command only works on holding accounts");
    
    std::string address = witnessAddressForAccount(pwallet, forAccount);
    CKeyID witnessKeyID = spendingKeyForWitnessAccount(pwallet, forAccount);
    CKey witnessPrivKey;
    if (!forAccount->GetKey(witnessKeyID, witnessPrivKey))
    {
        throw std::runtime_error("Unable to read private key for holding account");
    }
    if (!witnessPrivKey.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");

    CEncodedSecretKey vchSecret;
    CHashWriter ss(SER_GETHASH, 0);
    ss << std::string("Florin Proof Of Funds:\n");
    // Introduce sufficient entropy such that it would be difficult to force signing of a weak message
    auto signingChainTip = chainActive.Tip();
    auto prev = signingChainTip;
    for (int i=0; i < 20; ++i)
    {
        prev->GetBlockHashPoW2().Serialize(ss);
        prev=prev->pprev;
    }

    std::vector<unsigned char> vchSig;
    if (!witnessPrivKey.SignCompact(ss.GetHash(), vchSig))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");

    return EncodeBase64(&vchSig[0], vchSig.size()) + ":" + signingChainTip->GetBlockHashPoW2().ToString();
    #endif
}

UniValue forcesigseg(const JSONRPCRequest& request)
{
 if (request.fHelp)
        throw std::runtime_error("force program to perform an illegal operation and trigger a sigseg, useful to test debugging features");
  ++*(int*)0;
  return NullUniValue;
}


UniValue setmocktime(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "setmocktime timestamp\n"
            "\nSet the local time to given timestamp (-regtest only)\n"
            "\nArguments:\n"
            "1. timestamp  (integer, required) Unix seconds-since-epoch timestamp\n"
            "   Pass 0 to go back to using the system time."
        );

    if (!Params().MineBlocksOnDemand())
        throw std::runtime_error("setmocktime for regression testing (-regtest mode) only");

    // For now, don't change mocktime if we're in the middle of validation, as
    // this could have an effect on mempool time-based eviction, as well as
    // IsCurrentForFeeEstimation() and IsInitialBlockDownload().
    // TODO: figure out the right way to synchronize around mocktime, and
    // ensure all call sites of GetTime() are accessing this safely.
    LOCK(cs_main);

    RPCTypeCheck(request.params, {UniValue::VNUM});
    SetMockTime(request.params[0].get_int64());

    return NullUniValue;
}

static UniValue RPCLockedMemoryInfo()
{
    LockedPool::Stats stats = LockedPoolManager::Instance().stats();
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("used", uint64_t(stats.used));
    obj.pushKV("free", uint64_t(stats.free));
    obj.pushKV("total", uint64_t(stats.total));
    obj.pushKV("locked", uint64_t(stats.locked));
    obj.pushKV("chunks_used", uint64_t(stats.chunks_used));
    obj.pushKV("chunks_free", uint64_t(stats.chunks_free));
    return obj;
}

#ifdef HAVE_MALLOC_INFO
static std::string RPCMallocInfo()
{
    char *ptr = nullptr;
    size_t size = 0;
    FILE *f = open_memstream(&ptr, &size);
    if (f) {
        malloc_info(0, f);
        fclose(f);
        if (ptr) {
            std::string rv(ptr, size);
            free(ptr);
            return rv;
        }
    }
    return "";
}
#endif

UniValue getmemoryinfo(const JSONRPCRequest& request)
{
    /* Please, avoid using the word "pool" here in the RPC interface or help,
     * as users will undoubtedly confuse it with the other "memory pool"
     */
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getmemoryinfo (\"mode\")\n"
            "Returns an object containing information about memory usage.\n"
            "Arguments:\n"
            "1. \"mode\" determines what kind of information is returned. This argument is optional, the default mode is \"stats\".\n"
            "  - \"stats\" returns general statistics about memory usage in the daemon.\n"
            "  - \"mallocinfo\" returns an XML string describing low-level heap state (only available if compiled with glibc 2.10+).\n"
            "\nResult (mode \"stats\"):\n"
            "{\n"
            "  \"locked\": {               (json object) Information about locked memory manager\n"
            "    \"used\": xxxxx,          (numeric) Number of bytes used\n"
            "    \"free\": xxxxx,          (numeric) Number of bytes available in current arenas\n"
            "    \"total\": xxxxxxx,       (numeric) Total number of bytes managed\n"
            "    \"locked\": xxxxxx,       (numeric) Amount of bytes that succeeded locking. If this number is smaller than total, locking pages failed at some point and key data could be swapped to disk.\n"
            "    \"chunks_used\": xxxxx,   (numeric) Number allocated chunks\n"
            "    \"chunks_free\": xxxxx,   (numeric) Number unused chunks\n"
            "  }\n"
            "}\n"
            "\nResult (mode \"mallocinfo\"):\n"
            "\"<malloc version=\"1\">...\"\n"
            "\nExamples:\n"
            + HelpExampleCli("getmemoryinfo", "")
            + HelpExampleRpc("getmemoryinfo", "")
        );

    std::string mode = (request.params.size() < 1 || request.params[0].isNull()) ? "stats" : request.params[0].get_str();
    if (mode == "stats") {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("locked", RPCLockedMemoryInfo());
        return obj;
    } else if (mode == "mallocinfo") {
#ifdef HAVE_MALLOC_INFO
        return RPCMallocInfo();
#else
        throw JSONRPCError(RPC_INVALID_PARAMETER, "mallocinfo is only available when compiled with glibc 2.10+");
#endif
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "unknown mode " + mode);
    }
}

uint32_t getCategoryMask(UniValue cats) {
    cats = cats.get_array();
    uint32_t mask = 0;
    for (unsigned int i = 0; i < cats.size(); ++i) {
        uint32_t flag = 0;
        std::string cat = cats[i].get_str();
        if (!GetLogCategory(&flag, &cat)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "unknown logging category " + cat);
        }
        mask |= flag;
    }
    return mask;
}

UniValue logging(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2) {
        throw std::runtime_error(
            "logging [include,...] <exclude>\n"
            "Gets and sets the logging configuration.\n"
            "When called without an argument, returns the list of categories that are currently being debug logged.\n"
            "When called with arguments, adds or removes categories from debug logging.\n"
            "The valid logging categories are: " + ListLogCategories() + "\n"
            "libevent logging is configured on startup and cannot be modified by this RPC during runtime."
            "Arguments:\n"
            "1. \"include\" (array of strings) add debug logging for these categories.\n"
            "2. \"exclude\" (array of strings) remove debug logging for these categories.\n"
            "\nResult: <categories>  (string): a list of the logging categories that are active.\n"
            "\nExamples:\n"
            + HelpExampleCli("logging", "\"[\\\"all\\\"]\" \"[\\\"http\\\"]\"")
            + HelpExampleRpc("logging", "[\"all\"], \"[libevent]\"")
        );
    }

    uint32_t originalLogCategories = logCategories;
    if (request.params.size() > 0 && request.params[0].isArray()) {
        logCategories |= getCategoryMask(request.params[0]);
    }

    if (request.params.size() > 1 && request.params[1].isArray()) {
        logCategories &= ~getCategoryMask(request.params[1]);
    }

    // Update libevent logging if BCLog::LIBEVENT has changed.
    // If the library version doesn't allow it, UpdateHTTPServerLogging() returns false,
    // in which case we should clear the BCLog::LIBEVENT flag.
    // Throw an error if the user has explicitly asked to change only the libevent
    // flag and it failed.
    uint32_t changedLogCategories = originalLogCategories ^ logCategories;
    if (changedLogCategories & BCLog::LIBEVENT) {
        if (!UpdateHTTPServerLogging(logCategories & BCLog::LIBEVENT)) {
            logCategories &= ~BCLog::LIBEVENT;
            if (changedLogCategories == BCLog::LIBEVENT) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "libevent logging cannot be updated when using libevent before v2.1.1.");
            }
        }
    }

    UniValue result(UniValue::VOBJ);
    std::vector<CLogCategoryActive> vLogCatActive = ListActiveLogCategories();
    for (const auto& logCatActive : vLogCatActive) {
        result.pushKV(logCatActive.category, logCatActive.active);
    }

    return result;
}

UniValue echo(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            "echo|echojson \"message\" ...\n"
            "\nSimply echo back the input arguments. This command is for testing.\n"
            "\nThe difference between echo and echojson is that echojson has argument conversion enabled in the client-side table in"
            GLOBAL_APPNAME "-cli and the GUI. There is no server-side difference."
        );

    return request.params;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "control",            "getinfo",                &getinfo,                true,  {} }, /* uses wallet if enabled */
    { "control",            "getmemoryinfo",          &getmemoryinfo,          true,  {"mode"} },

    { "util",               "getaddress",             &getaddress,             true,  {"pubkey_or_script"} },
    { "util",               "validateaddress",        &validateaddress,        true,  {"address"} }, /* uses wallet if enabled */
    { "util",               "createmultisig",         &createmultisig,         true,  {"num_required","keys"} },
    { "util",               "verifymessage",          &verifymessage,          true,  {"address","signature","message"} },
    { "util",               "signmessagewithprivkey", &signmessagewithprivkey, true,  {"privkey","message"} },
    { "util",               "verifyproofoffunds",     &verifyproofoffunds,     true,  {"signature"} },
    { "util",               "generateproofoffunds",   &generateproofoffunds,   true,  {"address"} },

    /* Not shown in help */
    { "hidden",             "setmocktime",            &setmocktime,            true,  {"timestamp"}},
    { "hidden",             "forcesigseg",            &forcesigseg,      true,  {}},
    { "hidden",             "echo",                   &echo,                   true,  {"arg0","arg1","arg2","arg3","arg4","arg5","arg6","arg7","arg8","arg9"}},
    { "hidden",             "echojson",               &echo,                   true,  {"arg0","arg1","arg2","arg3","arg4","arg5","arg6","arg7","arg8","arg9"}},
    { "hidden",             "logging",                &logging,                true,  {"include", "exclude"}},
};

void RegisterMiscRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
