// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2019-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#ifndef CHECKPOINTS_H
#define CHECKPOINTS_H

#include "chainparams.h"

class CBlockIndex;

/**
 * Block-chain checkpoints are compiled-in sanity checks.
 * They are updated every release or three.
 */
namespace Checkpoints
{

//! Returns last CBlockIndex* in mapBlockIndex that is a checkpoint in Params()
CBlockIndex* GetLastCheckpointIndex();

//! Height of last checkpoint in Params()
int LastCheckPointHeight();

/** Last checkpoint with block height before or at blockHeight.
    Returns height of the checkpoint, or -1 iof there is none. */
int LastCheckpointBeforeBlock(uint64_t blockHeight);

/** Last checkpoint with timestamp before or at atTime.
    Returns height of the checkpoint, or -1 iof there is none. */
int LastCheckpointBeforeTime(uint64_t atTime);

} //namespace Checkpoints

#endif
