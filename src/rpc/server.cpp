// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2020-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#include "rpc/server.h"

#include "appname.h"
#include "base58.h"
#include "fs.h"
#include "init.h"
#include "random.h"
#include "sync.h"
#include "ui_interface.h"
#include "util.h"
#include "util/strencodings.h"

#include <unity/appmanager.h>

#include <univalue.h>

#include <boost/bind.hpp>

#include <boost/signals2/signal.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_upper()
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

#include <memory> // for unique_ptr
#include <unordered_map>

static bool fRPCRunning = false;
static bool fRPCInWarmup = true;
static std::string rpcWarmupStatus("RPC server started");
static RecursiveMutex cs_rpcWarmup;


static struct CRPCSignals
{
    boost::signals2::signal<void ()> Started;
    boost::signals2::signal<void ()> Stopped;
    boost::signals2::signal<void (const CRPCCommand&)> PreCommand;
} g_rpcSignals;

void RPCServer::OnStarted(std::function<void ()> slot)
{
    g_rpcSignals.Started.connect(slot);
}

void RPCServer::OnStopped(std::function<void ()> slot)
{
    g_rpcSignals.Stopped.connect(slot);
}

void RPCServer::OnPreCommand(std::function<void (const CRPCCommand&)> slot)
{
    g_rpcSignals.PreCommand.connect(boost::bind(slot, _1));
}

void RPCTypeCheck(const UniValue& params,
                  const std::list<UniValue::VType>& typesExpected,
                  bool fAllowNull)
{
    unsigned int i = 0;
    for(UniValue::VType t : typesExpected)
    {
        if (params.size() <= i)
            break;

        const UniValue& v = params[i];
        if (!(fAllowNull && v.isNull())) {
            RPCTypeCheckArgument(v, t);
        }
        i++;
    }
}

void RPCTypeCheckArgument(const UniValue& value, UniValue::VType typeExpected)
{
    if (value.type() != typeExpected) {
        throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Expected type %s, got %s", uvTypeName(typeExpected), uvTypeName(value.type())));
    }
}

void RPCTypeCheckObj(const UniValue& o,
    const std::map<std::string, UniValueType>& typesExpected,
    bool fAllowNull,
    bool fStrict)
{
    for (const auto& t : typesExpected) {
        const UniValue& v = find_value(o, t.first);
        if (!fAllowNull && v.isNull())
            throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Missing %s", t.first));

        if (!(t.second.typeAny || v.type() == t.second.type || (fAllowNull && v.isNull()))) {
            std::string err = strprintf("Expected type %s for %s, got %s",
                uvTypeName(t.second.type), t.first, uvTypeName(v.type()));
            throw JSONRPCError(RPC_TYPE_ERROR, err);
        }
    }

    if (fStrict)
    {
        for(const std::string& k : o.getKeys())
        {
            if (typesExpected.count(k) == 0)
            {
                std::string err = strprintf("Unexpected key %s", k);
                throw JSONRPCError(RPC_TYPE_ERROR, err);
            }
        }
    }
}

CAmount AmountFromValue(const UniValue& value, bool allowNegative)
{
    if (!value.isNum() && !value.isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount is not a number or string");
    CAmount amount;
    if (!ParseFixedPoint(value.getValStr(), 8, &amount))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
    if (!allowNegative && !MoneyRange(amount))
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount out of range");
    return amount;
}

UniValue ValueFromAmount(const CAmount& amount)
{
    bool sign = amount < 0;
    int64_t n_abs = (sign ? -amount : amount);
    int64_t quotient = n_abs / COIN;
    int64_t remainder = n_abs % COIN;
    return UniValue(UniValue::VNUM,
            strprintf("%s%d.%08d", sign ? "-" : "", quotient, remainder));
}

uint256 ParseHashV(const UniValue& v, std::string strName)
{
    std::string strHex;
    if (v.isStr())
        strHex = v.get_str();
    if (!IsHex(strHex)) // Note: IsHex("") is false
        throw JSONRPCError(RPC_INVALID_PARAMETER, strName+" must be hexadecimal string (not '"+strHex+"')");
    if (64 != strHex.length())
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("%s must be of length %d (not %d)", strName, 64, strHex.length()));
    uint256 result;
    result.SetHex(strHex);
    return result;
}
uint256 ParseHashO(const UniValue& o, std::string strKey)
{
    return ParseHashV(find_value(o, strKey), strKey);
}
std::vector<unsigned char> ParseHexV(const UniValue& v, std::string strName)
{
    std::string strHex;
    if (v.isStr())
        strHex = v.get_str();
    if (!IsHex(strHex))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strName+" must be hexadecimal string (not '"+strHex+"')");
    return ParseHex(strHex);
}
std::vector<unsigned char> ParseHexO(const UniValue& o, std::string strKey)
{
    return ParseHexV(find_value(o, strKey), strKey);
}

