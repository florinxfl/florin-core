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

#ifndef VALIDATION_VALIDATION_H
#define VALIDATION_VALIDATION_H

#if defined(HAVE_CONFIG_H)
#include "config/build-config.h"
#endif

#include <compat/sys.h> // For PLATFORM_MOBILE define

#include "amount.h"
#include "coins.h"
#include "fs.h"
#include "policy/feerate.h"
#include "script/script_error.h"
#include "sync.h"
#include "chain.h"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include "consensus/tx_verify.h"
#include "txmempool.h"

#include <script/interpreter.h>

#include <atomic>

#include "uint256.h"

#include <LRUCache/LRUCache11.hpp>

#if defined(__APPLE__) && defined(__MACH__)
#include <TargetConditionals.h>
#endif

#ifdef PLATFORM_MOBILE
#define VALIDATION_MOBILE
#endif
class CBlockIndex;
class CBlockTreeDB;
class CChainParams;
class CCoinsViewDB;
class CInv;
class CConnman;
class CScriptCheck;
class CBlockPolicyEstimator;
class CTxMemPool;
class CValidationInterface;
class CValidationState;


struct PrecomputedTransactionData;
struct LockPoints;

class CWitViewDB;

enum FlushStateMode {
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};

/** Default for accepting alerts from the P2P network. */
static const bool DEFAULT_ALERTS = true;
/** Default for DEFAULT_WHITELISTRELAY. */
static const bool DEFAULT_WHITELISTRELAY = true;
/** Default for DEFAULT_WHITELISTFORCERELAY. */
static const bool DEFAULT_WHITELISTFORCERELAY = true;
/** Default for -minrelaytxfee, minimum relay fee for transactions */
static const unsigned int DEFAULT_MIN_RELAY_TX_FEE = 100;
//! -maxtxfee default
static const CAmount DEFAULT_TRANSACTION_MAXFEE = 1 * COIN;
//! Discourage users to set fees higher than this amount (in satoshis) per kB
static const CAmount HIGH_TX_FEE_PER_KB = 0.01 * COIN;
//! -maxtxfee will warn if called with a higher fee than this amount (in satoshis)
static const CAmount HIGH_MAX_TX_FEE = 100 * HIGH_TX_FEE_PER_KB;
/** Default for -limitancestorcount, max number of in-mempool ancestors */
static const unsigned int DEFAULT_ANCESTOR_LIMIT = 25;
/** Default for -limitancestorsize, maximum kilobytes of tx + all in-mempool ancestors */
static const unsigned int DEFAULT_ANCESTOR_SIZE_LIMIT = 101;
/** Default for -limitdescendantcount, max number of in-mempool descendants */
static const unsigned int DEFAULT_DESCENDANT_LIMIT = 25;
/** Default for -limitdescendantsize, maximum kilobytes of in-mempool descendants */
static const unsigned int DEFAULT_DESCENDANT_SIZE_LIMIT = 101;
/** Default for -mempoolexpiry, expiration time for mempool transactions in hours */
static const unsigned int DEFAULT_MEMPOOL_EXPIRY = 1;
/** Maximum kilobytes for transactions to store for processing during reorg */
static const unsigned int MAX_DISCONNECTED_TX_POOL_SIZE = 20000;
#if defined(VALIDATION_MOBILE)
/** The maximum size of a blk?????.dat file (since 0.8) */
static const unsigned int MAX_BLOCKFILE_SIZE = 0x2000000; // 32 MiB
/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
static const unsigned int BLOCKFILE_CHUNK_SIZE = 0x0400000; // 4 MiB
#else
/** The maximum size of a blk?????.dat file (since 0.8) */
static const unsigned int MAX_BLOCKFILE_SIZE = 0x8000000; // 128 MiB
/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
static const unsigned int BLOCKFILE_CHUNK_SIZE = 0x1000000; // 16 MiB
#endif
/** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
static const unsigned int UNDOFILE_CHUNK_SIZE = 0x100000; // 1 MiB
/** Dust Soft Limit, allowed with additional fee per output */
static const int64_t DUST_SOFT_LIMIT = 100000000; // 1 NLG
/** Dust Hard Limit, ignored as wallet inputs (mininput default) */
static const int64_t DUST_HARD_LIMIT = 1000;   // 0.00001 NLG mininput
/** Maximum number of script-checking threads allowed */
static const int MAX_SCRIPTCHECK_THREADS = 16;
/** -par default (number of script-checking threads, 0 = auto) */
static const int DEFAULT_SCRIPTCHECK_THREADS = 0;
/** Number of blocks that can be requested at any given time from a single peer. */
static const int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 64;
/** Timeout in seconds during which a peer must stall block download progress before being disconnected. */
static const unsigned int BLOCK_STALLING_TIMEOUT = 2;
/** Number of headers sent in one getheaders result. We rely on the assumption that if a peer sends
 *  less than this number, we reached its tip. Changing this value is a protocol upgrade. */
