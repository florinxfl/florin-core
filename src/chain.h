// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2016-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the Libre Chain License, see the accompanying
// file COPYING

#ifndef CHAIN_H
#define CHAIN_H

#include "arith_uint256.h"
#include "primitives/block.h"
#include "pow/pow.h"
#include "tinyformat.h"
#include "uint256.h"

#include <vector>
#include <valarray>

#include "chainparams.h"
/**
 * Maximum amount of time that a block timestamp is allowed to exceed the
 * current network-adjusted time before the block will be accepted.
 */
static const int64_t MAX_FUTURE_BLOCK_TIME = 60;

/**
 * Timestamp window used as a grace period by code that compares external
 * timestamps (such as timestamps passed to RPCs, or wallet key creation times)
 * to block timestamps. This should be set at least as high as
 * MAX_FUTURE_BLOCK_TIME.
 */
static const int64_t TIMESTAMP_WINDOW = MAX_FUTURE_BLOCK_TIME;

class CBlockFileInfo
{
public:
    unsigned int nBlocks;      //!< number of blocks stored in file
    unsigned int nSize;        //!< number of used bytes of block file
    unsigned int nUndoSize;    //!< number of used bytes in the undo file
    unsigned int nHeightFirst; //!< lowest height of block in file
    unsigned int nHeightLast;  //!< highest height of block in file
    uint64_t nTimeFirst;       //!< earliest time of block in file
    uint64_t nTimeLast;        //!< latest time of block in file

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(nBlocks));
        READWRITE(VARINT(nSize));
        READWRITE(VARINT(nUndoSize));
        READWRITE(VARINT(nHeightFirst));
        READWRITE(VARINT(nHeightLast));
        READWRITE(VARINT(nTimeFirst));
        READWRITE(VARINT(nTimeLast));
    }

     void SetNull() {
         nBlocks = 0;
         nSize = 0;
         nUndoSize = 0;
         nHeightFirst = 0;
         nHeightLast = 0;
         nTimeFirst = 0;
         nTimeLast = 0;
     }

     CBlockFileInfo() {
         SetNull();
     }

     std::string ToString() const;

     /** update statistics (does not update nSize) */
     void AddBlock(unsigned int nHeightIn, uint64_t nTimeIn) {
         if (nBlocks==0 || nHeightFirst > nHeightIn)
             nHeightFirst = nHeightIn;
         if (nBlocks==0 || nTimeFirst > nTimeIn)
             nTimeFirst = nTimeIn;
         nBlocks++;
         if (nHeightIn > nHeightLast)
             nHeightLast = nHeightIn;
         if (nTimeIn > nTimeLast)
             nTimeLast = nTimeIn;
     }
};

struct CDiskBlockPos
{
    int nFile;
    unsigned int nPos;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(nFile));
        READWRITE(VARINT(nPos));
    }

    CDiskBlockPos() {
        SetNull();
    }

    CDiskBlockPos(int nFileIn, unsigned int nPosIn) {
        nFile = nFileIn;
        nPos = nPosIn;
    }

    friend bool operator==(const CDiskBlockPos &a, const CDiskBlockPos &b) {
        return (a.nFile == b.nFile && a.nPos == b.nPos);
    }

    friend bool operator!=(const CDiskBlockPos &a, const CDiskBlockPos &b) {
        return !(a == b);
    }
    
    #ifdef WITNESS_HEADER_SYNC
    friend bool operator< (const CDiskBlockPos a, const CDiskBlockPos b)
    {
        if (a.nFile > b.nFile)
            return false;
        else if (a.nFile < b.nFile)
            return true;
        if (a.nPos > b.nPos)
            return false;
        else if (a.nPos < b.nPos)
            return true;
        return false;
    }
    #endif

    void SetNull() { nFile = -1; nPos = 0; }
    bool IsNull() const { return (nFile == -1); }

    std::string ToString() const
    {
        return strprintf("CBlockDiskPos(nFile=%i, nPos=%i)", nFile, nPos);
    }

};

enum BlockStatus: uint32_t {
    //! Unused.
    BLOCK_VALID_UNKNOWN      =    0,

