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

#include "appname.h"
#include "amount.h"
#include "base58.h"
#include "chain.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include <unity/appmanager.h>
#include "validation/validation.h"
#include "net.h"
#include "policy/feerate.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "policy/rbf.h"
#include "rpc/server.h"
#include "script/sign.h"
#include "timedata.h"
#include "util.h"
#include "witnessutil.h"
#include "util/moneystr.h"
#include "wallet/coincontrol.h"
#include "wallet/feebumper.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "script/ismine.h"

#include <stdint.h>

#include <univalue.h>


CWallet *GetWalletForJSONRPCRequest(const JSONRPCRequest& request)
{
    // TODO: Some way to access secondary wallets
    return vpwallets.empty() ? nullptr : vpwallets[0];
}

std::string HelpRequiringPassphrase(CWallet * const pwallet)
{
    return pwallet && pwallet->IsCrypted()
        ? "\nRequires wallet passphrase to be set with walletpassphrase call."
        : "";
}

bool EnsureWalletIsAvailable(CWallet * const pwallet, bool avoidException)
{
    if (!pwallet) {
        if (!avoidException)
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");
        else
            return false;
    }
    return true;
}

void EnsureWalletIsUnlocked(CWallet * const pwallet)
{
    if (pwallet->IsLocked()) {
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");
    }
}

void WalletTxToJSON(const CWalletTx& wtx, UniValue& entry)
{
    int confirms = wtx.GetDepthInMainChain();
    entry.pushKV("confirmations", confirms);
    if (wtx.IsCoinBase())
        entry.pushKV("generated", true);
    if (confirms > 0)
    {
        entry.pushKV("blockhash", wtx.hashBlock.GetHex());
        entry.pushKV("blockindex", wtx.nIndex);
        entry.pushKV("blocktime", mapBlockIndex[wtx.hashBlock]->GetBlockTime());
    } else {
        entry.pushKV("trusted", wtx.IsTrusted());
    }
    uint256 hash = wtx.GetHash();
    entry.pushKV("txid", hash.GetHex());
    UniValue conflicts(UniValue::VARR);
    for(const uint256& conflict : wtx.GetConflicts())
        conflicts.push_back(conflict.GetHex());
    entry.pushKV("walletconflicts", conflicts);
    entry.pushKV("time", wtx.GetTxTime());
    entry.pushKV("timereceived", (int64_t)wtx.nTimeReceived);

    // Add opt-in RBF status
    std::string rbfStatus = "no";
    if (confirms <= 0) {
        LOCK(mempool.cs);
        RBFTransactionState rbfState = IsRBFOptIn(wtx, mempool);
        if (rbfState == RBF_TRANSACTIONSTATE_UNKNOWN)
            rbfStatus = "unknown";
        else if (rbfState == RBF_TRANSACTIONSTATE_REPLACEABLE_BIP125)
            rbfStatus = "yes";
    }
    entry.pushKV("bip125-replaceable", rbfStatus);

    for(const PAIRTYPE(std::string, std::string)& item : wtx.mapValue)
        entry.pushKV(item.first, item.second);
}

CAccount* AccountFromValue(CWallet* pwallet, const UniValue& value, bool useDefaultIfEmpty)
{
    std::string strAccountUUIDOrLabel = value.get_str();

    if (!pwallet)
        throw std::runtime_error("Cannot use command without an active wallet");

    //fixme: (FUT) (ACCOUNTS) (HIGH) Forbid "*" as an account name to prevent clash with this.
    if (strAccountUUIDOrLabel == "*")
        return nullptr;

    if (strAccountUUIDOrLabel.empty())
    {
        if (useDefaultIfEmpty)
        {
            if (!pwallet->getActiveAccount())
            {
                throw std::runtime_error("No account identifier passed, and no active account selected, please select an active account or pass a valid identifier.");
            }
            return pwallet->getActiveAccount();
        }
        else
        {
            return nullptr;
        }
    }

    CAccount* foundAccount = NULL;
    if (pwallet->mapAccounts.find(getUUIDFromString(strAccountUUIDOrLabel)) != pwallet->mapAccounts.end())
    {
        foundAccount = pwallet->mapAccounts[getUUIDFromString(strAccountUUIDOrLabel)];
    }
    for (const auto& [accountUUID, account] : pwallet->mapAccounts)
    {
        (unused)accountUUID;
        if (account->getLabel() == strAccountUUIDOrLabel)
        {
            if (foundAccount)
            {
                throw std::runtime_error("failed due to ambiguity, given UUID or label matches multiple accounts.");
            }
            foundAccount = account;
        }
    }

    if (!foundAccount)
        throw JSONRPCError(RPC_WALLET_INVALID_ACCOUNT_NAME, "Not a valid account UUID or label.");

    return foundAccount;
}

UniValue getnewaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getnewaddress ( \"account\" )\n"
            "\nReturns a new " GLOBAL_APPNAME " address for receiving payments.\n"
            "If 'account' is not specified the currently active account is used \n"
            "\nArguments:\n"
            "1. \"account\"        (string, optional) The unique account name or UUID for the address to be linked to. If not provided, the currently active account is used. Account must already exist.\n"
            "\nResult:\n"
            "\"address\"    (string) The new " GLOBAL_APPNAME " address\n"
            "\nExamples:\n"
            + HelpExampleCli("getnewaddress", "")
            + HelpExampleRpc("getnewaddress", "")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    // Parse the account first so we don't generate a key if there's an error
    CAccount* account;
    if (request.params.size() > 0)
        account = AccountFromValue(pwallet, request.params[0], true);
    else
        account = AccountFromValue(pwallet, UniValue(""), true);

    if (!pwallet->IsLocked()) {
        pwallet->TopUpKeyPool();
    }

    // Generate a new key that is added to wallet
    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey, account, KEYCHAIN_EXTERNAL))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    CKeyID keyID = newKey.GetID();

    return CNativeAddress(keyID).ToString();
}

UniValue getaccountaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getaccountaddress ( \"account\" )\n"
            "\nReturns the current " GLOBAL_APPNAME " address for receiving payments to this account.\n"
            "\nArguments:\n"
            "1. \"account\"       (string, optional) The unique account name or UUID for the address to retrieve. If not provided, the currently active account is used. Account must already exist.\n"
            "\nResult:\n"
            "\"address\"          (string) The current " GLOBAL_APPNAME " address\n"
            "\nExamples:\n"
            + HelpExampleCli("getaccountaddress", "")
            + HelpExampleCli("getaccountaddress", "\"myaccount\"")
            + HelpExampleRpc("getaccountaddress", "\"myaccount\"")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    // Parse the account first so we don't generate a key if there's an error
    CAccount* account;
    if (request.params.size() > 0)
        account = AccountFromValue(pwallet, request.params[0], true);
    else
        account = AccountFromValue(pwallet, UniValue(""), true);

    CReserveKeyOrScript reservekey(pwallet, account, KEYCHAIN_EXTERNAL);
    CPubKey vchPubKey;
    if (!reservekey.GetReservedKey(vchPubKey))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

    CKeyID keyID = vchPubKey.GetID();

    return CNativeAddress(keyID).ToString();
}

UniValue getrawchangeaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getrawchangeaddress\n"
            "\nReturns a new " GLOBAL_APPNAME " address, for receiving change.\n"
            "1. \"account\"        (string, optional) The unique account name or UUID for the address to be linked to. If not provided, the currently active account is used. Account must already exist.\n"
            "This is for use with raw transactions, NOT normal use.\n"
            "\nResult:\n"
            "\"address\"    (string) The address\n"
            "\nExamples:\n"
            + HelpExampleCli("getrawchangeaddress", "")
            + HelpExampleRpc("getrawchangeaddress", "")
       );

    DS_LOCK2(cs_main, pwallet->cs_wallet);
    CAccount* fromAccount;
    if (request.params.size() > 0)
        fromAccount = AccountFromValue(pwallet, request.params[0], true);
    else
        fromAccount = AccountFromValue(pwallet, UniValue(""), true);

    if (!fromAccount)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No active account for request.");


    if (!pwallet->IsLocked()) {
        pwallet->TopUpKeyPool();
    }


    CReserveKeyOrScript reservekey(pwallet, fromAccount, KEYCHAIN_CHANGE);
    CPubKey vchPubKey;
    if (!reservekey.GetReservedKey(vchPubKey))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

    reservekey.KeepKey();

    CKeyID keyID = vchPubKey.GetID();

    return CNativeAddress(keyID).ToString();
}


UniValue getaccount(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaccount \"address\"\n"
            "\nReturns the UUID and label of the account associated with the given address.\n"
            "\nArguments:\n"
            "1. \"address\"         (string, required) The " GLOBAL_APPNAME " address for account lookup.\n"
            "\nResult:\n"
            "[                     (json array of string)\n"
            "  \"accountUUID\"   (string) a " GLOBAL_APPNAME " address associated with the given account\n"
            "  \"accountLabel\"  (string) a " GLOBAL_APPNAME " address associated with the given account\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaccount", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\"")
            + HelpExampleRpc("getaccount", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\"")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    CNativeAddress address(request.params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid " GLOBAL_APPNAME " address");

    UniValue jsonGroupings(UniValue::VARR);

    //fixme: (FUT) (WATCH_ONLY)
    for(const auto& [accountUUID, account] : pwallet->mapAccounts)
    {
        if (::IsMine(*account, address.Get()))
        {
            UniValue jsonGrouping(UniValue::VARR);
            UniValue addressInfo(UniValue::VARR);
            addressInfo.push_back(getUUIDAsString(accountUUID));
            addressInfo.push_back(account->getLabel());
            jsonGrouping.push_back(addressInfo);
            jsonGroupings.push_back(jsonGrouping);
        }
    }

    return jsonGroupings;
}


UniValue getaddressesbyaccount(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaddressesbyaccount \"account\"\n"
            "\nReturns the list of assigned addresses for the given account, includes empty addresses but excludes addresses still in the keypool.\n"
            "\nArguments:\n"
            "1. \"account\"  (string, required) The UUID or unique label of the account to move funds from. May be the currently active account using \"\"\n"
            "\nResult:\n"
            "[                     (json array of string)\n"
            "  \"address\"         (string) a " GLOBAL_APPNAME " address associated with the given account\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressesbyaccount", "\"tabby\"")
            + HelpExampleRpc("getaddressesbyaccount", "\"tabby\"")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    CAccount* fromAccount = AccountFromValue(pwallet, request.params[0], true);

    // Find all addresses that have the given account and are not in the key pool
    std::set<CKeyID> setAddressExternal;
    std::set<CKeyID> setAddressInternal;
    fromAccount->GetKeys(setAddressExternal, setAddressInternal);

    CWalletDB walletdb(*pwallet->dbw);
    for (auto& keyChain : { KEYCHAIN_EXTERNAL, KEYCHAIN_CHANGE })
    {
        CKeyPool keypoolentry;
        auto& setAddresses = keyChain == KEYCHAIN_EXTERNAL ? setAddressExternal : setAddressInternal;
        for (const auto& keyIndex : keyChain == KEYCHAIN_EXTERNAL ? fromAccount->setKeyPoolExternal : fromAccount->setKeyPoolInternal)
        {
            if (!walletdb.ReadPool(keyIndex, keypoolentry))
                throw std::runtime_error(std::string(__func__) + ": read failed");

            if (setAddresses.find(keypoolentry.vchPubKey.GetID()) != setAddresses.end())
            {
                setAddresses.erase(setAddresses.find(keypoolentry.vchPubKey.GetID()));
            }
        }
    }

    UniValue ret(UniValue::VARR);
    // Enumerate witness addresses first
    if (fromAccount->IsPoW2Witness() && !fromAccount->IsWitnessOnly())
    {
        for (const auto& externalKey : setAddressExternal)
        {
            for (const auto& internalKey : setAddressInternal)
            {
                ret.push_back(CNativeAddress(CPoW2WitnessDestination(externalKey, internalKey)).ToString());
            }
        }
    }
    // Then normal addresses
    for(const auto& setAddresses : { setAddressExternal, setAddressInternal })
    {
        for(const auto& key : setAddresses)
        {
            ret.push_back(CNativeAddress(key).ToString());
        }
    }
    return ret;
}