static const unsigned int MAX_HEADERS_RESULTS = 2000;
static const unsigned int MAX_RHEADERS_RESULTS = 4000;
/** Maximum depth of blocks we're willing to serve as compact blocks to peers
 *  when requested. For older blocks, a regular BLOCK response will be sent. */
static const int MAX_CMPCTBLOCK_DEPTH = 5;
/** Maximum depth of blocks we're willing to respond to GETBLOCKTXN requests for. */
static const int MAX_BLOCKTXN_DEPTH = 10;
/** Size of the "block download window": how far ahead of our current height do we fetch?
 *  Larger windows tolerate larger download speed differences between peer, but increase the potential
 *  degree of disordering of blocks on disk (which make reindexing and in the future perhaps pruning
 *  harder). We'll probably want to make this a per-peer adaptive value at some point. */
static const unsigned int BLOCK_DOWNLOAD_WINDOW = 1024;
/** Time to wait (in seconds) between writing blocks/block index to disk.
 *  Use a very short write interval on mobile platforms as it is very likely (and normal procedure)
 *  to be terminated without notice. Also the writing is quite fast as there is little work in pure SPV mode.
*/
#if defined(VALIDATION_MOBILE)
static const unsigned int DATABASE_WRITE_INTERVAL = 10;
#else
static const unsigned int DATABASE_WRITE_INTERVAL = 60 * 60;
#endif
/** Time to wait (in seconds) between flushing chainstate to disk. */
static const unsigned int DATABASE_FLUSH_INTERVAL = 24 * 60 * 60;
/** Maximum length of reject messages. */
static const unsigned int MAX_REJECT_MESSAGE_LENGTH = 111;
/** Average delay between local address broadcasts in seconds. */
static const unsigned int AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL = 24 * 60 * 60;
/** Average delay between peer address broadcasts in seconds. */
static const unsigned int AVG_ADDRESS_BROADCAST_INTERVAL = 30;
/** Average delay between trickled inventory transmissions in seconds.
 *  Blocks and whitelisted receivers bypass this, outbound peers get half this delay. */
static const unsigned int INVENTORY_BROADCAST_INTERVAL = 5;
/** Maximum number of inventory items to send per transmission.
 *  Limits the impact of low-fee transaction floods. */
static const unsigned int INVENTORY_BROADCAST_MAX = 7 * INVENTORY_BROADCAST_INTERVAL;
/** Average delay between feefilter broadcasts in seconds. */
static const unsigned int AVG_FEEFILTER_BROADCAST_INTERVAL = 10 * 60;
/** Maximum feefilter broadcast delay after significant change. */
static const unsigned int MAX_FEEFILTER_CHANGE_DELAY = 5 * 60;
/** Block download timeout base, expressed in microseconds */
static const int64_t BLOCK_DOWNLOAD_TIMEOUT_BASE = 10000000;
/** Additional block download timeout per parallel downloading peer, expressed in microseconds*/
static const int64_t BLOCK_DOWNLOAD_TIMEOUT_PER_PEER = 5000000;

