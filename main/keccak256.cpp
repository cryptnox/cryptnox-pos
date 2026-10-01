/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file keccak256.cpp
 * @brief Original (pre-NIST) Keccak-256 implementation, 0x01 padding.
 */

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "keccak256.h"
#include <string.h>

#include "CW_Utils.h"   /* hardened memory primitives (CODING_RULES §1.4) */

/******************************************************************
 * 2. Constants
 ******************************************************************/

#define KECCAK_ROUNDS     24U
#define KECCAK_RATE       136U   /* 1088 / 8 bytes — rate for 256-bit output */
#define KECCAK_STATE_LANE 25U    /* 5×5 uint64_t lanes                       */

static const uint64_t kRC[KECCAK_ROUNDS] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808AULL, 0x8000000080008000ULL,
    0x000000000000808BULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008AULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000AULL,
    0x000000008000808BULL, 0x800000000000008BULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800AULL, 0x800000008000000AULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL,
};

/* Rho rotation offsets for lane [x + 5*y], derived from the Keccak spec. */
static const uint8_t kRHO[KECCAK_STATE_LANE] = {
     0,  1, 62, 28, 27,   /* y=0 */
    36, 44,  6, 55, 20,   /* y=1 */
     3, 10, 43, 25, 39,   /* y=2 */
    41, 45, 15, 21,  8,   /* y=3 */
    18,  2, 61, 56, 14    /* y=4 */
};

/******************************************************************
 * 3. Internal helpers
 ******************************************************************/

/**
 * @brief Rotate a 64-bit lane left by @p n bits.
 *
 * @param[in] x Lane value.
 * @param[in] n Rotation amount, 0-63.
 * @return Rotated value.
 */
static uint64_t rot64(uint64_t x, uint8_t n)
{
    return (n == 0U) ? x : ((x << n) | (x >> (64U - n)));
}

/**
 * @brief Apply the full 24-round Keccak-f[1600] permutation in place.
 *
 * @param[in,out] st 5x5 lane state, modified in place.
 */
static void keccak_f1600(uint64_t st[KECCAK_STATE_LANE])
{
    uint64_t C[5], D[5], B[KECCAK_STATE_LANE];
    unsigned int round, x, y;

    for (round = 0U; round < KECCAK_ROUNDS; round++) {
        /* Theta */
        for (x = 0U; x < 5U; x++) {
            C[x] = st[x] ^ st[x + 5U] ^ st[x + 10U] ^ st[x + 15U] ^ st[x + 20U];
        }
        for (x = 0U; x < 5U; x++) {
            D[x] = C[(x + 4U) % 5U] ^ rot64(C[(x + 1U) % 5U], 1U);
        }
        for (x = 0U; x < 5U; x++) {
            for (y = 0U; y < 5U; y++) {
                st[x + 5U * y] ^= D[x];
            }
        }

        /* Rho + Pi: B[dst] = ROT(st[src], rho[src]) */
        for (x = 0U; x < 5U; x++) {
            for (y = 0U; y < 5U; y++) {
                unsigned int src   = x + 5U * y;
                unsigned int dst_x = y;
                unsigned int dst_y = (2U * x + 3U * y) % 5U;
                B[dst_x + 5U * dst_y] = rot64(st[src], kRHO[src]);
            }
        }

        /* Chi */
        for (x = 0U; x < 5U; x++) {
            for (y = 0U; y < 5U; y++) {
                st[x + 5U * y] = B[x + 5U * y] ^
                                  ((~B[(x + 1U) % 5U + 5U * y]) &
                                     B[(x + 2U) % 5U + 5U * y]);
            }
        }

        /* Iota */
        st[0] ^= kRC[round];
    }
}

/******************************************************************
 * 4. Public API
 ******************************************************************/

void keccak256(const uint8_t *input, size_t length, uint8_t digest[32])
{
    uint64_t state[KECCAK_STATE_LANE];
    uint8_t *sb = reinterpret_cast<uint8_t *>(state);
    size_t offset = 0U;
    size_t i;

    CW_Utils::secure_wipe(sb, sizeof(state));

    /* Absorb full blocks */
    while ((length - offset) >= KECCAK_RATE) {
        for (i = 0U; i < KECCAK_RATE; i++) {
            sb[i] ^= input[offset + i];
        }
        keccak_f1600(state);
        offset += KECCAK_RATE;
    }

    /* Absorb remaining bytes */
    size_t rem = length - offset;
    for (i = 0U; i < rem; i++) {
        sb[i] ^= input[offset + i];
    }

    /* Pad: 0x01 for Ethereum Keccak (pre-NIST), 0x80 at end of rate block */
    sb[rem]              ^= 0x01U;
    sb[KECCAK_RATE - 1U] ^= 0x80U;

    keccak_f1600(state);

    /* Squeeze first 32 bytes */
    (void)CW_Utils::safe_memcpy(digest, 32U, sb, 32U);
}
