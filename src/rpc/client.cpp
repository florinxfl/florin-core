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

#include "rpc/client.h"
#include "rpc/protocol.h"
#include "util.h"

#include <set>
#include <stdint.h>

#include <univalue.h>

class CRPCConvertParam
{
public:
    std::string methodName; //!< method whose params want conversion
    int paramIdx;           //!< 0-based idx of param to convert
    std::string paramName;  //!< parameter name
};

/**
 * Specify a (method, idx, name) here if the argument is a non-string RPC
 * argument and needs to be converted from JSON.
 *
 * @note Parameter indexes start from 0.
 */
static const CRPCConvertParam vRPCConvertParams[] =
{
    { "setmocktime", 0, "timestamp" },
    { "generate", 0, "num_blocks" },
    { "generate", 1, "max_tries" },
    { "generatetoaddress", 0, "num_blocks" },
    { "generatetoaddress", 2, "max_tries" },
    { "getnetworkhashps", 0, "num_blocks" },
    { "getnetworkhashps", 1, "height" },
    { "sendtoaddress", 1, "amount" },
    { "sendtoaddress", 4, "subtract_fee_from_amount" },
    { "settxfee", 0, "amount" },
    { "getreceivedbyaddress", 1, "min_conf" },
    { "getreceivedbyaccount", 1, "min_conf" },
    { "listreceivedbyaddress", 0, "min_conf" },
    { "listreceivedbyaddress", 1, "include_empty" },
    { "listreceivedbyaddress", 2, "include_watchonly" },
    { "listreceivedbyaccount", 0, "min_conf" },
    { "listreceivedbyaccount", 1, "include_empty" },
    { "listreceivedbyaccount", 2, "include_watchonly" },
    { "getbalance", 1, "min_conf" },
    { "getbalance", 2, "include_watchonly" },
    { "getaccountbalances", 0, "min_conf" },
    { "getaccountbalances", 1, "include_watchonly" },
    { "getblockhash", 0, "height" },
    { "invalidateblocksatheight", 0, "block_height" },
    { "waitforblockheight", 0, "height" },
    { "waitforblockheight", 1, "timeout" },
    { "waitforblock", 1, "timeout" },
    { "waitfornewblock", 0, "timeout" },
    { "move", 2, "amount" },
    { "move", 3, "min_conf" },
    { "defrag", 2, "min_input_amount" },
    { "defrag", 3, "max_input_amount" },
    { "defrag", 4, "max_input_quantity" },
    { "defrag", 5, "min_conf" },
    { "sendfrom", 2, "amount" },
    { "sendfrom", 3, "min_conf" },
    { "listtransactions", 1, "count" },
    { "listtransactions", 2, "skip" },
    { "listtransactions", 3, "include_watchonly" },
    { "walletpassphrase", 1, "timeout" },
    { "getblocktemplate", 0, "template_request" },
    { "listsinceblock", 1, "target_confirmations" },
    { "listsinceblock", 2, "include_watchonly" },
    { "sendmany", 1, "amounts" },
    { "sendmany", 2, "min_conf" },
    { "sendmany", 4, "subtract_fee_from" },
    { "addmultisigaddress", 0, "num_required" },
    { "addmultisigaddress", 1, "keys" },
    { "createmultisig", 0, "num_required" },
    { "createmultisig", 1, "keys" },
    { "listunspent", 0, "min_conf" },
    { "listunspent", 1, "max_conf" },
    { "listunspent", 2, "addresses" },
    { "listunspent", 4, "query_options" },
    { "listunspentforaccount", 1, "min_conf" },
    { "listunspentforaccount", 2, "max_conf" },
    { "listunspentforaccount", 3, "addresses" },
    { "listunspentforaccount", 5, "query_options" },
    { "getblock", 1, "verbosity" },
    { "getblockheader", 1, "verbose" },
    { "getchaintxstats", 0, "num_blocks" },
    { "gettransaction", 1, "include_watchonly" },
    { "getrawtransaction", 1, "verbose" },
    { "createrawtransaction", 0, "inputs" },
    { "createrawtransaction", 1, "outputs" },
    { "createrawtransaction", 2, "lock_time" },
    { "createrawtransaction", 3, "opt_in_to_rbf" },
    { "signrawtransaction", 1, "prev_txs" },
    { "signrawtransaction", 2, "priv_keys" },
    { "sendrawtransaction", 1, "allow_high_fees" },
    { "fundrawtransaction", 1, "options" },
    { "gettxout", 1, "n" },
    { "gettxout", 2, "include_mempool" },
    { "gettxoutproof", 0, "txids" },
    { "lockunspent", 0, "unlock" },
    { "lockunspent", 1, "transactions" },
    { "importprivkey", 2, "rescan" },
    { "importaddress", 2, "rescan" },
    { "importaddress", 3, "p2sh" },
    { "importpubkey", 2, "rescan" },
    { "importmulti", 1, "requests" },
    { "importmulti", 2, "options" },
    { "verifychain", 0, "check_level" },
    { "verifychain", 1, "num_blocks" },
    { "getblockstats", 0, "hash_or_height" },
    { "getblockstats", 1, "stats" },
    { "pruneblockchain", 0, "height" },
    { "keypoolrefill", 0, "new_size" },
    { "getrawmempool", 0, "verbose" },
    { "estimatefee", 0, "num_blocks" },
    { "estimatesmartfee", 0, "num_blocks" },
    { "estimaterawfee", 0, "num_blocks" },
    { "estimaterawfee", 1, "threshold" },
    { "estimaterawfee", 2, "horizon" },
    { "prioritisetransaction", 1, "dummy" },
    { "prioritisetransaction", 2, "fee_delta" },
    { "setban", 2, "ban_time" },
    { "setban", 3, "absolute" },
    { "setnetworkactive", 0, "state" },
    { "getmempoolancestors", 1, "verbose" },
    { "getmempooldescendants", 1, "verbose" },
    { "bumpfee", 1, "options" },
    { "logging", 0, "include" },
    { "logging", 1, "exclude" },
    { "disconnectnode", 1, "node_id" },
    // Echo with conversion (For testing only)
    { "echojson", 0, "arg0" },
    { "echojson", 1, "arg1" },
    { "echojson", 2, "arg2" },
    { "echojson", 3, "arg3" },
    { "echojson", 4, "arg4" },
    { "echojson", 5, "arg5" },
    { "echojson", 6, "arg6" },
    { "echojson", 7, "arg7" },
    { "echojson", 8, "arg8" },
    { "echojson", 9, "arg9" },
    { "setgenerate", 0, "generate" },
    { "setgenerate", 1, "gen_proc_limit" },
    { "setgenerate", 2, "gen_arena_proc_limit" },
    { "deleteseed", 1, "should_purge_accounts" },
    { "importseed", 2, "is_read_only" },
    { "importwitnesskeys", 2, "create_account" },
    { "importwitnesskeys", 3, "rescan" },
    { "splitwitnessaccount", 2, "amounts" },
    { "setwitnesscompound", 1, "amount" },
    { "getwitnessinfo", 1, "verbose" },
    { "getwitnessinfo", 2, "mine_only" },
    { "fundwitnessaccount", 4, "force_multiple" },
    { "setwitnessrewardscript", 2, "force_pubkey" },
    { "setwitnessrewardtemplate", 1, "reward_template" },
    { "importholdingkeys", 2, "create_account" },
    { "importholdingkeys", 3, "rescan" },
    { "splitholdingaccount", 2, "amounts" },
    { "setholdingcompound", 1, "amount" },
    { "getholdinginfo", 1, "verbose" },
    { "getholdinginfo", 2, "mine_only" },
    { "fundholdingaccount", 4, "force_multiple" },
    { "setholdingrewardscript", 2, "force_pubkey" },
    { "setholdingrewardtemplate", 1, "reward_template" },
    { "sethashlimit", 0, "limit" },
    { "getlastblocks", 0, "num_blocks" },
    { "dumpdiffarray", 0, "height" },
    { "dumpblockgaps", 0, "start_height" },
    { "dumpblockgaps", 1, "count" },
    { "dumptransactionstats", 0, "start_height" },
    { "dumptransactionstats", 1, "count" },
    { "sendtoaddressfromaccount", 2, "amount" },
    { "sendtoaddressfromaccount", 5, "subtract_fee_from_amount" },
};