static const int64_t DEFAULT_MAX_TIP_AGE = 24 * 60 * 60;
/** Maximum age of our tip in seconds for us to be considered current for fee estimation */
static const int64_t MAX_FEE_ESTIMATION_TIP_AGE = 3 * 60 * 60;

/** Default for -permitbaremultisig */
static const bool DEFAULT_PERMIT_BAREMULTISIG = true;
static const bool DEFAULT_CHECKPOINTS_ENABLED = true;
static const bool DEFAULT_TXINDEX = false;
static const unsigned int DEFAULT_BANSCORE_THRESHOLD = 100;
/** Default for -persistmempool */
static const bool DEFAULT_PERSIST_MEMPOOL = true;
/** Default for -mempoolreplacement */
static const bool DEFAULT_ENABLE_REPLACEMENT = true;
/** Default for using fee filter */
static const bool DEFAULT_FEEFILTER = true;

/** Maximum number of headers to announce when relaying blocks with headers message.*/
static const unsigned int MAX_BLOCKS_TO_ANNOUNCE = 8;

//fixme: (PHASE5) Temporary increase from 10 (if lots of absent witnesses a miner could send quite a few unconnecting headers)
//re-look at special logic for this.
/** Maximum number of unconnecting headers announcements before DoS score */
static const int MAX_UNCONNECTING_HEADERS = 200;

static const bool DEFAULT_PEERBLOOMFILTERS = true;

/** Default for -stopatheight */
static const int DEFAULT_STOPATHEIGHT = 0;

/** if disabled full sync and validation will be skipped, ie. no chain is build. */
static const bool DEFAULT_FULL_SYNC_MODE = true;

struct BlockHasher
{
    size_t operator()(const uint256& hash) const { return hash.GetCheapHash(); }
};

extern CScript COINBASE_FLAGS;
extern RecursiveMutex cs_main;
extern CBlockPolicyEstimator feeEstimator;
extern CTxMemPool mempool;
using BlockMap = std::unordered_map<uint256, CBlockIndex*, BlockHasher>;

#if defined(__clang__)
// Specialize BlockMap [] operator to ensure that it is only used for
// (valid) lookup and never triggers an insert (which is considered a bug).
// Unfortunatly this specialization won't work on GCC (why?) so it only detected on clang
template<> inline BlockMap::mapped_type& BlockMap::operator [](const uint256& key)
{
    const auto i = find(key);
    assert(i != end());
    return (*i).second;
}
#endif

extern BlockMap mapBlockIndex;
extern uint64_t nLastBlockTx;
extern uint64_t nLastBlockSize;
extern uint64_t nLastBlockWeight;
extern const std::string strMessageMagic;
extern Mutex csBestBlock;
extern std::condition_variable cvBlockChange;
extern std::atomic_bool fImporting;
extern bool fReindex;
extern bool fReverseHeaders;
extern int nScriptCheckThreads;
extern bool fTxIndex;
extern bool fIsBareMultisigStd;
extern bool fRequireStandard;
extern bool fCheckBlockIndex;
extern bool fCheckpointsEnabled;
extern size_t nCoinCacheUsage;
/** A fee rate smaller than this is considered zero fee (for relaying, mining and transaction creation) */
extern CFeeRate minRelayTxFee;
/** Absolute maximum transaction fee (in satoshis) used by wallet and mempool (rejects high fee in sendrawtransaction) */
extern CAmount maxTxFee;
extern bool fAlerts;
/** If the tip is older than this (in seconds), the node is considered to be in initial block download. */
extern int64_t nMaxTipAge;
extern bool fEnableReplacement;

/** Block hash whose ancestors we will assume to have valid scripts without checking them. */
extern uint256 hashAssumeValid;