static void SendMoney(CWallet * const pwallet, CAccount* fromAccount, const CTxDestination &address, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew)
{
    CAmount curBalance = pwallet->GetBalance();

    if (fromAccount->IsReadOnly())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Can't send from a read only account");

    // Check amount
    if (nValue <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    if (nValue > curBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

    if (pwallet->GetBroadcastTransactions() && !g_connman) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    // Create and send the transaction
    CReserveKeyOrScript reservekey(pwallet, fromAccount, KEYCHAIN_CHANGE);
    CAmount nFeeRequired;
    std::string strError;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    CRecipient recipient = GetRecipientForDestination(address, nValue, fSubtractFeeFromAmount, GetPoW2Phase(chainActive.Tip()));
    vecSend.push_back(recipient);
    
    std::vector<CKeyStore*> accountsToTry;
    accountsToTry.push_back(fromAccount);
    for ( const auto& accountPair : pactiveWallet->mapAccounts )
    {
        if(accountPair.second->getParentUUID() == fromAccount->getUUID())
        {
            accountsToTry.push_back(accountPair.second);
        }
    }
    if (!pwallet->CreateTransaction(accountsToTry, vecSend, wtxNew, reservekey, nFeeRequired, nChangePosRet, strError)) {
        if (!fSubtractFeeFromAmount && nValue + nFeeRequired > curBalance)
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    CValidationState state;
    if (!pwallet->CommitTransaction(wtxNew, reservekey, g_connman.get(), state)) {
        strError = strprintf("Error: The transaction was rejected! Reason given: %s", state.GetRejectReason());
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
}

UniValue sendtoaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 5)
        throw std::runtime_error(
            "sendtoaddress \"address\" amount ( \"comment\" \"comment_to\" subtract_fee_from_amount )\n"
            "\nSend an amount to a given address using the currently active account. If you want to use a specific account then use sendtoaddressfromaccount instead\n"
            + HelpRequiringPassphrase(pwallet) +
            "\nArguments:\n"
            "1. \"address\"            (string, required) The " GLOBAL_APPNAME " address to send to.\n"
            "2. \"amount\"             (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
            "3. \"comment\"            (string, optional) A comment used to store what the transaction is for. \n"
            "                             This is not part of the transaction, just kept in your wallet.\n"
            "4. \"comment_to\"         (string, optional) A comment to store the name of the person or organization \n"
            "                             to which you're sending the transaction. This is not part of the \n"
            "                             transaction, just kept in your wallet.\n"
            "5. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
            "                             The recipient will receive less " GLOBAL_APPNAME " than you enter in the amount field.\n"
            "\nResult:\n"
            "\"txid\"                  (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("sendtoaddress", "\"GM72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1")
            + HelpExampleCli("sendtoaddress", "\"GM72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1 \"donation\" \"seans outpost\"")
            + HelpExampleCli("sendtoaddress", "\"GM72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1 \"\" \"\" true")
            + HelpExampleRpc("sendtoaddress", "\"GM72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\", 0.1, \"donation\", \"seans outpost\"")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    CNativeAddress address(request.params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid " GLOBAL_APPNAME " address");

    // Amount
    CAmount nAmount = AmountFromValue(request.params[1]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    // Wallet comments
    CWalletTx wtx;
    if (request.params.size() > 2 && !request.params[2].isNull() && !request.params[2].get_str().empty())
        wtx.mapValue["comment"] = request.params[2].get_str();
    if (request.params.size() > 3 && !request.params[3].isNull() && !request.params[3].get_str().empty())
        wtx.mapValue["to"]      = request.params[3].get_str();

    bool fSubtractFeeFromAmount = false;
    if (request.params.size() > 4)
        fSubtractFeeFromAmount = request.params[4].get_bool();

    EnsureWalletIsUnlocked(pwallet);

    SendMoney(pwallet, pwallet->getActiveAccount(), address.Get(), nAmount, fSubtractFeeFromAmount, wtx);

    return wtx.GetHash().GetHex();
}

UniValue sendtoaddressfromaccount(const JSONRPCRequest& request)
{
    #ifdef ENABLE_WALLET
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);

    DS_LOCK2(cs_main, pwallet ? &pwallet->cs_wallet : NULL);
    #else
    LOCK(cs_main);
    #endif

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    if (request.fHelp || request.params.size() < 3 || request.params.size() > 6)
        throw std::runtime_error(
            "sendtoaddressfromaccount \"from_account\" \"florinaddress\" amount ( \"comment\" \"comment-to\" subtract_fee_from_amount )\n"
            "\nSend an amount to \"florinaddress\" using \"account\"\n"
            + HelpRequiringPassphrase(pwallet) +
            "\nArguments:\n"
            "1. \"from_account\"           (string, required) The UUID or unique label of the account to move funds from. May be the currently active account using \"\".\n"
            "2. \"florinaddress\"            (string, required) The " GLOBAL_APPNAME " address to send to.\n"
            "3. \"amount\"                 (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
            "4. \"comment\"                (string, optional) A comment used to store what the transaction is for. \n"
            "                            This is not part of the transaction, just kept in your wallet.\n"
            "5. \"comment-to\"             (string, optional) A comment to store the name of the person or organization \n"
            "                            to which you're sending the transaction. This is not part of the \n"
            "                            transaction, just kept in your wallet.\n"
            "6. subtract_fee_from_amount (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
            "                             The recipient will receive less " GLOBAL_APPNAME " than you enter in the amount field.\n"
            "\nResult:\n"
            "\"transactionid\"  (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("sendtoaddressfromaccount", "\"My account\" \"GM72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1")
            + HelpExampleCli("sendtoaddressfromaccount", "\"\" \"GM72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1 \"donation\" \"seans outpost\"")
            + HelpExampleCli("sendtoaddressfromaccount", "\"My account\" \"GM72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1 \"\" \"\" true")
            + HelpExampleRpc("sendtoaddressfromaccount", "\"\" \"GM72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\", 0.1, \"donation\", \"seans outpost\"")
        );

    CAccount* fromAccount = AccountFromValue(pwallet, request.params[0], true);

    CNativeAddress address(request.params[1].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid " GLOBAL_APPNAME " address");

    // Amount
    CAmount nAmount = AmountFromValue(request.params[2]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    // Wallet comments
    CWalletTx wtx;
    if (request.params.size() > 3 && !request.params[3].isNull() && !request.params[3].get_str().empty())
        wtx.mapValue["comment"] = request.params[2].get_str();
    if (request.params.size() > 4 && !request.params[4].isNull() && !request.params[4].get_str().empty())
        wtx.mapValue["to"]      = request.params[4].get_str();

    bool fSubtractFeeFromAmount = false;
    if (request.params.size() > 5)
        fSubtractFeeFromAmount = request.params[5].get_bool();

    EnsureWalletIsUnlocked(pwallet);

    SendMoney(pwallet, fromAccount, address.Get(), nAmount, fSubtractFeeFromAmount, wtx);

    return wtx.GetHash().GetHex();
}

UniValue listaddressgroupings(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp)
        throw std::runtime_error(
            "listaddressgroupings\n"
            "\nLists groups of addresses which have had their common ownership\n"
            "made public by common use as inputs or as the resulting change\n"
            "in past transactions\n"
            "\nResult:\n"
            "[\n"
            "  [\n"
            "    [\n"
            "      \"address\",            (string) The " GLOBAL_APPNAME " address\n"
            "      amount,                 (numeric) The amount in " + CURRENCY_UNIT + "\n"
            "      \"account\"             (string, optional) The account\n"
            "    ]\n"
            "    ,...\n"
            "  ]\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("listaddressgroupings", "")
            + HelpExampleRpc("listaddressgroupings", "")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    UniValue jsonGroupings(UniValue::VARR);
    std::map<CTxDestination, CAmount> balances = pwallet->GetAddressBalances();
    for (std::set<CTxDestination> grouping : pwallet->GetAddressGroupings()) {
        UniValue jsonGrouping(UniValue::VARR);
        for(CTxDestination address : grouping)
        {
            UniValue addressInfo(UniValue::VARR);
            addressInfo.push_back(CNativeAddress(address).ToString());
            addressInfo.push_back(ValueFromAmount(balances[address]));
            //fixme: (FUT) CBSU - Rather do this inside GetAddressGroupings
            for(const auto& [accountUUID, account] : pwallet->mapAccounts)
            {
                (unused)(accountUUID);
                if (IsMine(*account, address))
                {
                    addressInfo.push_back(account->getLabel());
                }
            }
            jsonGrouping.push_back(addressInfo);
        }
        jsonGroupings.push_back(jsonGrouping);
    }
    return jsonGroupings;
}

UniValue signmessage(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "signmessage \"address\" \"message\"\n"
            "\nSign a message with the private key of an address"
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            "1. \"address\"         (string, required) The " GLOBAL_APPNAME " address to use for the private key.\n"
            "2. \"message\"         (string, required) The message to create a signature of.\n"
            "\nResult:\n"
            "\"signature\"          (string) The signature of the message encoded in base 64\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n"
            + HelpExampleCli("signmessage", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"my message\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifymessage", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"signature\" \"my message\"") +
            "\nAs json rpc\n"
            + HelpExampleRpc("signmessage", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\", \"my message\"")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    std::string strAddress = request.params[0].get_str();
    std::string strMessage = request.params[1].get_str();

    CNativeAddress addr(strAddress);
    if (!addr.IsValid())
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");

    CKey key;
    if (!pwallet->GetKey(keyID, key)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key not available");
    }

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");

    return EncodeBase64(&vchSig[0], vchSig.size());
}

UniValue getreceivedbyaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getreceivedbyaddress \"address\" ( min_conf )\n"
            "\nReturns the total amount received by the given address in transactions with at least minconf confirmations.\n"
            "\nNB! For witness addresses this will not return locked funds or earnings, but will return regular received funds that have been sent to either of the two address keys.\n"
            "\nArguments:\n"
            "1. \"address\"     (string, required) The " GLOBAL_APPNAME " address for transactions.\n"
            "2. min_conf      (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
            "\nResult:\n"
            "amount           (numeric) The total amount in " + CURRENCY_UNIT + " received at this address.\n"
            "\nExamples:\n"
            "\nThe amount from transactions with at least 1 confirmation\n"
            + HelpExampleCli("getreceivedbyaddress", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\"") +
            "\nThe amount including unconfirmed transactions, zero confirmations\n"
            + HelpExampleCli("getreceivedbyaddress", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" 0") +
            "\nThe amount with at least 6 confirmation, very safe\n"
            + HelpExampleCli("getreceivedbyaddress", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" 6") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("getreceivedbyaddress", "\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\", 6")
       );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    // Munt address
    CNativeAddress address = CNativeAddress(request.params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid " GLOBAL_APPNAME " address");

    // Below check handles both script, standard key hash and witness cases
    if (!IsMine(*pwallet, address.Get()))
    {
        return ValueFromAmount(0);
    }

    // Minimum confirmations
    int nMinDepth = 1;
    if (request.params.size() > 1)
        nMinDepth = request.params[1].get_int();

    // Tally
    CAmount nAmount = 0;
    for (const std::pair<uint256, CWalletTx>& pairWtx : pwallet->mapWallet)
    {
        const CWalletTx& wtx = pairWtx.second;
        if (wtx.IsCoinBase() || !CheckFinalTx(*wtx.tx, chainActive))
            continue;

        for(const CTxOut& txout : wtx.tx->vout)
        {
            bool matchedOutput = false;
            CKeyID idPrimary;
            CKeyID idSecondary;
            address.GetKeyID(idPrimary, &idSecondary);
            switch(txout.output.nType)
            {
                case CTxOutType::ScriptLegacyOutput:
                {
                    if (txout.output.scriptPubKey == GetScriptForDestination(address.Get())) { matchedOutput = true; }; break;
                }
                case CTxOutType::StandardKeyHashOutput:
                {
                    bool primaryMatched = ((!idPrimary.IsNull()) && txout.output.standardKeyHash.keyID == idPrimary);
                    bool secondaryMatched = ((!idSecondary.IsNull()) && txout.output.standardKeyHash.keyID == idSecondary);
                    if (primaryMatched || secondaryMatched)
                        matchedOutput = true;
                    break;
                }
                //NB! The below test will not succeed except on a new witness account because PoW2WitnessOutput is always in coinbase
                //Leaving the code here for clarity and possible future implementation.
                #if 0
                case CTxOutType::PoW2WitnessOutput:
                {
                    bool primaryMatched = ((!idPrimary.IsNull()) && txout.output.witnessDetails.witnessKeyID == idPrimary);
                    bool secondaryMatched = ((!idSecondary.IsNull()) && txout.output.witnessDetails.spendingKeyID == idSecondary);
                    if (primaryMatched || secondaryMatched)
                        matchedOutput = true;
                    break;
                }
                #else
                case CTxOutType::PoW2WitnessOutput: break;
                #endif
            }
            if (matchedOutput && wtx.GetDepthInMainChain() >= nMinDepth)
                nAmount += txout.nValue;
        }
    }

    return  ValueFromAmount(nAmount);
}


UniValue getbalance(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 3)
        throw std::runtime_error(
            "getbalance ( \"account\" min_conf include_watchonly )\n"
            "\nIf account is not specified, returns the server's total available balance.\n"
            "If account is specified, returns the balance in the account.\n"
            "Note that the account \"\" is not the same as leaving the parameter out, but rather will use the currently selected account.\n"
            "\nArguments:\n"
            "1. \"account\"      (string, optional). The selected account, or \"*\" for entire wallet. It may be the currently active account using \"\".\n"
            "                     specific account name to find the balance associated with wallet keys in\n"
            "                     a named account, or as the empty string (\"\") to find the balance\n"
            "                     associated with wallet keys not in any named account, or as \"*\" to find\n"
            "                     the balance associated with all wallet keys regardless of account.\n"
            "2. min_conf          (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
            "3. include_watchonly (bool, optional, default=false) Also include balance in watch-only addresses (see 'importaddress')\n"
            "\nResult:\n"
            "amount              (numeric) The total amount in " + CURRENCY_UNIT + " received for this account.\n"
            "\nExamples:\n"
            "\nThe total amount in the wallet\n"
            + HelpExampleCli("getbalance", "") +
            "\nThe total amount in the wallet at least 5 blocks confirmed\n"
            + HelpExampleCli("getbalance", "\"*\" 6") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("getbalance", "\"*\", 6")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    boost::uuids::uuid accountUUID = boost::uuids::nil_generator()();
    int nMinDepth = 1;
    bool includeWatchOnly = false;

    CAccount* forAccount = nullptr;
    if (request.params.size() > 0)
    {
        if (request.params.size() > 1)
            nMinDepth = request.params[1].get_int();

        if (request.params.size() > 2)
            includeWatchOnly = request.params[2].get_bool();

        if (request.params[0].get_str() != "" && request.params[0].get_str() != "*")
        {
            //NB! - Intermediate AccountFromValue step is required in order to handle default account semantics.
            forAccount = request.params[0].get_str() != "*" ? AccountFromValue(pwallet, request.params[0], true) : nullptr;
            if (forAccount)
                accountUUID = forAccount->getUUID();
        }
    }

    int64_t nTime = GetTimeMicros();
    CAmount balance = pwallet->GetBalanceForDepth(nMinDepth, forAccount, false, true);
    if (includeWatchOnly)
        balance += pwallet->GetWatchOnlyBalance(nMinDepth, forAccount, true);
    int64_t nTime1 = GetTimeMicros();
    LogPrint(BCLog::BENCH, "GetBalanceTiming: %.2fms [%.2fs]\n", (nTime1 - nTime) * 0.001, (nTime1 - nTime) * 0.000001);
    return ValueFromAmount(balance);
}

UniValue getunconfirmedbalance(const JSONRPCRequest &request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
                "getunconfirmedbalance \"for_account\"\n"
                "\nArguments:\n"
                "1. \"for_account\"   (string, optional) The UUID or unique label of the account you want the balance for.\n"
                "Returns the server's total unconfirmed balance\n");

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    CAccount* forAccount = nullptr;
    std::string accountSpecifier = "";
    if (request.params.size() > 0)
    {
        accountSpecifier = request.params[0].get_str();
        forAccount = AccountFromValue(pwallet, request.params[0], false);
    }
    if (!forAccount && !accountSpecifier.empty() && accountSpecifier != std::string("*"))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid account label or UUID"));

    return ValueFromAmount(pwallet->GetUnconfirmedBalance(forAccount, false, true));
}

UniValue getimmaturebalance(const JSONRPCRequest &request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
                "getimmaturebalance \"for_account\"\n"
                "\nArguments:\n"
                "1. \"for_account\"   (string, optional) The UUID or unique label of the account you want the balance for.\n"
                "Returns the server's total immature balance\n");

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    CAccount* forAccount = nullptr;
    std::string accountSpecifier = "";
    if (request.params.size() > 0)
    {
        accountSpecifier = request.params[0].get_str();
        forAccount = AccountFromValue(pwallet, request.params[0], false);
    }
    if (!forAccount && !accountSpecifier.empty() && accountSpecifier != std::string("*"))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid account label or UUID"));

    return ValueFromAmount(pwallet->GetImmatureBalance(forAccount, false, true));
}

UniValue getlockedbalance(const JSONRPCRequest &request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
                "getlockedbalance \"for_account\"\n"
                "\nArguments:\n"
                "1. \"for_account\"   (string, optional) The UUID or unique label of the account you want the balance for.\n"
                "Returns the server's total locked balance (inclusive of immature and unconfirmed transactions)\n");

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    CAccount* forAccount = nullptr;
    std::string accountSpecifier = "";
    if (request.params.size() > 0)
    {
        accountSpecifier = request.params[0].get_str();
        forAccount = AccountFromValue(pwallet, request.params[0], false);
    }
    if (!forAccount && !accountSpecifier.empty() && accountSpecifier != std::string("*"))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid account label or UUID"));

    return ValueFromAmount(pwallet->GetLockedBalance(forAccount, true));
}