    //! Parsed, version ok, hash satisfies claimed PoW, 1 <= vtx count <= max, timestamp not in future
    BLOCK_VALID_HEADER       =    1,

    //! All parent headers found, difficulty matches, timestamp >= median previous, checkpoint. Implies all parents
    //! are also at least TREE.
    BLOCK_VALID_TREE         =    2,

    /**
     * Only first tx is coinbase, 2 <= coinbase input script length <= 100, transactions valid, no duplicate txids,
     * sigops, size, merkle root. Implies all parents are at least TREE but not necessarily TRANSACTIONS. When all
     * parent blocks also have TRANSACTIONS, CBlockIndex::nChainTx will be set.
     */
    BLOCK_VALID_TRANSACTIONS =    3,

    //! Outputs do not overspend inputs, no double spends, coinbase output ok, no immature coinbase spends, BIP30.
    //! Implies all parents are also at least CHAIN.
    BLOCK_VALID_CHAIN        =    4,

    //! Scripts & signatures ok. Implies all parents are also at least SCRIPTS.
    BLOCK_VALID_SCRIPTS      =    5,

    //! All validity bits.
    BLOCK_VALID_MASK         =   BLOCK_VALID_HEADER | BLOCK_VALID_TREE | BLOCK_VALID_TRANSACTIONS |
                                 BLOCK_VALID_CHAIN | BLOCK_VALID_SCRIPTS,

    BLOCK_HAVE_DATA          =    8, //!< full block available in blk*.dat
    BLOCK_HAVE_UNDO          =   16, //!< undo data available in rev*.dat
    BLOCK_HAVE_MASK          =   BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO,

    BLOCK_FAILED_VALID       =   32, //!< stage after last reached validness failed
    BLOCK_FAILED_CHILD       =   64, //!< descends from failed block
    BLOCK_FAILED_MASK        =   BLOCK_FAILED_VALID | BLOCK_FAILED_CHILD,

    BLOCK_OPT_WITNESS       =   128, //!< block data in blk*.data was received with a witness-enforcing client

    BLOCK_PARTIAL_TREE         =  256, //! block is in partial tree and all parents are also at least BLOCK_PARTIAL_TREE
    BLOCK_PARTIAL_TRANSACTIONS =  512, //! Partial tree analog of BLOCK_VALID_TRANSACTION, except CBlockIndex::nChainTx is NOT set.
    BLOCK_PARTIAL_RESERVED1    = 1024, //! Claim bits for future partial validation levels
    BLOCK_PARTIAL_RESERVED2    = 2048,

    // All validation bits releated to partial tree validation
    BLOCK_PARTIAL_MASK = BLOCK_PARTIAL_TREE | BLOCK_PARTIAL_TRANSACTIONS | BLOCK_PARTIAL_RESERVED1 | BLOCK_PARTIAL_RESERVED2,
};

/** The block chain is a tree shaped structure starting with the
 * genesis block at the root, with each block potentially having multiple
 * candidates to be the next block. A blockindex may have multiple pprev pointing
 * to it, but at most one of them can be part of the currently active branch.
 */
class CBlockIndex
{
public:
    //! pointer to the hash of the block, if any. IMPORTANT: Memory is owned by the mapBlockIndex!
    //! So the hash pointer can only be valid if this CBlockIndex is in mapBlockIndex!
    const uint256* phashBlock;

    //! pointer to the index of the predecessor of this block
    CBlockIndex* pprev;

    //! pointer to the index of some further predecessor of this block
    CBlockIndex* pskip;

    //! height of the entry in the chain. The genesis block has height 0
    int nHeight;

    //! Which # file this block is stored in (blk?????.dat)
    int nFile;

    //! Byte offset within blk?????.dat where this block's data is stored
    unsigned int nDataPos;

    //! Byte offset within rev?????.dat where this block's undo data is stored
    unsigned int nUndoPos;

    //! (memory only) Total amount of work (expected number of hashes) in the chain up to and including this block
    arith_uint256 nChainWork;

    //! Number of transactions in this block.
    //! Note: in a potential headers-first mode, this number cannot be relied upon
    unsigned int nTx;

