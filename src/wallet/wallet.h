// Copyright (c) 2009-2010 Satoshi Nakamoto
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

#ifndef WALLET_WALLET_H
#define WALLET_WALLET_H

#include "wallettx.h"

#include "amount.h"
#include "policy/feerate.h"
#include "streams.h"
#include "tinyformat.h"
#include "ui_interface.h"
#include "util/strencodings.h"
#include "validation/validationinterface.h"
#include "script/ismine.h"
#include "script/sign.h"
#include "wallet/crypter.h"
#include "wallet/walletdb.h"
#include "wallet/rpcwallet.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

typedef CWallet* CWalletRef;
extern std::vector<CWalletRef> vpwallets;
extern CWalletRef pactiveWallet;

//Munt specific includes
#include "wallet/walletdberrors.h"
#include "wallet/extwallet.h"
#include "wallet/account.h"

#include <boost/thread.hpp>
#include <consensus/consensus.h>

/**
 * Settings
 */
extern CFeeRate payTxFee;
extern unsigned int nTxConfirmTarget;
extern bool bSpendZeroConfChange;
extern bool fWalletRbf;
extern bool fSPV;

static const unsigned int DEFAULT_ACCOUNT_KEYPOOL_SIZE = 30;
//! -paytxfee default
static const CAmount DEFAULT_TRANSACTION_FEE = 1000;
//! -fallbackfee default
static const CAmount DEFAULT_FALLBACK_FEE = 2000;
//! -mintxfee default
static const CAmount DEFAULT_TRANSACTION_MINFEE = 100;
//! minimum recommended increment for BIP 125 replacement txs
static const CAmount WALLET_INCREMENTAL_RELAY_FEE = 100;
//! target minimum change amount
static const CAmount MIN_CHANGE = CENT;
//! final minimum change amount after paying for fees
static const CAmount MIN_FINAL_CHANGE = MIN_CHANGE/2;
//! Default for -spendzeroconfchange
static const bool DEFAULT_SPEND_ZEROCONF_CHANGE = true;
//! Default for -walletrejectlongchains
static const bool DEFAULT_WALLET_REJECT_LONG_CHAINS = false;
//! -txconfirmtarget default
static const unsigned int DEFAULT_TX_CONFIRM_TARGET = 6;
//! -walletrbf default
static const bool DEFAULT_WALLET_RBF = false;
static const bool DEFAULT_WALLETBROADCAST = true;
static const bool DEFAULT_DISABLE_WALLET = false;
//! if set, all keys will be derived by using BIP32
static const bool DEFAULT_USE_HD_WALLET = true;

extern const char * DEFAULT_WALLET_DAT;

class CBlockIndex;
class CCoinControl;
class COutput;
class CReserveKeyOrScript;
class CScript;
class CScheduler;
class CTxMemPool;
class CBlockPolicyEstimator;
class CWalletTx;
class CWalletDB;
class CSPVScanner;

/** (client) version numbers for particular wallet features */
enum WalletFeature
{
    FEATURE_BASE = 10500, // the earliest version new wallets supports (only useful for getinfo's clientversion output)

    FEATURE_WALLETCRYPT = 40000, // wallet encryption
    FEATURE_COMPRPUBKEY = 60000, // compressed public keys

    FEATURE_HD = 130000, // Hierarchical key derivation after BIP32 (HD Wallet)
    FEATURE_LATEST = FEATURE_COMPRPUBKEY // HD is optional, use FEATURE_COMPRPUBKEY as latest version
};


/** A key pool entry */
class CKeyPool
{
public:
    int64_t nTime;
    CPubKey vchPubKey;
    std::string accountName;
    int64_t nChain;//internal or external keypool (HD wallets)


    CKeyPool();
    CKeyPool(const CPubKey& vchPubKeyIn, const std::string& accountNameIn, int64_t nChainIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(nTime);
        READWRITE(vchPubKey);

        //Allow to fail for legacy accounts.
        try
        {
            READWRITE(accountName);
            READWRITE(nChain);
        }
        catch(...)
        {
        }
    }
};

/** Address book data */
class CAddressBookData
{
public:
    std::string name;
    std::string description;
    std::string purpose;

    CAddressBookData() : purpose("unknown") {}

    typedef std::map<std::string, std::string> StringMap;
    StringMap destdata;
};

class CRecipient
{
public:
    CRecipient() : nType(CTxOutType::ScriptLegacyOutput) {}
    CRecipient(CScript scriptPubKey_, CAmount nAmount_, bool fSubtractFeeFromAmount_) : nType(CTxOutType::ScriptLegacyOutput), scriptPubKey(scriptPubKey_), nAmount(nAmount_), fSubtractFeeFromAmount(fSubtractFeeFromAmount_) {}
    CRecipient(CTxOutPoW2Witness witnessDetails_, CAmount nAmount_, bool fSubtractFeeFromAmount_) : nType(CTxOutType::PoW2WitnessOutput), witnessDetails(witnessDetails_), nAmount(nAmount_), fSubtractFeeFromAmount(fSubtractFeeFromAmount_) {}
    CRecipient(CTxOutStandardKeyHash standardKeyHash_, CAmount nAmount_, bool fSubtractFeeFromAmount_) : nType(CTxOutType::StandardKeyHashOutput), standardKeyHash(standardKeyHash_), nAmount(nAmount_), fSubtractFeeFromAmount(fSubtractFeeFromAmount_) {}