/** Cache to prevent repeated calls of same expensive CheckProofOfWork in certain situations */
inline lru11::Cache<uint256, bool, lru11::NullLock, std::unordered_map<uint256, typename std::list<lru11::KeyValuePair<uint256, bool>>::iterator, BlockHasher>> checkedPoWCache(2000, 100);

/** Best header we've seen so far (used for getheaders queries' starting points). */
extern CBlockIndex *pindexBestHeader;

/** Best header sofar connecting to the partial chain. */
extern CBlockIndex *pindexBestPartial;

extern int64_t nMinimumInputValue;

/** Minimum disk space required - used in CheckDiskSpace() */
static const uint64_t nMinDiskSpace = 52428800;

/** Pruning-related variables and constants */
/** True if any block files have ever been pruned. */
extern bool fHavePruned;
/** True if we're running in -prune mode. */
extern bool fPruneMode;
/** Number of MiB of block files that we're trying to stay below. */
extern uint64_t nPruneTarget;
/** Track pruning of partial chain to optimize & prevent duplicate erase */
extern int nPartialPruneHeightDone;

/** Block files containing a block-height within MIN_BLOCKS_TO_KEEP of chainActive.Tip() will not be pruned. */
static const unsigned int MIN_BLOCKS_TO_KEEP = 288;

static const signed int DEFAULT_CHECKBLOCKS = 6;
static const unsigned int DEFAULT_CHECKLEVEL = 3;

// Require that user allocate at least 550MB for block & undo files (blk???.dat and rev???.dat)
// At 1MB per block, 288 blocks = 288MB.
// Add 15% for Undo data = 331MB
// Add 20% for Orphan block rate = 397MB
// We want the low water mark after pruning to be at least 397 MB and since we prune in
// full block file chunks, we need the high water mark which triggers the prune to be
// one 128MB block file + added 15% undo data = 147MB greater for a total of 545MB
// Setting the target to > than 550MB will make it likely we can respect the target.
static const uint64_t MIN_DISK_SPACE_FOR_BLOCK_FILES = 550 * 1024 * 1024;

/** 
 * Process an incoming block. This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 *
 * If you want to *possibly* get feedback on whether pblock is valid, you must
 * install a CValidationInterface (see validation/validationinterface.h) - this will have
 * its BlockChecked method called whenever *any* block completes validation.
 *
 * Note that we guarantee that either the proof-of-work is valid on pblock, or
 * (and possibly also) BlockChecked will have been called.
 * 
 * Call without cs_main held.
 *
 * @param[in]   pblock  The block we want to process.
 * @param[in]   fForceProcessing Process this block even if unrequested; used for non-network block sources and whitelisted peers.
 * @param[out]  fNewBlock A boolean which is set to indicate if the block was first received via this call
 * @param[in]  fAssumePOWGood A boolean to indicate that the POW can be assumed to be correct
 * @param[in]   activateBestChain will allow to bypass the activate best chain logic (for priority block downloads)
 * @return True if state.IsValid()
 */
bool ProcessNewBlock(const CChainParams& chainparams, const std::shared_ptr<const CBlock> pblock, bool fForceProcessing, bool* fNewBlock, bool fAssumePOWGood, bool activateBestChain);

/**
 * Process incoming block headers.
 *
 * Call without cs_main held.
 *
 * @param[in]  block The block headers themselves
 * @param[out] state This may be set to an Error state if any error occurred processing them
 * @param[in]  chainparams The params for the chain we want to connect to
 * @param[in]  fAssumePOWGood A boolean to indicate that the POW can be assumed to be correct
 * @param[out] ppindex If set, the pointer will be set to point to the last new block index object for the given headers
 */
bool ProcessNewBlockHeaders(const std::vector<CBlockHeader>& block, CValidationState& state, const CChainParams& chainparams, const CBlockIndex** ppindex=NULL, bool fAssumePOWGood = false);