    //! (memory only) Number of transactions in the chain up to and including this block.
    //! This value will be non-zero only if and only if transactions for this block and all its parents are available.
    //! Change to 64-bit type when necessary; won't happen before 2030
    unsigned int nChainTx;

    //! Verification status of this block. See enum BlockStatus
    unsigned int nStatus;

    //! PoW2 witness block header
    int32_t nVersionPoW2Witness;
    uint32_t nTimePoW2Witness;
    uint256 hashMerkleRootPoW2Witness;
    std::vector<unsigned char> witnessHeaderPoW2Sig; // 65 bytes

    #ifdef WITNESS_HEADER_SYNC
    // Changes in the witness UTXO that this block causes
    std::vector<unsigned char> witnessUTXODelta;
    #endif

    //! block header
    int32_t nVersion;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    //fixme: (SIGMA) - Ensure this works on both big and little endian - if not we might have to drop the struct and just use bit manipulation instead.
    union
    {
        struct
        {
            uint16_t nPreNonce;
            uint16_t nPostNonce;
        };
        uint32_t nNonce;
    };

    //! (memory only) Sequential id assigned to distinguish order in which blocks are received.
    int32_t nSequenceId;

    //! (memory only) Maximum nTime in the chain upto and including this block.
    unsigned int nTimeMax;

    void SetNull()
    {
        phashBlock = NULL;
        pprev = NULL;
        pskip = NULL;
        nHeight = 0;
        nFile = 0;
        nDataPos = 0;
        nUndoPos = 0;
        nChainWork = arith_uint256();
        nTx = 0;
        nChainTx = 0;
        nStatus = 0;
        nSequenceId = 0;
        nTimeMax = 0;

        nVersionPoW2Witness = 0;
        nTimePoW2Witness = 0;
        hashMerkleRootPoW2Witness = uint256();
        witnessHeaderPoW2Sig.clear();
        #ifdef WITNESS_HEADER_SYNC
        witnessUTXODelta.clear();
        #endif

        nVersion       = 0;
        hashMerkleRoot = uint256();
        nTime          = 0;
        nBits          = 0;
        nNonce         = 0;
    }

    CBlockIndex()
    {
        SetNull();
    }

    CBlockIndex(const CBlockHeader& block)
    {
        SetNull();

        nVersionPoW2Witness = block.nVersionPoW2Witness;
        nTimePoW2Witness = block.nTimePoW2Witness;
        hashMerkleRootPoW2Witness = block.hashMerkleRootPoW2Witness;
        witnessHeaderPoW2Sig = block.witnessHeaderPoW2Sig;
        #ifdef WITNESS_HEADER_SYNC
        witnessUTXODelta = block.witnessUTXODelta;
        #endif
        nVersion       = block.nVersion;
        hashMerkleRoot = block.hashMerkleRoot;
        nTime          = block.nTime;
        nBits          = block.nBits;
        nNonce         = block.nNonce;
    }

    CDiskBlockPos GetBlockPos() const {
        CDiskBlockPos ret;
        if (nStatus & BLOCK_HAVE_DATA) {
            ret.nFile = nFile;
            ret.nPos  = nDataPos;
        }
        return ret;
    }

    CDiskBlockPos GetUndoPos() const {
        CDiskBlockPos ret;
        if (nStatus & BLOCK_HAVE_UNDO) {
            ret.nFile = nFile;
            ret.nPos  = nUndoPos;
        }
        return ret;
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersionPoW2Witness = nVersionPoW2Witness;
        block.nTimePoW2Witness = nTimePoW2Witness;
        block.hashMerkleRootPoW2Witness = hashMerkleRootPoW2Witness;
        block.witnessHeaderPoW2Sig = witnessHeaderPoW2Sig;
        #ifdef WITNESS_HEADER_SYNC
        block.witnessUTXODelta = witnessUTXODelta;
        #endif
        block.nVersion       = nVersion;
        if (pprev)
            block.hashPrevBlock = pprev->GetBlockHashPoW2();
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        return block;
    }

