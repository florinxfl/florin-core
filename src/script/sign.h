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

#ifndef SCRIPT_SIGN_H
#define SCRIPT_SIGN_H

#include "script/interpreter.h"

class CKeyID;
class CKeyStore;
class CScript;
class CTransaction;

struct CMutableTransaction;

/** Virtual base class for signature creators. */
class BaseSignatureCreator {
protected:
    std::vector<CKeyStore*> accountsToTry;

public:
    BaseSignatureCreator(const std::vector<CKeyStore*>& accountsToTryIn) : accountsToTry(accountsToTryIn) {}
    const std::vector<CKeyStore*>& accounts() const { return accountsToTry; };
    virtual ~BaseSignatureCreator() {}
    virtual const BaseSignatureChecker& Checker() const =0;

    /** Create a singular (non-script) signature. */
    virtual bool CreateSig(std::vector<unsigned char>& vchSig, const CKeyID& keyid, const CScript& scriptCode, SigVersion sigversion) const =0;
};

/** A signature creator for transactions. */
class TransactionSignatureCreator : public BaseSignatureCreator {
    const CTransaction* txTo;
    unsigned int nIn;
    int nHashType;
    CAmount amount;
    const TransactionSignatureChecker checker;

public:
    TransactionSignatureCreator(CKeyID signingKeyID, const std::vector<CKeyStore*>& accountsToTryIn, const CTransaction* txToIn, unsigned int nInIn, const CAmount& amountIn, int nHashTypeIn=SIGHASH_ALL);
    const BaseSignatureChecker& Checker() const { return checker; }
    bool CreateSig(std::vector<unsigned char>& vchSig, const CKeyID& keyid, const CScript& scriptCode, SigVersion sigversion) const;
};

class MutableTransactionSignatureCreator : public TransactionSignatureCreator {
    CTransaction tx;

public:
    MutableTransactionSignatureCreator(CKeyID signingKeyID, const std::vector<CKeyStore*>& accountsToTryIn, const CMutableTransaction* txToIn, unsigned int nInIn, const CAmount& amountIn, int nHashTypeIn) : TransactionSignatureCreator(signingKeyID, accountsToTryIn, &tx, nInIn, amountIn, nHashTypeIn), tx(*txToIn) {}
};

/** A signature creator that just produces 72-byte empty signatures. */
class DummySignatureCreator : public BaseSignatureCreator {
public:
    DummySignatureCreator(const std::vector<CKeyStore*>& accountsToTryIn) : BaseSignatureCreator(accountsToTryIn) {}
    const BaseSignatureChecker& Checker() const;
    bool CreateSig(std::vector<unsigned char>& vchSig, const CKeyID& keyid, const CScript& scriptCode, SigVersion sigversion) const;
};

struct SignatureData {
    CScript scriptSig;
    CSegregatedSignatureData segregatedSignatureData;

    SignatureData() {}
    explicit SignatureData(const CScript& script) : scriptSig(script) {}
};

enum SignType
{
    Spend,
    Witness
};

/** Get the CKeyID of the pubkey for the key that should be used to sign an output */
CKeyID ExtractSigningPubkeyFromTxOutput(const CTxOut& txOut, SignType type);

/** Produce a script signature using a generic signature creator. */
bool ProduceSignature(const BaseSignatureCreator& creator, const CTxOut& fromOutput, SignatureData& sigdata, SignType type, uint64_t nVersion);

/** Produce a script signature for a transaction. */
bool SignSignature(const std::vector<CKeyStore*>& accountsToTry, const CTxOut& fromOutput, CMutableTransaction& txTo, unsigned int nIn, const CAmount& amount, int nHashType, SignType type);
bool SignSignature(const std::vector<CKeyStore*>& accountsToTry, const CTransaction& txFrom, CMutableTransaction& txTo, unsigned int nIn, int nHashType, SignType type);

/** Combine two script signatures using a generic signature checker, intelligently, possibly with OP_0 placeholders. */
SignatureData CombineSignatures(const CScript& scriptPubKey, const BaseSignatureChecker& checker, const SignatureData& scriptSig1, const SignatureData& scriptSig2, SigVersion sigversion);

/** Extract signature data from a transaction, and insert it. */
SignatureData DataFromTransaction(const CMutableTransaction& tx, unsigned int nIn);
void UpdateTransaction(CMutableTransaction& tx, unsigned int nIn, const SignatureData& data);

#endif