class CRPCConvertTable
{
private:
    std::set<std::pair<std::string, int>> members;
    std::set<std::pair<std::string, std::string>> membersByName;

public:
    CRPCConvertTable();

    bool convert(const std::string& method, int idx) {
        return (members.count(std::pair(method, idx)) > 0);
    }
    bool convert(const std::string& method, const std::string& name) {
        return (membersByName.count(std::pair(method, name)) > 0);
    }
};

CRPCConvertTable::CRPCConvertTable()
{
    const unsigned int n_elem =
        (sizeof(vRPCConvertParams) / sizeof(vRPCConvertParams[0]));

    for (unsigned int i = 0; i < n_elem; i++) {
        members.insert(std::pair(vRPCConvertParams[i].methodName,
                                      vRPCConvertParams[i].paramIdx));
        membersByName.insert(std::pair(vRPCConvertParams[i].methodName,
                                            vRPCConvertParams[i].paramName));
    }
}

static CRPCConvertTable rpcCvtTable;

/** Non-RFC4627 JSON parser, accepts internal values (such as numbers, true, false, null)
 * as well as objects and arrays.
 */
UniValue ParseNonRFCJSONValue(const std::string& strVal)
{
    UniValue jVal;
    if (!jVal.read(std::string("[")+strVal+std::string("]")) ||
        !jVal.isArray() || jVal.size()!=1)
        throw std::runtime_error(std::string("Error parsing JSON:")+strVal);
    return jVal[0];
}

UniValue RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    UniValue params(UniValue::VARR);

    for (unsigned int idx = 0; idx < strParams.size(); idx++) {
        const std::string& strVal = strParams[idx];

        if (!rpcCvtTable.convert(strMethod, idx)) {
            // insert string value directly
            params.push_back(strVal);
        } else {
            // parse string as JSON, insert bool/number/object/etc. value
            params.push_back(ParseNonRFCJSONValue(strVal));
        }
    }

    return params;
}

UniValue RPCConvertNamedValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    UniValue params(UniValue::VOBJ);

    for (const std::string &s: strParams) {
        size_t pos = s.find("=");
        if (pos == std::string::npos) {
            throw(std::runtime_error("No '=' in named argument '"+s+"', this needs to be present for every argument (even if it is empty)"));
        }

        std::string name = s.substr(0, pos);
        std::string value = s.substr(pos+1);

        if (!rpcCvtTable.convert(strMethod, name)) {
            // insert string value directly
            params.pushKV(name, value);
        } else {
            // parse string as JSON, insert bool/number/object/etc. value
            params.pushKV(name, ParseNonRFCJSONValue(value));
        }
    }

    return params;
}