    CRecipient(const CRecipient& copy)
    {
        nType = copy.nType;
        scriptPubKey = copy.scriptPubKey;
        witnessForAccount = copy.witnessForAccount;
        witnessDetails = copy.witnessDetails;
        standardKeyHash = copy.standardKeyHash;
        nAmount = copy.nAmount;
        fSubtractFeeFromAmount = copy.fSubtractFeeFromAmount;
    }
    CTxOutType nType;
    CScript scriptPubKey;
    CAccount* witnessForAccount = nullptr;
    CTxOutPoW2Witness witnessDetails;
    CTxOutStandardKeyHash standardKeyHash;
    CAmount nAmount;
    bool fSubtractFeeFromAmount;
    CTxOut GetTxOut() const
    {
        if (nType <= ScriptLegacyOutput)
        {
            return CTxOut(nAmount, scriptPubKey);
        }
        else if (nType == PoW2WitnessOutput)
        {
            return CTxOut(nAmount, witnessDetails);
        }
        else if (nType == StandardKeyHashOutput)
        {
            return CTxOut(nAmount, standardKeyHash);
        }
        else
        {
            assert(0);
        }
        return CTxOut();
    }
};

CRecipient GetRecipientForDestination(const CTxDestination& dest, CAmount nValue, bool fSubtractFeeFromAmount, int nPoW2Phase);
CRecipient GetRecipientForTxOut(const CTxOut& out, CAmount nValue, bool fSubtractFeeFromAmount);



class CInputCoin {
public:
    CInputCoin(const CWalletTx* walletTx, unsigned int i, bool allowIndexBased)
    {
        if (!walletTx)
            throw std::invalid_argument("walletTx should not be null");
        if (i >= walletTx->tx->vout.size())
            throw std::out_of_range("The output index is out of range");
        
        isCoinBase = walletTx->tx->IsCoinBase();

        if (allowIndexBased && walletTx->GetDepthInMainChain() > COINBASE_MATURITY && walletTx->nHeight > 1 && walletTx->nIndex >= 0)
        {
            // Convert to an index based outpoint, whenever possible
            outpoint = COutPoint(walletTx->nHeight, walletTx->nIndex, i);
        }
        else
        {
            // Use a regular hash based outpoint
            outpoint = COutPoint(walletTx->GetHash(), i);
        }
        
        txout = walletTx->tx->vout[i];
    }

    CInputCoin(const COutPoint& outpoint_, const CTxOut& txout_, bool allowIndexBased, bool isCoinBase_, uint64_t nBlockHeight=0, uint64_t nTxIndex=0)
    : isCoinBase(isCoinBase_)
    {
        if (allowIndexBased && nBlockHeight < (uint64_t)chainActive.Tip()->nHeight && ((uint64_t)chainActive.Tip()->nHeight - nBlockHeight > (uint64_t)COINBASE_MATURITY))
        {
            // Convert to an index based outpoint, whenever possible
            outpoint = COutPoint(nBlockHeight, nTxIndex, outpoint_.n);
        }
        else
        {
            // Use a regular hash based outpoint
            outpoint = outpoint_;
        }
        txout = txout_;
    }

    COutPoint outpoint;
    CTxOut txout;
    bool isCoinBase;

    bool operator<(const CInputCoin& rhs) const {
        return outpoint < rhs.outpoint;
    }

    bool operator!=(const CInputCoin& rhs) const {
        return outpoint != rhs.outpoint;
    }

    bool operator==(const CInputCoin& rhs) const {
        return outpoint == rhs.outpoint;
    }
};

class COutput
{
public:
    const CWalletTx *tx;
    int i;
    int nDepth;

    /** Whether we have the private keys to spend this output */
    bool fSpendable;

    /** Whether we know how to spend this output, ignoring the lack of keys */
    bool fSolvable;

    /**
     * Whether this output is considered safe to spend. Unconfirmed transactions
     * from outside keys and unconfirmed replacement transactions are considered
     * unsafe and will not be used to fund new spending transactions.
     */
    bool fSafe;

    COutput(const CWalletTx *txIn, int iIn, int nDepthIn, bool fSpendableIn, bool fSolvableIn, bool fSafeIn)
    {
        tx = txIn; i = iIn; nDepth = nDepthIn; fSpendable = fSpendableIn; fSolvable = fSolvableIn; fSafe = fSafeIn;
    }

    std::string ToString() const;
};




/** Private key that includes an expiration date in case it never gets used. */
class CWalletKey
{
public:
    CPrivKey vchPrivKey;
    int64_t nTimeCreated;
    int64_t nTimeExpires;
    std::string strComment;
    //! todo: add something to note what created it (user, getnewaddress, change)
    //!   maybe should have a map<string, string> property map

    CWalletKey(int64_t nExpires=0);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITECOMPACTSIZEVECTOR(vchPrivKey);
        READWRITE(nTimeCreated);
        READWRITE(nTimeExpires);
        READWRITE(LIMITED_STRING(strComment, 65536));
    }
};

/**
 * Internal transfers.
 * Database key is acentry<account><counter>.
 */
class CAccountingEntry
{
public:
    std::string strAccount;
    CAmount nCreditDebit;
    int64_t nTime;
    std::string strOtherAccount;
    std::string strComment;
    mapValue_t mapValue;
    int64_t nOrderPos; //!< position in ordered transaction list
    uint64_t nEntryNo;

    CAccountingEntry()
    {
        SetNull();
    }

