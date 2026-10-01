/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file rpc_error.h
 * @ingroup eth
 * @brief Turn a node's JSON-RPC refusal into a line an operator can act on.
 *
 * Every reason a node gives for rejecting a transaction used to arrive on the
 * panel as the same four words, "Broadcast failed", which names the symptom and
 * hides the cause. The cause is the whole value of the message: an empty gas
 * tank, a fee under the network's floor and a duplicate submission need three
 * different responses from whoever is standing at the till, and two of them are
 * not "try again".
 *
 * Header-only so it can be tested on the host without ESP-IDF or a network —
 * same shape as card_status.h and ota_version.h.
 */

#ifndef RPC_ERROR_H
#define RPC_ERROR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Case-insensitive substring search — node wording varies by client. */
static inline bool rpc_err_contains(const char *hay, const char *needle)
{
    if ((hay == NULL) || (needle == NULL) || (needle[0] == '\0')) { return false; }
    for (size_t i = 0U; hay[i] != '\0'; i++) {
        size_t j = 0U;
        while (needle[j] != '\0') {
            char a = hay[i + j];
            char b = needle[j];
            if ((a >= 'A') && (a <= 'Z')) { a = (char)(a + ('a' - 'A')); }
            if ((b >= 'A') && (b <= 'Z')) { b = (char)(b + ('a' - 'A')); }
            if (a != b) { break; }
            j++;
        }
        if (needle[j] == '\0') { return true; }
    }
    return false;
}

/**
 * @brief Render a broadcast refusal for the Declined screen.
 *
 * @param[in]  node_msg The node's @c error.message, or NULL/"" when the request
 *                      never got an answer worth quoting.
 * @param[in]  native   true when the sale is the network's own coin, so the
 *                      balance has to cover the amount as well as the gas.
 * @param[in]  polygon  true on Polygon, which names its gas coin POL not ETH.
 * @param[out] out      Panel line, always NUL-terminated.
 * @param[in]  n        Capacity of @p out (the panel's own buffer is 64).
 */
static inline void rpc_error_text(const char *node_msg, bool native,
                                  bool polygon, char *out, size_t n)
{
    /* Substring -> what to say instead. Substrings, not equality: the same
     * condition is worded differently by geth, erigon and bor, and several of
     * them append the offending numbers to the sentence. */
    static const struct { const char *needle; const char *say; } MAP[] = {
        { "nonce too low",            "Already submitted - check the explorer" },
        { "already known",            "Already submitted - check the explorer" },
        { "underpriced",              "Fee too low - raise it on Settings > Tx" },
        { "less than block base fee", "Fee too low - raise it on Settings > Tx" },
        { "gas price too low",        "Fee too low - raise it on Settings > Tx" },
        { "intrinsic gas too low",    "Gas limit too low for this transfer"    },
        { "exceeds block gas limit",  "Gas limit too high for this network"    },
        { "gas limit reached",        "Network is full - try again in a moment" },
    };

    if ((out == NULL) || (n == 0U)) { return; }

    if ((node_msg == NULL) || (node_msg[0] == '\0')) {
        /* No JSON-RPC error object: the request never completed, or the body was
         * not something we could read. Distinct wording from a refusal, because
         * the operator's next move is different — this one is worth retrying. */
        (void)snprintf(out, n, "No answer from the node - try again");
        return;
    }

    /* Handled ahead of the table because the right sentence depends on what was
     * being sent: a token transfer needs gas only, the coin's own transfer needs
     * the amount too, and saying "not enough ETH" during a USDT sale would send
     * the operator to top up the wrong thing.
     *
     * Three spellings of one condition. geth's is the familiar one; "OutOfFunds"
     * is what PublicNode's Sepolia endpoint actually returned on 2026-08-31, and
     * it shares not one word with geth's — which is the argument for matching on
     * a list of observed strings rather than on what the JSON-RPC spec implies
     * a node ought to say. */
    if (rpc_err_contains(node_msg, "insufficient funds") ||
        rpc_err_contains(node_msg, "insufficient balance") ||
        rpc_err_contains(node_msg, "outoffunds")) {
        (void)snprintf(out, n,
                       native ? "Not enough %s for the amount plus gas"
                              : "Not enough %s on the card for gas",
                       polygon ? "POL" : "ETH");
        return;
    }

    for (size_t i = 0U; i < (sizeof(MAP) / sizeof(MAP[0])); i++) {
        if (rpc_err_contains(node_msg, MAP[i].needle)) {
            (void)snprintf(out, n, "%s", MAP[i].say);
            return;
        }
    }

    /* Not one we have words for. Pass the node's own sentence through, clipped
     * to the panel: an unfamiliar reason in the node's wording is still a lead,
     * and this is the line somebody will read out over the phone. */
    (void)snprintf(out, n, "%s", node_msg);
}

#ifdef __cplusplus
}
#endif

#endif /* RPC_ERROR_H */
