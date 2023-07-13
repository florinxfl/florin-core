// Copyright (c) 2018-2022 The Centure developers
// Authored by: Willem de Jonge (willem@isnapp.nl)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#include "spvscanner.h"
#include "../net_processing.h"
#include "timedata.h"
#include "../validation/validation.h"
#include "wallet.h"
#include "checkpoints.h"

#include <algorithm>

#include <boost/bind/bind.hpp>
using namespace boost::placeholders;

// Maximum number of requests pending. This number should be high enough so that
// the requests can be reasonably distributed over our peers.
const int MAX_PENDING_REQUESTS = 512;

// Duration (seconds) of longest fork that can be handled
const int64_t MAX_FORK_DURATION = 1 * 12 * 3600;

// Persisting wallet db updates are limited as they are very expensive
// and updating at every processed block would give very poor performance.
// Therefore the state is only written to the db when a minimum time interval has passed,
// or a certain of number of blocks have been processed (whichever comes first).
// Note that due to this some blocks might be re-processed in case of abnormal termination,
// this is fine.

// Interval in seconds for writing the scan progress to the wallet db
const int64_t PERSIST_INTERVAL_SEC = 5;

// Blocks count after which scan processing state is written to the db.
const int PERSIST_BLOCK_COUNT = 500;

// limit UI update notifications (except when catched up)
const int UI_UPDATE_LIMIT = 50;

std::atomic<int> CSPVScanner::lastProcessedBlockHeight = 0;
std::atomic<float> CSPVScanner::lastProgressReported = -1.0f;

CSPVScanner::CSPVScanner(CWallet& _wallet) :
    wallet(_wallet),
    numConnections(0)
{
    LOCK(cs_main);
    Init();
}

CSPVScanner::~CSPVScanner()
{
    uiInterface.NotifyNumConnectionsChanged.disconnect(boost::bind(&CSPVScanner::OnNumConnectionsChanged, this, _1));
}

void CSPVScanner::Init()
{
    AssertLockHeld(cs_main);

    blockLastProcessed = nullptr;
    lastProgressReported = -1.0f;
    lastPersistedBlockTime = 0;
    lastPersistTime = 0;
    startHeight = -1;
    nRequestsPending = 0;
    lastProcessedBlockHeight = 0;

    // init scan starting time to birth of first key
    startTime =  wallet.nTimeFirstKey;

    // forward scan starting time to last block processed if available
    CWalletDB walletdb(*wallet.dbw);
    CBlockLocator locator;
    if (walletdb.ReadLastSPVBlockProcessed(locator, lastPersistedBlockTime))
        startTime = lastPersistedBlockTime;

    // rewind scan starting time by maximum handled fork duration
    startTime = std::max(Params().GenesisBlock().GetBlockTime(), startTime - MAX_FORK_DURATION);
}

void CSPVScanner::ResetScan()
{
    LOCK2(cs_main, wallet.cs_wallet);

    StopPartialHeaders(std::bind(&CSPVScanner::HeaderTipChanged, this, std::placeholders::_1));
    wallet.NotifyKeyPoolToppedUp.disconnect(boost::bind(&CSPVScanner::onKeyPoolToppedUp, this));
    CancelAllPriorityDownloads();

    // erase persisted spv progress
    CWalletDB walletdb(*wallet.dbw);
    walletdb.EraseLastSPVBlockProcessed();

    Init();

    ResetUnifiedProgressNotification();

}

bool CSPVScanner::StartScan()
{
    LOCK(cs_main);
    if (StartPartialHeaders(startTime, std::bind(&CSPVScanner::HeaderTipChanged, this, std::placeholders::_1)))
    {
        uiInterface.NotifyNumConnectionsChanged.connect(boost::bind(&CSPVScanner::OnNumConnectionsChanged, this, _1));
        HeaderTipChanged(partialChain.Tip());
        NotifyUnifiedProgress();
        {
            LOCK(wallet.cs_wallet);
            wallet.NotifyKeyPoolToppedUp.connect(boost::bind(&CSPVScanner::onKeyPoolToppedUp, this));
        }
        return true;
    }
    else
        return false;
}