    //Munt - phashBlock contains 'legacy' hash for PoW blocks and PoW2 hash for witness blocks.
    //fixme: (PHASE5) (HIGH) - We can get rid of all this legacy/pow2 hash nonsense for 2.1 and just use the same hash everywhere...
    uint256 GetBlockHashLegacy() const
    {
        if (nVersionPoW2Witness == 0)
            return *phashBlock;
        else
            return GetBlockHeader().GetHashLegacy();
    }

    uint256 GetBlockHashPoW2() const
    {
        return *phashBlock;
    }

    //fixme: (PHASE5) All time related things should be unsigned.
    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }

    int64_t GetBlockTimePoW2Witness() const
    {
        return nTimePoW2Witness == 0 ? (int64_t)nTime : (int64_t)nTimePoW2Witness;
    }

    int64_t GetBlockTimeMax() const
    {
        return (int64_t)nTimeMax;
    }

    //enum { nMedianTimeSpan=11 };

    int64_t GetMedianTimePast() const
    {
        int nMedianTimeSpan = 11;
        if (nHeight >  437500 || Params().IsTestnet())
            nMedianTimeSpan = 3;

        //fixme: (PHASE5) - This needs unit tests
        if (this->nTimePoW2Witness != 0 && nHeight > 20)
        {
            nMedianTimeSpan *= 2;
            int nMid = nMedianTimeSpan/2;

            std::valarray<int64_t> pmedian(nMedianTimeSpan);
            int64_t* pbegin = &pmedian[0]+nMedianTimeSpan;
            int64_t* pend = &pmedian[0]+nMedianTimeSpan;

            const CBlockIndex* pindex = this;
            for (int i = 0; i < nMedianTimeSpan/2 && pindex; i++, pindex = pindex->pprev)
            {
                *(--pbegin) = pindex->nTimePoW2Witness == 0 ? pindex->GetBlockTime() : pindex->nTimePoW2Witness;
                *(--pbegin) = pindex->GetBlockTime();
            }

            std::sort(pbegin, pend);
            return ( pbegin[nMid-1] + pbegin[nMid] ) / 2;
        }
        else
        {
            std::valarray<int64_t> pmedian(nMedianTimeSpan);
            int64_t* pbegin = &pmedian[0]+nMedianTimeSpan;
            int64_t* pend = &pmedian[0]+nMedianTimeSpan;

            const CBlockIndex* pindex = this;
            for (int i = 0; i < nMedianTimeSpan && pindex; i++, pindex = pindex->pprev)
                *(--pbegin) = pindex->GetBlockTime();

            std::sort(pbegin, pend);
            return pbegin[(pend - pbegin)/2];
        }
    }

    int64_t GetMedianTimePastPoW() const
    {
        int nMedianTimeSpan = 11;
        if (nHeight >  437500 || Params().IsTestnet())
            nMedianTimeSpan = 3;

        std::valarray<int64_t> pmedian(nMedianTimeSpan);
        int64_t* pbegin = &pmedian[0]+nMedianTimeSpan;
        int64_t* pend = &pmedian[0]+nMedianTimeSpan;

        const CBlockIndex* pindex = this;
        for (int i = 0; i < nMedianTimeSpan && pindex; i++, pindex = pindex->pprev)
            *(--pbegin) = pindex->GetBlockTime();

        std::sort(pbegin, pend);
        return pbegin[(pend - pbegin)/2];
    }

    int64_t GetMedianTimePastWitness() const
    {
        int nMedianTimeSpan = 11;
        if (nHeight >  437500 || Params().IsTestnet())
            nMedianTimeSpan = 3;

        std::valarray<int64_t> pmedian(nMedianTimeSpan);
        int64_t* pbegin = &pmedian[0]+nMedianTimeSpan;
        int64_t* pend = &pmedian[0]+nMedianTimeSpan;

        const CBlockIndex* pindex = this;
        for (int i = 0; i < nMedianTimeSpan && pindex; i++, pindex = pindex->pprev)
            *(--pbegin) = pindex->nTimePoW2Witness == 0 ? pindex->GetBlockTime() : pindex->nTimePoW2Witness;

        std::sort(pbegin, pend);
        return pbegin[(pend - pbegin)/2];
    }

    std::string ToString() const
    {
        return strprintf("CBlockIndex(pprev=%p, nHeight=%d, merkle=%s, hashBlock=%s)",
            pprev, nHeight,
            hashMerkleRoot.ToString(),
            GetBlockHashPoW2().ToString());
    }

    //! Check whether this block index entry is valid up to the passed validity level.
    bool IsValid(enum BlockStatus nUpTo = BLOCK_VALID_TRANSACTIONS) const
    {
        assert(!(nUpTo & ~BLOCK_VALID_MASK)); // Only validity flags allowed.
        if (nStatus & BLOCK_FAILED_MASK)
            return false;
        return ((nStatus & BLOCK_VALID_MASK) >= nUpTo);
    }

    //! Raise the validity level of this block index entry.
    //! Returns true if the validity was changed.
    bool RaiseValidity(enum BlockStatus nUpTo)
    {
        assert(!(nUpTo & ~BLOCK_VALID_MASK)); // Only validity flags allowed.
        if (nStatus & BLOCK_FAILED_MASK)
            return false;
        if ((nStatus & BLOCK_VALID_MASK) < nUpTo) {
            nStatus = (nStatus & ~BLOCK_VALID_MASK) | nUpTo;
            return true;
        }
        return false;
    }

    //! Check whether this block index entry is valid up to the passed partial validity level.
    bool IsPartialValid(enum BlockStatus nUpTo = BLOCK_PARTIAL_TREE) const
    {
        assert(!(nUpTo & ~BLOCK_PARTIAL_MASK)); // Only validity flags allowed.
        if (nStatus & BLOCK_FAILED_MASK)
            return false;
        return ((nStatus & BLOCK_PARTIAL_MASK) >= nUpTo);
    }

    //! Raise the partial validity level of this block index entry.
    //! Returns true if the partial validity was changed.
    bool RaisePartialValidity(enum BlockStatus nUpTo)
    {
        assert(!(nUpTo & ~BLOCK_PARTIAL_MASK)); // Only validity flags allowed.
        if (nStatus & BLOCK_FAILED_MASK)
            return false;
        if ((nStatus & BLOCK_PARTIAL_MASK) < nUpTo) {
            nStatus = (nStatus & ~BLOCK_PARTIAL_MASK) | nUpTo;
            return true;
        }
        return false;
    }

    //! Build the skiplist pointer for this entry.
    void BuildSkip();

    //! Efficiently find an ancestor of this block.
    CBlockIndex* GetAncestor(int height);
    const CBlockIndex* GetAncestor(int height) const;
};

