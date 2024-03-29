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
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#ifndef BLAKE_ROUND_MKA_OPT_SSE3_H
#define BLAKE_ROUND_MKA_OPT_SSE3_H

#include <compat/arch.h>
#ifdef ARCH_CPU_X86_FAMILY // Only x86 family CPUs have SSE3

#include "blake2-impl.h"
#include "compat.h"

#include <emmintrin.h>
#include <tmmintrin.h> /* for _mm_shuffle_epi8 and _mm_alignr_epi8 */


#define r16 (_mm_setr_epi8(2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9))
#define r24 (_mm_setr_epi8(3, 4, 5, 6, 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10))
#define _mm_roti_epi64(x, c)                                                   \
    (-(c) == 32)                                                               \
        ? _mm_shuffle_epi32((x), _MM_SHUFFLE(2, 3, 0, 1))                      \
        : (-(c) == 24)                                                         \
              ? _mm_shuffle_epi8((x), r24)                                     \
              : (-(c) == 16)                                                   \
                    ? _mm_shuffle_epi8((x), r16)                               \
                    : (-(c) == 63)                                             \
                          ? _mm_xor_si128(_mm_srli_epi64((x), -(c)),           \
                                          _mm_add_epi64((x), (x)))             \
                          : _mm_xor_si128(_mm_srli_epi64((x), -(c)),           \
                                          _mm_slli_epi64((x), 64 - (-(c))))


static BLAKE2_INLINE __m128i fBlaMka_SSE3(__m128i x, __m128i y)
{
    const __m128i z = _mm_mul_epu32(x, y);
    return _mm_add_epi64(_mm_add_epi64(x, y), _mm_add_epi64(z, z));
}

#define G1(A0, B0, C0, D0, A1, B1, C1, D1)                                 \
do {                                                                       \
    A0 = fBlaMka_SSE3(A0, B0);                                             \
    A1 = fBlaMka_SSE3(A1, B1);                                             \
    D0 = _mm_xor_si128(D0, A0);                                            \
    D1 = _mm_xor_si128(D1, A1);                                            \
    D0 = _mm_roti_epi64(D0, -32);                                          \
    D1 = _mm_roti_epi64(D1, -32);                                          \
    C0 = fBlaMka_SSE3(C0, D0);                                             \
    C1 = fBlaMka_SSE3(C1, D1);                                             \
    B0 = _mm_xor_si128(B0, C0);                                            \
    B1 = _mm_xor_si128(B1, C1);                                            \
    B0 = _mm_roti_epi64(B0, -24);                                          \
    B1 = _mm_roti_epi64(B1, -24);                                          \
} while ((void)0, 0)

#define G2(A0, B0, C0, D0, A1, B1, C1, D1)                                 \
do {                                                                       \
    A0 = fBlaMka_SSE3(A0, B0);                                             \
    A1 = fBlaMka_SSE3(A1, B1);                                             \
    D0 = _mm_xor_si128(D0, A0);                                            \
    D1 = _mm_xor_si128(D1, A1);                                            \
    D0 = _mm_roti_epi64(D0, -16);                                          \
    D1 = _mm_roti_epi64(D1, -16);                                          \
    C0 = fBlaMka_SSE3(C0, D0);                                             \
    C1 = fBlaMka_SSE3(C1, D1);                                             \
    B0 = _mm_xor_si128(B0, C0);                                            \
    B1 = _mm_xor_si128(B1, C1);                                            \
    B0 = _mm_roti_epi64(B0, -63);                                          \
    B1 = _mm_roti_epi64(B1, -63);                                          \
} while ((void)0, 0)

#define DIAGONALIZE(A0, B0, C0, D0, A1, B1, C1, D1)                        \
do {                                                                       \
    __m128i t0 = _mm_alignr_epi8(B1, B0, 8);                               \
    __m128i t1 = _mm_alignr_epi8(B0, B1, 8);                               \
    B0 = t0;                                                               \
    B1 = t1;                                                               \
    t0 = C0;                                                               \
    C0 = C1;                                                               \
    C1 = t0;                                                               \
    t0 = _mm_alignr_epi8(D1, D0, 8);                                       \
    t1 = _mm_alignr_epi8(D0, D1, 8);                                       \
    D0 = t1;                                                               \
    D1 = t0;                                                               \
} while ((void)0, 0)

#define UNDIAGONALIZE(A0, B0, C0, D0, A1, B1, C1, D1)                      \
do {                                                                       \
    __m128i t0 = _mm_alignr_epi8(B0, B1, 8);                               \
    __m128i t1 = _mm_alignr_epi8(B1, B0, 8);                               \
    B0 = t0;                                                               \
    B1 = t1;                                                               \
    t0 = C0;                                                               \
    C0 = C1;                                                               \
    C1 = t0;                                                               \
    t0 = _mm_alignr_epi8(D0, D1, 8);                                       \
    t1 = _mm_alignr_epi8(D1, D0, 8);                                       \
    D0 = t1;                                                               \
    D1 = t0;                                                               \
} while ((void)0, 0)

#define BLAKE2_ROUND_SSE3(A0, A1, B0, B1, C0, C1, D0, D1)                  \
do {                                                                       \
    G1(A0, B0, C0, D0, A1, B1, C1, D1);                                    \
    G2(A0, B0, C0, D0, A1, B1, C1, D1);                                    \
    DIAGONALIZE(A0, B0, C0, D0, A1, B1, C1, D1);                           \
    G1(A0, B0, C0, D0, A1, B1, C1, D1);                                    \
    G2(A0, B0, C0, D0, A1, B1, C1, D1);                                    \
    UNDIAGONALIZE(A0, B0, C0, D0, A1, B1, C1, D1);                         \
} while ((void)0, 0)

#endif
#endif
