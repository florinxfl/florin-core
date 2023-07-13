// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#include "checkpoints.h"

#include "chain.h"
#include "chainparams.h"
#include "validation/validation.h"
#include "uint256.h"

#include <boost/range/adaptor/reversed.hpp>
#include <stdint.h>
#include <boost/range/rbegin.hpp>

namespace Checkpoints {

    /**
     * Last checkpoint present in the BlockIndex which connects to the full tree
     * So checkpoints in the partial tree which don't connect are ignored
    */
    CBlockIndex* GetLastCheckpointIndex()
    {
        for (const auto& i: boost::adaptors::reverse(Params().Checkpoints()))
        {
            BlockMap::const_iterator t = mapBlockIndex.find(i.second.hash);
            if (t != mapBlockIndex.end() && t->second->IsValid(BLOCK_VALID_TREE))
            {
                return t->second;
            }
        }
        return nullptr;
    }

    int LastCheckPointHeight()
    {
        if (Params().Checkpoints().size() > 0)
        {
            auto lastCheckpoint = Params().Checkpoints().rbegin();
            return lastCheckpoint->first;
        }
        else
        {
            return 0;
        }
    }

    int LastCheckpointBeforeBlock(uint64_t blockHeight)
    {
        for (const auto& i: boost::adaptors::reverse(Params().Checkpoints()))
        {
            if ((uint64_t)i.first <= blockHeight)
            {
                return i.first;
            }
        }
        return -1;
    }

    int LastCheckpointBeforeTime(uint64_t atTime)
    {
        for (const auto& i: boost::adaptors::reverse(Params().Checkpoints()))
        {
            if ((uint64_t)i.second.nTime <= atTime)
            {
                return i.first;
            }
        }
        return -1;
    }

} // namespace Checkpoints
