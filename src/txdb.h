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

#ifndef TXDB_H
#define TXDB_H

#include "coins.h"
#include "dbwrapper.h"
#include "chain.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

class CBlockIndex;
class CCoinsViewDBCursor;
class uint256;
class CWitViewDB;

//! Compensate for extra memory peak (x1.5-x1.9) at flush time.
static constexpr int DB_PEAK_USAGE_FACTOR = 2;
//! No need to periodic flush if at least this much space still available.
static constexpr int MAX_BLOCK_COINSDB_USAGE = 10 * DB_PEAK_USAGE_FACTOR;
//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 450;
//! max. -dbcache (MiB)
static const int64_t nMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;
//! min. -dbcache (MiB)
static const int64_t nMinDbCache = 4;
//! Max memory allocated to block tree DB specific cache, if no -txindex (MiB)
static const int64_t nMaxBlockDBCache = 2;
//! Max memory allocated to block tree DB specific cache, if -txindex (MiB)
// Unlike for the UTXO database, for the txindex scenario the leveldb cache make
// a meaningful difference: https://github.com/bitcoin/bitcoin/pull/8273#issuecomment-229601991
static const int64_t nMaxBlockDBAndTxIndexCache = 1024;
//! Max memory allocated to coin DB specific cache (MiB)
static const int64_t nMaxCoinsDBCache = 8;

struct CDiskTxPos : public CDiskBlockPos
{
    unsigned int nTxOffset; // after header

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(*(CDiskBlockPos*)this);
        READWRITE(VARINT(nTxOffset));
    }

    CDiskTxPos(const CDiskBlockPos &blockIn, unsigned int nTxOffsetIn) : CDiskBlockPos(blockIn.nFile, blockIn.nPos), nTxOffset(nTxOffsetIn) {
    }

    CDiskTxPos() {
        SetNull();
    }

    void SetNull() {
        CDiskBlockPos::SetNull();
        nTxOffset = 0;
    }

    #ifdef WITNESS_HEADER_SYNC    
    friend bool operator< (const CDiskTxPos a, const CDiskTxPos b)
    {
        if ((CDiskBlockPos)a < (CDiskBlockPos)b)
            return true;
        if ((CDiskBlockPos)b < (CDiskBlockPos)a)
            return false;
        if (a.nTxOffset < b.nTxOffset)
            return true;
        return false;
    }
    #endif
};

/** CCoinsView backed by the coin database (chainstate/) */
class CCoinsViewDB : public CCoinsView
{
protected:
    CDBWrapper db;
public:
    CCoinsViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false, std::string name="chainstate");

    bool GetCoin(const COutPoint &outpoint, Coin &coin, COutPoint* pOutpointRet=nullptr) const override;
    bool HaveCoin(const COutPoint &outpoint) const override;
    uint256 GetBestBlock() const override;
    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) override;
    CCoinsViewCursor *Cursor() const override;

    //fixme: (PHASE5) We can remove some of these once phase4 is active.
    //We will first need to figure out how to handle testnet however.
    void SetPhase2ActivationHash(const uint256 &hashPhase2ActivationPoint);
    uint256 GetPhase2ActivationHash();
    void SetPhase3ActivationHash(const uint256 &hashPhase3ActivationPoint);
    uint256 GetPhase3ActivationHash();
    void SetPhase4ActivationHash(const uint256 &hashPhase4ActivationPoint);
    uint256 GetPhase4ActivationHash();
    void SetPhase5ActivationHash(const uint256 &hashPhase5ActivationPoint);
    uint256 GetPhase5ActivationHash();

    //! Attempt to update from an older database format. Returns whether an error occurred.
    bool Upgrade();
    
    //! In extreme cases an update might require a complete re-index; if this is the case then this function will return true.
    bool RequiresReindex();
    
    //! Write the version number after creating a new database; no-op if its an existing database with an existing version.
    bool WriteVersion();
    
    
    size_t EstimateSize() const override;
    // For handling of upgrades.
    #ifdef WITNESS_HEADER_SYNC
    uint32_t nCurrentVersion=4;
    #else
    uint32_t nCurrentVersion=3;
    #endif
    uint32_t nPreviousVersion=1;

    void GetAllCoins(std::map<COutPoint, Coin>& allCoins) const override;
    #ifdef WITNESS_HEADER_SYNC
    void GetAllCoinsIndexBased(std::map<COutPoint, Coin>& allCoins) const override;
    void GetAllCoinsIndexBasedDirect(std::map<COutPoint, Coin>& allCoins) const override;
    #endif
};

/** CWitViewDB backed by the witness database (witstate/) */
class CWitViewDB : public CCoinsViewDB
{
public:
    CWitViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
};

/** Specialization of CCoinsViewCursor to iterate over a CCoinsViewDB */
class CCoinsViewDBCursor: public CCoinsViewCursor
{
public:
    ~CCoinsViewDBCursor() {}

    bool GetKey(COutPoint &key) const;
    bool GetValue(Coin &coin) const;
    unsigned int GetValueSize() const;

    bool Valid() const;
    void Next();

private:
    CCoinsViewDBCursor(CDBIterator* pcursorIn, const uint256 &hashBlockIn):
        CCoinsViewCursor(hashBlockIn), pcursor(pcursorIn) {}
    std::unique_ptr<CDBIterator> pcursor;
    std::pair<char, COutPoint> keyTmp;

    friend class CCoinsViewDB;
};

/** Access to the block database (blocks/index/) */
class CBlockTreeDB : public CDBWrapper
{
public:
    CBlockTreeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
private:
    CBlockTreeDB(const CBlockTreeDB&);
    void operator=(const CBlockTreeDB&);
public:
    bool UpdateBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile,
                         const std::vector<const CBlockIndex*>& vWriteIndices,
                         const std::vector<uint256>& vEraseHashes);
    bool EraseBatchSync(const std::vector<uint256>& vEraseHashes);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo);
    bool ReadLastBlockFile(int &nFile);
    bool WriteReindexing(bool fReindex);
    bool ReadReindexing(bool &fReindex);
    bool ReadTxIndex(const uint256 &txid, CDiskTxPos &pos);
    //NB! This is only for RPC code and similar (display purposes) and only works when txindex is enabled. DO NOT call this in any validation or similar code
    uint256 ReadTxIndexRef(uint64_t nBlockHeight, uint64_t nPos);
    bool WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> > &list, uint64_t nHeight);
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);
    bool LoadBlockIndexGuts(std::function<CBlockIndex*(const uint256&)> insertBlockIndex);
};

#endif
