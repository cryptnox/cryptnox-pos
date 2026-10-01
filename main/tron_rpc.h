/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file tron_rpc.h
 * @ingroup tron
 * @brief Tron HTTP API client (Nile testnet): build a TRX transfer, broadcast
 *        it signed, poll its receipt.
 *
 * Tron has no RLP and no local nonce: the full node serialises the transaction
 * (@c /wallet/createtransaction, which also supplies the reference block and
 * expiry) and we sign its txID. @ref tron_rpc_create_transfer therefore does
 * NOT trust what comes back — it verifies both that the returned txID really is
 * @c sha256(raw_data) and that raw_data carries exactly the requested
 * TransferContract, so the card never signs a hash of somebody else's payment.
 */

#ifndef TRON_RPC_H
#define TRON_RPC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Hex capacity for a raw_data protobuf.
 *
 * A TRX transfer needs ~280; a TRC-20 TriggerSmartContract carries a longer
 * type_url plus 68 bytes of calldata and lands near ~400. */
#define TRON_RAW_HEX_MAX  768U

/** @brief A created-and-verified transfer, ready to sign and broadcast. */
typedef struct {
    uint8_t txid[32];                    /**< The 32 bytes the card signs.   */
    char    txid_hex[65];                /**< Same, hex — for receipt polls. */
    char    raw_hex[TRON_RAW_HEX_MAX];   /**< Verified raw_data hex.         */
    uint64_t expiration_ms;              /**< raw_data.expiration: after this
                                              the chain can no longer take it. */
} tron_tx_ctx_t;

/** @brief Outcome of one @c gettransactioninfobyid poll. */
typedef enum {
    TRON_RECEIPT_PENDING,    /**< Not in a block yet — poll again later.     */
    TRON_RECEIPT_SUCCESS,    /**< In a block, contract executed — final.     */
    TRON_RECEIPT_FAILED,     /**< In a block but failed — funds NOT moved.   */
    TRON_RECEIPT_RPC_ERROR,  /**< Transport or parse error (transient).      */
} tron_receipt_t;

/**
 * @brief Set the Tron HTTP API base URL (no trailing slash).
 *
 * Lifetime: the pointer is stored as-is — pass a literal or static storage.
 *
 * @param[in] base_url e.g. "https://nile.trongrid.io".
 */
void tron_rpc_init(const char *base_url);

/**
 * @brief Optional: pin the Tron endpoint's TLS certificate.
 *
 * The sibling of @c eth_rpc_set_ca_cert, and for the same reason: unset, the
 * connection is validated against the whole Mozilla CA bundle, so any one of
 * ~150 CAs can stand in for the node. That is survivable here — @ref
 * tron_tx_contract_ok re-derives the bytes a transfer must contain, so a node
 * (or anything impersonating one) cannot redirect a payment — but it is the
 * difference between one check standing between an attacker and the funds and
 * two. Pin it in production.
 *
 * Pointer stored as-is (must outlive every call). NULL keeps the CA bundle.
 *
 * @param[in] ca_pem NUL-terminated PEM certificate, or NULL for the bundle.
 */
void tron_rpc_set_ca_cert(const char *ca_pem);

/**
 * @brief Read an account's TRX balance, in sun.
 *
 * For the pre-flight check that refuses a sale the tapped card cannot fund,
 * before it signs anything — see @c tron_balance_ok in main.cpp.
 *
 * An account the chain has never seen answers with an empty object. That is
 * reported as a balance of zero and not as an error, because it is the true
 * answer and it is the case a terminal most needs to catch.
 *
 * @param[in]  owner_hex Account address, "41"-prefixed 42-char hex.
 * @param[out] sun_out   Balance in sun on success; untouched on failure.
 * @return true if the node answered with parseable JSON.
 */
bool tron_rpc_get_balance(const char *owner_hex, uint64_t *sun_out);

/**
 * @brief Read the energy an account still has available.
 *
 * @c EnergyLimit minus @c EnergyUsed from @c /wallet/getaccountresource. Only
 * the distinction between none and some is used: a TRC-20 transfer burns energy
 * paid for out of a stake or out of TRX, so an account with zero TRX can still
 * pay if it has frozen some — and refusing that sale would be worse than the
 * late failure the check exists to avoid.
 *
 * @param[in]  owner_hex  Account address, "41"-prefixed 42-char hex.
 * @param[out] energy_out Energy available on success; untouched on failure.
 * @return true if the node answered with parseable JSON.
 */
