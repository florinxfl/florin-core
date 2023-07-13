// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2017-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#include "core_io.h"

#include "base58.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/standard.h"
#include "serialize.h"
#include "streams.h"
#include <univalue.h>
#include "util.h"
#include "util/moneystr.h"
#include "util/strencodings.h"
#include <txdb.h>


std::string FormatScript(const CScript& script)
{
    std::string ret;
    CScript::const_iterator it = script.begin();
    opcodetype op;
    while (it != script.end()) {
        CScript::const_iterator it2 = it;
        std::vector<unsigned char> vch;
        if (script.GetOp2(it, op, &vch)) {
            if (op == OP_0) {
                ret += "0 ";
                continue;
            } else if ((op >= OP_1 && op <= OP_16) || op == OP_1NEGATE) {
                ret += strprintf("%i ", op - OP_1NEGATE - 1);
                continue;
            } else if (op >= OP_NOP && op <= OP_NOP10) {
                std::string str(GetOpName(op));
                if (str.substr(0, 3) == std::string("OP_")) {
                    ret += str.substr(3, std::string::npos) + " ";
                    continue;
                }
            }
            if (vch.size() > 0) {
                ret += strprintf("0x%x 0x%x ", HexStr(it2, it - vch.size()), HexStr(it - vch.size(), it));
            } else {
                ret += strprintf("0x%x ", HexStr(it2, it));
            }
            continue;
        }
        ret += strprintf("0x%x ", HexStr(it2, script.end()));
        break;
    }
    return ret.substr(0, ret.size() - 1);
}

const std::map<unsigned char, std::string> mapSigHashTypes = {
    {static_cast<unsigned char>(SIGHASH_ALL), std::string("ALL")},
    {static_cast<unsigned char>(SIGHASH_ALL|SIGHASH_ANYONECANPAY), std::string("ALL|ANYONECANPAY")},
    {static_cast<unsigned char>(SIGHASH_NONE), std::string("NONE")},
    {static_cast<unsigned char>(SIGHASH_NONE|SIGHASH_ANYONECANPAY), std::string("NONE|ANYONECANPAY")},
    {static_cast<unsigned char>(SIGHASH_SINGLE), std::string("SINGLE")},
    {static_cast<unsigned char>(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY), std::string("SINGLE|ANYONECANPAY")},
};

/**
 * Create the assembly string representation of a CScript object.
 * @param[in] script    CScript object to convert into the asm string representation.
 * @param[in] fAttemptSighashDecode    Whether to attempt to decode sighash types on data within the script that matches the format
 *                                     of a signature. Only pass true for scripts you believe could contain signatures. For example,
 *                                     pass false, or omit the this argument (defaults to false), for scriptPubKeys.
 */
std::string ScriptToAsmStr(const CScript& script, const bool fAttemptSighashDecode)
{
    std::string str;
    opcodetype opcode;
    std::vector<unsigned char> vch;
    CScript::const_iterator pc = script.begin();
    while (pc < script.end()) {
        if (!str.empty()) {
            str += " ";
        }
        if (!script.GetOp(pc, opcode, vch)) {
            str += "[error]";
            return str;
        }
        if (0 <= opcode && opcode <= OP_PUSHDATA4) {
            if (vch.size() <= static_cast<std::vector<unsigned char>::size_type>(4)) {
                str += strprintf("%d", CScriptNum(vch, false).getint());
            } else {
                // the IsUnspendable check makes sure not to try to decode OP_RETURN data that may match the format of a signature
                if (fAttemptSighashDecode && !script.IsUnspendable()) {
                    std::string strSigHashDecode;
                    // goal: only attempt to decode a defined sighash type from data that looks like a signature within a scriptSig.
                    // this won't decode correctly formatted public keys in Pubkey or Multisig scripts due to
                    // the restrictions on the pubkey formats (see IsCompressedOrUncompressedPubKey) being incongruous with the
                    // checks in CheckSignatureEncoding.
                    if (CheckSignatureEncoding(vch, SCRIPT_VERIFY_STRICTENC, NULL)) {
                        const unsigned char chSigHashType = vch.back();
                        if (mapSigHashTypes.count(chSigHashType)) {
                            strSigHashDecode = "[" + mapSigHashTypes.find(chSigHashType)->second + "]";
                            vch.pop_back(); // remove the sighash type byte. it will be replaced by the decode.
                        }
                    }
                    str += HexStr(vch) + strSigHashDecode;
                } else {
                    str += HexStr(vch);
                }
            }
        } else {
            str += GetOpName(opcode);
        }
    }
    return str;
}

std::string EncodeHexTx(const CTransaction& tx, const int serializeFlags)
{
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION | serializeFlags);
    ssTx << tx;
    return HexStr(ssTx.begin(), ssTx.end());
}

void ScriptPubKeyToUniv(const CScript& scriptPubKey,
                        UniValue& out, bool fIncludeHex)
{
    txnouttype type;
    std::vector<CTxDestination> addresses;
    int nRequired;

    out.pushKV("asm", ScriptToAsmStr(scriptPubKey));
    if (fIncludeHex)
        out.pushKV("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        out.pushKV("type", GetTxnOutputType(type));
        return;
    }

    out.pushKV("reqSigs", nRequired);
    out.pushKV("type", GetTxnOutputType(type));

    UniValue a(UniValue::VARR);
    for(const CTxDestination& addr : addresses)
        a.push_back(CNativeAddress(addr).ToString());
    out.pushKV("addresses", a);
}