UniValue movecmd(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 3 || request.params.size() > 5)
        throw std::runtime_error(
            "move \"from_account\" \"to_account\" amount ( min_conf \"comment\" )\n"
            "\nMove a specified amount from one account in your wallet to another.\n"
            "\nArguments:\n"
            "1. \"from_account\"   (string, required) The UUID or unique label of the account to move funds from. May be the currently active account using \"\".\n"
            "2. \"to_account\"     (string, required) The UUID or unique label of the account to move funds to. May be the currently active account using \"\".\n"
            "3. amount               (numeric) Quantity of " + CURRENCY_UNIT + " to move between accounts, -1 to move all available funds (based on min depth).\n"
            "4. \"min_conf\"       (numeric, optional, default=1) Only use funds with at least this many confirmations.\n"
            "5. \"comment\"        (string, optional) An optional comment, stored in the wallet only.\n"
            "\nResult:\n"
            "true|false              (boolean) true if successful.\n"
            "\nExamples:\n"
            "\nMove 0.01 " + CURRENCY_UNIT + " from the default account to the account named tabby\n"
            + HelpExampleCli("move", "\"\" \"tabby\" 0.01") +
            "\nMove 0.01 " + CURRENCY_UNIT + " timotei to akiko with a comment and funds have 6 confirmations\n"
            + HelpExampleCli("move", "\"timotei\" \"akiko\" 0.01 6 \"happy birthday!\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("move", "\"timotei\", \"akiko\", 0.01, 6, \"happy birthday!\"")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    CAccount* fromAccount = AccountFromValue(pwallet, request.params[0], true);
    CAccount* toAccount = AccountFromValue(pwallet, request.params[1], true);

    if (fromAccount->getUUID() == toAccount->getUUID())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("From and to account are the same"));
    }

    std::string strComment = "";
    int nMinDepth = 1;
    if (request.params.size() > 3)
        nMinDepth = request.params[3].get_int();
    if (request.params.size() > 4)
        strComment = request.params[4].get_str();

    bool subtractFeeFromAmount = false;
    CAmount nBalance = pwallet->GetBalanceForDepth(nMinDepth, fromAccount, false, true);

    CAmount nAmount = 0;
    if (request.params[2].getValStr() == "-1")
    {
        subtractFeeFromAmount = true;
        nAmount = nBalance;
    }
    else
        nAmount = AmountFromValue(request.params[2]);

    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");


    EnsureWalletIsUnlocked(pwallet);

    // Check funds
    if (nAmount > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

    CWalletTx wtx;
    wtx.strFromAccount = getUUIDAsString(fromAccount->getUUID());
    if (!strComment.empty())
        wtx.mapValue["comment"] = request.params[4].get_str();


    CReserveKeyOrScript receiveKey(pwallet, toAccount, KEYCHAIN_EXTERNAL);
    CPubKey vchPubKey;
    if (!receiveKey.GetReservedKey(vchPubKey))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

    SendMoney(pwallet, fromAccount, vchPubKey.GetID(), nAmount, subtractFeeFromAmount, wtx);

    receiveKey.KeepKey();

    return true;
}

UniValue defrag(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 5 || request.params.size() > 6)
    {
        throw std::runtime_error(
            "defrag \"from_account\" \"to_address\" min_input_amount max_input_amount max_input_quantity ( min_conf )\n"
            "\nCombine all inputs larger than \"min_input_amount\" and smaller than \"max_input_amount\" into a single output belonging to \"to_address\"\n"
            "\nStop when number of inputs exceeds \"max_input_quantity\"; for large wallet repeated calls may be necessary before all inputs are combined\n"
            "\nArguments:\n"
            "1. \"from_account\"     (string, required) The UUID or unique label of the account to move funds from. May be the currently active account using \"\".\n"
            "2. \"to_address\"       (string, required) Address to combine funds into. May be the currently active account using \"\".\n"
            "3. min_input_amount   (numeric string) Ignore inputs smaller than this size\n"
            "4. max_input_amount   (numeric string) Ignore inputs larger than this size\n"
            "5. max_input_quantity (numeric) Maximum number of inputs to combine in a single run\n"
            "6. min_conf           (numeric, optional, default=100) Only use funds with at least this many confirmations.\n"
            "\nResult:\n"
            "Returns the number of inputs that were combined on this run.\n"
        );
    }

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    LogPrintf("DEFRAG: Start account defrag\n");

    CAccount* fromAccount = AccountFromValue(pwallet, request.params[0], true);
    CNativeAddress receiveAddress(request.params[1].get_str());
    if (!receiveAddress.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid " GLOBAL_APPNAME " address");

    CAmount nMinimumAmount = AmountFromValue(request.params[2]);
    CAmount nMaximumAmount = AmountFromValue(request.params[3]);
    uint64_t nMaximumCount = request.params[4].get_int();

    int nMinDepth = 100;
    if (request.params.size() > 5)
        nMinDepth = request.params[5].get_int();

    EnsureWalletIsUnlocked(pwallet);

    // Grab as many outputs as possible that meet our constraints
    LogPrintf("DEFRAG: Retrieving a list of viable inputs matching our paramaters\n");
    std::vector<COutput> vecOutputs;
    pwallet->AvailableCoins(fromAccount, vecOutputs, false, NULL, nMinimumAmount, nMaximumAmount, MAX_MONEY, nMaximumCount, nMinDepth);
    if (vecOutputs.size()==0)
        return 0;

    LogPrintf("DEFRAG: Retrieved [%d] inputs matching our paramaters\n", vecOutputs.size());
    uint64_t totalInputCount = 0;
    uint64_t assembledTransactionCount=0;
    const int batchSize=1500;
    do
    {
        LogPrintf("DEFRAG: Assembling transaction [%d] of [%d]\n", ++assembledTransactionCount, vecOutputs.size()/batchSize);

        // Place them all in our transaction and sum the total value
        CMutableTransaction rawTx(CURRENT_TX_VERSION_POW2);
        CAmount nTotalSent=0;
        uint64_t inputCount = 0;
        for (uint64_t idx = totalInputCount; idx < vecOutputs.size(); ++idx )
        {
            const auto& input = vecOutputs[idx];
            CTxIn in(COutPoint(input.tx->GetHash(), input.i), CScript(), 0, 0);
            rawTx.vin.push_back(in);
            nTotalSent += input.tx->tx->vout[input.i].nValue;
            if (++totalInputCount > nMaximumCount)
                break;
            if (++inputCount > batchSize)
                break;
        }

        if (rawTx.vin.size() == 0)
            break;

        // Add a single output to which the entire amount goes
        CScript scriptPubKey = GetScriptForDestination(receiveAddress.Get());
        {
            CTxOut out(nTotalSent, scriptPubKey);
            rawTx.vout.push_back(out);
        }

        // Calculate transaction fee
        const int64_t maxNewTxSize = CalculateMaximumSignedTxSize(rawTx, pwallet);
        CAmount nFeeNeeded = std::max(pwallet->GetMinimumFee(maxNewTxSize, 1, ::mempool, ::feeEstimator), COIN/100);
        nFeeNeeded = std::min(nFeeNeeded, COIN/30);

        // re-add the output, this time subtracting the transaction fee
        rawTx.vout.clear();
        {
            CTxOut out(nTotalSent-nFeeNeeded, scriptPubKey);
            rawTx.vout.push_back(out);
        }

        // Sign and send transaction
        LogPrintf("DEFRAG: Sign and send transaction\n");
        std::shared_ptr<CReserveKeyOrScript> reservedScript = std::make_shared<CReserveKeyOrScript>(scriptPubKey);
        std::string strError;
        if (!pwallet->SignAndSubmitTransaction(*reservedScript, rawTx, strError))
        {
            LogPrintf("DEFRAG: Failed to sign transaction [%s]\n", strError.c_str());
        }
    }
    while (totalInputCount < nMaximumCount);

    LogPrintf("DEFRAG: Done, processed [%d] inputs\n", totalInputCount);
    return totalInputCount;
}