const CBlockIndex* CSPVScanner::LastBlockProcessed() const
{
    LOCK(cs_main);
    return blockLastProcessed;
}

void CSPVScanner::onKeyPoolToppedUp()
{
    static std::atomic_flag computingRanges;

    const CBlockIndex* pIndexLast = LastBlockProcessed();
    if (pIndexLast == nullptr)
        return;

    if (computingRanges.test_and_set())
    {
        std::thread([&, pIndexLast]() {
            uint64_t nBirthBLockHard = pIndexLast->nHeight;
            uint64_t nDummyBlockSoft = Checkpoints::LastCheckPointHeight();
            ComputeNewFilterRanges(nBirthBLockHard, nDummyBlockSoft);
            computingRanges.clear();
        }).detach();
    }
}

// If we are before the first range or not in one of the ranges then we can skip fetching the data.
// If we are in one of the ranges or if we are after the last checkpoint then we must fetch the data.
// If we have no filter ranges then we must fetch the data.
bool CSPVScanner::CanSkipBlockFetch(const CBlockIndex* pIndex, uint64_t lastCheckPointHeight)
{
    AssertLockHeld(partialChain.cs_blockFilterRanges);
    if ((uint64_t)pIndex->nHeight > lastCheckPointHeight)
        return false;

    if (partialChain.blockFilterRanges.empty())
        return false;

    for (const auto& [rangeStart, rangeEnd] : partialChain.blockFilterRanges)
    {
        if ((uint64_t)pIndex->nHeight >= rangeStart)
        {
            if ((uint64_t)pIndex->nHeight < rangeEnd)
                return false;
        }
        else
        {
            // Short circuit optimisation: Ranges are ordered so if we aren't > this first one then we won't be > than any of the subsequent ones.
            break;
        }
    }
    return true;
}

void CSPVScanner::RequestBlocks()
{
    LOCK2(cs_main, wallet.cs_wallet);

    // put blockLastProcessed and/or blockRequestTip back on chain if forked
    while (!partialChain.Contains(blockRequestTip)) {
        if (blockRequestTip->nHeight > blockLastProcessed->nHeight) {
            CancelPriorityDownload(blockRequestTip, std::bind(&CSPVScanner::ProcessPriorityRequest, this, std::placeholders::_1, std::placeholders::_2));
            blockRequestTip = blockRequestTip->pprev;
        }
        else { // so here blockRequestTip == blockLastProcessed
            std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
            if (ReadBlockFromDisk(*pblock, blockLastProcessed, Params())) {
                wallet.BlockDisconnected(pblock);
            }
            else {
                // This block must be on disk, it was processed before.
                // So pruning has to keep at least as many blocks back as the longest fork we are willing to handle.
                assert(false);
            }
            UpdateLastProcessed(blockLastProcessed->pprev);
            blockRequestTip = blockLastProcessed;
        }
    }

    // skip blocks that are before startTime
    CBlockIndex* skip = blockLastProcessed;
    while (skip->GetBlockTime() < startTime && partialChain.Height() > skip->nHeight)
    {
        skip = partialChain.Next(skip);
    }
    if (skip != blockLastProcessed)
    {
        LogPrint(BCLog::WALLET, "Skipping %d old blocks for SPV scan, up to height %d\n", skip->nHeight - blockLastProcessed->nHeight, skip->nHeight);
        UpdateLastProcessed(skip);
        if (blockLastProcessed->nHeight > blockRequestTip->nHeight)
        {
            blockRequestTip = blockLastProcessed;
        }
    }

    std::vector<const CBlockIndex*> blocksToRequest;

    // add requests for as long as nMaxPendingRequests is not reached and there are still higher blocks in headerChain
    // In the special case of 'skipped' blocks we allow a higher restriction - as they don't represent real network processing so won't starve peers
    int nNumSkipped=0;
    {
        LOCK(partialChain.cs_blockFilterRanges);
        while (nRequestsPending < MAX_PENDING_REQUESTS && partialChain.Height() > blockRequestTip->nHeight)
        {
            blockRequestTip = partialChain.Next(blockRequestTip);
            if (CanSkipBlockFetch(blockRequestTip, Checkpoints::LastCheckPointHeight()))
            {
                ++nNumSkipped;
                LogPrint(BCLog::WALLET, "Skip block fetch [%d]\n", blockRequestTip->nHeight);
            }
            else
            {
                LogPrint(BCLog::WALLET, "Unable to skip block fetch [%d]\n", blockRequestTip->nHeight);
                blocksToRequest.push_back(blockRequestTip);
                nRequestsPending++;
            }
        }
    }

    if (!blocksToRequest.empty()) {
        LogPrint(BCLog::WALLET, "Requesting %d blocks for SPV, up to height %d\n", blocksToRequest.size(), blockRequestTip->nHeight);
        AddPriorityDownload(blocksToRequest, std::bind(&CSPVScanner::ProcessPriorityRequest, this, std::placeholders::_1, std::placeholders::_2));
    }
}