/** Find the last common ancestor two blocks have. Both pa and pb must be non-NULL. */
const CBlockIndex* LastCommonAncestor(const CBlockIndex* pa, const CBlockIndex* pb);

arith_uint256 GetBlockProof(const CBlockIndex& block);
/** Return the time it would take to redo the work difference between from and to, assuming the current hashrate corresponds to the difficulty at tip, in seconds. */
int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params&);

/** Used to marshal pointers into hashes for db storage. */
class CDiskBlockIndex : public CBlockIndex
{
public:
    uint256 hashPrev;

    CDiskBlockIndex() {
        hashPrev = uint256();
    }

    explicit CDiskBlockIndex(const CBlockIndex* pindex) : CBlockIndex(*pindex) {
        hashPrev = (pprev ? pprev->GetBlockHashPoW2() : uint256());
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int _nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(VARINT(_nVersion));

        READWRITE(VARINT(nHeight));
        READWRITE(VARINT(nStatus));
        READWRITE(VARINT(nTx));
        if (nStatus & (BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO))
            READWRITE(VARINT(nFile));
        if (nStatus & BLOCK_HAVE_DATA)
            READWRITE(VARINT(nDataPos));
        if (nStatus & BLOCK_HAVE_UNDO)
            READWRITE(VARINT(nUndoPos));

        // block header
        READWRITE(this->nVersion);
        READWRITE(hashPrev);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);

