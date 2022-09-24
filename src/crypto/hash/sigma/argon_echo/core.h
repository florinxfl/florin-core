/*
 * Argon2 reference source code package - reference C implementations
 *
 * Copyright 2015
 * Daniel Dinu, Dmitry Khovratovich, Jean-Philippe Aumasson, and Samuel Neves
 *
 * You may use this work under the terms of a Creative Commons CC0 1.0
 * License/Waiver or the Apache Public License 2.0, at your option. The terms of
 * these licenses can be found at:
 *
 * - CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
 * - Apache 2.0        : http://www.apache.org/licenses/LICENSE-2.0
 *
 * You should have received a copy of both of these licenses along with this
 * software. If not, they may be obtained at the above URLs.
 */
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2019-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the Libre Chain License, see the accompanying
// file COPYING

#ifndef ARGON2_CORE_OPT_IMPL
#ifndef ARGON2_ECHO_CORE_H
#define ARGON2_ECHO_CORE_H

#include "argon_echo.h"

/**********************Argon2 internal constants*******************************/

enum argon2_echo_core_constants
{
    /* Memory block size in bytes */
    ARGON2_BLOCK_SIZE = 1024,
    ARGON2_QWORDS_IN_BLOCK = ARGON2_BLOCK_SIZE / 8,
    ARGON2_OWORDS_IN_BLOCK = ARGON2_BLOCK_SIZE / 16,
    ARGON2_HWORDS_IN_BLOCK = ARGON2_BLOCK_SIZE / 32,
    ARGON2_512BIT_WORDS_IN_BLOCK = ARGON2_BLOCK_SIZE / 64,

    /* Number of pseudo-random values generated by one call to Blake in Argon2i to generate reference block positions */
    ARGON2_ADDRESSES_IN_BLOCK = 128,

    /* Pre-hashing digest length and its extension*/
    ARGON2_PREHASH_DIGEST_LENGTH = 64,
    ARGON2_PREHASH_SEED_LENGTH = 72
};

/*************************Argon2 internal data types***********************/

/*
 * Structure for the (1KB) memory block implemented as 128 64-bit words.
 * Memory blocks can be copied, XORed. Internal words can be accessed by [] (no
 * bounds checking).
 */
struct argon2_echo_block
{
    uint64_t v[ARGON2_QWORDS_IN_BLOCK];
};

/*
 * Argon2 instance: memory pointer, number of passes, amount of memory,
 * and derived values.
 * Used to evaluate the number and location of blocks to construct in each
 * thread
 */
struct argon2_echo_instance_t
{
    argon2_echo_block* memory;        /* Memory pointer */
    uint32_t passes;                  /* Number of passes */
    uint32_t memory_blocks;           /* Number of blocks in memory */
    uint32_t segment_length;
    uint32_t lane_length;
    uint32_t lanes;
    uint32_t threads;
    int print_internals;              /* whether to print the memory blocks */
    argon2_echo_context* context_ptr; /* points back to original context */
};

/*
 * Argon2 position: where we construct the block right now. Used to distribute
 * work between threads.
 */
struct argon2_echo_position_t
{
    uint32_t pass=0;
    uint32_t lane;
    uint8_t slice;
    uint32_t index;
};

/*Struct that holds the inputs for thread handling FillSegment*/
struct argon2_echo_thread_data
{
    argon2_echo_instance_t *instance_ptr;
    argon2_echo_position_t pos;
};

#endif
#endif

/*****************Functions that work with the block******************/

/* Initialize each byte of the block with @in */
void init_block_value(argon2_echo_block* b, uint8_t in);

/* Copy block @src to block @dst */
void copy_block(argon2_echo_block* dst, const argon2_echo_block* src);

/* XOR @src onto @dst bytewise */
void xor_block(argon2_echo_block* dst, const argon2_echo_block* src);

/*************************Argon2_echo core functions********************************/
/*
 * Computes absolute position of reference block in the lane following a skewed
 * distribution and using a pseudo-random value as input
 * @param instance Pointer to the current instance
 * @param position Pointer to the current position
 * @param pseudo_rand 32-bit pseudo-random value used to determine the position
 * @param same_lane Indicates if the block will be taken from the current lane.
 * If so we can reference the current segment
 * @pre All pointers must be valid
 */
uint32_t index_alpha(const argon2_echo_instance_t* instance, const argon2_echo_position_t* position, uint32_t pseudo_rand, int same_lane);

/*
 * Function that validates all inputs against predefined restrictions and return
 * an error code
 * @param context Pointer to current Argon2 context
 * @return ARGON2_OK if everything is all right, otherwise one of error codes
 * (all defined in <argon2.h>
 */
int validate_inputs(const argon2_echo_context* context);

/*
 * Hashes all the inputs into @a blockhash[PREHASH_DIGEST_LENGTH], clears
 * password and secret if needed
 * @param  context  Pointer to the Argon2 internal structure containing memory
 * pointer, and parameters for time and space requirements.
 * @param  blockhash Buffer for pre-hashing digest
  * @pre    @a blockhash must have at least @a PREHASH_DIGEST_LENGTH bytes
 * allocated
 */
void initial_hash(uint8_t* blockhash, argon2_echo_context* context);

/*
 * Function creates first 2 blocks per lane
 * @param instance Pointer to the current instance
 * @param blockhash Pointer to the pre-hashing digest
 * @pre blockhash must point to @a PREHASH_SEED_LENGTH allocated values
 */
void fill_first_blocks(uint8_t* blockhash, const argon2_echo_instance_t* instance);

/*
 * Function allocates memory, hashes the inputs with Blake,  and creates first
 * two blocks. Returns the pointer to the main memory with 2 blocks per lane
 * initialized
 * @param  context  Pointer to the Argon2 internal structure containing memory
 * pointer, and parameters for time and space requirements.
 * @param  instance Current Argon2 instance
 * @return Zero if successful, -1 if memory failed to allocate. @context->state
 * will be modified if successful.
 */
int initialize(argon2_echo_instance_t* instance, argon2_echo_context* context);

/*
 * XORing the last block of each lane, hashing it, making the tag. Deallocates
 * the memory.
 * @param context Pointer to current Argon2 context (use only the out parameters
 * from it)
 * @param instance Pointer to current instance of Argon2
 * @pre instance->state must point to necessary amount of memory
 * @pre if context->free_cbk is not NULL, it should point to a function that
 * deallocates memory
 */
void finalize(const argon2_echo_context* context, argon2_echo_instance_t* instance);

/*
 * Function that fills the segment using previous segments also from other
 * threads
 * @param context current context
 * @param instance Pointer to the current instance
 * @param position Current position
 * @pre all block pointers must be valid
 */
void fill_segment(const argon2_echo_instance_t* instance, argon2_echo_position_t position);

/*
 * Function that fills the entire memory t_cost times based on the first two
 * blocks in each lane
 * @param instance Pointer to the current instance
 * @return ARGON2_OK if successful, @context->state
 */
int fill_memory_blocks(argon2_echo_instance_t* instance);