void CSPVScanner::ProcessPriorityRequest(const std::shared_ptr<const CBlock> &block, const CBlockIndex *pindex)
{
    LOCK2(cs_main, wallet.cs_wallet);

    nRequestsPending--;

    // if chainActive is up-to-date no SPV blocks need to be requested, we can update SPV to the activeChain
    if (chainActive.Tip() == partialChain.Tip()) {
        LogPrint(BCLog::WALLET, "chainActive is up-to-date, skipping SPV processing block %d\n", pindex->nHeight);
        if (blockLastProcessed != partialChain.Tip()) {
            UpdateLastProcessed(chainActive.Tip());
            blockRequestTip = blockLastProcessed;
        }
    }

    // The block we're getting might have skipped some because they were filtered out and so never requested.
    // Blocks are guaranteed to be delivered in requested order and therefore we can safely "fastforward" the blockLastProcessed
    // knowing that the blocks in between have never been requested.
    if (blockLastProcessed->nHeight < Checkpoints::LastCheckPointHeight() && pindex->pprev != blockLastProcessed) {
        CBlockIndex* pSkip = blockLastProcessed;
        while (pindex->pprev != pSkip && pSkip != nullptr) {
            pSkip = partialChain.Next(pSkip);
        }

        assert(pSkip != nullptr); // The block has a pprev that is not in partialChain, impossible that it was requested

        UpdateLastProcessed(pSkip);
    }

    if (pindex->pprev == blockLastProcessed) {
        LogPrint(BCLog::WALLET, "SPV processing block %d\n", pindex->nHeight);


        std::vector<CTransactionRef> vtxConflicted; // dummy for now
        wallet.BlockConnected(block, pindex, vtxConflicted);

        UpdateLastProcessed((CBlockIndex*)pindex);

        RequestBlocks();

        if (partialChain.Height() == pindex->nHeight || pindex->nHeight % UI_UPDATE_LIMIT == 0)
            uiInterface.NotifySPVProgress(startHeight, pindex->nHeight, partialChain.Height());

        NotifyUnifiedProgress();

        blocksSincePersist++;

        ExpireMempoolForPartialSync(block, blockLastProcessed);
    }
}

void CSPVScanner::HeaderTipChanged(const CBlockIndex* pTip)
{
    LOCK(cs_main);
    if (pTip)
    {
        // initialization on the first header tip notification
        if (blockLastProcessed == nullptr)
        {
            if (partialChain.Height() >= partialChain.HeightOffset()
                    && partialChain[partialChain.HeightOffset()]->GetBlockTime() <= startTime)
            {
                // use start of partial chain to init blockLastProcessed
                // forks are handled when requesting blocks which will also fast-forward to startTime
                // should the headerChain be very early
                blockLastProcessed = partialChain[partialChain.HeightOffset()];
                blockRequestTip = blockLastProcessed;
                startHeight = blockLastProcessed->nHeight;

                LogPrint(BCLog::WALLET, "SPV init using %s (height = %d) as last processed block\n",
                          blockLastProcessed->GetBlockHashPoW2().ToString(), blockLastProcessed->nHeight);
            }
            else
            {
                // headerChain not usable, it does not start early enough or has no data.
                // This should not happen, as StartPartialHeaders was explicitly given the startTime
                // so if this occurs it's a bug.
                throw std::runtime_error("partialChain not usable, starting late or too little data");
            }
        }

        RequestBlocks();

        NotifyUnifiedProgress();
    }
    else // pTip == nullptr => partial sync stopped
    {
        CancelAllPriorityDownloads();
        Persist();
    }
}

