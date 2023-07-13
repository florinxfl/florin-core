// Copyright (c) 2009-2010 Satoshi Nakamoto
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

#ifndef SCRIPT_STANDARD_H
#define SCRIPT_STANDARD_H

#include "script/interpreter.h"
#include "uint256.h"
#include "pubkey.h"

#include <boost/variant.hpp>

#include <stdint.h>

static const bool DEFAULT_ACCEPT_DATACARRIER = true;

class CKeyID;
class CScript;

/** A reference to a CScript: the Hash160 of its serialization (see script.h) */
class CScriptID : public uint160
{
public:
    CScriptID() : uint160() {}
    CScriptID(const CScript& in);
    CScriptID(const uint160& in) : uint160(in) {}
};

static const unsigned int MAX_OP_RETURN_RELAY = 83; //!< bytes (+1 for OP_RETURN, +2 for the pushdata opcodes)
extern bool fAcceptDatacarrier;
extern unsigned nMaxDatacarrierBytes;

/**
 * Mandatory script verification flags that all new blocks must comply with for
 * them to be valid. (but old blocks may not comply with) Currently just P2SH,
 * but in the future other flags may be added, such as a soft-fork to enforce
 * strict DER encoding.
 * 
 * Failing one of these tests may trigger a DoS ban - see CheckInputs() for
 * details.
 */
static const unsigned int MANDATORY_SCRIPT_VERIFY_FLAGS = SCRIPT_VERIFY_P2SH;

enum txnouttype
{
    TX_NONSTANDARD,
    // 'standard' transaction types:
    TX_PUBKEY,
    TX_PUBKEYHASH,
    TX_SCRIPTHASH,
    TX_MULTISIG,
    TX_NULL_DATA,
    // NB! Not actual script types, just workarounds for other transaction types in stat geneation
    TX_STANDARD_PUBKEY_HASH,
    TX_STANDARD_WITNESS,
};

class CNoDestination {
public:
    friend bool operator==([[maybe_unused]] const CNoDestination &a, [[maybe_unused]] const CNoDestination &b) { return true; }
    friend bool operator<([[maybe_unused]] const CNoDestination &a, [[maybe_unused]] const CNoDestination &b) { return true; }
};

class CPoW2WitnessDestination{
public:
    //Double check this.
    CPoW2WitnessDestination(const CKeyID& spendingKeyIn, const CKeyID& witnessKeyIn) : spendingKey(spendingKeyIn), witnessKey(witnessKeyIn), lockFromBlock(0), lockUntilBlock(0), failCount(0) {}
    CPoW2WitnessDestination() : spendingKey(CKeyID()), witnessKey(CKeyID()), lockFromBlock(0), lockUntilBlock(0), failCount(0), actionNonce(0) {}

    CKeyID spendingKey;
    CKeyID witnessKey;
    uint64_t lockFromBlock;
    uint64_t lockUntilBlock;
    uint64_t failCount;
    uint64_t actionNonce;

    //fixme: (PHASE5) NB! This compares only on keys, it doesn't consider other variables
    // We should somehow make this more explicit to prevent accidental usage by code that expects better
    // e.g. Should these comparators consider the lock block or not?
    // e.g. Should these return = if the witnessKey is different but spending key is the same? Depends where exactly this is called from...
    // Current behaviour is probably fine, but make explicit that this is the case
    friend bool operator==(const CPoW2WitnessDestination &a, const CPoW2WitnessDestination &b) { return a.spendingKey == b.spendingKey && a.witnessKey == b.witnessKey; }
    friend bool operator<(const CPoW2WitnessDestination &a, const CPoW2WitnessDestination &b) { return a.spendingKey < b.spendingKey || (a.spendingKey == b.spendingKey && a.witnessKey < b.witnessKey); }

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(spendingKey);
        READWRITE(witnessKey);
        READWRITE(lockFromBlock);
        READWRITE(lockUntilBlock);
        READWRITE(VARINT(failCount));
        READWRITE(VARINT(actionNonce));
    }
};

/** 
 * A txout script template with a specific destination. It is either:
 *  * CNoDestination: no destination set
 *  * CKeyID: TX_PUBKEYHASH destination
 *  * CScriptID: TX_SCRIPTHASH destination
 *  A CTxDestination is the internal data type encoded in a CNativeAddress
 */
typedef boost::variant<CNoDestination, CKeyID, CScriptID, CPoW2WitnessDestination> CTxDestination;

const char* GetTxnOutputType(txnouttype t);

bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<std::vector<unsigned char> >& vSolutionsRet);
bool ExtractDestination(const CTxOut& out, CTxDestination& addressRet);
bool ExtractDestinations(const CTxOut& out, txnouttype& typeRet, std::vector<CTxDestination>& addressRet, int& nRequiredRet);
bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet);
bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<CTxDestination>& addressRet, int& nRequiredRet);

CScript GetScriptForDestination(const CTxDestination& dest);
CScript GetScriptForRawPubKey(const CPubKey& pubkey);
CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys);
CScript GetScriptForWitness(const CScript& redeemscript);

#endif
