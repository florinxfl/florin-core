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

#include "primitives/transaction.h"

#include "hash.h"
#include "tinyformat.h"
#include "util/strencodings.h"

uint256 COutPoint::getBucketHash() const
{
    if (isHash)
    {
        return hash;
    }
    else
    {
        return prevBlock.getHash();
    }
}

uint256 COutPoint::getTransactionHash() const
{
    assert(isHash);
    return hash;
}

std::string COutPoint::ToString() const
{
    if (isHash)
        return strprintf("COutPoint(hash=%s, out_index=%u)", hash.ToString().substr(0,10), n);
    else
        return strprintf("COutPoint(tx_height=%u, tx_index=%u, out_index=%u)", prevBlock.blockNumber, prevBlock.transactionIndex, n);
}

CTxIn::CTxIn(COutPoint prevoutIn, CScript scriptSigIn, uint32_t nSequenceIn, uint8_t nFlagsIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
    nTypeAndFlags = CURRENT_TYPE | nFlagsIn;
    if (!prevout.isHash)
        SetFlag(CTxInFlags::IndexBasedOutpoint);
}

CTxIn::CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn, uint32_t nSequenceIn, uint8_t nFlagsIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
    nTypeAndFlags = CURRENT_TYPE | nFlagsIn;
}

std::string CTxIn::ToString() const
{
    std::string str;
    str += "CTxIn(";
    str += prevout.ToString();
    if (prevout.IsNull())
        str += strprintf(", coinbase %s", HexStr(scriptSig));
    else
        str += strprintf(", scriptSig=%s", HexStr(scriptSig).substr(0, 24));
        
    if (FlagIsSet(CTxInFlags::HasRelativeLock))
    {
        str += strprintf(", relativeLockSequence=%u", nSequence);
    }
    else if (FlagIsSet(CTxInFlags::HasAbsoluteLock))
    {
        str += strprintf(", absoluteLockSequence=%u", nSequence);
    }
    //fixme: (PHASE5) - Other sequence behaviours we should log?
    //else if(...)
    //{
    //}
    //fixme: (PHASE5) - we can remove the below
    else
    {
        if (nSequence != SEQUENCE_FINAL)
            str += strprintf(", nSequence=%u", nSequence);
    }
    str += ")";
    return str;
}

CTxOut::CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn)
{
    //NB! Important otherwise we fail to initialise some member variables.
    //CBSU - possibly initialise only those variables instead of double assigning nValue and nType
    SetNull();

    nValue = nValueIn;
    SetType(CTxOutType::ScriptLegacyOutput);
    output.scriptPubKey = scriptPubKeyIn;
}

CTxOut::CTxOut(const CAmount& nValueIn, CTxOutPoW2Witness witnessDetails)
{
    //NB! Important otherwise we fail to initialise some member variables.
    //CBSU - possibly initialise only those variables instead of double assigning nValue and nType
    SetNull();

    nValue = nValueIn;
    SetType(CTxOutType::PoW2WitnessOutput);
    output.witnessDetails = witnessDetails;
}

CTxOut::CTxOut(const CAmount& nValueIn, CTxOutStandardKeyHash standardKeyHash)
{
    //NB! Important otherwise we fail to initialise some member variables.
    //CBSU - possibly initialise only those variables instead of double assigning nValue and nType
    SetNull();

    nValue = nValueIn;
    SetType(CTxOutType::StandardKeyHashOutput);
    output.standardKeyHash = standardKeyHash;
}

std::string CTxOut::ToString() const
{
    return strprintf("CTxOut(nValue=%d.%08d, type=%s, data=%s)", nValue / COIN, nValue % COIN, GetTypeAsString(), output.GetHex().substr(0, 30));
}

//fixme: (PHASE5) restore CURRENT_VERSION behaviour here.
//CMutableTransaction::CMutableTransaction() : nVersion(CTransaction::CURRENT_VERSION), nLockTime(0), flags(0), extraFlags(0) {}
CMutableTransaction::CMutableTransaction(int32_t nVersion_) : nVersion(nVersion_), nLockTime(0), flags(0), extraFlags(0) {}
CMutableTransaction::CMutableTransaction(const CTransaction& tx) : nVersion(tx.nVersion), vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime), flags(tx.flags), extraFlags(tx.extraFlags) {}

uint256 CMutableTransaction::GetHash() const
{
    if (!IsOldTransactionVersion(nVersion))
        return SerializeHash(*this, SER_GETHASH, 0);
    else
        return SerializeHash(*this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_SEGREGATED_SIGNATURES);
}

uint256 CTransaction::ComputeHash() const
{
    if (!IsOldTransactionVersion(nVersion))
        return SerializeHash(*this, SER_GETHASH, 0);
    else
        return SerializeHash(*this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_SEGREGATED_SIGNATURES);
}

uint256 CTransaction::GetWitnessHash() const
{
    if (nVersion < 3)
    {
        if (!HasSegregatedSignatures()) {
            return GetHash();
        }
    }
    return SerializeHash(*this, SER_GETHASH, 0);
}

/* For backward compatibility, the hash is initialized to 0. TODO: remove the need for this default constructor entirely. */
//fixme: (PHASE5) restore CURRENT_VERSION behaviour here.
//CTransaction::CTransaction() : nVersion(CTransaction::CURRENT_VERSION), vin(), vout(), nLockTime(0), flags(0), extraFlags(0), hash() {}
CTransaction::CTransaction(int32_t nVersion_) : nVersion(nVersion_), vin(), vout(), nLockTime(0), flags(0), extraFlags(0), hash() {}
CTransaction::CTransaction(const CMutableTransaction &tx) : nVersion(tx.nVersion), vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime), flags(tx.flags), extraFlags(tx.extraFlags), hash(ComputeHash()) {}
CTransaction::CTransaction(CMutableTransaction &&tx) : nVersion(tx.nVersion), vin(std::move(tx.vin)), vout(std::move(tx.vout)), nLockTime(tx.nLockTime), flags(tx.flags), extraFlags(tx.extraFlags), hash(ComputeHash()) {}

CAmount CTransaction::GetValueOut() const
{
    CAmount nValueOut = 0;
    for (std::vector<CTxOut>::const_iterator it(vout.begin()); it != vout.end(); ++it)
    {
        if (it->nValue > 0)
        {
            nValueOut += it->nValue;
            if (!MoneyRange(it->nValue) || !MoneyRange(nValueOut))
                throw std::runtime_error(std::string(__func__) + ": value out of range");
        }
    }
    return nValueOut;
}

unsigned int CTransaction::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

std::string CTransaction::ToString() const
{
    std::string str;
    str += strprintf("CTransaction(hash=%s, ver=%d, vin.size=%u, vout.size=%u, nLockTime=%u)\n",
        GetHash().ToString().substr(0,10),
        nVersion,
        vin.size(),
        vout.size(),
        nLockTime);
    for (unsigned int i = 0; i < vin.size(); i++)
        str += "    " + vin[i].ToString() + "\n";
    for (unsigned int i = 0; i < vin.size(); i++)
        str += "    " + vin[i].segregatedSignatureData.ToString() + "\n";
    for (unsigned int i = 0; i < vout.size(); i++)
        str += "    " + vout[i].ToString() + "\n";
    return str;
}

int64_t GetTransactionWeight(const CTransaction& tx)
{
    // segsig: transaction weight = transaction size, including the size of the segregated signatures - no complicated segwit weighting shenanigans necessary.
    return ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
}