/** Check whether enough disk space is available for an incoming block */
bool CheckDiskSpace(uint64_t nAdditionalBytes = 0);
/** Import blocks from an external file */
bool LoadExternalBlockFile(const CChainParams& chainparams, FILE* fileIn, CDiskBlockPos *dbp = NULL);
/** Initialize a new block tree database + block data on disk */
bool InitBlockIndex(const CChainParams& chainparams);
/** Load the block tree and coins database from disk */
bool LoadBlockIndex(const CChainParams& chainparams);
/** Unload database information */
void UnloadBlockIndex();
/** Run instances of script checking worker threads */
void StartScriptCheckWorkerThreads(int threads_num);
/** Stop all of the script checking worker threads */
void StopScriptCheckWorkerThreads();
/** Check whether we are doing an initial block download (synchronizing from disk or network) */
bool IsInitialBlockDownload();
/** Retrieve a transaction (from memory pool, or from disk, if possible) */
bool GetTransaction(const uint256 &hash, CTransactionRef &tx, const CChainParams& params, uint256 &hashBlock, bool fAllowSlow = false);
/** Find the best known block, and make it the tip of the block chain */
bool ActivateBestChain(CValidationState& state, const CChainParams& chainparams, std::shared_ptr<const CBlock> pblock = std::shared_ptr<const CBlock>());

struct BlockSubsidy
{
    BlockSubsidy(CAmount mining_, CAmount witness_, CAmount dev_)
    : mining(mining_)
    , witness(witness_)
    , dev(dev_)
    , total(mining_ + witness_ + dev_)
    {
    }
    CAmount mining;
    CAmount witness;
    CAmount dev;
    CAmount total;
};

/** The reward that must be paid out per block */
BlockSubsidy GetBlockSubsidy(uint64_t nHeight);
inline std::string devSubsidyAddress = "024ab66a6765794f3e5149b633950285b18e0e0b1dab4f19fc5d62710e6d539c66";

/** Guess verification progress (as a fraction between 0.0=genesis and 1.0=current tip). */
double GuessVerificationProgress(CBlockIndex* pindex);

/**
 *  Mark one block file as pruned.
 */
void PruneOneBlockFile(const int fileNumber);

/** Create a new block index entry for a given block hash */
CBlockIndex * InsertBlockIndex(uint256 hash);
/** Flush all state, indexes and buffers to disk. */
void FlushStateToDisk();
/** Prune block files and flush state to disk. */
void PruneAndFlush();
/** Prune block files up to a given height */
void PruneBlockFilesManual(int nManualPruneHeight);
/**
 * Persist and prune block index and blk/rev files if using partial sync only
 * Deleting block indexes unnesesary for partial sync reduces database size, memory usage and
 * results in faster startup times.
 * Call periodically to prevent re-download after forced kill (= normal iOS behavior) or crash
 * also call at shutdown.
*/
void PersistAndPruneForPartialSync(bool periodic=false);
/** (try to) add transaction to memory pool
 * plTxnReplaced will be appended to with all transactions replaced from mempool **/
bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState &state, const CTransactionRef &tx, bool fLimitFree,
                        bool* pfMissingInputs, std::list<CTransactionRef>* plTxnReplaced = NULL,
                        bool fOverrideMempoolLimit=false, const CAmount nAbsurdFee=0);

/**
 * In pure partial sync expire transactions that stay in the memorypool too long.
 * It's for example possible that created transaction pass local spv validation but are
 * rejected by the full validation.
 */
int ExpireMempoolForPartialSync(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* tip);

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state);

/** Apply the effects of this transaction on the UTXO set represented by view */
void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, uint32_t nHeight, uint32_t nTxIndex);

/** Transaction validation functions */

