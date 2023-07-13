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

#include "chain.h"

/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex *pindex) {
    if (pindex == nullptr) {
        vChain.clear();
        vChain.shrink_to_fit();
        return;
    }
    vChain.resize(pindex->nHeight + 1);
    while (pindex && vChain[pindex->nHeight] != pindex) {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockLocator CChain::GetLocatorLegacy(const CBlockIndex *pindex) const {
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHashLegacy());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

CBlockLocator CChain::GetLocatorPoW2(const CBlockIndex *pindex) const {
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHashPoW2());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const {
    if (pindex == NULL) {
        return NULL;
    }
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

void CCloneChain::FreeMemory()
{
    //fixme: (POST-PHASE5) - We should use shared_ptr for the chain instead of raw pointers
    //This will remove the need for this messy vFree situation; and also allow us to re-enable the miner test that relied on nHeight++ that we have had to disable due to a crash in this FreeMemory call.
    for (auto index : vFree)
    {
        if (vChain[index->nHeight - cloneFrom] == index)
            vChain[index->nHeight - cloneFrom] = nullptr;
        delete index;
    }
    for (auto index : vChain)
    {
        if (index)
        {
            delete index;
        }
    }
    vFree.clear();
    vChain.clear();
}

CCloneChain::CCloneChain(const CChain& _origin, unsigned int _cloneFrom, const CBlockIndex *retainIndexIn, CBlockIndex *&retainIndexOut)
: CChain()
, origin(_origin)
, cloneFrom(_cloneFrom)
{
    //fixme: (POST-PHASE4) - Temporarily allow nested cloning 'getwitnessinfo' needs this; however we should fix this in the near future.
    // NB! Nested cloning is okay IFF we stick to a fixed clone height, the second we start trying to optimise by using a non-fixed clone height there will be problems.
    // forbid nested cloning
    //assert(dynamic_cast<const CCloneChain*>(&origin) == nullptr);

    assert(cloneFrom <= origin.Height());
    assert(cloneFrom >=0);

    vChain.reserve(origin.Height() + 1 - cloneFrom);
    vFree.reserve(origin.Height() + 1 - cloneFrom);

    CBlockIndex* pprev = nullptr;
    for (int i=cloneFrom; i<=origin.Height(); i++)
    {
        CBlockIndex* index = origin[i];
        vChain.emplace_back(new CBlockIndex(*index));
        vFree.push_back(vChain.back());
        if (pprev)
            vChain.back()->pprev = pprev;
        vChain.back()->pskip = nullptr;
        vChain.back()->BuildSkip();
        pprev = vChain.back();
        if (index == retainIndexIn)
            retainIndexOut = pprev;
    }

    // case where retainIndexIn is in the chain before the part we've cloned
    // you better know what you're doing!
    if (!retainIndexOut && origin.Contains(retainIndexIn))
        retainIndexOut = const_cast<CBlockIndex*>(retainIndexIn);

    if (retainIndexIn && !retainIndexOut)
    {
        // Link the pprev(s) of our new potential tip with a new pointer thats inside the cloned chain instead of the old pointer to the old chain.
        retainIndexOut = new CBlockIndex(*retainIndexIn);
        {
            //Don't worry about leaks, these become part of tempChain and are cleaned up along with tempChain.
            CBlockIndex* pNotInChain = retainIndexOut;
            // We might be sitting multiple blocks ahead of the chain, in which case we need to clone those blocks as well.
            while (pNotInChain->pprev->nHeight > Tip()->nHeight || pNotInChain->pprev->GetBlockHashPoW2() != vChain[pNotInChain->pprev->nHeight - cloneFrom]->GetBlockHashPoW2())
            {
                pNotInChain->pskip = nullptr;
                pNotInChain->pprev = new CBlockIndex(*pNotInChain->pprev);
                pNotInChain = pNotInChain->pprev;
            }
            while (pNotInChain->pprev && pNotInChain->pprev != vChain[pNotInChain->pprev->nHeight - cloneFrom])
            {
                pNotInChain->pskip = nullptr;
                pNotInChain->pprev = vChain[pNotInChain->pprev->nHeight - cloneFrom];
                pNotInChain = pNotInChain->pprev;
            }
        }
    }
}

CBlockIndex *CCloneChain::operator[](int nHeight) const
{
    if (nHeight >= cloneFrom && nHeight <= Height())
    {
        return vChain[nHeight - cloneFrom];
    }
    else
        return origin[nHeight];
}

int CCloneChain::Height() const
{
    return cloneFrom + vChain.size() - 1;
}

void CCloneChain::SetTip(CBlockIndex *pindex)
{
    // not allowed to modify origin chain
    assert(pindex != nullptr && pindex->nHeight >= cloneFrom);

    vChain.resize(pindex->nHeight - cloneFrom + 1);

    while (pindex && vChain[pindex->nHeight - cloneFrom] != pindex) {
        vChain[pindex->nHeight - cloneFrom] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockIndex* CChain::FindEarliestAtLeast(int64_t nTime) const
{
    std::vector<CBlockIndex*>::const_iterator lower = std::lower_bound(vChain.begin(), vChain.end(), nTime,
        [](CBlockIndex* pBlock, const int64_t& time) -> bool { return pBlock->GetBlockTimeMax() < time; });
    return (lower == vChain.end() ? nullptr : *lower);
}

/** Turn the lowest '1' bit in the binary representation of a number into a '0'. */
int static inline InvertLowestOne(int n) { return n & (n - 1); }

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height) {
    if (height < 2)
        return 0;

    // Determine which height to jump back to. Any number strictly lower than height is acceptable,
    // but the following expression seems to perform well in simulations (max 110 steps to go back
    // up to 2**18 blocks).
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1 : InvertLowestOne(height);
}

CBlockIndex* CBlockIndex::GetAncestor(int height)
{
    if (height > (int)nHeight || height < 0)
        return NULL;

    CBlockIndex* pindexWalk = this;
    int heightWalk = nHeight;
    while (heightWalk > height) {
        int heightSkip = GetSkipHeight(heightWalk);
        int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip != NULL &&
            (heightSkip == height ||
             (heightSkip > height && !(heightSkipPrev < heightSkip - 2 &&
                                       heightSkipPrev >= height)))) {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        }
        else if (pindexWalk->pprev)
        {
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
        else
            return nullptr;
    }
    return pindexWalk;
}

const CBlockIndex* CBlockIndex::GetAncestor(int height) const
{
    return const_cast<CBlockIndex*>(this)->GetAncestor(height);
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}

arith_uint256 GetBlockProof(const CBlockIndex& block)
{
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a arith_uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}

const CBlockIndex* LastCommonAncestor(const CBlockIndex* pa, const CBlockIndex* pb)
{
    if (pa->nHeight > pb->nHeight)
    {
        pa = pa->GetAncestor(pb->nHeight);
    }
    else if (pb->nHeight > pa->nHeight)
    {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb)
    {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}

int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params& params)
{
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nPowTargetSpacing) / GetBlockProof(tip);
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}

CPartialChain::CPartialChain() :
    nHeightOffset(0)
{

}

void CPartialChain::SetHeightOffset(int offset)
{
    // changing the offset is only allowed if the chain is empty
    assert(vChain.empty());

    nHeightOffset = offset;
}

int CPartialChain::HeightOffset() const
{
    return nHeightOffset;
}

int CPartialChain::Length() const
{
    return vChain.size();
}

CBlockIndex *CPartialChain::operator[](int nHeight) const
{
    if (nHeight>= nHeightOffset && nHeight <= Height())
        return vChain[nHeight - nHeightOffset];
    else
        return nullptr;
}

int CPartialChain::Height() const
{
    return vChain.size() + nHeightOffset - 1;
}

void CPartialChain::SetTip(CBlockIndex *pindex)
{
    if (pindex == nullptr) {
        vChain.clear();
        vChain.shrink_to_fit();
        return;
    }

    assert(pindex->nHeight >= nHeightOffset);

    vChain.resize(pindex->nHeight - nHeightOffset + 1);

    while (pindex && pindex->nHeight >= nHeightOffset && vChain[pindex->nHeight - nHeightOffset] != pindex) {
        vChain[pindex->nHeight - nHeightOffset] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockLocator CPartialChain::GetLocatorPoW2(const CBlockIndex *pindex) const
{
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHashPoW2());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == nHeightOffset)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, nHeightOffset);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