/**
 * Note: This interface may still be subject to change.
 */

std::string CRPCTable::help(const std::string& strCommand, const JSONRPCRequest& helpreq) const
{
    std::string strRet;
    std::string category;
    std::set<rpcfn_type> setDone;
    std::vector<std::pair<std::string, const CRPCCommand*> > vCommands;

    for (std::map<std::string, const CRPCCommand*>::const_iterator mi = mapCommands.begin(); mi != mapCommands.end(); ++mi)
        vCommands.push_back(std::pair(mi->second->category + mi->first, mi->second));
    sort(vCommands.begin(), vCommands.end());

    JSONRPCRequest jreq(helpreq);
    jreq.fHelp = true;
    jreq.params = UniValue();

    for(const PAIRTYPE(std::string, const CRPCCommand*)& command : vCommands)
    {
        const CRPCCommand *pcmd = command.second;
        std::string strMethod = pcmd->name;
        if ((strCommand != "" || pcmd->category == "hidden" || pcmd->category == "support") && strMethod != strCommand)
            continue;
        jreq.strMethod = strMethod;
        try
        {
            rpcfn_type pfn = pcmd->actor;
            if (setDone.insert(pfn).second)
                (*pfn)(jreq);
        }
        catch (const std::exception& e)
        {
            // Help text is returned in an exception
            std::string strHelp = std::string(e.what());
            if (strCommand == "")
            {
                if (strHelp.find('\n') != std::string::npos)
                    strHelp = strHelp.substr(0, strHelp.find('\n'));

                int commandEnd = strHelp.find(" ");
                if (commandEnd == std::string::npos)
                    strHelp = command.second->name;
                else
                    strHelp = command.second->name + " " + strHelp.substr(commandEnd, std::string::npos);
                
                if (category != pcmd->category)
                {
                    if (!category.empty())
                        strRet += "\n";
                    category = pcmd->category;
                    std::string firstLetter = category.substr(0,1);
                    boost::to_upper(firstLetter);
                    strRet += "== " + firstLetter + category.substr(1) + " ==\n";
                }
            }
            strRet += strHelp + "\n";
        }
    }
    if (strRet == "")
        strRet = strprintf("help: unknown command: %s\n", strCommand);
    strRet = strRet.substr(0,strRet.size()-1);
    return strRet;
}

static UniValue help(const JSONRPCRequest& jsonRequest)
{
    if (jsonRequest.fHelp || jsonRequest.params.size() > 1)
        throw std::runtime_error(
            "help ( \"command\" )\n"
            "\nList all commands, or get help for a specified command.\n"
            "\nArguments:\n"
            "1. \"command\"     (string, optional) The command to get help on\n"
            "\nResult:\n"
            "\"text\"     (string) The help text\n"
        );

    std::string strCommand;
    if (jsonRequest.params.size() > 0)
        strCommand = jsonRequest.params[0].get_str();

    return tableRPC.help(strCommand, jsonRequest);
}


static UniValue stop(const JSONRPCRequest& jsonRequest)
{
    // Accept the deprecated and ignored 'detach' boolean argument
    // Also accept the hidden 'wait' integer argument (milliseconds)
    // For instance, 'stop 1000' makes the call wait 1 second before returning
    // to the client (intended for testing)
    if (jsonRequest.fHelp || jsonRequest.params.size() > 1)
        throw std::runtime_error(
            "stop\n"
            "\nStop " GLOBAL_APPNAME " server.");
    // Event loop will exit after current HTTP requests have been handled, so
    // this reply will get back to the client.
    LogPrintf("shutdown: 'stop' called via RPC, terminating app");
    AppLifecycleManager::gApp->shutdown();
    if (jsonRequest.params[0].isNum()) {
        MilliSleep(jsonRequest.params[0].get_int());
    }
    return GLOBAL_APPNAME" server stopping";
}