UniValue sendfrom(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 3 || request.params.size() > 6)
        throw std::runtime_error(
            "sendfrom \"from_account\" \"to_address\" amount ( min_conf \"comment\" \"comment_to\" )\n"
            "\nDEPRECATED (use sendtoaddress). Sent an amount from an account to a " GLOBAL_APPNAME " address."
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            "1. \"from_account\"      (string, required) The UUID or unique label of the account to send funds from. May be the active account using \"\".\n"
            "                       Specifying an account does not influence coin selection, but it does associate the newly created\n"
            "                       transaction with the account, so the account's balance computation and transaction history can reflect\n"
            "                       the spend.\n"
            "2. \"to_address\"        (string, required) The " GLOBAL_APPNAME " address to send funds to.\n"
            "3. amount              (numeric or string, required) The amount in " + CURRENCY_UNIT + " (transaction fee is added on top).\n"
            "4. min_conf            (numeric, optional, default=1) Only use funds with at least this many confirmations.\n"
            "5. \"comment\"           (string, optional) A comment used to store what the transaction is for. \n"
            "                       This is not part of the transaction, just kept in your wallet.\n"
            "6. \"comment_to\"        (string, optional) An optional comment to store the name of the person or organization \n"
            "                       to which you're sending the transaction. This is not part of the transaction, \n"
            "                       it is just kept in your wallet.\n"
            "\nResult:\n"
            "\"txid\"                 (string) The transaction id.\n"
            "\nExamples:\n"
            "\nSend 0.01 " + CURRENCY_UNIT + " from the default account to the address, must have at least 1 confirmation\n"
            + HelpExampleCli("sendfrom", "\"\" \"GM72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.01") +
            "\nSend 0.01 from the tabby account to the given address, funds must have at least 6 confirmations\n"
            + HelpExampleCli("sendfrom", "\"tabby\" \"GM72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.01 6 \"donation\" \"seans outpost\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendfrom", "\"tabby\", \"GM72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\", 0.01, 6, \"donation\", \"seans outpost\"")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    CAccount* fromAccount = AccountFromValue(pwallet, request.params[0], true);
    CNativeAddress address(request.params[1].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid " GLOBAL_APPNAME " address");
    CAmount nAmount = AmountFromValue(request.params[2]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
    int nMinDepth = 1;
    if (request.params.size() > 3)
        nMinDepth = request.params[3].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = getUUIDAsString(fromAccount->getUUID());
    if (request.params.size() > 4 && !request.params[4].isNull() && !request.params[4].get_str().empty())
        wtx.mapValue["comment"] = request.params[4].get_str();
    if (request.params.size() > 5 && !request.params[5].isNull() && !request.params[5].get_str().empty())
        wtx.mapValue["to"]      = request.params[5].get_str();

    EnsureWalletIsUnlocked(pwallet);

    // Check funds
    CAmount nBalance = pwallet->GetBalanceForDepth(nMinDepth, fromAccount, false, true);
    if (nAmount > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

    SendMoney(pwallet, fromAccount, address.Get(), nAmount, false, wtx);

    return wtx.GetHash().GetHex();
}


UniValue sendmany(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 5)
        throw std::runtime_error(
            "sendmany \"from_account\" {\"address_or_account\":amount,...} ( min_conf \"comment\" [\"address_or_account\",...] )\n"
            "\nSend multiple times. Amounts are double-precision floating point numbers."
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            "1. \"from_account\"        (string, required) The UUID or unique label of the account to send the funds from. Should be \"\" for the active account\n"
            "2. \"amounts\"             (string, required) A json object with addresses and amounts\n"
            "    {\n"
            "      \"address_or_account\":amount   (numeric or string) The " GLOBAL_APPNAME " address or account is the key, the numeric amount (can be string) in " + CURRENCY_UNIT + " is the value\n"
            "      ,...\n"
            "    }\n"
            "3. min_conf              (numeric, optional, default=1) Only use the balance confirmed at least this many times.\n"
            "4. \"comment\"             (string, optional) A comment\n"
            "5. subtract_fee_from     (array, optional) A json array with addresses.\n"
            "                         The fee will be equally deducted from the amount of each selected address.\n"
            "                         Those recipients will receive less " GLOBAL_APPNAME " than you enter in their corresponding amount field.\n"
            "                         If no addresses are specified here, the sender pays the fee.\n"
            "    [\n"
            "      \"address_or_account\"          (string) Subtract fee from this address or account\n"
            "      ,...\n"
            "    ]\n"
            "\nResult:\n"
            "\"txid\"                   (string) The transaction id for the send. Only 1 transaction is created regardless of \n"
            "                                    the number of addresses.\n"
            "\nExamples:\n"
            "\nSend two amounts to two different addresses:\n"
            + HelpExampleCli("sendmany", "\"\" \"{\\\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\\\":0.01,\\\"G353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\"") +
            "\nSend two amounts to two different addresses setting the confirmation and comment:\n"
            + HelpExampleCli("sendmany", "\"\" \"{\\\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\\\":0.01,\\\"G353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\" 6 \"testing\"") +
            "\nSend two amounts to two different addresses, subtract fee from amount:\n"
            + HelpExampleCli("sendmany", "\"\" \"{\\\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\\\":0.01,\\\"G353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\" 1 \"\" \"[\\\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\\\",\\\"G353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\"]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendmany", "\"\", \"{\\\"GD1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\\\":0.01,\\\"G353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\", 6, \"testing\"")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    if (pwallet->GetBroadcastTransactions() && !g_connman) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    CAccount* fromAccount = AccountFromValue(pwallet, request.params[0], true);
    UniValue sendTo = request.params[1].get_obj();
    int nMinDepth = 1;
    if (request.params.size() > 2)
        nMinDepth = request.params[2].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = getUUIDAsString(fromAccount->getUUID());
    if (request.params.size() > 3 && !request.params[3].isNull() && !request.params[3].get_str().empty())
        wtx.mapValue["comment"] = request.params[3].get_str();

    UniValue subtractFeeFromAmount(UniValue::VARR);
    if (request.params.size() > 4)
        subtractFeeFromAmount = request.params[4].get_array();

    std::set<CNativeAddress> setAddress;
    std::vector<CRecipient> vecSend;

    CAmount totalAmount = 0;
    std::vector<std::string> keys = sendTo.getKeys();
    std::vector<CReserveKeyOrScript> reservedKeys;
    for(const std::string& name_ : keys)
    {
        CNativeAddress address(name_);

        CAccount* toAccount = nullptr;
        try{ toAccount = AccountFromValue(pwallet, name_, false); } catch(...) {}
        if (toAccount)
        {
            CReserveKeyOrScript receiveKey(pwallet, toAccount, KEYCHAIN_EXTERNAL);
            CPubKey vchPubKey;
            if (!receiveKey.GetReservedKey(vchPubKey))
                throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

            address = CNativeAddress(vchPubKey.GetID());

            reservedKeys.emplace_back(std::move(receiveKey));
        }
        else
        {
            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ")+name_);
            setAddress.insert(address);
        }

        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid " GLOBAL_APPNAME " address: ")+name_);

        CAmount nAmount = AmountFromValue(sendTo[name_]);
        if (nAmount <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
        totalAmount += nAmount;

        bool fSubtractFeeFromAmount = false;
        for (unsigned int idx = 0; idx < subtractFeeFromAmount.size(); idx++) {
            const UniValue& addr = subtractFeeFromAmount[idx];
            if (addr.get_str() == name_)
                fSubtractFeeFromAmount = true;
        }

        CRecipient recipient = GetRecipientForDestination(address.Get(), nAmount, fSubtractFeeFromAmount, GetPoW2Phase(chainActive.Tip()));
        vecSend.push_back(recipient);
    }

    EnsureWalletIsUnlocked(pwallet);

    // Check funds
    CAmount nBalance = pwallet->GetBalanceForDepth(nMinDepth, fromAccount, false, true);
    if (totalAmount > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

    // Send
    CReserveKeyOrScript keyChange(pwallet, fromAccount, KEYCHAIN_CHANGE);
    CAmount nFeeRequired = 0;
    int nChangePosRet = -1;
    std::string strFailReason;
    bool fCreated = pwallet->CreateTransaction(fromAccount, vecSend, wtx, keyChange, nFeeRequired, nChangePosRet, strFailReason);
    if (!fCreated)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strFailReason);
    CValidationState state;
    if (!pwallet->CommitTransaction(wtx, keyChange, g_connman.get(), state)) {
        strFailReason = strprintf("Transaction commit failed:: %s", state.GetRejectReason());
        throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);
    }

    for (auto& reservedKey : reservedKeys)
    {
        reservedKey.KeepKey();
    }

    return wtx.GetHash().GetHex();
}

// Defined in rpc/misc.cpp
extern CScript _createmultisig_redeemScript(CAccount* const forAccount, const UniValue& params);

UniValue addmultisigaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 3)
    {
        std::string msg = "addmultisigaddress num_required [\"key\",...] ( \"account\" )\n"
            "\nAdd a num-required-to-sign multisignature address to the wallet.\n"
            "Each key is a " GLOBAL_APPNAME " address or hex-encoded public key.\n"
            "If 'account' is specified (DEPRECATED), assign address to that account.\n"

            "\nArguments:\n"
            "1. num_required   (numeric, required) The number of required signatures out of the n keys or addresses.\n"
            "2. \"keys\"         (string, required) A json array of " GLOBAL_APPNAME " addresses or hex-encoded public keys\n"
            "     [\n"
            "       \"address\"  (string) " GLOBAL_APPNAME " address or hex-encoded public key\n"
            "       ...,\n"
            "     ]\n"
            "3. \"account\"      (string) An account to assign the addresses to.\n"

            "\nResult:\n"
            "\"address\"         (string) A " GLOBAL_APPNAME " address associated with the keys.\n"

            "\nExamples:\n"
            "\nAdd a multisig address from 2 addresses\n"
            + HelpExampleCli("addmultisigaddress", "2 \"[\\\"G6sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"G71sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"") +
            "\nAs json rpc call\n"
            + HelpExampleRpc("addmultisigaddress", "2, \"[\\\"G6sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"G71sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"")
        ;
        throw std::runtime_error(msg);
    }

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    CAccount* forAccount = AccountFromValue(pactiveWallet, request.params[2], false);
    
    // Construct using pay-to-script-hash:
    CScript inner = _createmultisig_redeemScript(forAccount, request.params);
    CScriptID innerID(inner);
    forAccount->AddCScript(inner);

    return CNativeAddress(innerID).ToString();
}

struct tallyitem
{
    CAmount nAmount;
    int nConf;
    std::vector<uint256> txids;
    bool fIsWatchonly;
    tallyitem()
    {
        nAmount = 0;
        nConf = std::numeric_limits<int>::max();
        fIsWatchonly = false;
    }
};

UniValue ListReceived(CWallet * const pwallet, const UniValue& params, bool fByAccounts)
{
    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    // Whether to include empty accounts
    bool fIncludeEmpty = false;
    if (params.size() > 1)
        fIncludeEmpty = params[1].get_bool();

    isminefilter filter = ISMINE_SPENDABLE;
    if(params.size() > 2)
        if(params[2].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    // Tally
    std::map<CNativeAddress, tallyitem> mapTally;
    for (const std::pair<uint256, CWalletTx>& pairWtx : pwallet->mapWallet) {
        const CWalletTx& wtx = pairWtx.second;

        if (wtx.IsCoinBase() || !CheckFinalTx(*wtx.tx, chainActive))
            continue;

        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < nMinDepth)
            continue;

        for(const CTxOut& txout : wtx.tx->vout)
        {
            CTxDestination address;
            if (!ExtractDestination(txout, address))
                continue;

            isminefilter mine = IsMine(*pwallet, address);
            if(!(mine & filter))
                continue;

            tallyitem& item = mapTally[address];
            item.nAmount += txout.nValue;
            item.nConf = std::min(item.nConf, nDepth);
            item.txids.push_back(wtx.GetHash());
            if (mine & ISMINE_WATCH_ONLY)
                item.fIsWatchonly = true;
        }
    }

    // Reply
    UniValue ret(UniValue::VARR);
    std::map<boost::uuids::uuid, tallyitem> mapAccountTally;
    for (const auto& [accountUUID, account] : pwallet->mapAccounts)
    {
        if (account->m_State == AccountState::Shadow)
            continue;

        std::string accountLabel = account->getLabel();

        if (account->m_State == AccountState::Shadow)
            accountLabel = pwallet->mapAccounts[accountUUID]->getLabel();

        std::set<CKeyID> setAddress;
        account->GetKeys(setAddress);
        for(const auto& key : setAddress)
        {
            CNativeAddress address(key);
            std::map<CNativeAddress, tallyitem>::iterator it = mapTally.find(address);
            if (it == mapTally.end() && !fIncludeEmpty)
                continue;

            CAmount nAmount = 0;
            int nConf = std::numeric_limits<int>::max();
            bool fIsWatchonly = false;
            if (it != mapTally.end())
            {
                nAmount = (*it).second.nAmount;
                nConf = (*it).second.nConf;
                fIsWatchonly = (*it).second.fIsWatchonly;
            }

            if (fByAccounts)
            {
                tallyitem& _item = mapAccountTally[accountUUID];
                _item.nAmount += nAmount;
                _item.nConf = std::min(_item.nConf, nConf);
                _item.fIsWatchonly = fIsWatchonly;
            }
            else
            {
                UniValue obj(UniValue::VOBJ);
                if(fIsWatchonly)
                    obj.pushKV("involvesWatchonly", true);
                obj.pushKV("address",       address.ToString());
                obj.pushKV("account",       getUUIDAsString(accountUUID));
                obj.pushKV("accountlabel",  accountLabel);
                obj.pushKV("amount",        ValueFromAmount(nAmount));
                obj.pushKV("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf));
                if (!fByAccounts)
                    obj.pushKV("label", accountLabel);
                UniValue transactions(UniValue::VARR);
                if (it != mapTally.end())
                {
                    for(const uint256& _item : (*it).second.txids)
                    {
                        transactions.push_back(_item.GetHex());
                    }
                }
                obj.pushKV("txids", transactions);
                ret.push_back(obj);
            }
        }
    }

    if (fByAccounts)
    {
        for (auto it = mapAccountTally.begin(); it != mapAccountTally.end(); ++it)
        {
            CAmount nAmount = (*it).second.nAmount;
            int nConf = (*it).second.nConf;
            UniValue obj(UniValue::VOBJ);
            if((*it).second.fIsWatchonly)
                obj.pushKV("involvesWatchonly", true);
            obj.pushKV("account",       getUUIDAsString((*it).first));
            obj.pushKV("amount",        ValueFromAmount(nAmount));
            obj.pushKV("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf));
            ret.push_back(obj);
        }
    }

    return ret;
}

UniValue listreceivedbyaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 3)
        throw std::runtime_error(
            "listreceivedbyaddress ( min_conf include_empty include_watchonly)\n"
            "\nList balances by receiving address.\n"
            "\nArguments:\n"
            "1. min_conf          (numeric, optional, default=1) The minimum number of confirmations before payments are included.\n"
            "2. include_empty     (bool, optional, default=false) Whether to include addresses that haven't received any payments.\n"
            "3. include_watchonly (bool, optional, default=false) Whether to include watch-only addresses (see 'importaddress').\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"involvesWatchonly\" : true,        (bool) Only returned if imported addresses were involved in transaction\n"
            "    \"address\" : \"receivingaddress\",  (string) The receiving address\n"
            "    \"account\" : \"accountname\",       (string) The account of the receiving address.\n"
            "    \"amount\" : x.xxx,                  (numeric) The total amount in " + CURRENCY_UNIT + " received by the address\n"
            "    \"confirmations\" : n,               (numeric) The number of confirmations of the most recent transaction included\n"
            "    \"label\" : \"label\",               (string) A comment for the address/transaction, if any\n"
            "    \"txids\": [\n"
            "       n,                                (numeric) The ids of transactions received with the address \n"
            "       ...\n"
            "    ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("listreceivedbyaddress", "")
            + HelpExampleCli("listreceivedbyaddress", "6 true")
            + HelpExampleRpc("listreceivedbyaddress", "6, true, true")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    return ListReceived(pwallet, request.params, false);
}

UniValue listreceivedbyaccount(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 3)
        throw std::runtime_error(
            "listreceivedbyaccount ( min_conf include_empty include_watchonly)\n"
            "\nList balances by account.\n"
            "\nArguments:\n"
            "1. min_conf          (numeric, optional, default=1) The minimum number of confirmations before payments are included.\n"
            "2. include_empty     (bool, optional, default=false) Whether to include accounts that haven't received any payments.\n"
            "3. include_watchonly (bool, optional, default=false) Whether to include watch-only addresses (see 'importaddress').\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"involvesWatchonly\" : true,   (bool) Only returned if imported addresses were involved in transaction\n"
            "    \"account\" : \"UUID\",         (string) The UUID of the receiving account\n"
            "    \"amount\" : x.xxx,             (numeric) The total amount received by addresses with this account\n"
            "    \"confirmations\" : n,          (numeric) The number of confirmations of the most recent transaction included\n"
            "    \"label\" : \"label\"           (string) A comment for the address/transaction, if any\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("listreceivedbyaccount", "")
            + HelpExampleCli("listreceivedbyaccount", "6 true")
            + HelpExampleRpc("listreceivedbyaccount", "6, true, true")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    return ListReceived(pwallet, request.params, true);
}

static void MaybePushAddress(UniValue & entry, const CTxDestination &dest)
{
    CNativeAddress addr;
    if (addr.Set(dest))
        entry.pushKV("address", addr.ToString());
}

void ListTransactions(CWallet * const pwallet, const CWalletTx& wtx, const std::string& strAccount, int nMinDepth, bool fLong, UniValue& ret, const isminefilter& filter)
{
    CAmount nFee;
    std::string strSentAccount;
    std::list<COutputEntry> listReceived;
    std::list<COutputEntry> listSent;

    std::vector<CAccount*> doForAccounts;
    if (strAccount == std::string("*"))
    {
        for (const auto& [accountUUID, account] : pwallet->mapAccounts)
        {
            (unused)accountUUID;
            if (account->m_State != AccountState::Shadow)
            {
                doForAccounts.push_back(account);
            }
            //fixme: (FUT) (ACCOUNTS) (MED) - Handle shadow children
        }
    }
    else
    {
        doForAccounts.push_back(AccountFromValue(pwallet, strAccount, true));
    }


    for (auto& account : doForAccounts)
    {
        wtx.GetAmounts(listReceived, listSent, nFee, filter, account);

        bool involvesWatchonly = wtx.IsFromMe(ISMINE_WATCH_ONLY);

        // Sent
        if ((!listSent.empty() || nFee != 0) )
        {
            for(const COutputEntry& s : listSent)
            {
                UniValue entry(UniValue::VOBJ);
                if(involvesWatchonly || (::IsMine(*pwallet, s.destination) & ISMINE_WATCH_ONLY))
                    entry.pushKV("involvesWatchonly", true);
                entry.pushKV("account", getUUIDAsString(account->getUUID()));
                entry.pushKV("accountlabel", account->getLabel());
                MaybePushAddress(entry, s.destination);
                entry.pushKV("category", "send");
                entry.pushKV("amount", ValueFromAmount(-s.amount));
                if (pwallet->mapAddressBook.count(CNativeAddress(s.destination).ToString())) {
                    entry.pushKV("label", pwallet->mapAddressBook[CNativeAddress(s.destination).ToString()].name);
                }
                entry.pushKV("vout", s.vout);
                entry.pushKV("fee", ValueFromAmount(-nFee));
                if (fLong)
                    WalletTxToJSON(wtx, entry);
                entry.pushKV("abandoned", wtx.isAbandoned());
                ret.push_back(entry);
            }
        }

        // Received
        if (listReceived.size() > 0 && wtx.GetDepthInMainChain() >= nMinDepth)
        {
            for(const COutputEntry& r : listReceived)
            {
                UniValue entry(UniValue::VOBJ);
                if (involvesWatchonly || (::IsMine(*pwallet, r.destination) & ISMINE_WATCH_ONLY)) {
                    entry.pushKV("involvesWatchonly", true);
                }
                entry.pushKV("account", getUUIDAsString(account->getUUID()));
                entry.pushKV("accountlabel", account->getLabel());
                MaybePushAddress(entry, r.destination);
                if (wtx.IsCoinBase())
                {
                    if (wtx.GetDepthInMainChain() < 1)
                        entry.pushKV("category", "orphan");
                    else if (wtx.GetBlocksToMaturity() > 0)
                        entry.pushKV("category", "immature");
                    else
                        entry.pushKV("category", "generate");
                }
                else
                {
                    entry.pushKV("category", "receive");
                }
                entry.pushKV("amount", ValueFromAmount(r.amount));
                if (pwallet->mapAddressBook.count(CNativeAddress(r.destination).ToString())) {
                    entry.pushKV("label", account);
                }
                entry.pushKV("vout", r.vout);
                if (fLong)
                    WalletTxToJSON(wtx, entry);
                ret.push_back(entry);
            }
        }
    }
}

void AcentryToJSON(const CAccountingEntry& acentry, const std::string& strAccount, UniValue& ret)
{
    bool fAllAccounts = (strAccount == std::string("*"));

    if (fAllAccounts || acentry.strAccount == strAccount)
    {
        UniValue entry(UniValue::VOBJ);
        entry.pushKV("account", acentry.strAccount);
        entry.pushKV("category", "move");
        entry.pushKV("time", acentry.nTime);
        entry.pushKV("amount", ValueFromAmount(acentry.nCreditDebit));
        entry.pushKV("otheraccount", acentry.strOtherAccount);
        entry.pushKV("comment", acentry.strComment);
        ret.push_back(entry);
    }
}

UniValue listtransactions(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 4)
        throw std::runtime_error(
            "listtransactions ( \"account\" count skip include_watchonly)\n"
            "\nReturns up to 'count' most recent transactions skipping the first 'from' transactions for account 'account'.\n"
            "\nArguments:\n"
            "1. \"account\"    (string, optional) The account UUID or unique label. \"*\" for all accounts.\n"
            "2. count          (numeric, optional, default=10) The number of transactions to return\n"
            "3. skip           (numeric, optional, default=0) The number of transactions to skip\n"
            "4. include_watchonly (bool, optional, default=false) Include transactions to watch-only addresses (see 'importaddress')\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"account\":\"accountname\",       (string) The account name associated with the transaction. \n"
            "                                                It will be \"\" for the active account.\n"
            "    \"address\":\"address\",    (string) The " GLOBAL_APPNAME " address of the transaction. Not present for \n"
            "                                                move transactions (category = move).\n"
            "    \"category\":\"send|receive|move\", (string) The transaction category. 'move' is a local (off blockchain)\n"
            "                                                transaction between accounts, and not associated with an address,\n"
            "                                                transaction id or block. 'send' and 'receive' transactions are \n"
            "                                                associated with an address, transaction id and block details\n"
            "    \"amount\": x.xxx,          (numeric) The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and for the\n"
            "                                         'move' category for moves outbound. It is positive for the 'receive' category,\n"
            "                                         and for the 'move' category for inbound funds.\n"
            "    \"label\": \"label\",       (string) A comment for the address/transaction, if any\n"
            "    \"vout\": n,                (numeric) the vout value\n"
            "    \"fee\": x.xxx,             (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the \n"
            "                                         'send' category of transactions.\n"
            "    \"confirmations\": n,       (numeric) The number of confirmations for the transaction. Available for 'send' and \n"
            "                                         'receive' category of transactions. Negative confirmations indicate the\n"
            "                                         transaction conflicts with the block chain\n"
            "    \"trusted\": xxx,           (bool) Whether we consider the outputs of this unconfirmed transaction safe to spend.\n"
            "    \"blockhash\": \"hashvalue\", (string) The block hash containing the transaction. Available for 'send' and 'receive'\n"
            "                                          category of transactions.\n"
            "    \"blockindex\": n,          (numeric) The index of the transaction in the block that includes it. Available for 'send' and 'receive'\n"
            "                                          category of transactions.\n"
            "    \"blocktime\": xxx,         (numeric) The block time in seconds since epoch (1 Jan 1970 GMT).\n"
            "    \"txid\": \"transactionid\", (string) The transaction id. Available for 'send' and 'receive' category of transactions.\n"
            "    \"time\": xxx,              (numeric) The transaction time in seconds since epoch (midnight Jan 1 1970 GMT).\n"
            "    \"timereceived\": xxx,      (numeric) The time received in seconds since epoch (midnight Jan 1 1970 GMT). Available \n"
            "                                          for 'send' and 'receive' category of transactions.\n"
            "    \"comment\": \"...\",       (string) If a comment is associated with the transaction.\n"
            "    \"otheraccount\": \"accountname\",  (string) DEPRECATED. For the 'move' category of transactions, the account the funds came \n"
            "                                          from (for receiving funds, positive amounts), or went to (for sending funds,\n"
            "                                          negative amounts).\n"
            "    \"bip125-replaceable\": \"yes|no|unknown\",  (string) Whether this transaction could be replaced due to BIP125 (replace-by-fee);\n"
            "                                                     may be unknown for unconfirmed transactions not in the mempool\n"
            "    \"abandoned\": xxx          (bool) 'true' if the transaction has been abandoned (inputs are respendable). Only available for the \n"
            "                                         'send' category of transactions.\n"
            "  }\n"
            "]\n"

            "\nExamples:\n"
            "\nList the most recent 10 transactions in the systems\n"
            + HelpExampleCli("listtransactions", "") +
            "\nList transactions 100 to 120\n"
            + HelpExampleCli("listtransactions", "\"*\" 20 100") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("listtransactions", "\"*\", 20, 100")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    std::string strAccount = "*";
    if (request.params.size() > 0)
        strAccount = request.params[0].get_str();
    int nCount = 10;
    if (request.params.size() > 1)
        nCount = request.params[1].get_int();
    int nFrom = 0;
    if (request.params.size() > 2)
        nFrom = request.params[2].get_int();
    isminefilter filter = ISMINE_SPENDABLE;
    if(request.params.size() > 3)
        if(request.params[3].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    if (nFrom < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");

    UniValue ret(UniValue::VARR);

    const CWallet::TxItems & txOrdered = pwallet->wtxOrdered;

    // iterate backwards until we have nCount items to return:
    for (CWallet::TxItems::const_reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
    {
        CWalletTx *const pwtx = (*it).second.first;
        if (pwtx != 0)
            ListTransactions(pwallet, *pwtx, strAccount, 0, true, ret, filter);
        CAccountingEntry *const pacentry = (*it).second.second;
        if (pacentry != 0)
            AcentryToJSON(*pacentry, strAccount, ret);

        if ((int)ret.size() >= (nCount+nFrom)) break;
    }
    // ret is newest to oldest

    if (nFrom > (int)ret.size())
        nFrom = ret.size();
    if ((nFrom + nCount) > (int)ret.size())
        nCount = ret.size() - nFrom;

    std::vector<UniValue> arrTmp = ret.getValues();

    std::vector<UniValue>::iterator first = arrTmp.begin();
    std::advance(first, nFrom);
    std::vector<UniValue>::iterator last = arrTmp.begin();
    std::advance(last, nFrom+nCount);

    if (last != arrTmp.end()) arrTmp.erase(last, arrTmp.end());
    if (first != arrTmp.begin()) arrTmp.erase(arrTmp.begin(), first);

    std::reverse(arrTmp.begin(), arrTmp.end()); // Return oldest to newest

    ret.clear();
    ret.setArray();
    ret.push_backV(arrTmp);

    return ret;
}

UniValue listsinceblock(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp)
        throw std::runtime_error(
            "listsinceblock ( \"blockhash\" target_confirmations include_watchonly)\n"
            "\nGet all transactions in blocks since block [blockhash], or all transactions if omitted\n"
            "\nArguments:\n"
            "1. \"blockhash\"            (string, optional) The block hash to list transactions since\n"
            "2. target_confirmations:    (numeric, optional) The confirmations required, must be 1 or more\n"
            "3. include_watchonly:       (bool, optional, default=false) Include transactions to watch-only addresses (see 'importaddress')"
            "\nResult:\n"
            "{\n"
            "  \"transactions\": [\n"
            "    \"account\":\"accountname\",       (string) The account name associated with the transaction.\n"
            "    \"address\":\"address\",    (string) The " GLOBAL_APPNAME " address of the transaction. Not present for move transactions (category = move).\n"
            "    \"category\":\"send|receive\",     (string) The transaction category. 'send' has negative amounts, 'receive' has positive amounts.\n"
            "    \"amount\": x.xxx,          (numeric) The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and for the 'move' category for moves \n"
            "                                          outbound. It is positive for the 'receive' category, and for the 'move' category for inbound funds.\n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"fee\": x.xxx,             (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the 'send' category of transactions.\n"
            "    \"confirmations\": n,       (numeric) The number of confirmations for the transaction. Available for 'send' and 'receive' category of transactions.\n"
            "                                          When it's < 0, it means the transaction conflicted that many blocks ago.\n"
            "    \"blockhash\": \"hashvalue\",     (string) The block hash containing the transaction. Available for 'send' and 'receive' category of transactions.\n"
            "    \"blockindex\": n,          (numeric) The index of the transaction in the block that includes it. Available for 'send' and 'receive' category of transactions.\n"
            "    \"blocktime\": xxx,         (numeric) The block time in seconds since epoch (1 Jan 1970 GMT).\n"
            "    \"txid\": \"transactionid\",  (string) The transaction id. Available for 'send' and 'receive' category of transactions.\n"
            "    \"time\": xxx,              (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT).\n"
            "    \"timereceived\": xxx,      (numeric) The time received in seconds since epoch (Jan 1 1970 GMT). Available for 'send' and 'receive' category of transactions.\n"
            "    \"bip125-replaceable\": \"yes|no|unknown\",  (string) Whether this transaction could be replaced due to BIP125 (replace-by-fee);\n"
            "                                                   may be unknown for unconfirmed transactions not in the mempool\n"
            "    \"abandoned\": xxx,         (bool) 'true' if the transaction has been abandoned (inputs are respendable). Only available for the 'send' category of transactions.\n"
            "    \"comment\": \"...\",       (string) If a comment is associated with the transaction.\n"
            "    \"label\" : \"label\"       (string) A comment for the address/transaction, if any\n"
            "    \"to\": \"...\",            (string) If a comment to is associated with the transaction.\n"
             "  ],\n"
            "  \"lastblock\": \"lastblockhash\"     (string) The hash of the last block\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("listsinceblock", "")
            + HelpExampleCli("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\" 6")
            + HelpExampleRpc("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\", 6")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    const CBlockIndex *pindex = NULL;
    int target_confirms = 1;
    isminefilter filter = ISMINE_SPENDABLE;

    if (request.params.size() > 0)
    {
        uint256 blockId;

        blockId.SetHex(request.params[0].get_str());
        BlockMap::iterator it = mapBlockIndex.find(blockId);
        if (it != mapBlockIndex.end())
        {
            pindex = it->second;
            if (chainActive[pindex->nHeight] != pindex)
            {
                // the block being asked for is a part of a deactivated chain;
                // we don't want to depend on its perceived height in the block
                // chain, we want to instead use the last common ancestor
                pindex = chainActive.FindFork(pindex);
            }
        }
    }

    if (request.params.size() > 1)
    {
        target_confirms = request.params[1].get_int();

        if (target_confirms < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
    }

    if (request.params.size() > 2 && request.params[2].get_bool())
    {
        filter = filter | ISMINE_WATCH_ONLY;
    }

    int depth = pindex ? (1 + chainActive.Height() - pindex->nHeight) : -1;

    UniValue transactions(UniValue::VARR);

    for (const std::pair<uint256, CWalletTx>& pairWtx : pwallet->mapWallet) {
        CWalletTx tx = pairWtx.second;

        if (depth == -1 || tx.GetDepthInMainChain() < depth)
            ListTransactions(pwallet, tx, "*", 0, true, transactions, filter);
    }

    CBlockIndex *pblockLast = chainActive[chainActive.Height() + 1 - target_confirms];
    uint256 lastblock = pblockLast ? pblockLast->GetBlockHashPoW2() : uint256();

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("transactions", transactions);
    ret.pushKV("lastblock", lastblock.GetHex());

    return ret;
}

UniValue gettransaction(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "gettransaction \"txid\" ( include_watchonly )\n"
            "\nGet detailed information about in-wallet transaction <txid>\n"
            "\nArguments:\n"
            "1. \"txid\"                  (string, required) The transaction id\n"
            "2. \"include_watchonly\"     (bool, optional, default=false) Whether to include watch-only addresses in balance calculation and details[]\n"
            "\nResult:\n"
            "{\n"
            "  \"amount\" : x.xxx,        (numeric) The transaction amount in " + CURRENCY_UNIT + "\n"
            "  \"fee\": x.xxx,            (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the \n"
            "                              'send' category of transactions.\n"
            "  \"confirmations\" : n,     (numeric) The number of confirmations\n"
            "  \"blockhash\" : \"hash\",  (string) The block hash\n"
            "  \"blockindex\" : xx,       (numeric) The index of the transaction in the block that includes it\n"
            "  \"blocktime\" : ttt,       (numeric) The time in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"txid\" : \"transactionid\",   (string) The transaction id.\n"
            "  \"time\" : ttt,            (numeric) The transaction time in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"timereceived\" : ttt,    (numeric) The time received in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"bip125-replaceable\": \"yes|no|unknown\",  (string) Whether this transaction could be replaced due to BIP125 (replace-by-fee);\n"
            "                                                   may be unknown for unconfirmed transactions not in the mempool\n"
            "  \"details\" : [\n"
            "    {\n"
            "      \"account\" : \"accountname\",  (string) The account name involved in the transaction.\n"
            "      \"address\" : \"address\",          (string) The " GLOBAL_APPNAME " address involved in the transaction\n"
            "      \"category\" : \"send|receive\",    (string) The category, either 'send' or 'receive'\n"
            "      \"amount\" : x.xxx,                 (numeric) The amount in " + CURRENCY_UNIT + "\n"
            "      \"label\" : \"label\",              (string) A comment for the address/transaction, if any\n"
            "      \"vout\" : n,                       (numeric) the vout value\n"
            "      \"fee\": x.xxx,                     (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the \n"
            "                                           'send' category of transactions.\n"
            "      \"abandoned\": xxx                  (bool) 'true' if the transaction has been abandoned (inputs are respendable). Only available for the \n"
            "                                           'send' category of transactions.\n"			
            "    }\n"
            "    ,...\n"
            "  ],\n"
            "  \"hex\" : \"data\"         (string) Raw data for transaction\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("gettransaction", "\"G075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
            + HelpExampleCli("gettransaction", "\"G075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\" true")
            + HelpExampleRpc("gettransaction", "\"G075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    uint256 hash;
    hash.SetHex(request.params[0].get_str());

    isminefilter filter = ISMINE_SPENDABLE;
    if(request.params.size() > 1)
        if(request.params[1].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    UniValue entry(UniValue::VOBJ);
    if (!pwallet->mapWallet.count(hash)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    }
    const CWalletTx& wtx = pwallet->mapWallet[hash];

    CAmount nCredit = wtx.GetCredit(filter);
    CAmount nDebit = wtx.GetDebit(filter);
    CAmount nNet = nCredit - nDebit;
    CAmount nFee = (wtx.IsFromMe(filter) ? wtx.tx->GetValueOut() - nDebit : 0);

    entry.pushKV("amount", ValueFromAmount(nNet - nFee));
    if (wtx.IsFromMe(filter))
        entry.pushKV("fee", ValueFromAmount(nFee));

    WalletTxToJSON(wtx, entry);

    UniValue details(UniValue::VARR);
    ListTransactions(pwallet, wtx, "*", 0, false, details, filter);
    entry.pushKV("details", details);

    std::string strHex = EncodeHexTx(static_cast<CTransaction>(wtx), RPCSerializationFlags());
    entry.pushKV("hex", strHex);

    return entry;
}

UniValue abandontransaction(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "abandontransaction \"txid\"\n"
            "\nMark in-wallet transaction <txid> as abandoned\n"
            "This will mark this transaction and all its in-wallet descendants as abandoned which will allow\n"
            "for their inputs to be respent.  It can be used to replace \"stuck\" or evicted transactions.\n"
            "It only works on transactions which are not included in a block and are not currently in the mempool.\n"
            "It has no effect on transactions which are already conflicted or abandoned.\n"
            "\nArguments:\n"
            "1. \"txid\"    (string, required) The transaction id\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("abandontransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
            + HelpExampleRpc("abandontransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    uint256 hash;
    hash.SetHex(request.params[0].get_str());

    if (!pwallet->mapWallet.count(hash)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    }
    if (!pwallet->AbandonTransaction(hash)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not eligible for abandonment");
    }

    return NullUniValue;
}


UniValue backupwallet(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "backupwallet \"destination\"\n"
            "\nSafely copies current wallet file to destination, which can be a directory or a path with filename.\n"
            "\nArguments:\n"
            "1. \"destination\"   (string) The destination directory or file\n"
            "\nExamples:\n"
            + HelpExampleCli("backupwallet", "\"backup.dat\"")
            + HelpExampleRpc("backupwallet", "\"backup.dat\"")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    std::string strDest = request.params[0].get_str();
    if (!pwallet->BackupWallet(strDest)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Wallet backup failed!");
    }

    return NullUniValue;
}


UniValue keypoolrefill(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "keypoolrefill ( new_size )\n"
            "\nFills the keypool."
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments\n"
            "1. new_size    (numeric, optional, default=100) The new keypool size\n"
            "\nExamples:\n"
            + HelpExampleCli("keypoolrefill", "")
            + HelpExampleRpc("keypoolrefill", "")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    // 0 is interpreted by TopUpKeyPool() as the default keypool size given by -keypool
    unsigned int kpSize = 0;
    if (request.params.size() > 0) {
        if (request.params[0].get_int() < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected valid size.");
        kpSize = (unsigned int)request.params[0].get_int();
    }

    EnsureWalletIsUnlocked(pwallet);
    pwallet->TopUpKeyPool(kpSize);

    //fixme: (FUT) (ACCOUNTS)
    /*
    if (pwallet->GetKeyPoolSize() < kpSize) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error refreshing keypool.");
    }
    */

    return NullUniValue;
}


UniValue walletpassphrase(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (pwallet->IsCrypted() && (request.fHelp || request.params.size() != 2)) {
        throw std::runtime_error(
            "walletpassphrase \"passphrase\" timeout\n"
            "\nStores the wallet decryption key in memory for 'timeout' seconds.\n"
            "This is needed prior to performing transactions related to private keys such as sending " GLOBAL_APPNAME "\n"
            "\nArguments:\n"
            "1. \"passphrase\"     (string, required) The wallet passphrase\n"
            "2. timeout            (numeric, required) The time to keep the decryption key in seconds.\n"
            "\nNote:\n"
            "Issuing the walletpassphrase command while the wallet is already unlocked will set a new unlock\n"
            "time that overrides the old one.\n"
            "\nExamples:\n"
            "\nunlock the wallet for 60 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 60") +
            "\nLock the wallet again (before 60 seconds)\n"
            + HelpExampleCli("walletlock", "") +
            "\nAs json rpc call\n"
            + HelpExampleRpc("walletpassphrase", "\"my pass phrase\", 60")
        );
    }

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    if (request.fHelp)
        return true;
    if (!pwallet->IsCrypted()) {
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrase was called.");
    }

    // Note that the walletpassphrase is stored in request.params[0] which is not mlock()ed
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make request.params[0] mlock()'d to begin with.
    strWalletPass = request.params[0].get_str().c_str();

    int64_t nSleepTime = request.params[1].get_int64();
    if (strWalletPass.length() > 0)
    {
        if (!pwallet->UnlockWithTimeout(strWalletPass, nSleepTime)) {
            throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");
        }
    }
    else
        throw std::runtime_error(
            "walletpassphrase <passphrase> <timeout>\n"
            "Stores the wallet decryption key in memory for <timeout> seconds.");

    pwallet->TopUpKeyPool();

    return NullUniValue;
}


UniValue walletpassphrasechange(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (pwallet->IsCrypted() && (request.fHelp || request.params.size() != 2)) {
        throw std::runtime_error(
            "walletpassphrasechange \"oldpassphrase\" \"newpassphrase\"\n"
            "\nChanges the wallet passphrase from 'oldpassphrase' to 'newpassphrase'.\n"
            "\nArguments:\n"
            "1. \"oldpassphrase\"      (string) The current passphrase\n"
            "2. \"newpassphrase\"      (string) The new passphrase\n"
            "\nExamples:\n"
            + HelpExampleCli("walletpassphrasechange", "\"old one\" \"new one\"")
            + HelpExampleRpc("walletpassphrasechange", "\"old one\", \"new one\"")
        );
    }

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    if (request.fHelp)
        return true;
    if (!pwallet->IsCrypted()) {
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrasechange was called.");
    }

    // TODO: get rid of these .c_str() calls by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make request.params[0] mlock()'d to begin with.
    SecureString strOldWalletPass;
    strOldWalletPass.reserve(100);
    strOldWalletPass = request.params[0].get_str().c_str();

    SecureString strNewWalletPass;
    strNewWalletPass.reserve(100);
    strNewWalletPass = request.params[1].get_str().c_str();

    if (strOldWalletPass.length() < 1 || strNewWalletPass.length() < 1)
        throw std::runtime_error(
            "walletpassphrasechange <oldpassphrase> <newpassphrase>\n"
            "Changes the wallet passphrase from <oldpassphrase> to <newpassphrase>.");

    if (!pwallet->ChangeWalletPassphrase(strOldWalletPass, strNewWalletPass)) {
        throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");
    }

    return NullUniValue;
}


UniValue walletlock(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (pwallet->IsCrypted() && (request.fHelp || request.params.size() != 0)) {
        throw std::runtime_error(
            "walletlock\n"
            "\nRemoves the wallet encryption key from memory, locking the wallet.\n"
            "After calling this method, you will need to call walletpassphrase again\n"
            "before being able to call any methods which require the wallet to be unlocked.\n"
            "\nExamples:\n"
            "\nSet the passphrase for 2 minutes to perform a transaction\n"
            + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 120") +
            "\nPerform a send (requires passphrase set)\n"
            + HelpExampleCli("sendtoaddress", "\"GM72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 1.0") +
            "\nClear the passphrase since we are done before 2 minutes is up\n"
            + HelpExampleCli("walletlock", "") +
            "\nAs json rpc call\n"
            + HelpExampleRpc("walletlock", "")
        );
    }

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    if (request.fHelp)
        return true;
    if (!pwallet->IsCrypted()) {
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletlock was called.");
    }

    pwallet->Lock();

    return NullUniValue;
}


UniValue encryptwallet(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (!pwallet->IsCrypted() && (request.fHelp || request.params.size() != 1)) {
        throw std::runtime_error(
            "encryptwallet \"passphrase\"\n"
            "\nEncrypts the wallet with 'passphrase'. This is for first time encryption.\n"
            "After this, any calls that interact with private keys such as sending or signing \n"
            "will require the passphrase to be set prior the making these calls.\n"
            "Use the walletpassphrase call for this, and then walletlock call.\n"
            "If the wallet is already encrypted, use the walletpassphrasechange call.\n"
            "Note that this will shutdown the server.\n"
            "\nArguments:\n"
            "1. \"passphrase\"    (string) The pass phrase to encrypt the wallet with. It must be at least 1 character, but should be long.\n"
            "\nExamples:\n"
            "\nEncrypt you wallet\n"
            + HelpExampleCli("encryptwallet", "\"my pass phrase\"") +
            "\nNow set the passphrase to use the wallet, such as for signing or sending " GLOBAL_APPNAME "\n"
            + HelpExampleCli("walletpassphrase", "\"my pass phrase\"") +
            "\nNow we can so something like sign\n"
            + HelpExampleCli("signmessage", "\"address\" \"test message\"") +
            "\nNow lock the wallet again by removing the passphrase\n"
            + HelpExampleCli("walletlock", "") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("encryptwallet", "\"my pass phrase\"")
        );
    }

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    if (request.fHelp)
        return true;
    if (pwallet->IsCrypted()) {
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an encrypted wallet, but encryptwallet was called.");
    }

    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make request.params[0] mlock()'d to begin with.
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = request.params[0].get_str().c_str();

    if (strWalletPass.length() < 1)
        throw std::runtime_error(
            "encryptwallet <passphrase>\n"
            "Encrypts the wallet with <passphrase>.");

    if (!pwallet->EncryptWallet(strWalletPass)) {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Error: Failed to encrypt the wallet.");
    }

    // BDB seems to have a bad habit of writing old data into
    // slack space in .dat files; that is bad if the old data is
    // unencrypted private keys. So:
    AppLifecycleManager::gApp->shutdown();
    return "wallet encrypted; " GLOBAL_APPNAME " server stopping, restart to run with encrypted wallet. You need to make a new backup.";
}

UniValue lockunspent(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "lockunspent unlock ([{\"txid\":\"txid\",\"vout\":n},...])\n"
            "\nUpdates list of temporarily unspendable outputs.\n"
            "Temporarily lock (unlock=false) or unlock (unlock=true) specified transaction outputs.\n"
            "If no transaction outputs are specified when unlocking then all current locked transaction outputs are unlocked.\n"
            "A locked transaction output will not be chosen by automatic coin selection, when spending " GLOBAL_APPNAME ".\n"
            "Locks are stored in memory only. Nodes start with zero locked outputs, and the locked output list\n"
            "is always cleared (by virtue of process exit) when a node stops or fails.\n"
            "Also see the listunspent call\n"
            "\nArguments:\n"
            "1. unlock            (boolean, required) Whether to unlock (true) or lock (false) the specified transactions\n"
            "2. \"transactions\"  (string, optional) A json array of objects. Each object the txid (string) vout (numeric)\n"
            "     [           (json array of json objects)\n"
            "       {\n"
            "         \"txid\":\"id\",    (string) The transaction id\n"
            "         \"vout\": n         (numeric) The output number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "true|false    (boolean) Whether the command was successful or not\n"

            "\nExamples:\n"
            "\nList the unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nLock an unspent transaction\n"
            + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nList the locked transactions\n"
            + HelpExampleCli("listlockunspent", "") +
            "\nUnlock the transaction again\n"
            + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("lockunspent", "false, \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    if (request.params.size() == 1)
        RPCTypeCheck(request.params, {UniValue::VBOOL});
    else
        RPCTypeCheck(request.params, {UniValue::VBOOL, UniValue::VARR});

    bool fUnlock = request.params[0].get_bool();

    if (request.params.size() == 1) {
        if (fUnlock)
            pwallet->UnlockAllCoins();
        return true;
    }

    UniValue outputs = request.params[1].get_array();
    for (unsigned int idx = 0; idx < outputs.size(); idx++) {
        const UniValue& output = outputs[idx];
        if (!output.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
        const UniValue& o = output.get_obj();

        RPCTypeCheckObj(o,
            {
                {"txid", UniValueType(UniValue::VSTR)},
                {"vout", UniValueType(UniValue::VNUM)},
            });

        std::string txid = find_value(o, "txid").get_str();
        if (!IsHex(txid))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

        int nOutput = find_value(o, "vout").get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        COutPoint outpt(uint256S(txid), nOutput);

        if (fUnlock)
            pwallet->UnlockCoin(outpt);
        else
            pwallet->LockCoin(outpt);
    }

    return true;
}

UniValue listlockunspent(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "listlockunspent\n"
            "\nReturns list of temporarily unspendable outputs.\n"
            "See the lockunspent call to lock and unlock transactions for spending.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txid\" : \"transactionid\",     (string) The transaction id locked\n"
            "    \"vout\" : n                      (numeric) The vout value\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            "\nList the unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nLock an unspent transaction\n"
            + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nList the locked transactions\n"
            + HelpExampleCli("listlockunspent", "") +
            "\nUnlock the transaction again\n"
            + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("listlockunspent", "")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    std::vector<COutPoint> vOutpts;
    pwallet->ListLockedCoins(vOutpts);

    UniValue ret(UniValue::VARR);

    for(COutPoint &outpt : vOutpts) {
        UniValue o(UniValue::VOBJ);

        uint256 txHash;
        if (GetTxHash(outpt, txHash))
            o.pushKV("txid", txHash.GetHex());
        o.pushKV("vout", (int)outpt.n);
        ret.push_back(o);
    }

    return ret;
}

UniValue settxfee(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 1)
        throw std::runtime_error(
            "settxfee amount\n"
            "\nSet the transaction fee per kB. Overwrites the paytxfee parameter.\n"
            "\nArguments:\n"
            "1. amount         (numeric or string, required) The transaction fee in " + CURRENCY_UNIT + "/kB\n"
            "\nResult\n"
            "true|false        (boolean) Returns true if successful\n"
            "\nExamples:\n"
            + HelpExampleCli("settxfee", "0.00001")
            + HelpExampleRpc("settxfee", "0.00001")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    // Amount
    CAmount nAmount = AmountFromValue(request.params[0]);

    payTxFee = CFeeRate(nAmount, 1000);
    return true;
}

UniValue rescan(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
             "rescan\n"
            "\nRun a wallet rescan against the blockchain.\n"
            "\nExamples:\n"
            "\nRescan for missing transactions\n"
            + HelpExampleCli("rescan", "\"\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("rescan", "")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    ResetSPVStartRescanThread();

    return NullUniValue;
}

UniValue getrescanprogress(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
             "getrescanprogress\n"
            "\nGet progress of wallet rescan.\n"
            "\nExamples:\n"
            "\nResult:\n"
            "false|number        Returns false if no scan is in progress, otherwise returns a percentage.\n"
            + HelpExampleCli("getrescanprogress", "\"\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("getrescanprogress", "")
        );

    //NB!! Intentionally don't lock here - rescan is very lock heavy
    //We instead make sure to only use methods that don't need locks
    if (!pwallet->IsScanning() || pwallet->IsAbortingRescan())
        return false;

    return pwallet->GetTransactionScanProgressPercent();
}

UniValue getwalletinfo(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getwalletinfo\n"
            "Returns an object containing various wallet state info.\n"
            "\nResult:\n"
            "{\n"
            "  \"walletversion\": xxxxx,       (numeric) the wallet version\n"
            "  \"balance\": xxxxxxx,           (numeric) the total confirmed balance of the wallet in " + CURRENCY_UNIT + "\n"
            "  \"unconfirmed_balance\": xxx,   (numeric) the total unconfirmed balance of the wallet in " + CURRENCY_UNIT + "\n"
            "  \"immature_balance\": xxxxxx,   (numeric) the total immature balance of the wallet in " + CURRENCY_UNIT + "\n"
            "  \"txcount\": xxxxxxx,           (numeric) the total number of transactions in the wallet\n"
            "  \"keypoololdest\": xxxxxx,      (numeric) the timestamp (seconds since Unix epoch) of the oldest pre-generated key in the key pool\n"
            //"  \"keypoolsize\": xxxx,          (numeric) how many new keys are pre-generated\n"
            "  \"unlocked_until\": ttt,        (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "  \"paytxfee\": x.xxxx,           (numeric) the transaction fee configuration, set in " + CURRENCY_UNIT + "/kB\n"
            //"  \"hdmasterkeyid\": \"<hash160>\" (string) the Hash160 of the HD master pubkey\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getwalletinfo", "")
            + HelpExampleRpc("getwalletinfo", "")
        );

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    UniValue obj(UniValue::VOBJ);

    //size_t kpExternalSize = pwallet->KeypoolCountExternalKeys();
    obj.pushKV("walletversion", pwallet->GetVersion());
    obj.pushKV("balance",       ValueFromAmount(pwallet->GetBalance()));
    obj.pushKV("unconfirmed_balance", ValueFromAmount(pwallet->GetUnconfirmedBalance()));
    obj.pushKV("immature_balance",    ValueFromAmount(pwallet->GetImmatureBalance()));
    obj.pushKV("txcount",       (int)pwallet->mapWallet.size());
    obj.pushKV("keypoololdest", pwallet->GetOldestKeyPoolTime());
    //fixme: (FUT) (ACCOUNTS)
    /*
    obj.pushKV("keypoolsize", (int64_t)kpExternalSize);
    CKeyID masterKeyID = pwallet->GetHDChain().masterKeyID;
    if (!masterKeyID.IsNull() && pwallet->CanSupportFeature(FEATURE_HD_SPLIT)) {
        obj.pushKV("keypoolsize_hd_internal",   (int64_t)(pwallet->GetKeyPoolSize() - kpExternalSize));
    }
    */
    if (pwallet->IsCrypted()) {
        obj.pushKV("unlocked_until", pwallet->nRelockTime);
    }
    obj.pushKV("paytxfee",      ValueFromAmount(payTxFee.GetFeePerK()));
    return obj;
}

UniValue resendwallettransactions(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "resendwallettransactions\n"
            "Immediately re-broadcast unconfirmed wallet transactions to all peers.\n"
            "Intended only for testing; the wallet code periodically re-broadcasts\n"
            "automatically.\n"
            "Returns array of transaction ids that were re-broadcast.\n"
            );

    if (!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    DS_LOCK2(cs_main, pwallet->cs_wallet);

    std::vector<uint256> txids = pwallet->ResendWalletTransactionsBefore(GetTime(), g_connman.get());
    UniValue result(UniValue::VARR);
    for(const uint256& txid : txids)
    {
        result.push_back(txid.ToString());
    }
    return result;
}


UniValue listunspentforaccounts(CWallet* pWallet, std::vector<CAccount*>& doForAccounts, const UniValue* pMinDepth, const UniValue* pMaxDepth, const UniValue* pFilterAddresses, const UniValue* pIncludeUnsafe, const UniValue* pOptions)
{
    int nMinDepth = 1;
    if (pMinDepth)
    {
        RPCTypeCheckArgument(*pMinDepth, UniValue::VNUM);
        nMinDepth = pMinDepth->get_int();
    }

    int nMaxDepth = 9999999;
    if (pMaxDepth)
    {
        RPCTypeCheckArgument(*pMaxDepth, UniValue::VNUM);
        nMaxDepth = pMaxDepth->get_int();
    }

    std::set<CNativeAddress> setAddress;
    if (pFilterAddresses)
    {
        RPCTypeCheckArgument(*pFilterAddresses, UniValue::VARR);
        UniValue inputs = pFilterAddresses->get_array();
        for (unsigned int idx = 0; idx < inputs.size(); idx++)
        {
            const UniValue& input = inputs[idx];
            CNativeAddress address(input.get_str());
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid " GLOBAL_APPNAME " address: ")+input.get_str());
            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ")+input.get_str());
           setAddress.insert(address);
        }
    }

    bool include_unsafe = true;
    if (pIncludeUnsafe)
    {
        RPCTypeCheckArgument(*pIncludeUnsafe, UniValue::VBOOL);
        include_unsafe = pIncludeUnsafe->get_bool();
    }

    CAmount nMinimumAmount = 0;
    CAmount nMaximumAmount = MAX_MONEY;
    CAmount nMinimumSumAmount = MAX_MONEY;
    uint64_t nMaximumCount = 0;

    if (pOptions)
    {
        const UniValue& options = pOptions->get_obj();

        if (options.exists("minimumAmount"))
            nMinimumAmount = AmountFromValue(options["minimumAmount"]);

        if (options.exists("maximumAmount"))
            nMaximumAmount = AmountFromValue(options["maximumAmount"]);

        if (options.exists("minimumSumAmount"))
            nMinimumSumAmount = AmountFromValue(options["minimumSumAmount"]);

        if (options.exists("maximumCount"))
            nMaximumCount = options["maximumCount"].get_int64();
    }

    UniValue results(UniValue::VARR);
    for (auto& account : doForAccounts)
    {
        std::vector<COutput> vecOutputs;
        pWallet->AvailableCoins(account, vecOutputs, !include_unsafe, NULL, nMinimumAmount, nMaximumAmount, nMinimumSumAmount, nMaximumCount, nMinDepth, nMaxDepth);
        for(const COutput& out : vecOutputs)
        {
            CTxDestination address;
            bool fValidAddress = ExtractDestination(out.tx->tx->vout[out.i], address);

            if (setAddress.size() && (!fValidAddress || !setAddress.count(address)))
                continue;

            UniValue entry(UniValue::VOBJ);
            entry.pushKV("txid", out.tx->GetHash().GetHex());
            entry.pushKV("vout", out.i);

            if (fValidAddress)
            {
                entry.pushKV("address", CNativeAddress(address).ToString());

                entry.pushKV("account", getUUIDAsString(account->getUUID()));
                entry.pushKV("accountlabel", account->getLabel());
            }

            if (out.tx->tx->vout[out.i].GetType() <= CTxOutType::ScriptLegacyOutput)
            {
                const CScript& scriptPubKey = out.tx->tx->vout[out.i].output.scriptPubKey;
                if (scriptPubKey.IsPayToScriptHash())
                {
                    const CScriptID& hash = boost::get<CScriptID>(address);
                    CScript redeemScript;
                    if (pWallet->GetCScript(hash, redeemScript))
                        entry.pushKV("redeemScript", HexStr(redeemScript.begin(), redeemScript.end()));
                }
                entry.pushKV("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end()));
            }

            entry.pushKV("amount", ValueFromAmount(out.tx->tx->vout[out.i].nValue));
            entry.pushKV("confirmations", out.nDepth);
            entry.pushKV("spendable", out.fSpendable);
            entry.pushKV("solvable", out.fSolvable);
            entry.pushKV("safe", out.fSafe);
            results.push_back(entry);
        }
    }

    return results;
}

UniValue listunspent(const JSONRPCRequest& request)
{
    CWallet * const pWallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pWallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 5)
        throw std::runtime_error(
            "listunspent ( min_conf max_conf  [\"addresses\",...] [include_unsafe] [query_options])\n"
            "\nReturns array of unspent transaction outputs\n"
            "with between min_conf and max_conf (inclusive) confirmations.\n"
            "Optionally filter to only include txouts paid to specified addresses.\n"
            "\nArguments:\n"
            "1. min_conf         (numeric, optional, default=1) The minimum confirmations to filter\n"
            "2. max_conf         (numeric, optional, default=9999999) The maximum confirmations to filter\n"
            "3. \"addresses\"      (string) A json array of " GLOBAL_APPNAME " addresses to filter\n"
            "    [\n"
            "      \"address\"     (string) " GLOBAL_APPNAME " address\n"
            "      ,...\n"
            "    ]\n"
            "4. include_unsafe (bool, optional, default=true) Include outputs that are not safe to spend\n"
            "                  See description of \"safe\" attribute below.\n"
            "5. query_options    (json, optional) JSON with query options\n"
            "    {\n"
            "      \"minimumAmount\"    (numeric or string, default=0) Minimum value of each UTXO in " + CURRENCY_UNIT + "\n"
            "      \"maximumAmount\"    (numeric or string, default=unlimited) Maximum value of each UTXO in " + CURRENCY_UNIT + "\n"
            "      \"maximumCount\"     (numeric or string, default=unlimited) Maximum number of UTXOs\n"
            "      \"minimumSumAmount\" (numeric or string, default=unlimited) Minimum sum value of all UTXOs in " + CURRENCY_UNIT + "\n"
            "    }\n"
            "\nResult\n"
            "[                   (array of json object)\n"
            "  {\n"
            "    \"txid\" : \"txid\",          (string) the transaction id \n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"address\" : \"address\",    (string) the " GLOBAL_APPNAME " address\n"
            "    \"account\" : \"account\",    (string) The associated account."
            "    \"scriptPubKey\" : \"key\",   (string) the script key\n"
            "    \"amount\" : x.xxx,         (numeric) the transaction output amount in " + CURRENCY_UNIT + "\n"
            "    \"confirmations\" : n,      (numeric) The number of confirmations\n"
            "    \"redeemScript\" : n        (string) The redeemScript if scriptPubKey is P2SH\n"
            "    \"spendable\" : xxx,        (bool) Whether we have the private keys to spend this output\n"
            "    \"solvable\" : xxx,         (bool) Whether we know how to spend this output, ignoring the lack of keys\n"
            "    \"safe\" : xxx              (bool) Whether this output is considered safe to spend. Unconfirmed transactions\n"
            "                              from outside keys and unconfirmed replacement transactions are considered unsafe\n"
            "                              and are not eligible for spending by fundrawtransaction and sendtoaddress.\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples\n"
            + HelpExampleCli("listunspent", "")
            + HelpExampleCli("listunspent", "6 9999999 \"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
            + HelpExampleRpc("listunspent", "6, 9999999 \"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
            + HelpExampleCli("listunspent", "6 9999999 '[]' true '{ \"minimumAmount\": 0.005 }'")
            + HelpExampleRpc("listunspent", "6, 9999999, [] , true, { \"minimumAmount\": 0.005 } ")
        );

    const UniValue* pMinDepth = nullptr;
    if (request.params.size() > 0 && !request.params[0].isNull())
        pMinDepth = &request.params[0];

    const UniValue* pMaxDepth = nullptr;
    if (request.params.size() > 1 && !request.params[1].isNull())
        pMaxDepth = &request.params[1];

    const UniValue* pFilterAddresses = nullptr;
    if (request.params.size() > 2 && !request.params[2].isNull())
        pFilterAddresses = &request.params[2];

    const UniValue* pIncludeUnsafe = nullptr;
    if (request.params.size() > 3 && !request.params[3].isNull())
        pIncludeUnsafe = &request.params[3];

    const UniValue* pOptions = nullptr;
    if (request.params.size() > 4 && !request.params[4].isNull())
        pOptions = &request.params[4];

    assert(pWallet != NULL);
    DS_LOCK2(cs_main, pWallet->cs_wallet);
    std::vector<CAccount*> doForAccounts;
    for (const auto& [accountUUID, account] : pWallet->mapAccounts)
    {
        (unused)accountUUID;
        if (account->m_State != AccountState::Shadow)
        {
            doForAccounts.push_back(account);
        }
        //fixme: (FUT) (ACCOUNTS) (MED) - Handle shadow children
    }

    return listunspentforaccounts(pWallet, doForAccounts, pMinDepth, pMaxDepth, pFilterAddresses, pIncludeUnsafe, pOptions);
}

UniValue listunspentforaccount(const JSONRPCRequest& request)
{
    CWallet * const pWallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pWallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 6)
        throw std::runtime_error(
            "listunspentforaccount \"account\" ( min_conf max_conf  [\"addresses\",...] [include_unsafe] [query_options])\n"
            "\nReturns array of unspent transaction outputs\n"
            "with between min_conf and max_conf (inclusive) confirmations.\n"
            "Optionally filter to only include txouts paid to specified addresses.\n"
            "\nArguments:\n"
            "1. account          (string) Account UUID or label. If empty the active account is used.\n"
            "2. min_conf         (numeric, optional, default=1) The minimum confirmations to filter\n"
            "3. max_conf         (numeric, optional, default=9999999) The maximum confirmations to filter\n"
            "4. \"addresses\"      (string) A json array of " GLOBAL_APPNAME " addresses to filter\n"
            "    [\n"
            "      \"address\"     (string) " GLOBAL_APPNAME " address\n"
            "      ,...\n"
            "    ]\n"
            "5. include_unsafe (bool, optional, default=true) Include outputs that are not safe to spend\n"
            "                  See description of \"safe\" attribute below.\n"
            "6. query_options    (json, optional) JSON with query options\n"
            "    {\n"
            "      \"minimumAmount\"    (numeric or string, default=0) Minimum value of each UTXO in " + CURRENCY_UNIT + "\n"
            "      \"maximumAmount\"    (numeric or string, default=unlimited) Maximum value of each UTXO in " + CURRENCY_UNIT + "\n"
            "      \"maximumCount\"     (numeric or string, default=unlimited) Maximum number of UTXOs\n"
            "      \"minimumSumAmount\" (numeric or string, default=unlimited) Minimum sum value of all UTXOs in " + CURRENCY_UNIT + "\n"
            "    }\n"
            "\nResult\n"
            "[                   (array of json object)\n"
            "  {\n"
            "    \"txid\" : \"txid\",          (string) the transaction id \n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"address\" : \"address\",    (string) the " GLOBAL_APPNAME " address\n"
            "    \"account\" : \"account\",    (string) The associated account."
            "    \"scriptPubKey\" : \"key\",   (string) the script key\n"
            "    \"amount\" : x.xxx,         (numeric) the transaction output amount in " + CURRENCY_UNIT + "\n"
            "    \"confirmations\" : n,      (numeric) The number of confirmations\n"
            "    \"redeemScript\" : n        (string) The redeemScript if scriptPubKey is P2SH\n"
            "    \"spendable\" : xxx,        (bool) Whether we have the private keys to spend this output\n"
            "    \"solvable\" : xxx,         (bool) Whether we know how to spend this output, ignoring the lack of keys\n"
            "    \"safe\" : xxx              (bool) Whether this output is considered safe to spend. Unconfirmed transactions\n"
            "                              from outside keys and unconfirmed replacement transactions are considered unsafe\n"
            "                              and are not eligible for spending by fundrawtransaction and sendtoaddress.\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples\n"
            + HelpExampleCli("listunspent", "")
            + HelpExampleCli("listunspent", "6 9999999 \"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
            + HelpExampleRpc("listunspent", "6, 9999999 \"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
            + HelpExampleCli("listunspent", "6 9999999 '[]' true '{ \"minimumAmount\": 0.005 }'")
            + HelpExampleRpc("listunspent", "6, 9999999, [] , true, { \"minimumAmount\": 0.005 } ")
        );

    CAccount* forAccount = AccountFromValue(pWallet, request.params[0], true);

    const UniValue* pMinDepth = nullptr;
    if (request.params.size() > 1 && !request.params[1].isNull())
        pMinDepth = &request.params[1];

    const UniValue* pMaxDepth = nullptr;
    if (request.params.size() > 2 && !request.params[2].isNull())
        pMaxDepth = &request.params[2];

    const UniValue* pFilterAddresses = nullptr;
    if (request.params.size() > 3 && !request.params[3].isNull())
        pFilterAddresses = &request.params[3];

    const UniValue* pIncludeUnsafe = nullptr;
    if (request.params.size() > 4 && !request.params[4].isNull())
        pIncludeUnsafe = &request.params[4];

    const UniValue* pOptions = nullptr;
    if (request.params.size() > 5 && !request.params[5].isNull())
        pOptions = &request.params[5];

    assert(pWallet != NULL);
    DS_LOCK2(cs_main, pWallet->cs_wallet);
    std::vector<CAccount*> doForAccounts;
    doForAccounts.push_back(forAccount);

    return listunspentforaccounts(pWallet, doForAccounts, pMinDepth, pMaxDepth, pFilterAddresses, pIncludeUnsafe, pOptions);
}

UniValue fundrawtransaction(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
                            "fundrawtransaction \"hexstring\" ( options )\n"
                            "\nAdd inputs to a transaction until it has enough in value to meet its out value.\n"
                            "This will not modify existing inputs, and will add at most one change output to the outputs.\n"
                            "No existing outputs will be modified unless \"subtractFeeFromOutputs\" is specified.\n"
                            "Note that inputs which were signed may need to be resigned after completion since in/outputs have been added.\n"
                            "The inputs added will not be signed, use signrawtransaction for that.\n"
                            "Note that all existing inputs must have their previous output transaction be in the wallet.\n"
                            "Note that all inputs selected must be of standard form and P2SH scripts must be\n"
                            "in the wallet using importaddress or addmultisigaddress (to calculate fees).\n"
                            "You can see whether this is the case by checking the \"solvable\" field in the listunspent output.\n"
                            "Only pay-to-pubkey, multisig, and P2SH versions thereof are currently supported for watch-only\n"
                            "\nArguments:\n"
                            "1. \"hexstring\"         (string, required) The hex string of the raw transaction\n"
                            "2. \"account\"           (string, required) The account from which to fund the transaction. \"\" to use the currently active account.\n"
                            "3. options               (object, optional)\n"
                            "   {\n"
                            "     \"changeAddress\"          (string, optional, default pool address) The " GLOBAL_APPNAME " address to receive the change\n"
                            "     \"changePosition\"         (numeric, optional, default random) The index of the change output\n"
                            "     \"includeWatching\"        (boolean, optional, default false) Also select inputs which are watch only\n"
                            "     \"lockUnspents\"           (boolean, optional, default false) Lock selected unspent outputs\n"
                            "     \"reserveChangeKey\"       (boolean, optional, default true) Reserves the change output key from the keypool\n"
                            "     \"feeRate\"                (numeric, optional, default not set: makes wallet determine the fee) Set a specific feerate (" + CURRENCY_UNIT + " per KB)\n"
                            "     \"subtractFeeFromOutputs\" (array, optional) A json array of integers.\n"
                            "                              The fee will be equally deducted from the amount of each specified output.\n"
                            "                              The outputs are specified by their zero-based index, before any change output is added.\n"
                            "                              Those recipients will receive less " GLOBAL_APPNAME " than you enter in their corresponding amount field.\n"
                            "                              If no outputs are specified here, the sender pays the fee.\n"
                            "                                  [vout_index,...]\n"
                            "     \"optIntoRbf\"             (boolean, optional) Allow this transaction to be replaced by a transaction with higher fees\n"
                            "   }\n"
                            "                         for backward compatibility: passing in a true instead of an object will result in {\"includeWatching\":true}\n"
                            "\nResult:\n"
                            "{\n"
                            "  \"hex\":       \"value\", (string)  The resulting raw transaction (hex-encoded string)\n"
                            "  \"fee\":       n,         (numeric) Fee in " + CURRENCY_UNIT + " the resulting transaction pays\n"
                            "  \"changepos\": n          (numeric) The position of the added change output, or -1\n"
                            "}\n"
                            "\nExamples:\n"
                            "\nCreate a transaction with no inputs\n"
                            + HelpExampleCli("createrawtransaction", "\"[]\" \"{\\\"myaddress\\\":0.01}\"") +
                            "\nAdd sufficient unsigned inputs to meet the output value\n"
                            + HelpExampleCli("fundrawtransaction", "\"rawtransactionhex\"") +
                            "\nSign the transaction\n"
                            + HelpExampleCli("signrawtransaction", "\"fundedtransactionhex\"") +
                            "\nSend the transaction\n"
                            + HelpExampleCli("sendrawtransaction", "\"signedtransactionhex\"")
                            );

    RPCTypeCheck(request.params, {UniValue::VSTR});

    CCoinControl coinControl;
    coinControl.destChange = CNoDestination();
    int changePosition = -1;
    coinControl.fAllowWatchOnly = false;  // include watching
    bool lockUnspents = false;
    bool reserveChangeKey = true;
    coinControl.nFeeRate = CFeeRate(0);
    coinControl.fOverrideFeeRate = false;
    UniValue subtractFeeFromOutputs;
    std::set<int> setSubtractFeeFromOutputs;

    if (request.params.size() > 2) {
      if (request.params[2].type() == UniValue::VBOOL) {
        // backward compatibility bool only fallback
        coinControl.fAllowWatchOnly = request.params[2].get_bool();
      }
      else {
        RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ});

        UniValue options = request.params[2];

        RPCTypeCheckObj(options,
            {
                {"changeAddress", UniValueType(UniValue::VSTR)},
                {"changePosition", UniValueType(UniValue::VNUM)},
                {"includeWatching", UniValueType(UniValue::VBOOL)},
                {"lockUnspents", UniValueType(UniValue::VBOOL)},
                {"reserveChangeKey", UniValueType(UniValue::VBOOL)},
                {"feeRate", UniValueType()}, // will be checked below
                {"subtractFeeFromOutputs", UniValueType(UniValue::VARR)},
                {"optIntoRbf", UniValueType(UniValue::VBOOL)},
            },
            true, true);

        if (options.exists("changeAddress")) {
            CNativeAddress address(options["changeAddress"].get_str());

            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "changeAddress must be a valid " GLOBAL_APPNAME " address");

            coinControl.destChange = address.Get();
        }

        if (options.exists("changePosition"))
            changePosition = options["changePosition"].get_int();

        if (options.exists("includeWatching"))
            coinControl.fAllowWatchOnly = options["includeWatching"].get_bool();

        if (options.exists("lockUnspents"))
            lockUnspents = options["lockUnspents"].get_bool();

        if (options.exists("reserveChangeKey"))
            reserveChangeKey = options["reserveChangeKey"].get_bool();

        if (options.exists("feeRate"))
        {
            coinControl.nFeeRate = CFeeRate(AmountFromValue(options["feeRate"]));
            coinControl.fOverrideFeeRate = true;
        }

        if (options.exists("subtractFeeFromOutputs"))
            subtractFeeFromOutputs = options["subtractFeeFromOutputs"].get_array();

        if (options.exists("optIntoRbf")) {
            coinControl.signalRbf = options["optIntoRbf"].get_bool();
        }
      }
    }

    // parse hex string from parameter
    CMutableTransaction tx(CURRENT_TX_VERSION_POW2);
    if (!DecodeHexTx(tx, request.params[0].get_str(), true))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    if (tx.vout.size() == 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "TX must have at least one output");

    if (changePosition != -1 && (changePosition < 0 || (unsigned int)changePosition > tx.vout.size()))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "changePosition out of bounds");

    for (unsigned int idx = 0; idx < subtractFeeFromOutputs.size(); idx++) {
        int pos = subtractFeeFromOutputs[idx].get_int();
        if (setSubtractFeeFromOutputs.count(pos))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, duplicated position: %d", pos));
        if (pos < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, negative position: %d", pos));
        if (pos >= int(tx.vout.size()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, position too large: %d", pos));
        setSubtractFeeFromOutputs.insert(pos);
    }

    CAmount nFeeOut;
    std::string strFailReason;

    CAccount* fundingAccount = AccountFromValue(pwallet, request.params[1], true);

    CReserveKeyOrScript reservekey(pwallet, fundingAccount, KEYCHAIN_CHANGE);
    if (!pwallet->FundTransaction(fundingAccount, tx, nFeeOut, changePosition, strFailReason, lockUnspents, setSubtractFeeFromOutputs, coinControl, reservekey)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);
    }

    if (reserveChangeKey)
        reservekey.KeepKey();

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(tx));
    result.pushKV("changepos", changePosition);
    result.pushKV("fee", ValueFromAmount(nFeeOut));

    return result;
}

UniValue bumpfee(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "bumpfee \"txid\" ( options ) \n"
            "\nBumps the fee of an opt-in-RBF transaction T, replacing it with a new transaction B.\n"
            "An opt-in RBF transaction with the given txid must be in the wallet.\n"
            "The command will pay the additional fee by decreasing (or perhaps removing) its change output.\n"
            "If the change output is not big enough to cover the increased fee, the command will currently fail\n"
            "instead of adding new inputs to compensate. (A future implementation could improve this.)\n"
            "The command will fail if the wallet or mempool contains a transaction that spends one of T's outputs.\n"
            "By default, the new fee will be calculated automatically using estimatefee.\n"
            "The user can specify a confirmation target for estimatefee.\n"
            "Alternatively, the user can specify totalFee, or use RPC settxfee to set a higher fee rate.\n"
            "At a minimum, the new fee rate must be high enough to pay an additional new relay fee (incrementalfee\n"
            "returned by getnetworkinfo) to enter the node's mempool.\n"
            "\nArguments:\n"
            "1. txid                  (string, required) The txid to be bumped\n"
            "2. options               (object, optional)\n"
            "   {\n"
            "     \"confTarget\"        (numeric, optional) Confirmation target (in blocks)\n"
            "     \"totalFee\"          (numeric, optional) Total fee (NOT feerate) to pay, in satoshis.\n"
            "                         In rare cases, the actual fee paid might be slightly higher than the specified\n"
            "                         totalFee if the tx change output has to be removed because it is too close to\n"
            "                         the dust threshold.\n"
            "     \"replaceable\"       (boolean, optional, default true) Whether the new transaction should still be\n"
            "                         marked bip-125 replaceable. If true, the sequence numbers in the transaction will\n"
            "                         be left unchanged from the original. If false, any input sequence numbers in the\n"
            "                         original transaction that were less than 0xfffffffe will be increased to 0xfffffffe\n"
            "                         so the new transaction will not be explicitly bip-125 replaceable (though it may\n"
            "                         still be replacable in practice, for example if it has unconfirmed ancestors which\n"
            "                         are replaceable).\n"
            "   }\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\":    \"value\",   (string)  The id of the new transaction\n"
            "  \"origfee\":  n,         (numeric) Fee of the replaced transaction\n"
            "  \"fee\":      n,         (numeric) Fee of the new transaction\n"
            "  \"errors\":  [ str... ] (json array of strings) Errors encountered during processing (may be empty)\n"
            "}\n"
            "\nExamples:\n"
            "\nBump the fee, get the new transaction\'s txid\n" +
            HelpExampleCli("bumpfee", "<txid>"));
    }

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ});
    uint256 hash;
    hash.SetHex(request.params[0].get_str());

    // optional parameters
    bool ignoreGlobalPayTxFee = false;
    int newConfirmTarget = nTxConfirmTarget;
    CAmount totalFee = 0;
    bool replaceable = true;
    if (request.params.size() > 1) {
        UniValue options = request.params[1];
        RPCTypeCheckObj(options,
            {
                {"confTarget", UniValueType(UniValue::VNUM)},
                {"totalFee", UniValueType(UniValue::VNUM)},
                {"replaceable", UniValueType(UniValue::VBOOL)},
            },
            true, true);

        if (options.exists("confTarget") && options.exists("totalFee")) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "confTarget and totalFee options should not both be set. Please provide either a confirmation target for fee estimation or an explicit total fee for the transaction.");
        } else if (options.exists("confTarget")) {
            // If the user has explicitly set a confTarget in this rpc call,
            // then override the default logic that uses the global payTxFee
            // instead of the confirmation target.
            ignoreGlobalPayTxFee = true;
            newConfirmTarget = options["confTarget"].get_int();
            if (newConfirmTarget <= 0) { // upper-bound will be checked by estimatefee/smartfee
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid confTarget (cannot be <= 0)");
            }
        } else if (options.exists("totalFee")) {
            totalFee = options["totalFee"].get_int64();
            if (totalFee <= 0) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid totalFee %s (must be greater than 0)", FormatMoney(totalFee)));
            }
        }

        if (options.exists("replaceable")) {
            replaceable = options["replaceable"].get_bool();
        }
    }

    DS_LOCK2(cs_main, pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);

    CFeeBumper feeBump(pwallet, hash, newConfirmTarget, ignoreGlobalPayTxFee, totalFee, replaceable);
    BumpFeeResult res = feeBump.getResult();
    if (res != BumpFeeResult::OK)
    {
        switch(res) {
            case BumpFeeResult::INVALID_ADDRESS_OR_KEY:
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, feeBump.getErrors()[0]);
                break;
            case BumpFeeResult::INVALID_REQUEST:
                throw JSONRPCError(RPC_INVALID_REQUEST, feeBump.getErrors()[0]);
                break;
            case BumpFeeResult::INVALID_PARAMETER:
                throw JSONRPCError(RPC_INVALID_PARAMETER, feeBump.getErrors()[0]);
                break;
            case BumpFeeResult::WALLET_ERROR:
                throw JSONRPCError(RPC_WALLET_ERROR, feeBump.getErrors()[0]);
                break;
            default:
                throw JSONRPCError(RPC_MISC_ERROR, feeBump.getErrors()[0]);
                break;
        }
    }

    // sign bumped transaction
    if (!feeBump.signTransaction(pwallet)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Can't sign transaction.");
    }
    // commit the bumped transaction
    if(!feeBump.commit(pwallet)) {
        throw JSONRPCError(RPC_WALLET_ERROR, feeBump.getErrors()[0]);
    }
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", feeBump.getBumpedTxId().GetHex());
    result.pushKV("origfee", ValueFromAmount(feeBump.getOldFee()));
    result.pushKV("fee", ValueFromAmount(feeBump.getNewFee()));
    UniValue errors(UniValue::VARR);
    for (const std::string& err: feeBump.getErrors())
        errors.push_back(err);
    result.pushKV("errors", errors);

    return result;
}

extern UniValue abortrescan(const JSONRPCRequest& request); // in rpcdump.cpp
extern UniValue dumpprivkey(const JSONRPCRequest& request); // in rpcdump.cpp
extern UniValue importprivkey(const JSONRPCRequest& request);
extern UniValue importaddress(const JSONRPCRequest& request);
extern UniValue importpubkey(const JSONRPCRequest& request);
extern UniValue dumpwallet(const JSONRPCRequest& request);
extern UniValue checkwalletagainstutxo(const JSONRPCRequest& request);
extern UniValue repairwalletfromutxo(const JSONRPCRequest& request);
extern UniValue removeallorphans(const JSONRPCRequest& request);
extern UniValue importwallet(const JSONRPCRequest& request);
extern UniValue importprunedfunds(const JSONRPCRequest& request);
extern UniValue removeprunedfunds(const JSONRPCRequest& request);
extern UniValue importmulti(const JSONRPCRequest& request);

static const CRPCCommand commands[] =
{ //  category              name                        actor (function)           okSafeMode
    //  --------------------- ------------------------    -----------------------    ----------
    { "rawtransactions",    "fundrawtransaction",       &fundrawtransaction,       false,  {"hexstring","options"} },
    { "hidden",             "resendwallettransactions", &resendwallettransactions, true,   {} },
    { "wallet",             "abandontransaction",       &abandontransaction,       false,  {"txid"} },
    { "wallet",             "abortrescan",              &abortrescan,              false,  {} },
    { "wallet",             "addmultisigaddress",       &addmultisigaddress,       true,   {"num_required","keys","account"} },
    { "wallet",             "backupwallet",             &backupwallet,             true,   {"destination"} },
    //{ "wallet",             "bumpfee",                  &bumpfee,                true,   {"txid", "options"} },
    { "wallet",             "dumpprivkey",              &dumpprivkey,              true,   {"address"}  },
    { "wallet",             "dumpwallet",               &dumpwallet,               true,   {"filename", "HDConsent"} },
    { "wallet",             "checkwalletagainstutxo",   &checkwalletagainstutxo,   true,   {} },
    { "wallet",             "repairwalletfromutxo",     &repairwalletfromutxo,     true,   {} },
    { "wallet",             "removeallorphans",         &removeallorphans,         true,   {} },
    { "wallet",             "encryptwallet",            &encryptwallet,            true,   {"passphrase"} },
    { "wallet",             "getaccount",               &getaccount,               true,   {"address"} },
    { "wallet",             "getaddressesbyaccount",    &getaddressesbyaccount,    true,   {"account"} },
    { "wallet",             "getbalance",               &getbalance,               false,  {"account","min_conf","include_watchonly"} },
    { "wallet",             "getnewaddress",            &getnewaddress,            true,   {"account"} },
    { "wallet",             "getaccountaddress",        &getaccountaddress,        true,   {"account"} },
    { "wallet",             "getrawchangeaddress",      &getrawchangeaddress,      true,   {} },
    { "wallet",             "getreceivedbyaddress",     &getreceivedbyaddress,     false,  {"address","min_conf"} },
    { "wallet",             "getrescanprogress",        &getrescanprogress,        false,  {} },
    { "wallet",             "gettransaction",           &gettransaction,           false,  {"txid","include_watchonly"} },
    { "wallet",             "getunconfirmedbalance",    &getunconfirmedbalance,    false,  {} },
    { "wallet",             "getimmaturebalance",       &getimmaturebalance,       false,  {} },
    { "wallet",             "getlockedbalance",         &getlockedbalance,         false,  {} },
    { "wallet",             "getwalletinfo",            &getwalletinfo,            false,  {} },
    { "wallet",             "importmulti",              &importmulti,              true,   {"account","requests","options"} },
    { "wallet",             "importprivkey",            &importprivkey,            true,   {"privkey", "account", "label","rescan"} },
    { "wallet",             "importwallet",             &importwallet,             true,   {"filename"} },
    { "wallet",             "importaddress",            &importaddress,            true,   {"address","label","rescan","p2sh"} },
    { "wallet",             "importprunedfunds",        &importprunedfunds,        true,   {"rawtransaction","txoutproof"} },
    { "wallet",             "importpubkey",             &importpubkey,             true,   {"pubkey","label","rescan"} },
    { "wallet",             "keypoolrefill",            &keypoolrefill,            true,   {"new_size"} },
    { "wallet",             "listaddressgroupings",     &listaddressgroupings,     false,  {} },
    { "wallet",             "listlockunspent",          &listlockunspent,          false,  {} },
    { "wallet",             "listreceivedbyaccount",    &listreceivedbyaccount,    false,  {"minconf","include_empty","include_watchonly"} },
    { "wallet",             "listreceivedbyaddress",    &listreceivedbyaddress,    false,  {"min_conf","include_empty","include_watchonly"} },
    { "wallet",             "listsinceblock",           &listsinceblock,           false,  {"blockhash","target_confirmations","include_watchonly"} },
    { "wallet",             "listtransactions",         &listtransactions,         false,  {"account","count","skip","include_watchonly"} },
    { "wallet",             "listunspent",              &listunspent,              false,  {"min_conf","max_conf","addresses","include_unsafe","query_options"} },
    { "wallet",             "listunspentforaccount",    &listunspentforaccount,    false,  {"account","min_conf","max_conf","addresses","include_unsafe","query_options"} },
    { "wallet",             "lockunspent",              &lockunspent,              true,   {"unlock","transactions"} },
    { "wallet",             "move",                     &movecmd,                  false,  {"from_account","to_account","amount","min_conf","comment"} },
    { "wallet",             "defrag",                   &defrag,                   false,  {"from_account","to_address","min_input_amount","max_input_amount","max_input_quantity", "min_conf"} },
    { "wallet",             "rescan",                   &rescan,                   false,  {} },
    { "wallet",             "sendfrom",                 &sendfrom,                 false,  {"from_account","to_address","amount","min_conf","comment","comment_to"} },
    { "wallet",             "sendmany",                 &sendmany,                 false,  {"from_account","amounts","min_conf","comment","subtract_fee_from"} },
    { "wallet",             "sendtoaddress",            &sendtoaddress,            false,  {"address","amount","comment","comment_to","subtract_fee_from_amount"} },
    { "wallet",             "sendtoaddressfromaccount", &sendtoaddressfromaccount, false,  {"from_account", "address", "amount", "comment", "comment-to", "subtract_fee_from_amount"} },
    { "wallet",             "settxfee",                 &settxfee,                 true,   {"amount"} },
    { "wallet",             "signmessage",              &signmessage,              true,   {"address","message"} },
    { "wallet",             "walletlock",               &walletlock,               true,   {} },
    { "wallet",             "walletpassphrasechange",   &walletpassphrasechange,   true,   {"oldpassphrase","newpassphrase"} },
    { "wallet",             "walletpassphrase",         &walletpassphrase,         true,   {"passphrase","timeout"} },
    { "wallet",             "removeprunedfunds",        &removeprunedfunds,        true,   {"txid"} },
};

void RegisterWalletRPCCommands(CRPCTable &t)
{
    if (GetBoolArg("-disablewallet", false))
        return;

    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