    void SetNull()
    {
        nCreditDebit = 0;
        nTime = 0;
        strAccount.clear();
        strOtherAccount.clear();
        strComment.clear();
        nOrderPos = -1;
        nEntryNo = 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        //! Note: strAccount is serialized as part of the key, not here.
        READWRITE(nCreditDebit);
        READWRITE(nTime);
        READWRITE(LIMITED_STRING(strOtherAccount, 65536));

        if (!ser_action.ForRead())
        {
            WriteOrderPos(nOrderPos, mapValue);

            if (!(mapValue.empty() && _ssExtra.empty()))
            {
                CDataStream ss(s.GetType(), s.GetVersion());
                ss.insert(ss.begin(), std::byte('\0'));
                ss << mapValue;
                ss.insert(ss.end(), _ssExtra.begin(), _ssExtra.end());
                strComment.append(ss.str());
            }
        }

        READWRITE(LIMITED_STRING(strComment, 65536));

        size_t nSepPos = strComment.find("\0", 0, 1);
        if (ser_action.ForRead())
        {
            mapValue.clear();
            if (std::string::npos != nSepPos)
            {
                CDataStream ss(AsBytes(Span(&strComment[0] + nSepPos + 1, strComment.length() -  (nSepPos + 1))), s.GetType(), s.GetVersion());
                ss >> mapValue;
                _ssExtra = CSerializeData(ss.begin(), ss.end());
            }
            ReadOrderPos(nOrderPos, mapValue);
        }
        if (std::string::npos != nSepPos)
            strComment.erase(nSepPos);

        mapValue.erase("n");
    }

private:
    CSerializeData _ssExtra;
};

struct WalletBalances
{
    CAmount availableIncludingLocked = -1;
    CAmount availableExcludingLocked = -1;
    CAmount availableLocked = -1;
    CAmount unconfirmedIncludingLocked = -1;
    CAmount unconfirmedExcludingLocked = -1;
    CAmount unconfirmedLocked = -1;
    CAmount immatureIncludingLocked = -1;
    CAmount immatureExcludingLocked = -1;
    CAmount immatureLocked = -1;
    CAmount totalLocked = -1;

    bool operator==(const WalletBalances& rhs) const
    {
        return availableIncludingLocked       == rhs.availableIncludingLocked
                && availableExcludingLocked   == rhs.availableExcludingLocked
                && availableLocked            == rhs.availableLocked
                && unconfirmedIncludingLocked == rhs.unconfirmedIncludingLocked
                && unconfirmedExcludingLocked == rhs.unconfirmedExcludingLocked
                && unconfirmedLocked          == rhs.unconfirmedLocked
                && immatureIncludingLocked    == rhs.immatureIncludingLocked
                && immatureExcludingLocked    == rhs.immatureExcludingLocked
                && immatureLocked             == rhs.immatureLocked
                && totalLocked                == rhs.totalLocked;
    }
    bool operator!=(const WalletBalances& rhs) const
    {
        return availableIncludingLocked       != rhs.availableIncludingLocked
                || availableExcludingLocked   != rhs.availableExcludingLocked
                || availableLocked            != rhs.availableLocked
                || unconfirmedIncludingLocked != rhs.unconfirmedIncludingLocked
                || unconfirmedExcludingLocked != rhs.unconfirmedExcludingLocked
                || unconfirmedLocked          != rhs.unconfirmedLocked
                || immatureIncludingLocked    != rhs.immatureIncludingLocked
                || immatureExcludingLocked    != rhs.immatureExcludingLocked
                || immatureLocked             != rhs.immatureLocked
                || totalLocked                != rhs.totalLocked;
    }
};

//fixme: (HIGH) Merge Cwallet and CExtWallet back into a single class
/*
All Novo specific functionality goes in base class CExtWallet
A little bit clumsy 
*/
class CWallet : public CExtWallet
{
private:
    static std::atomic<bool> fFlushScheduled;
    std::atomic<bool> fAbortRescan;
    std::atomic<bool> fScanningWallet;

    /**
     * Select a set of coins such that nValueRet >= nTargetValue and at least
     * all coins from coinControl are selected; Never select unconfirmed coins
     * if they are not ours
     */
    bool SelectCoins(bool allowIndexBased, const std::vector<COutput>& vAvailableCoins, const CAmount& nTargetValue, std::set<CInputCoin>& setCoinsRet, CAmount& nValueRet, const CCoinControl *coinControl = NULL) const;

    CWalletDB *pwalletdbEncryption;

    //! the current wallet version: clients below this version are not able to load the wallet
    int nWalletVersion;

    //! the maximum wallet format version: memory-only variable that specifies to what version this wallet may be upgraded
    int nWalletMaxVersion;

    std::unique_ptr<CSPVScanner> pSPVScanner;

    int64_t nNextResend;
    int64_t nLastResend;
    bool fBroadcastTransactions;

    /**
     * Used to keep track of spent outpoints, and
     * detect and report conflicts (double-spends or
     * mutated transactions where the mutant gets mined).
     */
    typedef std::multimap<COutPoint, uint256> TxSpends;
    TxSpends mapTxSpends;
    void AddToSpends(const COutPoint& outpoint, const uint256& wtxid);
    void AddToSpends(const uint256& wtxid);

    /* Mark a transaction (and its in-wallet descendants) as conflicting with a particular block. */
    void MarkConflicted(const uint256& hashBlock, const uint256& hashTx);

    void SyncMetaData(std::pair<TxSpends::iterator, TxSpends::iterator>);

    /* Used by TransactionAddedToMemorypool/BlockConnected/Disconnected.
     * Should be called with pindexBlock and posInBlock if this is for a transaction that is included in a block. */
    void SyncTransaction(const CTransactionRef& tx, const CBlockIndex *pindex = NULL, int posInBlock = 0);

    //int64_t nTimeFirstKey;