        try
        {
            READWRITE(nVersionPoW2Witness);
            if (nVersionPoW2Witness != 0)
            {
                READWRITE(nTimePoW2Witness);
                READWRITE(hashMerkleRootPoW2Witness);
                if (ser_action.ForRead())
                    witnessHeaderPoW2Sig.resize(65);
                READWRITENOSIZEVECTOR(witnessHeaderPoW2Sig);

                #ifdef WITNESS_HEADER_SYNC
                if( ((s.GetType() == SER_DISK) && (_nVersion>= 2030013)) || 
                ((s.GetType() == SER_NETWORK) && (_nVersion % 80000 >= WITNESS_SYNC_VERSION)) ||
                ((s.GetType() == SER_GETHASH) && (witnessUTXODelta.size() > 0)) )
                {
                    //fixme: (WITNESS_SYNC) - If size is frequently above 200 then switch to varint instead
                    READWRITECOMPACTSIZEVECTOR(witnessUTXODelta);
                }
                #endif
            }
        }
        catch (...)
        {
        }
    }

    uint256 GetBlockHashLegacy() const
    {
        CBlockHeader block;
        block.nVersionPoW2Witness = nVersionPoW2Witness;
        block.nTimePoW2Witness = nTimePoW2Witness;
        block.hashMerkleRootPoW2Witness = hashMerkleRootPoW2Witness;
        block.witnessHeaderPoW2Sig = witnessHeaderPoW2Sig;
        #ifdef WITNESS_HEADER_SYNC
        block.witnessUTXODelta = witnessUTXODelta;
        #endif
        block.nVersion        = nVersion;
        block.hashPrevBlock   = hashPrev;
        block.hashMerkleRoot  = hashMerkleRoot;
        block.nTime           = nTime;
        block.nBits           = nBits;
        block.nNonce          = nNonce;
        return block.GetHashLegacy();
    }

    uint256 GetBlockHashPoW2(bool force=false) const
    {
        CBlockHeader block;
        block.nVersionPoW2Witness = nVersionPoW2Witness;
        block.nTimePoW2Witness = nTimePoW2Witness;
        block.hashMerkleRootPoW2Witness = hashMerkleRootPoW2Witness;
        block.witnessHeaderPoW2Sig = witnessHeaderPoW2Sig;
        #ifdef WITNESS_HEADER_SYNC
        block.witnessUTXODelta = witnessUTXODelta;
        #endif
        block.nVersion        = nVersion;
        block.hashPrevBlock   = hashPrev;
        block.hashMerkleRoot  = hashMerkleRoot;
        block.nTime           = nTime;
        block.nBits           = nBits;
        block.nNonce          = nNonce;
        return block.GetHashPoW2(force);
    }


    std::string ToString() const
    {
        std::string str = "CDiskBlockIndex(";
        str += CBlockIndex::ToString();
        str += strprintf("\n                hashBlockPoW=%s, hashBlockPoW2=%s, hashPrev=%s)",
            GetBlockHashLegacy().ToString(),
            GetBlockHashPoW2().ToString(),
            hashPrev.ToString());
        return str;
    }
};

class CCloneChain;
/** An in-memory indexed chain of blocks. */
class CChain {
protected:
    std::vector<CBlockIndex*> vChain;

public:
    /** Returns the index entry for the genesis block of this chain, or NULL if none. */
    CBlockIndex *Genesis() const {
        return Height() >= 0 ? operator[](0) : nullptr;
    }

    /** Returns the index entry for the tip of this chain, or NULL if none. */
    CBlockIndex *Tip() const {
        return Height() >= 0 ? operator[](Height()) : nullptr;
    }

    /** Returns the index entry for the previout to tip of this chain, or NULL if none. */
    CBlockIndex* TipPrev() const
    {
        return Height() > 0 ? operator[](Height()-1) : nullptr;
    }

    /** Returns the index entry at a particular height in this chain, or NULL if no such height exists. */
    virtual CBlockIndex *operator[](int nHeight) const {
        if (nHeight < 0 || nHeight >= (int)vChain.size())
            return nullptr;
        return vChain[nHeight];
    }

    /** Compare two chains efficiently. */
    friend bool operator==(const CChain &a, const CChain &b) {
        return a.Height() == b.Height() &&
               a[a.Height()] == b[a.Height()];
    }

    /** Efficiently check whether a block is present in this chain. */
    bool Contains(const CBlockIndex *pindex) const {
        return (*this)[pindex->nHeight] == pindex;
    }

