/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * test_rpc_error.cpp — host unit test for main/rpc_error.h, which turns a node's
 * refusal into the line printed under "Declined".
 *
 * Worth a test because the failure is silent: every branch here produces a
 * plausible-looking sentence, so a wrong one is not a crash or a blank screen —
 * it is a terminal confidently sending an operator to fix the wrong thing. The
 * expensive direction is the gas message during a token sale: "not enough USDT"
 * when the tank is empty, or "not enough ETH" when it is the token that ran out,
 * both end with somebody topping up an account that was never the problem.
 *
 * The real node wordings below are the ones geth, erigon and bor actually send,
 * numbers and all, because the matching is substring-based and a needle that
 * only works against a tidied-up string is a needle that never fires in the field.
 *
 * Header-only unit, no ESP-IDF. Build & run from the repo root:
 *
 *   g++ -std=c++14 -Imain tests/units/test_rpc_error.cpp -o test_rpc_error \
 *       && ./test_rpc_error
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rpc_error.h"

/* The panel's own buffer (main.cpp err_msg / ui.cpp s_tx_info). Everything this
 * produces has to survive it. */
#define PANEL_N  64

static const char *say(const char *node_msg, bool native, bool polygon)
{
    static char out[PANEL_N];
    memset(out, 0, sizeof(out));
    rpc_error_text(node_msg, native, polygon, out, sizeof(out));
    /* Never empty, always terminated, always fits: this string is the only thing
     * on screen explaining a refused sale. */
    assert(out[0] != '\0');
    assert(strlen(out) < PANEL_N);
    return out;
}

int main(void)
{
    /* ── The case that prompted all this: a real geth refusal. ── */
    const char *geth_funds =
        "insufficient funds for gas * price + value: address "
        "0x00112233445566778899AaBbCcDdEeFf00112233 have 0 want 21000000000000";

    /* A token sale needs gas only — the tokens themselves are not what is
     * short, and saying so would be the wrong instruction. */
    assert(strcmp(say(geth_funds, false, false),
                  "Not enough ETH on the card for gas") == 0);
    /* The coin's own transfer is short of the amount as well. */
    assert(strcmp(say(geth_funds, true, false),
                  "Not enough ETH for the amount plus gas") == 0);
    /* Polygon's gas coin is POL. An operator told to add ETH on Amoy would be
     * hunting for an asset that does not pay for anything there. */
    assert(strcmp(say(geth_funds, false, true),
                  "Not enough POL on the card for gas") == 0);
    assert(strcmp(say(geth_funds, true, true),
                  "Not enough POL for the amount plus gas") == 0);
    /* The shorter wording for a plain value transfer hits the same branch. */
    assert(strstr(say("insufficient funds for transfer", true, false),
                  "Not enough ETH") != NULL);

    /* The same condition, in wordings that share nothing with geth's. Both were
     * taken off a live node: the first is what Polygon's Amoy endpoint (bor)
     * prefixes its message with, the second is PublicNode's Sepolia endpoint on
     * 2026-08-31. The second is the reason this matches a list of observed
     * strings and not the one phrase the spec suggests. */
    assert(strcmp(say("failed with 33554432 gas: insufficient funds for gas * "
                      "price + value: address 0x00112233445566778899AaBbCcDdEe"
                      "Ff00112233 have 0 want 1000000000000000000", false, true),
                  "Not enough POL on the card for gas") == 0);
    assert(strcmp(say("EVM error: OutOfFunds", false, false),
                  "Not enough ETH on the card for gas") == 0);
    assert(strcmp(say("insufficient balance for transfer", true, false),
                  "Not enough ETH for the amount plus gas") == 0);

    /* ── Already on the chain. "Try again" is the one thing not to do. ── */
    assert(strcmp(say("nonce too low: next nonce 800, tx nonce 799", false, false),
                  "Already submitted - check the explorer") == 0);
    assert(strcmp(say("already known", false, false),
                  "Already submitted - check the explorer") == 0);

    /* ── Fee floors. All three clients, one instruction. ── */
    const char *fee = "Fee too low - raise it on Settings > Tx";
    assert(strcmp(say("transaction underpriced", false, false), fee) == 0);
    assert(strcmp(say("replacement transaction underpriced", false, false), fee) == 0);
    assert(strcmp(say("max fee per gas less than block base fee: address 0x…, "
                      "maxFeePerGas: 1, baseFee: 25000000000", false, true), fee) == 0);
    assert(strcmp(say("gas price too low", false, true), fee) == 0);

    /* ── Gas limit, both directions. ── */
    assert(strcmp(say("intrinsic gas too low: have 21000, want 21064", false, false),
                  "Gas limit too low for this transfer") == 0);
    assert(strcmp(say("exceeds block gas limit", false, false),
                  "Gas limit too high for this network") == 0);

    /* ── Matching is case-insensitive: clients capitalise differently. ── */
    assert(strcmp(say("Nonce Too Low", false, false),
                  "Already submitted - check the explorer") == 0);
    assert(strcmp(say("INSUFFICIENT FUNDS", false, false),
                  "Not enough ETH on the card for gas") == 0);

    /* ── Nothing to quote. Distinct from a refusal: this one is worth a retry,
     * and it must not read as though the chain rejected the payment. ── */
    assert(strcmp(say(NULL, false, false), "No answer from the node - try again") == 0);
    assert(strcmp(say("",   false, false), "No answer from the node - try again") == 0);

    /* ── A reason we have no words for is passed through, not swallowed. That
     * is the whole point of the change: an unfamiliar message beats a generic
     * one, because somebody can read it out over the phone. ── */
    assert(strcmp(say("only replay-protected (EIP-155) transactions allowed",
                      false, false),
                  "only replay-protected (EIP-155) transactions allowed") == 0);

    /* A node message longer than the panel is clipped, not dropped — say()
     * already asserted it is non-empty and fits. */
    char big[400];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    assert(strlen(say(big, false, false)) == PANEL_N - 1);

    /* ── The substring search itself, including the boundary that a naive
     * implementation gets wrong: a needle running off the end of the haystack. ── */
    assert(rpc_err_contains("abcdef", "cde"));
    assert(rpc_err_contains("abcdef", "abcdef"));
    assert(!rpc_err_contains("abcdef", "abcdefg"));   /* would over-read */
    assert(!rpc_err_contains("abc", "xyz"));
    assert(!rpc_err_contains(NULL, "abc"));
    assert(!rpc_err_contains("abc", NULL));
    assert(!rpc_err_contains("abc", ""));

    /* A zero-capacity buffer must not be written to at all. */
    char guard[2] = { 'A', 'B' };
    rpc_error_text("nonce too low", false, false, guard, 0U);
    assert((guard[0] == 'A') && (guard[1] == 'B'));

    printf("test_rpc_error OK\n");
    return 0;
}