void CSPVScanner::OnNumConnectionsChanged(int newNumConnections)
{
    LOCK(cs_main);

    numConnections = newNumConnections;
    NotifyUnifiedProgress();
}

void CSPVScanner::ResetUnifiedProgressNotification()
{
    LOCK(cs_main);

    lastProgressReported = -1.0f;
    startHeight = -1;
    if (blockLastProcessed)
        startHeight = blockLastProcessed->nHeight;
    NotifyUnifiedProgress();
}

void CSPVScanner::NotifyUnifiedProgress()
{
    AssertLockHeld(cs_main);

    const float CONNECTION_WEIGHT = 0.05f;
    const float MIN_REPORTING_DELTA = 0.005f;

    float newProgress = 0.0f;

    // Only calculate progress if there are connections. Without connections progress is reported as zero
    // which is the only case where progress can decrease during a session (ie. if all connections are lost)
    if (numConnections > 0) {
        newProgress += CONNECTION_WEIGHT;

        int probableHeight = GetProbableHeight();

        if (probableHeight > 0 && startHeight >= 0 &&
            probableHeight != startHeight &&
                blockLastProcessed != nullptr && blockLastProcessed->nHeight > 0)
        {
            float pgs = (blockLastProcessed->nHeight - startHeight)/float(probableHeight - startHeight);
            newProgress += (1.0f - CONNECTION_WEIGHT) * pgs;
        }
        else if (probableHeight == startHeight)
            newProgress = 1.0f;

        // Limit progress notification to reduce overhead, only notify if delta since previous ntf is > MIN_REPORTING_DELTA
        if (lastProgressReported >= 0.0f && newProgress < 1.0f && fabs(newProgress - lastProgressReported) < MIN_REPORTING_DELTA)
                return;
        // else lastProgressReported < 0 => progress was reset/initialized so always notify
    }

    if (newProgress != lastProgressReported) {
        uiInterface.NotifyUnifiedProgress(newProgress);
        lastProgressReported = newProgress;
    }
}

void CSPVScanner::UpdateLastProcessed(CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);

    blockLastProcessed = pindex;

    lastProcessedBlockHeight = blockLastProcessed ? blockLastProcessed->nHeight : 0;

    int64_t now = GetAdjustedTime();
    if (now - lastPersistTime > PERSIST_INTERVAL_SEC || blocksSincePersist >= PERSIST_BLOCK_COUNT)
        Persist();
}

void CSPVScanner::Persist()
{
    LOCK(cs_main);

    if (blockLastProcessed != nullptr && blockLastProcessed->GetBlockTime() > lastPersistedBlockTime)
    {
        // persist & prune block index
        PersistAndPruneForPartialSync();

        // persist lastProcessed
        CWalletDB walletdb(*wallet.dbw);
        walletdb.WriteLastSPVBlockProcessed(partialChain.GetLocatorPoW2(blockLastProcessed), blockLastProcessed->GetBlockTime());

        // now that we are sure both the index and lastProcessed time locator have been saved
        // compute the new pruning height for the next iteration

        int64_t forkTimeLimit = blockLastProcessed->GetBlockTime() - 2 * MAX_FORK_DURATION;

        int maxPruneHeight =
                // determine oldest block that is at most forkTimeLimit in the past
                partialChain.LowerBound(partialChain.HeightOffset(),
                                        std::min(blockLastProcessed->nHeight, partialChain.Height()),
                                        forkTimeLimit,
                                        [](const CBlockIndex* index, int64_t limit){ return index->GetBlockTime() < limit; })
                // the block before that is the youngest that is more than forkTimeLimit ago
                - 1
                // the window required to do context checks on the headers
                - 576;

        if (maxPruneHeight > 0)
            SetMaxSPVPruneHeight(maxPruneHeight);

        lastPersistTime = GetAdjustedTime();
        lastPersistedBlockTime = blockLastProcessed->GetBlockTime();
        blocksSincePersist = 0;
    }
}

int CSPVScanner::getProcessedHeight()
{
    return lastProcessedBlockHeight;
}