/**
 * Check if transaction will be final in the next block to be created.
 *
 * Calls IsFinalTx() with current block height and appropriate block time.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckFinalTx(const CTransaction &tx, const CChain& chain, int flags = -1);

/**
 * Test whether the LockPoints height and time are still valid on the current chain
 */
bool TestLockPointValidity(const LockPoints* lp);

/**
 * Check if transaction will be BIP 68 final in the next block to be created.
 *
 * Simulates calling SequenceLocks() with data from the tip of the current active chain.
 * Optionally stores in LockPoints the resulting height and time calculated and the hash
 * of the block needed for calculation or skips the calculation and uses the LockPoints
 * passed in for evaluation.
 * The LockPoints should not be considered valid if CheckSequenceLocks returns false.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckSequenceLocks(const CTransaction &tx, int flags, LockPoints* lp = NULL, bool useExistingLockPoints = false);

/**
 * Closure representing one script verification
 * Note that this stores references to the spending transaction 
 */
class CScriptCheck
{
private:
    CKeyID signingKeyID;
    CScript scriptPubKey;
    CAmount amount;
    const CTransaction *ptxTo;
    unsigned int nIn;
    unsigned int nFlags;
    bool cacheStore;
    ScriptError error;
    PrecomputedTransactionData *txdata;
    ScriptVersion scriptVer;

public:

    CScriptCheck(): signingKeyID(CKeyID()), amount(0), ptxTo(0), nIn(0), nFlags(0), cacheStore(false), error(SCRIPT_ERR_UNKNOWN_ERROR), scriptVer(SCRIPT_V1) {}
    CScriptCheck(CKeyID signingKeyID_, const CScript& scriptPubKeyIn, const CAmount amountIn, const CTransaction& txToIn, unsigned int nInIn, unsigned int nFlagsIn, bool cacheIn, PrecomputedTransactionData* txdataIn, ScriptVersion scriptVerIn)
    : signingKeyID(signingKeyID_)
    , scriptPubKey(scriptPubKeyIn)
    , amount(amountIn)
    , ptxTo(&txToIn)
    , nIn(nInIn)
    , nFlags(nFlagsIn)
    , cacheStore(cacheIn)
    , error(SCRIPT_ERR_UNKNOWN_ERROR)
    , txdata(txdataIn)
    , scriptVer(scriptVerIn)
    { }

    bool operator()();

    void swap(CScriptCheck &check) {
        scriptPubKey.swap(check.scriptPubKey);
        std::swap(ptxTo, check.ptxTo);
        std::swap(amount, check.amount);
        std::swap(nIn, check.nIn);
        std::swap(nFlags, check.nFlags);
        std::swap(cacheStore, check.cacheStore);
        std::swap(error, check.error);
        std::swap(txdata, check.txdata);
        std::swap(signingKeyID, check.signingKeyID);
        std::swap(scriptVer, check.scriptVer);
    }

    ScriptError GetScriptError() const { return error; }
};

/** Functions for disk access for blocks */
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex, const CChainParams& params);

/** Functions for validating blocks and updating the block tree */

/** Context-independent validity checks */
bool CheckBlock(const CBlock& block, CValidationState& state, const Consensus::Params& consensusParams, bool fCheckPOW = true, bool fCheckMerkleRoot = true, bool fAssumePOWGood = false);

/** Check a block is completely valid from start to finish (only works on top of our current best block, with cs_main held) */
bool TestBlockValidity(CChain& chain, CValidationState& state, const CChainParams& chainparams, const CBlock& block, CBlockIndex* pindexPrev, bool fCheckPOW = true, bool fCheckMerkleRoot = true, CCoinsViewCache* cacheOverride = nullptr);

/** Check whether segregated signatures are required for block. */
bool IsSegSigEnabled(const CBlockIndex* pindexPrev);

/** When there are blocks in the active chain with missing data, rewind the chainstate and remove them from the block index */
bool RewindBlockIndex(const CChainParams& params);

