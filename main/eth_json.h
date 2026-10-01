/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file eth_json.h
 * @ingroup eth
 * @brief JSON-RPC response parsing helpers — kept in their own unit (cJSON +
 *        CW_Utils only, no ESP-IDF networking) so they can be fuzzed on the
 *        host (see fuzz/fuzz_eth_rpc_json.cpp).
 */

#ifndef ETH_JSON_H
#define ETH_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Verdict for an @c eth_getTransactionReceipt response body. */
typedef enum {
    ETH_JSON_RECEIPT_ERROR = 0, /**< Not valid JSON, or an unexpected shape
                                     (caller should treat as a transient RPC
                                     error and retry).                       */
    ETH_JSON_RECEIPT_PENDING,   /**< @c result is null — not mined yet.       */
    ETH_JSON_RECEIPT_SUCCESS,   /**< @c result.status == "0x1".               */
    ETH_JSON_RECEIPT_REVERTED,  /**< @c result is an object but status != 0x1.*/
    ETH_JSON_RECEIPT_MISMATCH,  /**< A mined receipt that is not the payment
                                     asked for (@ref eth_json_receipt_check):
                                     another hash, recipient, token or amount. */
} eth_json_receipt_t;

/** @brief What a receipt must show for the payment to count — see
 *         @ref eth_json_receipt_check. */
typedef struct {
    const char    *tx_hash;  /**< "0x" + 64 hex, hashed on the device from the
                                  signed bytes — never the node's answer.   */
    const uint8_t *to;       /**< 20 bytes: the tx's @c to — the token contract,
                                  or the payee for a native transfer.       */
    const uint8_t *payee;    /**< ERC-20: @c Transfer recipient; NULL native. */
    uint64_t       amount;   /**< ERC-20: @c Transfer value, base units.    */
} eth_receipt_expect_t;

/**
 * @brief Extract the top-level @c "result" string from a JSON-RPC response.
 *
 * Uses a real JSON parser (cJSON) instead of strstr so that JSON-RPC error
 * objects, HTTP error bodies, or look-alike substrings are rejected.
 *
 * @param[in]  resp     NUL-terminated response body.
 * @param[out] out      NUL-terminated "result" value on success; untouched
 *                      on failure.
 * @param[in]  out_size Capacity of @p out.
 * @return true on success; false if the body is not valid JSON, "result" is
 *         absent or not a string, or the value does not fit in @p out.
 */
bool eth_json_result_string(const char *resp, char *out, size_t out_size);

/**
 * @brief Extract the JSON-RPC @c error.message from a response body.
 *
 * The counterpart to @ref eth_json_result_string, and the only place a node
 * ever says *why* it refused a transaction: "insufficient funds for gas * price
 * + value", "nonce too low", "transaction underpriced". Dropping it on the
 * floor is what turns every one of those into an identical "Broadcast failed"
 * on the panel, which tells an operator nothing they can act on.
 *
 * A message longer than @p out_size is truncated rather than rejected — half a
 * reason beats none, and the caller's buffer is a panel line, not a log.
 *
 * @param[in]  resp     NUL-terminated response body.
 * @param[out] out      NUL-terminated message on success; untouched otherwise.
 * @param[in]  out_size Capacity of @p out.
 * @return true if the body parsed and carried a non-empty error message.
 */
bool eth_json_error_message(const char *resp, char *out, size_t out_size);

/**
 * @brief Classify an @c eth_getTransactionReceipt response body.
 *
 * Parses the JSON and inspects the top-level @c result: null means the tx is
 * still pending; an object's @c status field ("0x1" = success, anything else =
 * reverted) gives the final verdict. Any other shape (invalid JSON, result
 * neither null nor object, missing/non-string status) yields
 * @ref ETH_JSON_RECEIPT_ERROR.
 *
 * @param[in] resp NUL-terminated response body.
 * @return One of @ref eth_json_receipt_t.
 */
eth_json_receipt_t eth_json_receipt_status(const char *resp);

/**
 * @brief Classify a receipt AND check it is the payment that was asked for.
 *
 * A status of 0x1 alone proves nothing: a node that never broadcast our
 * transaction can answer with the receipt of any other successful one. So on
 * top of @ref eth_json_receipt_status this requires @c transactionHash to be
 * the hash computed on the device, @c to to be the expected contract (or payee),
 * and — for a token — a @c Transfer log from that contract to @c payee for
 * exactly @c amount. A mined receipt failing any of those is
 * @ref ETH_JSON_RECEIPT_MISMATCH, never SUCCESS or REVERTED.
 *
 * @param[in] resp NUL-terminated response body.
 * @param[in] want What the receipt must show; NULL yields ERROR.
 * @return One of @ref eth_json_receipt_t.
 */
eth_json_receipt_t eth_json_receipt_check(const char *resp,
                                          const eth_receipt_expect_t *want);

/**
 * @brief Parse a JSON-RPC QUANTITY ("0x0", "0x1a", 64 hex chars) into a
 *        uint64, saturating instead of wrapping.
 *
 * A balance is a uint256 and this is not, so a value wider than 16 significant
 * hex digits reports @c UINT64_MAX. That direction is the safe one for the
 * callers here: these numbers are compared against what a sale costs, and an
 * over-reported balance only lets a doomed sale through to the node that would
 * have refused it anyway — which is the behaviour without this check at all —
 * whereas a wrapped one would refuse a sale that is funded. An account holding
 * 20 ETH is past 2^64 wei, so this is the ordinary case and not a corner.
 *
 * Header-only, and deliberately: it is the one piece of this unit a host test
 * can reach without cJSON (tests/units/test_eth_hex.cpp).
 *
 * @param[in]  hex "0x"-prefixed, NUL-terminated hex string.
 * @param[out] out Parsed value on success; untouched on failure.
 * @return true on a well-formed quantity; false on a missing prefix, an empty
 *         body, or any non-hex character anywhere in it.
 */
static inline bool eth_json_hex_quantity(const char *hex, uint64_t *out)
{
    if ((hex == NULL) || (out == NULL)) { return false; }
    if ((hex[0] != '0') || ((hex[1] != 'x') && (hex[1] != 'X'))) { return false; }

    const char *p = &hex[2];
    if (*p == '\0') { return false; }   /* "0x" on its own is not a quantity */

    /* Every digit is checked, leading zeros included: validating only the
     * significant ones would read "0x00zz" as zero rather than as garbage. */
    size_t len = 0U;
    while (p[len] != '\0') {
        const char c = p[len];
        const bool is_hex = ((c >= '0') && (c <= '9')) ||
                            ((c >= 'a') && (c <= 'f')) ||
                            ((c >= 'A') && (c <= 'F'));
        if (!is_hex) { return false; }
        len++;
    }

    /* Nodes pad a uint256 out to 64 characters; strip down to the significant
     * digits so the width test below is about the value, not the encoding.
     * One digit always survives, so "0x000" stays parseable as zero. */
    while ((*p == '0') && (p[1] != '\0')) { p++; len--; }

    if (len > 16U) { *out = UINT64_MAX; return true; }

    uint64_t v = 0U;
    for (size_t i = 0U; i < len; i++) {
        const char c = p[i];
        uint8_t n;
        if ((c >= '0') && (c <= '9'))      { n = (uint8_t)(c - '0'); }
        else if ((c >= 'a') && (c <= 'f')) { n = (uint8_t)((c - 'a') + 10); }
        else                               { n = (uint8_t)((c - 'A') + 10); }
        v = (v << 4) | (uint64_t)n;
    }
    *out = v;
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* ETH_JSON_H */