void StandardKeyHashToUniv(const CTxOut& txout, UniValue& out, bool fIncludeHex)
{
    if (fIncludeHex)
        out.pushKV("hex", txout.output.GetHex());

    out.pushKV("address", CNativeAddress(txout.output.standardKeyHash.keyID).ToString());
}

void PoW2WitnessToUniv(const CTxOut& txout, UniValue& out, bool fIncludeHex)
{
    if (fIncludeHex)
        out.pushKV("hex", txout.output.GetHex());

    out.pushKV("lock_from_block", txout.output.witnessDetails.lockFromBlock);
    out.pushKV("lock_until_block", txout.output.witnessDetails.lockUntilBlock);
    out.pushKV("fail_count", txout.output.witnessDetails.failCount);
    out.pushKV("action_nonce", txout.output.witnessDetails.actionNonce);
    out.pushKV("pubkey_spend", txout.output.witnessDetails.spendingKeyID.ToString());
    out.pushKV("pubkey_witness", txout.output.witnessDetails.witnessKeyID.ToString());

    out.pushKV("address", CNativeAddress(CPoW2WitnessDestination(txout.output.witnessDetails.spendingKeyID, txout.output.witnessDetails.witnessKeyID)).ToString());
}

extern uint256 getHashFromTxIndexRef(uint64_t blockHeight, uint64_t txIndex);

void TxToUniv(const CTransaction& tx, const uint256& hashBlock, UniValue& entry)
{
    entry.pushKV("txid", tx.GetHash().GetHex());
    entry.pushKV("hash", tx.GetWitnessHash().GetHex());
    entry.pushKV("version", tx.nVersion);
    entry.pushKV("size", (int)::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION));
    entry.pushKV("vsize", GetTransactionWeight(tx));
    entry.pushKV("locktime", (int64_t)tx.nLockTime);

    UniValue vin(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxIn& txin = tx.vin[i];
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase() && !tx.IsPoW2WitnessCoinBase())
        {
            in.pushKV("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
        }
        else
        {
            if ( tx.IsPoW2WitnessCoinBase() )
            {
                in.pushKV("pow2_coinbase", "");
            }
            if (txin.GetPrevOut().isHash)
            {
                in.pushKV("prevout_type", "hash");
                in.pushKV("txid", txin.GetPrevOut().getTransactionHash().GetHex());
                in.pushKV("tx_height", "");
                in.pushKV("tx_index", "");
            }
            else
            {
                in.pushKV("prevout_type", "index");
                
                //NB! This will only work on machines that have a txindex that has been rebuilt with the code where this was added (2.0.12)
                uint256 correspondingHash = getHashFromTxIndexRef(txin.GetPrevOut().getTransactionBlockNumber(), txin.GetPrevOut().getTransactionIndex());
                if (!correspondingHash.IsNull())
                {
                    in.pushKV("txid", correspondingHash.GetHex());
                }
                else
                {
                    in.pushKV("txid", "");
                }
                in.pushKV("tx_height", txin.GetPrevOut().getTransactionBlockNumber());
                in.pushKV("tx_index", txin.GetPrevOut().getTransactionIndex());
            }
            
            in.pushKV("vout", (int64_t)txin.GetPrevOut().n);
            UniValue o(UniValue::VOBJ);
            o.pushKV("asm", ScriptToAsmStr(txin.scriptSig, true));
            o.pushKV("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
            in.pushKV("scriptSig", o);
            if (!tx.vin[i].segregatedSignatureData.IsNull()) 
            {
                UniValue txinSigData(UniValue::VARR);
                for (const auto& item : tx.vin[i].segregatedSignatureData.stack)
                {
                    txinSigData.push_back(HexStr(item.begin(), item.end()));
                }
                in.pushKV("txin_sig_data", txinSigData);
            }
        }
        if (IsOldTransactionVersion(tx.nVersion) || txin.FlagIsSet(CTxInFlags::HasRelativeLock))
        {
            in.pushKV("sequence", (int64_t)txin.GetSequence(tx.nVersion));
        }
        if (txin.FlagIsSet(CTxInFlags::OptInRBF))
        {
            in.pushKV("rbf", true);
        }
        else
        {
            in.pushKV("rbf", false);
        }
        vin.push_back(in);
    }
    entry.pushKV("vin", vin);

    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        UniValue out(UniValue::VOBJ);

        UniValue outValue(UniValue::VNUM, FormatMoney(txout.nValue));
        out.pushKV("value", outValue);
        out.pushKV("n", (int64_t)i);

        switch (txout.GetType())
        {
            case CTxOutType::ScriptLegacyOutput:
            {
                UniValue o(UniValue::VOBJ);
                ScriptPubKeyToUniv(txout.output.scriptPubKey, o, true);
                out.pushKV("scriptPubKey", o);
                vout.push_back(out);
                break;
            }
            case CTxOutType::PoW2WitnessOutput:
            {
                UniValue o(UniValue::VOBJ);
                PoW2WitnessToUniv(txout, o, true);
                out.pushKV("PoW²-witness", o);
                vout.push_back(out);
                break;
            }
            case CTxOutType::StandardKeyHashOutput:
            {
                UniValue o(UniValue::VOBJ);
                StandardKeyHashToUniv(txout, o, true);
                out.pushKV("standard-key-hash", o);
                vout.push_back(out);
                break;
            }
        }
    }
    entry.pushKV("vout", vout);

    if (!hashBlock.IsNull())
        entry.pushKV("blockhash", hashBlock.GetHex());

    entry.pushKV("hex", EncodeHexTx(tx)); // the hex-encoded transaction. used the name "hex" to be consistent with the verbose output of "getrawtransaction".
}