/** RAII wrapper for VerifyDB: Verify consistency of the block and coin databases */
class CVerifyDB {
public:
    CVerifyDB();
    ~CVerifyDB();
    bool VerifyDB(const CChainParams& chainparams, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth);
};

/** Find the last common block between the parameter chain and a locator. */
CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator);

/** Mark a block as precious and reorganize. */
bool PreciousBlock(CValidationState& state, const CChainParams& params, CBlockIndex *pindex);

/** Mark a block as invalid. */
bool InvalidateBlock(CValidationState& state, const CChainParams& chainparams, CBlockIndex *pindex);

/** Remove invalidity status from a block and its descendants. */
bool ResetBlockFailureFlags(CBlockIndex *pindex);

/** Remove invalidity status from a block. */
bool ResetBlockFailureFlagsForSingleBlock(CBlockIndex *pindex);

/** The currently-connected chain of blocks (protected by cs_main). */
extern CChain chainActive;

/** The currently-connected chain of header, POW only validated (protected by cs_main). */
extern CPartialChain partialChain;

extern bool fSPV;
//! Chain height for active chain (either chainActive or partialChain)
int ChainHeight();

//! Chain tip for active chain (either chainActive or partialChain)
CBlockIndex* chainTip();

//! Chain previous for active chain (either chainActive or partialChain)
CBlockIndex* chainPrevTip();

/** Global variable that points to the coins database (protected by cs_main) */
extern CCoinsViewDB *pcoinsdbview;

/** Global variable that points to the active CCoinsView (protected by cs_main) */
extern CCoinsViewCache *pcoinsTip;

/** Global variable that points to the active block tree (protected by cs_main) */
extern CBlockTreeDB *pblocktree;

struct CBlockIndexWorkComparator
{
    bool operator()(CBlockIndex *pa, CBlockIndex *pb) const {
        // First sort by most total work, ...
        if (pa->nChainWork > pb->nChainWork) return false;
        if (pa->nChainWork < pb->nChainWork) return true;

        // (Phase 3) PoW blocks at same height as witness blocks - always make the PoW blocks rank higher.
        if (pa->nHeight == pb->nHeight)
        {
            if (pa->nVersionPoW2Witness == 0 && pb->nVersionPoW2Witness != 0)
            {
                return false;
            }
            if (pa->nVersionPoW2Witness != 0 && pb->nVersionPoW2Witness == 0)
            {
                return true;
            }
        }

        // ... then by earliest time received, ...
        if (pa->nSequenceId < pb->nSequenceId) return false;
        if (pa->nSequenceId > pb->nSequenceId) return true;

        // Use pointer address as tie breaker (should only happen with blocks
        // loaded from disk, as those all have id 0).
        if (pa < pb) return false;
        if (pa > pb) return true;

        // Identical blocks.
        return false;
    }
};

//fixme: (FUT) (MED) (REFACTOR) Move DisconnectBlock/ConnectBlock/setBlockIndexCandidates/ContextualCheckBlockHeader/ContextualCheckBlock/CheckInputs/FlushStateToDisk/FindFilesToPruneManual/FindFilesToPrune/UpdateMempoolForReorg out of header files they should be source file only - this will require moving all the chain connect/disconnect/forceactivate stuff into their own shared source file
//validation.cpp is too large and needs to shrink.

void UpdateMempoolForReorg(DisconnectedBlockTransactions &disconnectpool, bool fAddToMempool);

// Force mempool to empty
void EmptyMempool(CTxMemPool& pool);

// See definition for documentation
bool FlushStateToDisk(const CChainParams& chainParams, CValidationState &state, FlushStateMode mode, int nManualPruneHeight=0, bool fFlushPartialSync=false);
void FindFilesToPruneManual(std::set<int>& setFilesToPrune, int nManualPruneHeight);
void FindFilesToPrune(std::set<int>& setFilesToPrune, uint64_t nPruneAfterHeight);

