// Copyright (c) 2015-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING
#include "diff_common.h"
#include "diff_delta.h"
#include "diff_old.h"
#include "../../chainparams.h"

extern const int32_t nMaxHeight;
unsigned int GetNextWorkRequired(const CBlockIndex* indexLast, const CBlockHeader* block, unsigned int nPowTargetSpacing, unsigned int nPowLimit)
{
    if (Params().GetConsensus().fPowNoRetargeting)
        return indexLast->nBits;

    static int nDeltaSwitchoverBlock = 0;
    static int nOldDiffSwitchoverBlock = DIFF_SWITCHOVER(0, 393218);

    if ((indexLast->nHeight+1) >= nOldDiffSwitchoverBlock)
    {
        // Delta can't handle a target spacing of 1 - so we just assume a static diff for testnets where target spacing is 1.
        if (nPowTargetSpacing>1 && (indexLast->nHeight+1) >= nDeltaSwitchoverBlock)
        {
            return GetNextWorkRequired_DELTA(indexLast, block, nPowTargetSpacing, nPowLimit, nDeltaSwitchoverBlock);
        }
        else
        {
            return nPowLimit;
        }
    }
    return diff_old(indexLast->nHeight+1, nPowLimit);
}