static UniValue uptime(const JSONRPCRequest& jsonRequest)
{
    if (jsonRequest.fHelp || jsonRequest.params.size() > 1)
        throw std::runtime_error(
            "uptime\n"
            "\nReturns the total uptime of the server (in seconds).");

    return GetTime() - GetStartupTime();
}

/**
 * Call Table
 */
static const CRPCCommand vRPCCommands[] =
{ //  category              name                      actor (function)         okSafe argNames
  //  --------------------- ------------------------  -----------------------  ------ ----------
    /* Overall control/query calls */
    { "control",            "help",                   &help,                   true,  {"command"}  },
    { "control",            "stop",                   &stop,                   true,  {"wait"}  },
    { "control",            "uptime",                 &uptime,                 true,  {""}  },
};

CRPCTable::CRPCTable()
{
    unsigned int vcidx;
    for (vcidx = 0; vcidx < (sizeof(vRPCCommands) / sizeof(vRPCCommands[0])); vcidx++)
    {
        const CRPCCommand *pcmd;

        pcmd = &vRPCCommands[vcidx];
        mapCommands[pcmd->name] = pcmd;
    }
}

const CRPCCommand *CRPCTable::operator[](const std::string &name) const
{
    std::map<std::string, const CRPCCommand*>::const_iterator it = mapCommands.find(name);
    if (it == mapCommands.end())
        return NULL;
    return (*it).second;
}

bool CRPCTable::appendCommand(const std::string& name, const CRPCCommand* pcmd)
{
    if (IsRPCRunning())
        return false;

    // don't allow overwriting for now
    std::map<std::string, const CRPCCommand*>::const_iterator it = mapCommands.find(name);
    if (it != mapCommands.end())
        return false;

    mapCommands[name] = pcmd;
    return true;
}

bool StartRPC()
{
    LogPrint(BCLog::RPC, "Starting RPC\n");
    fRPCRunning = true;
    g_rpcSignals.Started();
    return true;
}

void InterruptRPC()
{
    LogPrint(BCLog::RPC, "Interrupting RPC\n");
    // Interrupt e.g. running longpolls
    fRPCRunning = false;
}

void StopRPC()
{
    LogPrint(BCLog::RPC, "Stopping RPC\n");
    DeleteAuthCookie();
    g_rpcSignals.Stopped();
}

bool IsRPCRunning()
{
    return fRPCRunning;
}

void SetRPCWarmupStatus(const std::string& newStatus)
{
    LOCK(cs_rpcWarmup);
    rpcWarmupStatus = newStatus;
}

void SetRPCWarmupFinished()
{
    LOCK(cs_rpcWarmup);
    assert(fRPCInWarmup);
    fRPCInWarmup = false;
}

bool RPCIsInWarmup(std::string *outStatus)
{
    LOCK(cs_rpcWarmup);
    if (outStatus)
        *outStatus = rpcWarmupStatus;
    return fRPCInWarmup;
}

void JSONRPCRequest::parse(const UniValue& valRequest)
{
    // Parse request
    if (!valRequest.isObject())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Invalid Request object");
    const UniValue& request = valRequest.get_obj();

    // Parse id now so errors from here on will have the id
    id = find_value(request, "id");

    // Parse method
    UniValue valMethod = find_value(request, "method");
    if (valMethod.isNull())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Missing method");
    if (!valMethod.isStr())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Method must be a string");
    strMethod = valMethod.get_str();
    LogPrint(BCLog::RPC, "ThreadRPCServer method=%s\n", SanitizeString(strMethod));

    // Parse params
    UniValue valParams = find_value(request, "params");
    if (valParams.isArray() || valParams.isObject())
        params = valParams;
    else if (valParams.isNull())
        params = UniValue(UniValue::VARR);
    else
        throw JSONRPCError(RPC_INVALID_REQUEST, "Params must be an array or object");
}