bool CheckInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, unsigned int flags, bool cacheStore, PrecomputedTransactionData& txdata, const std::vector<CWitnessTxBundle>* pWitnessBundles, std::vector<CScriptCheck> *pvChecks = NULL);

/**
 * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
 * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
 * missing the data for the block.
 */
extern std::set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;

enum DisconnectResult
{
    DISCONNECT_OK,      // All good.
    DISCONNECT_UNCLEAN, // Rolled back, but UTXO set was inconsistent with block.
    DISCONNECT_FAILED   // Something else went wrong.
};

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  When UNCLEAN or FAILED is returned, view is left in an indeterminate state. */
DisconnectResult DisconnectBlock(const CBlock& block, const CBlockIndex* pindex, CCoinsViewCache& view);

/** Apply the effects of this block (with given index) on the UTXO set represented by coins.
 *  Validity checks that depend on the UTXO set are also done; ConnectBlock()
 *  can fail if those validity checks fail (among other reasons). */
bool ConnectBlock(CChain& chain, const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view, const CChainParams& chainparams, bool fJustCheck = false, bool fVerifyWitness=true, bool fDoScriptChecks=true);

/** Context-dependent validity checks.
 *  By "context", we mean only the previous block headers, but not the UTXO
 *  set; UTXO-related validity checks are done in ConnectBlock(). */
bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev, int64_t nAdjustedTime);

bool ContextualCheckBlock(const CBlock& block, CValidationState& state, const CChainParams& chainParams, const CBlockIndex* pindexPrev, CChain& chainOverride, CCoinsViewCache* viewOverride=nullptr, bool doUTXOChecks=false);

bool UpgradeBlockIndex(const CChainParams& chainparams, int nPreviousVersion, int nCurrentVersion);

/**
 * Return the spend height, which is one more than the inputs.GetBestBlock().
 * While checking, GetBestBlock() refers to the parent block. (protected by cs_main)
 * This is also true for mempool checks.
 */
int GetSpendHeight(const CCoinsViewCache& inputs);

/**
 * Determine what nVersion a new block should use.
 */
int32_t ComputeBlockVersion(const CBlockIndex* pindexPrev, const Consensus::Params& params);

/** Reject codes greater or equal to this can be returned by AcceptToMemPool
 * for transactions, to signal internal conditions. They cannot and should not
 * be sent over the P2P network.
 */
static const unsigned int REJECT_INTERNAL = 0x100;
/** Too high fee. Can not be triggered by P2P transactions */
static const unsigned int REJECT_HIGHFEE = 0x100;
/** Transaction is already known (either in mempool or blockchain) */
static const unsigned int REJECT_ALREADY_KNOWN = 0x101;
/** Transaction conflicts with a transaction already known */
static const unsigned int REJECT_CONFLICT = 0x102;

/** Get block file info entry for one block file */
CBlockFileInfo* GetBlockFileInfo(size_t n);

/** Dump the mempool to disk. */
void DumpMempool();

/** Load the mempool from disk. */
bool LoadMempool();

/** Full sync and validation. */
void SetFullSyncMode(bool state);
bool isFullSyncMode();

void StopPartialHeaders(const std::function<void(const CBlockIndex*)>& notifyCallback);
bool StartPartialHeaders(int64_t time, const std::function<void(const CBlockIndex*)>& notifyCallback);
void SetMaxSPVPruneHeight(int height);

/**
 * Compute new filter ranges, stored in partialChain.blockFilterRanges.
 * Filter range starts at nWalletBirthBlockHard and moves nWalletBirthBlockSoft down to the
 * first block that might contain data for the activeWallet.
 */
void ComputeNewFilterRanges(uint64_t nWalletBirthBlockHard, uint64_t& nWalletBirthBlockSoft);

/** Transaction hash from outpoint. Even if it is index based. */
bool GetTxHash(const COutPoint& outpoint, uint256& txHash);

#endif
