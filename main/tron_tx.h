/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file tron_tx.h
 * @ingroup tron
 * @brief Tron transaction hex helpers — protobuf varints, the TransferContract
 *        integrity check and the signed-Transaction envelope.
 *
 * Pure unit: string/hex work only, no IDF, no networking, no globals — so it
 * builds and self-checks on the host (see tests/units/test_tron_tx.cpp), the
 * same pattern as eth_json.cpp and civil_time.cpp.
 *
 * Why a check at all: a Tron terminal does not serialise its own transaction,
 * it asks a full node to (@c /wallet/createtransaction) and signs the txID the
 * node hands back. That makes the node a trust boundary — it could return a
 * transaction paying somebody else. @ref tron_tx_contract_ok and
 * @ref tron_tx_trc20_ok walk raw_data field by field, require exactly one
 * contract whose value is byte-for-byte the one we asked for, and refuse any
 * other field (a memo, extra authorities, a second contract), so a hostile or
 * buggy node cannot redirect funds.
 */

#ifndef TRON_TX_H
#define TRON_TX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Hex length of a Tron address (21 bytes: 0x41 || 20-byte key hash). */
#define TRON_ADDR_HEX_LEN  42U

/** @brief Hex length of a Tron signature (r || s || v). */
#define TRON_SIG_HEX_LEN   130U

/** @brief TRC-20 @c transfer(address,uint256) selector, hex (same as ERC-20). */
#define TRON_TRC20_SELECTOR       "a9059cbb"

/** @brief Hex length of the two ABI argument words that follow the selector. */
#define TRON_TRC20_PARAM_HEX_LEN  128U

/** @brief Latest raw_data.expiration accepted, relative to now (15 min). A real
 *         node answer expires ~60 s out; see raw_ok() in tron_tx.cpp. */
#define TRON_TX_MAX_EXPIRY_MS     (15ULL * 60ULL * 1000ULL)

/**
 * @brief Hex-encode a value as a protobuf base-128 varint.
 *
 * @param[out] out      Destination, NUL-terminated on success (>= 21 bytes is
 *                      always enough).
 * @param[in]  out_size Capacity of @p out.
 * @param[in]  v        Value to encode.
 * @return number of hex characters written (excluding the NUL), 0 if @p out is
 *         NULL or too small.
 */
size_t tron_varint_hex(uint64_t v, char *out, size_t out_size);

/**
 * @brief Verify that a node-serialised raw_data really transfers what we asked.
 *
 * Parses @p raw_data_hex as a @c Transaction.raw protobuf: exactly one
 * @c TransferContract whose value is exactly owner (field 1), to (field 2) and
 * amount (field 3); only the reference-block, expiration and timestamp fields
 * besides it; no fee_limit, memo, authorities or scripts; no repeated field.
 * Fails closed — the caller must decline the payment rather than sign it.
 *
 * @param[in]  raw_data_hex  Hex of the node's raw_data protobuf.
 * @param[in]  owner_hex     Sender, #TRON_ADDR_HEX_LEN hex chars, "41"-prefixed.
 * @param[in]  to_hex        Recipient, same form.
 * @param[in]  amount_sun    Amount in sun (1 TRX = 1e6 sun).
 * @param[in]  now_ms        Current Unix time in ms; must be non-zero.
 * @param[out] expiration_ms raw_data.expiration on success; may be NULL.
 * @return true only if raw_data is exactly the requested transfer and expires
 *         within #TRON_TX_MAX_EXPIRY_MS of @p now_ms.
 */
bool tron_tx_contract_ok(const char *raw_data_hex, const char *owner_hex,
                         const char *to_hex, uint64_t amount_sun,
                         uint64_t now_ms, uint64_t *expiration_ms);

/**
 * @brief ABI-encode the arguments of @c transfer(address,uint256), hex.
 *
 * Two 32-byte words, no selector: the recipient left-padded (its @c 0x41 Tron
 * prefix dropped — the ABI address word holds the bare 20-byte key hash) and
 * the amount big-endian. This is exactly what the HTTP API's @c parameter field
 * wants, and exactly what must reappear inside the node's raw_data.
 *
 * @param[in]  to_hex   Recipient, #TRON_ADDR_HEX_LEN hex chars, "41"-prefixed.
 * @param[in]  amount   Token amount in base units.
 * @param[out] out      Destination, NUL-terminated on success.
 * @param[in]  out_size Capacity of @p out (>= #TRON_TRC20_PARAM_HEX_LEN + 1).
 * @return #TRON_TRC20_PARAM_HEX_LEN on success, 0 on bad input or overflow.
 */
size_t tron_trc20_param_hex(const char *to_hex, uint64_t amount,
                            char *out, size_t out_size);

/**
 * @brief Verify that a node-serialised raw_data really is the TRC-20 transfer
 *        we asked for.
 *
 * The TRX sibling of this check is @ref tron_tx_contract_ok, with the same
 * strict parse; a token transfer is a @c TriggerSmartContract instead, whose
 * value must be exactly (call_value 0 may be explicit):
 *
 * @code 0a15 owner | 1215 contract | 2244 a9059cbb | to-word | amount-word @endcode
 *
 * The contract address decides *which asset* moves. The @c fee_limit (raw_data
 * field 18) must be present and equal to @p fee_limit_sun: it caps the TRX a
 * failed call may burn.
 *
 * @param[in] raw_data_hex  Hex of the node's raw_data protobuf.
 * @param[in] owner_hex     Sender, #TRON_ADDR_HEX_LEN hex chars, "41"-prefixed.
 * @param[in] contract_hex  Token contract, same form.
 * @param[in] to_hex        Recipient, same form.
 * @param[in] amount        Token amount in base units; must be non-zero.
 * @param[in] fee_limit_sun Fee cap in sun, as requested; must be non-zero.
 * @param[in] now_ms        Current Unix time in ms; must be non-zero.
 * @param[out] expiration_ms raw_data.expiration on success; may be NULL.
 * @return true only if raw_data is exactly the requested call.
 */
bool tron_tx_trc20_ok(const char *raw_data_hex, const char *owner_hex,
                      const char *contract_hex, const char *to_hex,
                      uint64_t amount, uint64_t fee_limit_sun,
                      uint64_t now_ms, uint64_t *expiration_ms);

/**
 * @brief Build the hex of a signed @c Transaction protobuf for broadcasthex.
 *
 * @code Transaction { 1: raw_data (bytes) , 2: signature (repeated bytes) } @endcode
 *
 * @param[in]  raw_data_hex Hex of the raw_data protobuf (even length).
 * @param[in]  sig_hex      Signature hex, exactly #TRON_SIG_HEX_LEN chars.
 * @param[out] out          Destination, NUL-terminated on success.
 * @param[in]  out_size     Capacity of @p out.
 * @return number of hex characters written, 0 on bad input or overflow.
 */
size_t tron_tx_envelope_hex(const char *raw_data_hex, const char *sig_hex,
                            char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* TRON_TX_H */
