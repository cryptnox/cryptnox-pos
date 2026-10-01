/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file keccak256.h
 * @ingroup eth
 * @brief Original Keccak-256 digest as used by Ethereum.
 */

#ifndef KECCAK256_H
#define KECCAK256_H

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************
 * 2. Public API
 ******************************************************************/

/**
 * @brief Compute the Keccak-256 digest of a buffer.
 *
 * Ethereum uses the original Keccak-256 (0x01 padding), NOT the
 * NIST-finalised SHA3-256 (0x06 padding).
 *
 * @param[in]  input  Input bytes (may be NULL only if @p length is 0).
 * @param[in]  length Number of input bytes.
 * @param[out] digest 32-byte output digest.
 */
void keccak256(const uint8_t *input, size_t length, uint8_t digest[32]);

#ifdef __cplusplus
}
#endif

#endif // KECCAK256_H