    /**
     * Private version of AddWatchOnly method which does not accept a
     * timestamp, and which will reset the wallet's nTimeFirstKey value to 1 if
     * the watch key did not previously have a timestamp associated with it.
     * Because this is an inherited virtual method, it is accessible despite
     * being marked private, but it is marked private anyway to encourage use
     * of the other AddWatchOnly which accepts a timestamp and sets
     * nTimeFirstKey more intelligently for more efficient rescans.
     */
#if 0
    bool AddWatchOnly(const CScript& dest) override;
#endif

    //std::unique_ptr<CWalletDBWrapper> dbw;

public:
    /*
     * Main wallet lock.
     * This lock protects all the fields added by CWallet.
     */
    //mutable CCriticalSection cs_wallet;//Moved to base

    /** Get database handle used by this wallet. Ideally this function would
     * not be necessary.
     */
    CWalletDBWrapper& GetDBHandle()
    {
        return *dbw;
    }

    /** Get a name for this wallet for logging/debugging purposes.
     */
    std::string GetName() const
    {
        if (dbw) {
            return dbw->GetName();
        } else {
            return "dummy";
        }
    }

    void LoadKeyPool(int nIndex, const CKeyPool &keypool)
    {
        // If no metadata exists yet, create a default with the pool key's
        // creation time. Note that this may be overwritten by actually
        // stored metadata for that key later, which is fine.
        CKeyID keyid = keypool.vchPubKey.GetID();
        if (mapKeyMetadata.count(keyid) == 0)
            mapKeyMetadata[keyid] = CKeyMetadata(keypool.nTime);
    }

    std::map<CKeyID, CKeyMetadata> mapKeyMetadata;

    typedef std::map<unsigned int, CMasterKey> MasterKeyMap;
    MasterKeyMap mapMasterKeys;
    unsigned int nMasterKeyMaxID;

    // Create wallet with dummy database handle
    CWallet();

    // Create wallet with passed-in database handle
    CWallet(std::unique_ptr<CWalletDBWrapper> dbw_in);

    ~CWallet();

    void SetNull()
    {
        nWalletVersion = FEATURE_BASE;
        nWalletMaxVersion = FEATURE_BASE;
        nMasterKeyMaxID = 0;
        pwalletdbEncryption = NULL;
        nOrderPosNext = 0;
        nAccountingEntryNumber = 0;
        nNextResend = 0;
        nLastResend = 0;
        nTimeFirstKey = 0;
        fBroadcastTransactions = false;
        nRelockTime = 0;
        fAbortRescan = false;
        fScanningWallet = false;
        activeAccount = NULL;
        activeSeed = NULL;
        nUnlockSessions = 0;
        nUnlockedSessionsOwnedByShadow = 0;
    }

    std::map<uint256, CWalletTx> mapWallet;
    std::map<uint256, uint256> mapWalletHash;
    void maintainHashMap(const CWalletTx& wtxIn, uint256& hash);
    /** Transaction hash from outpoint. Even if it is index based. */
    bool GetTxHash(const COutPoint& outpoint, uint256& txHash) const;
    std::list<CAccountingEntry> laccentries;

    int64_t nOrderPosNext;
    uint64_t nAccountingEntryNumber;
    std::map<uint256, int> mapRequestCount;

    std::map<std::string, CAddressBookData> mapAddressBook;

    std::set<COutPoint> setLockedCoins;

    const CWalletTx* GetWalletTx(const uint256& hash) const;
    CWalletTx* GetWalletTx(const COutPoint& outpoint) const;

    //! check whether we are allowed to upgrade (or already support) to the named feature
    bool CanSupportFeature(enum WalletFeature wf) { AssertLockHeld(cs_wallet); return nWalletMaxVersion >= wf; }

    /**
     * populate vCoins with vector of available COutputs.
     */
    void AvailableCoins(CAccount* forAccount, std::vector<COutput>& vCoins, bool fOnlySafe=true, const CCoinControl *coinControl = NULL, const CAmount& nMinimumAmount = 1, const CAmount& nMaximumAmount = MAX_MONEY, const CAmount& nMinimumSumAmount = MAX_MONEY, const uint64_t& nMaximumCount = 0, const int& nMinDepth = 0, const int& nMaxDepth = 9999999) const;
    void AvailableCoins(std::vector<CKeyStore*>& accountsToTry, std::vector<COutput>& vCoins, bool fOnlySafe=true, const CCoinControl *coinControl = NULL, const CAmount& nMinimumAmount = 1, const CAmount& nMaximumAmount = MAX_MONEY, const CAmount& nMinimumSumAmount = MAX_MONEY, const uint64_t& nMaximumCount = 0, const int& nMinDepth = 0, const int& nMaxDepth = 9999999) const;

    /**
     * Return list of available coins and locked coins grouped by non-change output address.
     */
    std::map<CTxDestination, std::vector<COutput>> ListCoins(CAccount* forAccount) const;

    /**
     * Find non-change parent output.
     */
    const CTxOut& FindNonChangeParentOutput(const CTransaction& tx, int output) const;

    /**
     * Shuffle and select coins until nTargetValue is reached while avoiding
     * small change; This method is stochastic for some inputs and upon
     * completion the coin set and corresponding actual target value is
     * assembled
     */
    bool SelectCoinsMinConf(const CAmount& nTargetValue, int nConfMine, int nConfTheirs, uint64_t nMaxAncestors, std::vector<COutput> vCoins, std::set<CInputCoin>& setCoinsRet, CAmount& nValueRet, bool allowIndexBased) const;

    bool IsSpent(const COutPoint& outpoint) const;