static UniValue JSONRPCExecOne(const UniValue& req)
{
    UniValue rpc_result(UniValue::VOBJ);

    JSONRPCRequest jreq;
    try {
        jreq.parse(req);

        UniValue result = tableRPC.execute(jreq);
        rpc_result = JSONRPCReplyObj(result, NullUniValue, jreq.id);
    }
    catch (const UniValue& objError)
    {
        rpc_result = JSONRPCReplyObj(NullUniValue, objError, jreq.id);
    }
    catch (const std::exception& e)
    {
        rpc_result = JSONRPCReplyObj(NullUniValue,
                                     JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
    }

    return rpc_result;
}

std::string JSONRPCExecBatch(const UniValue& vReq)
{
    UniValue ret(UniValue::VARR);
    for (unsigned int reqIdx = 0; reqIdx < vReq.size(); reqIdx++)
        ret.push_back(JSONRPCExecOne(vReq[reqIdx]));

    return ret.write() + "\n";
}

/**
 * Process named arguments into a vector of positional arguments, based on the
 * passed-in specification for the RPC call's arguments.
 */
static inline JSONRPCRequest transformNamedArguments(const JSONRPCRequest& in, const std::vector<std::string>& argNames)
{
    JSONRPCRequest out = in;
    out.params = UniValue(UniValue::VARR);
    // Build a map of parameters, and remove ones that have been processed, so that we can throw a focused error if
    // there is an unknown one.
    const std::vector<std::string>& keys = in.params.getKeys();
    const std::vector<UniValue>& values = in.params.getValues();
    std::unordered_map<std::string, const UniValue*> argsIn;
    for (size_t i=0; i<keys.size(); ++i) {
        argsIn[keys[i]] = &values[i];
    }
    // Process expected parameters.
    int hole = 0;
    for (const std::string &argNamePattern: argNames) {
        std::vector<std::string> vargNames;
        boost::algorithm::split(vargNames, argNamePattern, boost::algorithm::is_any_of("|"));
        auto fr = argsIn.end();
        for (const std::string & argName : vargNames) {
            fr = argsIn.find(argName);
            if (fr != argsIn.end()) {
                break;
            }
        }
        if (fr != argsIn.end()) {
            for (int i = 0; i < hole; ++i) {
                // Fill hole between specified parameters with JSON nulls,
                // but not at the end (for backwards compatibility with calls
                // that act based on number of specified parameters).
                out.params.push_back(UniValue());
            }
            hole = 0;
            out.params.push_back(*fr->second);
            argsIn.erase(fr);
        } else {
            hole += 1;
        }
    }
    // If there are still arguments in the argsIn map, this is an error.
    if (!argsIn.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown named parameter " + argsIn.begin()->first);
    }
    // Return request with named arguments transformed to positional arguments
    return out;
}

UniValue CRPCTable::execute(const JSONRPCRequest &request) const
{
    // Return immediately if in warmup
    {
        LOCK(cs_rpcWarmup);
        if (fRPCInWarmup)
            throw JSONRPCError(RPC_IN_WARMUP, rpcWarmupStatus);
    }

    // Find method
    const CRPCCommand *pcmd = tableRPC[request.strMethod];
    if (!pcmd)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found");

    g_rpcSignals.PreCommand(*pcmd);

    try
    {
        // Execute, convert arguments to array if necessary
        if (request.params.isObject()) {
            return pcmd->actor(transformNamedArguments(request, pcmd->argNames));
        } else {
            return pcmd->actor(request);
        }
    }
    catch (const std::exception& e)
    {
        throw JSONRPCError(RPC_MISC_ERROR, e.what());
    }
}

std::vector<std::string> CRPCTable::listCommands() const
{
    std::vector<std::string> commandList;
    for (const auto& [commandName, command] : mapCommands)
    {
        if (command->category != "hidden")
        {
            commandList.push_back(commandName);
        }
    }
    return commandList;
}

std::string HelpExampleCli(const std::string& methodname, const std::string& args)
{
    return "> " GLOBAL_APPNAME "-cli " + methodname + " " + args + "\n";
}

std::string HelpExampleRpc(const std::string& methodname, const std::string& args)
{
    return "> curl --user myusername --data-binary '{\"jsonrpc\": \"1.0\", \"id\":\"curltest\", "
        "\"method\": \"" + methodname + "\", \"params\": [" + args + "] }' -H 'content-type: text/plain;' http://127.0.0.1:9232/\n";
}

int RPCSerializationFlags()
{
    int flag = 0;
    return flag;
}

CRPCTable tableRPC;