bool tron_rpc_get_energy(const char *owner_hex, uint64_t *energy_out);

/**
 * @brief Read an account's TRC-20 balance via a constant @c balanceOf call.
 *
 * The check that @ref tron_rpc_create_trc20_transfer cannot make: creating a
 * TriggerSmartContract is only serialisation, and a node will serialise a
 * transfer of tokens the account does not hold just as readily as one it does.
 * Without this the card signs it, it broadcasts, it reverts on-chain, and the
 * customer is declined after the full wait — having paid the energy for it.
 *
 * @param[in]  owner_hex    Account address, "41"-prefixed 42-char hex.
 * @param[in]  contract_hex Token contract address, same form.
 * @param[out] units_out    Balance in the token's base units on success;
 *                          untouched on failure. Saturating, as for the EVM
 *                          side (see @ref eth_json_hex_quantity).
 * @return true if the node answered with a well-formed constant result.
 */
bool tron_rpc_get_trc20_balance(const char *owner_hex, const char *contract_hex,
                                uint64_t *units_out);

/**
 * @brief Read a TRC-20 contract's @c decimals() — see eth_rpc_get_token_decimals
 *        for why it has to be 6.
 *
 * @param[in]  contract_hex Contract, #TRON_ADDR_HEX_LEN hex chars, "41"-prefixed.
 * @param[out] dec_out      decimals() on success; untouched on failure.
 * @return false on transport error or a malformed answer.
 */
bool tron_rpc_get_trc20_decimals(const char *contract_hex, uint64_t *dec_out);

/**
 * @brief Create a TRX transfer and verify what the node serialised for us.
 *
 * @param[in]  owner_hex  Sender address, "41"-prefixed 42-char hex.
 * @param[in]  to_hex     Recipient address, same form.
 * @param[in]  amount_sun Amount in sun (1 TRX = 1e6 sun); must be non-zero.
 * @param[out] out        Filled on success; contents undefined on failure.
 * @return true only if the node answered AND the txID matches
 *         sha256(raw_data) AND raw_data contains exactly the requested
 *         TransferContract.
 */
bool tron_rpc_create_transfer(const char *owner_hex, const char *to_hex,
                              uint64_t amount_sun, tron_tx_ctx_t *out);

/**
 * @brief Create a TRC-20 @c transfer (USDT, USDC, …) and verify what the node
 *        serialised for us.
 *
 * Same trust model as @ref tron_rpc_create_transfer, with one more thing to get
 * wrong: the token contract. A node free to choose it could have the card sign
 * a transfer of a worthless token — or of a different one entirely — so the
 * contract address is pinned by the check just like the recipient is, and so is
 * the fee limit (see @ref tron_tx_trc20_ok).
 *
 * @param[in]  owner_hex     Sender address, "41"-prefixed 42-char hex.
 * @param[in]  contract_hex  Token contract address, same form.
 * @param[in]  to_hex        Recipient address, same form.
 * @param[in]  amount        Amount in token base units; must be non-zero.
 * @param[in]  fee_limit_sun Max TRX (in sun) the call may burn; must be
 *                           non-zero, or a failed call has no cap at all.
 * @param[out] out           Filled on success; contents undefined on failure.
 * @return true only if the node answered AND txID == sha256(raw_data) AND
 *         raw_data carries exactly the requested transfer under the requested
 *         fee limit.
 */
bool tron_rpc_create_trc20_transfer(const char *owner_hex,
                                    const char *contract_hex,
                                    const char *to_hex, uint64_t amount,
                                    uint64_t fee_limit_sun,
                                    tron_tx_ctx_t *out);

/**
 * @brief Broadcast a created transfer with its 65-byte signature.
 *
 * @param[in] tx  Context returned by @ref tron_rpc_create_transfer.
 * @param[in] sig Signature bytes: r(32) || s(32) || recovery id(1).
 * @return true if the node accepted the transaction into its mempool.
 */
bool tron_rpc_broadcast(const tron_tx_ctx_t *tx, const uint8_t sig[65]);

/**
 * @brief Poll a broadcast transaction's receipt (one shot).
 *
 * @param[in] txid_hex 64-char transaction id hex.
 * @return One of @ref tron_receipt_t.
 */
tron_receipt_t tron_rpc_get_receipt(const char *txid_hex);

#ifdef __cplusplus
}
#endif

#endif /* TRON_RPC_H */
