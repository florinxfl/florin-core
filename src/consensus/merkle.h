// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2017-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#ifndef CONSENSUS_MERKLE_H
#define CONSENSUS_MERKLE_H

#include <stdint.h>
#include <vector>

#include "primitives/transaction.h"
#include "primitives/block.h"
#include "uint256.h"

uint256 ComputeMerkleRoot(const std::vector<uint256>& leaves, bool* mutated = NULL);
std::vector<uint256> ComputeMerkleBranch(const std::vector<uint256>& leaves, uint32_t position);
uint256 ComputeMerkleRootFromBranch(const uint256& leaf, const std::vector<uint256>& branch, uint32_t position);

/*
 * Compute the Merkle root of the transactions in a block.
 * *mutated is set to true if a duplicated subtree was found.
 */
uint256 BlockMerkleRoot(const std::vector<CTransactionRef>::const_iterator txStartIter, const std::vector<CTransactionRef>::const_iterator txEndIter, bool* mutated = NULL);
uint256 BlockMerkleRoot(const CBlock& block, bool* mutated = NULL);

/*
 * Compute the Merkle branch for the tree of transactions in a block, for a
 * given position.
 * This can be verified using ComputeMerkleRootFromBranch.
 */
std::vector<uint256> BlockMerkleBranch(const CBlock& block, uint32_t position);

#endif
