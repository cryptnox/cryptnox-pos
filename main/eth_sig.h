/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file eth_sig.h
 * @ingroup ethereum
 * @brief The recovery bit of a secp256k1 signature, computed on the device.
 */

#ifndef ETH_SIG_H
#define ETH_SIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Verify (r, s) over @p hash against @p pub64 and return its y-parity.
 *
 * The card signs but does not say which of the two candidate points it used,
 * and an Ethereum transaction has to carry that bit (v). It used to come from the
 * RPC node: two eth_calls to the ecrecover precompile, compared against the
 * card's address — a network round-trip per sale, and a node's word on it. The
 * card's public key is already in hand, so this does the verification locally:
 * R = (e/s)·G + (r/s)·Q, check R.x ≡ r (mod n), and v is the parity of R.y.
 *
 * Refuses the rare signature whose R.x is at or above the group order (recovery
 * ids 2 and 3), which an Ethereum transaction cannot express.
 *
 * @param[in]  hash  32-byte message digest that was signed.
 * @param[in]  r     32-byte big-endian r.
 * @param[in]  s     32-byte big-endian s.
 * @param[in]  pub64 Signer's uncompressed public key, X || Y (no 0x04).
 * @param[out] v_out 0 or 1 on success; untouched otherwise.
 * @return true if the signature verifies under @p pub64; false otherwise —
 *         including a key that is not on the curve.
 */
bool eth_sig_parity(const uint8_t hash[32], const uint8_t r[32],
                    const uint8_t s[32], const uint8_t pub64[64],
                    uint8_t *v_out);

#ifdef __cplusplus
}
#endif

#endif /* ETH_SIG_H */
