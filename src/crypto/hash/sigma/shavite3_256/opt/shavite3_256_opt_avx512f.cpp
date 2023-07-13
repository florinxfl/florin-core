// Copyright (c) 2019-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

// This file is a thin wrapper around the actual 'shavite3_256_aesni_opt' implementation, along with various other similarly named files.
// The build system compiles each file with slightly different optimisation flags so that we have optimised implementations for a wide spread of processors.

#if defined(COMPILER_HAS_AVX512F)
    #define shavite3_256_opt_Init        shavite3_256_opt_avx512f_Init
    #define shavite3_256_opt_Update      shavite3_256_opt_avx512f_Update
    #define shavite3_256_opt_Final       shavite3_256_opt_avx512f_Final
    #define shavite3_256_opt_UpdateFinal shavite3_256_opt_avx512f_UpdateFinal
    #define shavite3_256_opt_Compress256 shavite3_256_opt_avx512f_Compress256

    #define SHAVITE3_256_OPT_IMPL
    #include "../shavite3_256_opt.cpp"
#endif