    bool IsLockedCoin(uint256 hash, unsigned int n) const;
    void LockCoin(const COutPoint& output);
    void UnlockCoin(const COutPoint& output);
    void UnlockAllCoins();
    void ListLockedCoins(std::vector<COutPoint>& vOutpts) const;
    
    // Checks for wallet vs. UTXO inconsistency; reports any spent state inconsistency found
    void CompareWalletAgainstUTXO(int& nMismatchFound, int& nWalletMissingUTXO, int& nWalletSpentCoinsStillInUTXO, int& nWalletCoinsNotInUTXO, int& nOrphansFound, int64_t& nBalanceInQuestion, bool attemptRepair=false);

    // Clear all orphan transactions from wallet
    bool RemoveAllOrphans(uint64_t& numErased, uint64_t& numDetected, std::string& strError);

    /*
     * Rescan abort properties
     */
    void AbortRescan() { fAbortRescan = true; }
    bool IsAbortingRescan() { return fAbortRescan; }
    bool IsScanning() { return fScanningWallet; }

    /**
     * keystore implementation
     * Generate a new key
     */
    CPubKey GenerateNewKey(CAccount& forAccount, int keyChain);
    //! Adds a key to the store, and saves it to disk.
    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey, CAccount& forAccount, int keyChain);
    //! Adds a key to the store, without saving it to disk (used by LoadWallet)
    bool LoadKey(const CKey& key, const CPubKey &pubkey, const std::string& forAccount, int64_t nKeyChain);
    //! Load metadata (used by LoadWallet)
    bool LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &metadata);

    bool LoadMinVersion(int nVersion) { AssertLockHeld(cs_wallet); nWalletVersion = nVersion; nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion); return true; }
    void UpdateTimeFirstKey(int64_t nCreateTime);