    /** Find the successor of a block in this chain, or NULL if the given index is not found or is the tip. */
    CBlockIndex *Next(const CBlockIndex *pindex) const {
        if (Contains(pindex))
            return (*this)[pindex->nHeight + 1];
        else
            return NULL;
    }

    /** Find the predecessor  of a block in this chain, or NULL if the given index is not found or it is the genisis block. */
    CBlockIndex *Prev(const CBlockIndex *pindex) const {
        if (Contains(pindex) && pindex->nHeight>0)
            return (*this)[pindex->nHeight - 1];
        else
            return NULL;
    }

    /** Return the maximal height in the chain. Is equal to chain.Tip() ? chain.Tip()->nHeight : -1. */
    virtual int Height() const {
        return vChain.size() - 1;
    }

    /** Set/initialize a chain with a given tip. */
    virtual void SetTip(CBlockIndex *pindex);

    /** Return a CBlockLocator that refers to a block in this chain (by default the tip). */
    CBlockLocator GetLocatorLegacy(const CBlockIndex *pindex = NULL) const;
    virtual CBlockLocator GetLocatorPoW2(const CBlockIndex *pindex = NULL) const;

    /** Find the last common block between this chain and a block index entry. */
    const CBlockIndex *FindFork(const CBlockIndex *pindex) const;

    /** Find the earliest block with timestamp equal or greater than the given. */
    CBlockIndex* FindEarliestAtLeast(int64_t nTime) const;

    /** Find the youngest (ie. most recent) block index with comp(val, index) == true
     *  Note that the chain should be ordered with respect to comp.
    */
    template<typename T, typename Compare>
    CBlockIndex* FindYoungest(const T& val, const Compare& comp) {
        const auto it = std::upper_bound (vChain.rbegin (), vChain.rend(), val, comp);
        return it == vChain.rend() ? nullptr : *it;
    }

    virtual ~CChain(){};
};

/** A partial chain only keeps the chain from a certain height-offset onwards.
 * It does not (unlike the CCloneChain) keep a reference to another (full) chain to forward to
 * for items it does not hold.
 * The partial chain is intended to create light clients (SPV) that will only ever see and keep a part
 * of the chain. Access to blocks below the height-offset is illegal.
*/
class CPartialChain : public CChain
{
public:
    CPartialChain();

    void SetHeightOffset(int offset);
    int HeightOffset() const;
    int Length() const;
    virtual CBlockIndex *operator[](int nHeight) const override;
    virtual int Height() const override;
    virtual void SetTip(CBlockIndex *pindex) override;
    CBlockLocator GetLocatorPoW2(const CBlockIndex *pindex = NULL) const override;

    template<typename T, typename Compare>
    int LowerBound(int beginHeight, int endHeight, const T& val, const Compare& comp) const {
        const auto beginRange = vChain.begin() + (beginHeight - HeightOffset());
        const auto endRange =  vChain.begin() + (endHeight - HeightOffset());
        const auto it = std::lower_bound(beginRange, endRange, val, comp);
        return it == endRange ? -1 : (*it)->nHeight;
    }

    // Up until latest built in checkpoint height we are only interested in these ranges and not all blocks.
    //fixme: (UNITY) (SPV) Move this into spvscanner rather.
    RecursiveMutex cs_blockFilterRanges;
    std::vector<std::tuple<uint64_t, uint64_t>> blockFilterRanges;
private:
    int nHeightOffset;
};

// Simple helper class to control memory of cloned chains.
class CCloneChain : public CChain
{
public:
    CCloneChain() = delete;
    CCloneChain(const CChain& _origin, unsigned int _cloneFrom, const CBlockIndex* retainIndexIn, CBlockIndex*& retainIndexOut);

    virtual ~CCloneChain()
    {
        FreeMemory();
    }

    virtual CBlockIndex *operator[](int nHeight) const override;

    virtual int Height() const override;

    virtual void SetTip(CBlockIndex *pindex) override;

private:
    void FreeMemory();

    const CChain& origin;
    int cloneFrom;
    std::vector<CBlockIndex*> vFree;
};

#endif
