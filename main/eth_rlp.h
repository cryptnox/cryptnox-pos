/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file eth_rlp.h
 * @ingroup eth
 * @brief Minimal RLP encoder for EIP-1559 (type 2) Ethereum transactions.
 */

#ifndef ETH_RLP_H
#define ETH_RLP_H

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************
 * 2. Types
 ******************************************************************/

/** @brief EIP-1559 (type 2) transaction parameters. */
typedef struct {
    uint64_t       chain_id;          /**< EIP-155 chain identifier.            */
    uint64_t       nonce;             /**< Account transaction count.           */
    uint64_t       max_priority_fee;  /**< Priority fee per gas, in wei.        */
    uint64_t       max_fee;           /**< Max fee per gas, in wei.             */
    uint64_t       gas_limit;         /**< Gas limit for the transaction.       */
    uint8_t        to[20];            /**< Recipient Ethereum address.          */
    uint64_t       eth_value;         /**< ETH value in wei (0 for pure ERC-20
                                           transfer).                           */
    const uint8_t *calldata;          /**< ABI-encoded calldata, or NULL.       */
    size_t         calldata_len;      /**< Length of @ref calldata in bytes.    */
} eth_tx_t;

/******************************************************************
 * 3. Public API
 ******************************************************************/

/**
 * @brief Encode an unsigned EIP-1559 transaction:
 *        0x02 || RLP([chainId, nonce, ...]).
 *
 * @param[in]  tx      Transaction parameters.
 * @param[out] out     Output buffer for the encoded bytes.
 * @param[in]  out_max Capacity of @p out.
 * @return Total bytes written, or 0 if the output would overflow @p out_max
 *         or tx->calldata_len exceeds the internal scratch bound.
 */
size_t eth_rlp_encode_unsigned(const eth_tx_t *tx, uint8_t *out, size_t out_max);

/**
 * @brief Encode a signed EIP-1559 transaction:
 *        0x02 || RLP([..., v, r, s]).
 *
 * @param[in]  tx      Transaction parameters.
 * @param[in]  v       Signature parity bit; must be 0 or 1.
 * @param[in]  r       32-byte big-endian ECDSA r component.
 * @param[in]  s       32-byte big-endian ECDSA s component.
 * @param[out] out     Output buffer for the encoded bytes.
 * @param[in]  out_max Capacity of @p out.
 * @return Total bytes written, or 0 if the output would overflow @p out_max
 *         or tx->calldata_len exceeds the internal scratch bound.
 */
size_t eth_rlp_encode_signed(const eth_tx_t *tx, uint8_t v,
                              const uint8_t r[32], const uint8_t s[32],
                              uint8_t *out, size_t out_max);

#ifdef __cplusplus
}
#endif

#endif // ETH_RLP_H