/*
    //! Adds an encrypted key to the store, and saves it to disk.
    bool AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret) override;
*/
    //! Adds an encrypted key to the store, without saving it to disk (used by LoadWallet)
    bool LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret, const std::string& forAccount, int64_t nKeyChain);
    bool AddCScript(const CScript& redeemScript);
    bool LoadCScript(const CScript& redeemScript);

    //! Adds a destination data tuple to the store, and saves it to disk
    bool AddDestData(const CTxDestination &dest, const std::string &key, const std::string &value);
    //! Erases a destination data tuple in the store and on disk
    bool EraseDestData(const CTxDestination &dest, const std::string &key);
    //! Adds a destination data tuple to the store, without saving it to disk
    bool LoadDestData(const CTxDestination &dest, const std::string &key, const std::string &value);
    //! Look up a destination data tuple in the store, return true if found false otherwise
    bool GetDestData(const CTxDestination &dest, const std::string &key, std::string *value) const;
    //! Get all destination values matching a prefix.
    std::vector<std::string> GetDestValues(const std::string& prefix) const;

    //! Adds a watch-only address to the store, and saves it to disk.
    bool AddWatchOnly(const CScript &dest, int64_t nCreateTime);
    bool RemoveWatchOnly(const CScript &dest);
    //! Adds a watch-only address to the store, without saving it to disk (used by LoadWallet)
    bool LoadWatchOnly(const CScript &dest);

    //! Holds a timestamp at which point the wallet is scheduled (externally) to be relocked. Caller must arrange for actual relocking to occur via Lock().
    int64_t nRelockTime;

    bool Unlock(const SecureString& strWalletPassphrase);
    bool UnlockWithTimeout(const SecureString& strWalletPassphrase, int64_t lockTimeoutSeconds);
    //fixme: (FUT) Handle this better with global scheduler (after we merge latest munt changes
    private:
    CScheduler* schedulerForLock=nullptr;
    public:
    bool ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase);
    bool EncryptWallet(const SecureString& strWalletPassphrase);

    /**
     * Begin an unlocked session. While at least one unlocked session is in progress the wallet cannot be locked.
     * If a lock attempt is done during an unlocked session the wallet will be kept unlocked untill all unlocked sessions are done.
     * Every BeginUnlocked() session must be closed by a matching EndUnlocked().
     *
     * If not already unlocked a passphrase request for unlocking is done using the reason passed.
     * When unlock is succesfull the callback will be executed.
     * If locked before the session begins the wallet will be locked again automatically when all sessions are finished.
     *
     * Note: while the previous Lock/Unlock still work their usage is discouraged. All new wallet unlocking should use this new API
     * and over time usage of the old Lock/Unlock API is to be removed.
     */
    void BeginUnlocked(std::string reason, std::function<void (void)> callback);

    /**
     * Synchronous version of BeginUnlocked.
     * @return Succesfully unlocked
     */
    bool BeginUnlocked(const SecureString& strWalletPassphrase);

    /**
     * End an unlocked session. When all unlocked session are ended automatically lock the wallet if it was locked when
     * the first active unlock session began or if a lock request was made during a session.
     */
    void EndUnlocked();

    void GetKeyBirthTimes(std::map<CKeyID, int64_t> &mapKeyBirth) const;
    unsigned int ComputeTimeSmart(const CWalletTx& wtx) const;

    //! Import a witness-only account from a URL
    void importWitnessOnlyAccountFromURL(const SecureString& sKey, std::string sAccountName);

    void importPrivKey(const SecureString& sKey, std::string sAccountName);
    void importPrivKey(const CKey& privKey, std::string sAccountName);
    bool importPrivKeyIntoAccount(CAccount* targetAccount, const CKey& privKey, const CKeyID& importKeyID, uint64_t keyBirthDate, bool allowRescan=true);
    bool forceKeyIntoKeypool(CAccount* forAccount, const CKey& privKey);

    /** 
     * Increment the next transaction order id
     * @return next transaction order id
     */
    int64_t IncOrderPosNext(CWalletDB *pwalletdb = NULL);
    DBErrors ReorderTransactions();
    bool AccountMove(std::string strFrom, std::string strTo, CAmount nAmount, std::string strComment = "");
    bool GetAccountPubkey(CPubKey &pubKey, std::string strAccount, bool bForceNew = false);

    void MarkDirty();
    bool AddToWallet(const CWalletTx& wtxIn, bool fFlushOnClose=true, bool fSelfComitted=false);
    //NB! After calling this function one or more times 'HandleTransactionsLoaded' must be called as well for final processing
    bool LoadToWallet(const CWalletTx& wtxIn);
    // This function allows us to move work out of LoadToWallet and into a place where mapWalletHash is already fully populated
    // As a result it performs massively faster on some wallets
    // NB! This must be called after 'LoadToWallet' is called one or more times
    void HandleTransactionsLoaded();
    
    void TransactionAddedToMempool(const CTransactionRef& tx) override;
    void TransactionRemovedFromMempool( const uint256 &hash, MemPoolRemovalReason reason) override;
    void BlockConnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex *pindex, const std::vector<CTransactionRef>& vtxConflicted) override;
    void BlockDisconnected(const std::shared_ptr<const CBlock>& pblock) override;
    void ClearCacheForTransaction(const uint256& hash);
    bool AddToWalletIfInvolvingMe(const CTransactionRef& tx, const CBlockIndex* pIndex, int posInBlock, bool fUpdate);
    int GetTransactionScanProgressPercent();
    int64_t RescanFromTime(int64_t startTime, bool update);
    CBlockIndex* ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate = false);
    void ReacceptWalletTransactions();
    std::vector<uint256> ResendWalletTransactionsBefore(int64_t nTime, CConnman* connman);
    void GetBalances(WalletBalances& balances, const CAccount* forAccount = nullptr, bool includeChildren=false) const;
    CAmount GetBalanceForDepth(int minDepth, const CAccount* forAccount = nullptr, bool includePoW2LockedWitnesses=false, bool includeChildren=false) const;
    CAmount GetBalance(const CAccount* forAccount = nullptr, bool useCache=true, bool includePoW2LockedWitnesses=false, bool includeChildren=false) const;
    CAmount GetLockedBalance(const CAccount* forAccount = nullptr, bool includeChildren=false);
    CAmount GetUnconfirmedBalance(const CAccount* forAccount = nullptr, bool includePoW2LockedWitnesses=false, bool includeChildren=false) const;
    CAmount GetImmatureBalance(const CAccount* forAccount = nullptr, bool includePoW2LockedWitnesses=false, bool includeChildren=false) const;
    CAmount GetWatchOnlyBalance(int minDepth=0, const CAccount* forAccount = nullptr, bool includeChildren=false) const;
    CAmount GetUnconfirmedWatchOnlyBalance() const;
    CAmount GetImmatureWatchOnlyBalance() const;
    #if 0
    CAmount GetLegacyBalance(const isminefilter& filter, int minDepth, const boost::uuids::uuid* accountUUID, bool includeChildren=false) const;
    #endif
    CAmount GetAvailableBalance(CAccount* forAccount, const CCoinControl* coinControl = nullptr) const;

    //! Fund a transaction that is otherwise already created
    // Where possible it is best to ensuire that all inputs of the transaction to be funded are constructed as index based (if they fit the criteria) instead of hash based
    // Otherwise this can result in some obscure issues, see comments in function body for more witnessDetails
    // The function automatically tries to take care of these issues but its better to avoid it having to do so entirely
    bool FundTransaction(CAccount* fundingAccount, CMutableTransaction& tx, CAmount& nFeeRet, int& nChangePosInOut, std::string& strFailReason, bool lockUnspents, const std::set<int>& setSubtractFeeFromOutputs, CCoinControl, CReserveKeyOrScript& reservekey);

    //! Sign a transaction that is already fully populated/funded
    //! The transaction inputs must be in the wallet as SignTransaction will look these up and fail if the are not
    //! For special cases of signing when not in wallet we provide prevOutOverride - however this only caters for one input to not be in the wallet...
    bool SignTransaction(CAccount* fromAccount, CMutableTransaction& tx, SignType type, CTxOut* prevOutOverride=nullptr);

    //! Create a transaction that renews an expired witness account
    bool PrepareRenewWitnessAccountTransaction(CAccount* funderAccount, CAccount* targetWitnessAccount, CReserveKeyOrScript& changeReserveKey, CMutableTransaction& tx, CAmount& nFeeOut, std::string& strError, uint64_t* skipPastTransaction=nullptr, CCoinControl* coinControl=nullptr);

    //! Create a transaction upgrades an old ScriptLegacyOutput witness to a new PoW2WitnessOutput
    void PrepareUpgradeWitnessAccountTransaction(CAccount* funderAccount, CAccount* targetWitnessAccount, CReserveKeyOrScript& changeReserveKey, CMutableTransaction& tx, CAmount& nFeeOut);

    /**
     * Insert additional inputs into the transaction by
     * calling CreateTransaction();
     */
    void AddTxInput(CMutableTransaction& tx, const CInputCoin& inputCoin, bool rbf);
    void AddTxInputs(CMutableTransaction& tx, std::set<CInputCoin>& setCoins, bool rbf);

    /**
     * Create a new transaction paying the recipients with a set of coins
     * selected by SelectCoins(); Also create the change output, when needed
     * @note passing nChangePosInOut as -1 will result in setting a random position
     */
    bool CreateTransaction(std::vector<CKeyStore*>& accountsToTry, const std::vector<CRecipient>& vecSend, CWalletTx& wtxNew, CReserveKeyOrScript& reservekey, CAmount& nFeeRet, int& nChangePosInOut,
                           std::string& strFailReason, const CCoinControl *coinControl = NULL, bool sign = true);
    bool CreateTransaction(CAccount* forAccount, const std::vector<CRecipient>& vecSend, CWalletTx& wtxNew, CReserveKeyOrScript& reservekey, CAmount& nFeeRet, int& nChangePosInOut,
                           std::string& strFailReason, const CCoinControl *coinControl = NULL, bool sign = true);

    //! Used currently by the witnessing code to add the fee for a witness renewal transaction, and various other special operation transactions
    bool AddFeeForTransaction(CAccount* forAccount, CMutableTransaction& txNew, CReserveKeyOrScript& reservekey, CAmount& nFeeOut, bool sign, std::string& strFailReason, const CCoinControl* coinControl);

    /**
     * Sign and submit a transaction (that has not yet been signed) to the network, add to wallet as appropriate etc.
     */
    bool SignAndSubmitTransaction(CReserveKeyOrScript& changeReserveKey, CMutableTransaction& tx, std::string& strError, uint256* pTransactionHashOut=nullptr, SignType type=SignType::Spend);

    bool CommitTransaction(CWalletTx& wtxNew, CReserveKeyOrScript& reservekey, CConnman* connman, CValidationState& state);

    void ListAccountCreditDebit(const std::string& strAccount, std::list<CAccountingEntry>& entries);
    bool AddAccountingEntry(const CAccountingEntry&);
    bool AddAccountingEntry(const CAccountingEntry&, CWalletDB *pwalletdb);
    template <typename ContainerType>
    bool DummySignTx(std::vector<CKeyStore*>& accountsToTry, CMutableTransaction &txNew, const ContainerType &coins, SignType type) const;

    static CFeeRate minTxFee;
    static CFeeRate fallbackFee;
    /**
     * Estimate the minimum fee considering user set parameters
     * and the required fee
     */
    static CAmount GetMinimumFee(unsigned int nTxBytes, unsigned int nConfirmTarget, const CTxMemPool& pool, const CBlockPolicyEstimator& estimator, bool ignoreGlobalPayTxFee = false);
    /**
     * Return the minimum required fee taking into account the
     * floating relay fee and user set minimum transaction fee
     */
    static CAmount GetRequiredFee(unsigned int nTxBytes);

    bool NewKeyPool();
    /*
    size_t KeypoolCountExternalKeys();
    */
    int TopUpKeyPool(unsigned int nTargetKeypoolSize = 0, unsigned int nMaxNewAllocations = 0, CAccount* forAccount = nullptr, unsigned int nMinimalKeypoolOverride = 1);
    void ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool, CAccount* forAccount, int64_t keyChain);
    void KeepKey(int64_t nIndex);
    void ReturnKey(int64_t nIndex, CAccount* forAccount, int64_t keyChain);
    bool GetKeyFromPool(CPubKey &key, CAccount* forAccount, int64_t keyChain);
    int64_t GetOldestKeyPoolTime();
    void GetAllReserveKeys(std::set<CKeyID>& setAddress) const;

    std::set< std::set<CTxDestination> > GetAddressGroupings();
    std::map<CTxDestination, CAmount> GetAddressBalances();

    std::set<CTxDestination> GetAccountAddresses(const std::string& strAccount) const;

    isminetype IsMine(const CTxIn& txin) const;
    isminetype IsMine(const CTxOut& txout) const;
    isminetype IsMineWitness(const CTxOut& txout) const;
    /**
     * Returns amount of debit if the input matches the
     * filter, otherwise returns 0
     */
    CAmount GetDebit(const CTxIn& txin, const isminefilter& filter, CAccount* forAccount=NULL, bool includeChildren=false) const;
    CAmount GetCredit(const CTxOut& txout, const isminefilter& filter, CAccount* forAccount=NULL, bool includeChildren=false) const;
    bool IsChange(const CTxOut& txout) const;
    CAmount GetChange(const CTxOut& txout) const;
    bool IsMine(const CTransaction& tx) const;
    /** should probably be renamed to IsRelevantToMe */
    bool IsFromMe(const CTransaction& tx) const;
    CAmount GetDebit(const CTransaction& tx, const isminefilter& filter, CAccount* forAccount=NULL) const;
    /** Returns whether all of the inputs match the filter */
    bool IsAllFromMe(const CTransaction& tx, const isminefilter& filter) const;
    CAmount GetCredit(const CTransaction& tx, const isminefilter& filter, CAccount* forAccount=NULL) const;
    CAmount GetChange(const CTransaction& tx) const;

    DBErrors LoadWallet(WalletLoadState& nExtraLoadState);
    DBErrors ZapWalletTx(std::vector<CWalletTx>& vWtx);
    DBErrors ZapSelectTx(CWalletDB& walletdb, std::vector<uint256>& vHashIn, std::vector<uint256>& vHashOut);

    bool SetAddressBook(const std::string& address, const std::string& strName, const std::string& strRecipientDescription, const std::string& purpose);

    bool DelAddressBook(const std::string& address);

    std::vector<CAccount*> FindAccountsForTransaction(const CTxOut& out);
    CAccount* FindBestWitnessAccountForTransaction(const CTxOut& out);

    // CValidationInterface updates
    void ResendWalletTransactions(int64_t nBestBlockTime, CConnman* connman) override;
    void SetBestChain(const CBlockLocator& loc) override;

    void ScriptForMining(std::shared_ptr<CReserveKeyOrScript> &script, CAccount* forAccount) override;
    void ScriptForWitnessing(std::shared_ptr<CReserveKeyOrScript> &script, CAccount* forAccount) override;

    unsigned int GetKeyPoolSize()
    {
        AssertLockHeld(cs_wallet); // setKeyPool
        int nPoolSize=0;
        for (const auto& [accountUUID, forAccount] : mapAccounts)
        {
            (unused) accountUUID;
            LOCK(forAccount->cs_keypool);
            nPoolSize += forAccount->GetKeyPoolSize();
        }
        return nPoolSize;
    }

    //! signify that a particular wallet feature is now used. this may change nWalletVersion and nWalletMaxVersion if those are lower
    bool SetMinVersion(enum WalletFeature, CWalletDB* pwalletdbIn = NULL, bool fExplicit = false);

    //! change which version we're allowed to upgrade to (note that this does not immediately imply upgrading to that format)
    bool SetMaxVersion(int nVersion);

    //! get the current wallet format (the oldest client version guaranteed to understand this wallet)
    int GetVersion() { LOCK(cs_wallet); return nWalletVersion; }

    //! Get wallet transactions that conflict with given transaction (spend same outputs)
    std::set<uint256> GetConflicts(const uint256& txid) const;

    //! Check if a given transaction has any of its outputs spent by another transaction in the wallet
    bool HasWalletSpend(const uint256& txid) const;

    //! Flush wallet (bitdb flush)
    void Flush(bool shutdown=false);

    //! Verify the wallet database and perform salvage if required
    static bool Verify();

    /** 
     * Address book entry changed.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const std::string
            &address, const std::string &label, bool isMine,
            const std::string &purpose,
            ChangeType status)> NotifyAddressBookChanged;

    /** 
     * Wallet transaction added, removed or updated.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const uint256 &hashTx, ChangeType status, bool fSelfComitted)> NotifyTransactionChanged;

    /** 
     * Wallet transaction depth in chain changed (only called up until depth 10)
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const uint256 &hashTx)> NotifyTransactionDepthChanged;

    /** Show progress e.g. for rescan */
    boost::signals2::signal<void (const std::string &title, int nProgress)> ShowProgress;

    /** Watch-only address added */
    boost::signals2::signal<void (bool fHaveWatchOnly)> NotifyWatchonlyChanged;

    /** A key pool was topped up with one or more keys. */
    boost::signals2::signal<void ()> NotifyKeyPoolToppedUp;

    /** Inquire whether this wallet broadcasts transactions. */
    bool GetBroadcastTransactions() const { return fBroadcastTransactions; }
    /** Set whether this wallet broadcasts transactions. */
    void SetBroadcastTransactions(bool broadcast) { fBroadcastTransactions = broadcast; }

    /** Return whether transaction can be abandoned */
    bool TransactionCanBeAbandoned(const uint256& hashTx) const;

    /* Mark a transaction (and it in-wallet descendants) as abandoned so its inputs may be respent. */
    bool AbandonTransaction(const uint256& hashTx);

    /** Mark a transaction as replaced by another transaction (e.g., BIP 125). */
    bool MarkReplaced(const uint256& originalHash, const uint256& newHash);

    /* Returns the wallets help message */
    static std::string GetWalletHelpString(bool showDebug);

    /* Initializes the wallet, returns a new CWallet instance or a null pointer in case of an error */
    static CWallet* CreateWalletFromFile(const std::string walletFile);
    static bool InitLoadWallet();
    static void CreateSeedAndAccountFromPhrase(CWallet* walletInstance);
    static void CreateSeedAndAccountFromLink(CWallet *walletInstance);

    /**
     * Wallet post-init setup
     * Gives the wallet a chance to register repetitive tasks and complete post-init tasks
     */
    void postInitProcess(CScheduler& scheduler);

    /* Wallets parameter interaction */
    static bool ParameterInteraction();

    bool BackupWallet(const std::string& strDest);

    const CBlockIndex* LastSPVBlockProcessed() const;

    static void ResetUnifiedSPVProgressNotification();

    /**
     * Birthtime computed from wallet transactions
     * If there are no transactions the tip of the known chain is used
     * If birthtime cannot be succesfully computed it will return 0
     */
    int64_t birthTime() const;

    void StartSPV();
    void ResetSPV();

    void EraseWalletSeedsAndAccounts();

protected:
    void PruningConflictingBlock(const uint256& orphanBlockHash) override;

private:
    int nTransactionScanProgressPercent;

    friend class CAccount;
};


// Helper for producing a bunch of max-sized low-S signatures (eg 72 bytes)
// ContainerType is meant to hold pair<CWalletTx *, int>, and be iterable
// so that each entry corresponds to each vIn, in order.
template <typename ContainerType>
bool CWallet::DummySignTx(std::vector<CKeyStore*>& accountsToTry, CMutableTransaction &txNew, const ContainerType &coins, SignType type) const
{
    // Fill in dummy signatures for fee calculation.
    int nIn = 0;
    for (const auto& coin : coins)
    {
        SignatureData sigdata;

        if (!ProduceSignature(DummySignatureCreator(accountsToTry), coin.txout, sigdata, type, txNew.nVersion))
        {
            return false;
        }
        else
        {
            UpdateTransaction(txNew, nIn, sigdata);
        }

        nIn++;
    }
    return true;
}
#endif
