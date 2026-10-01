/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * test_eth_hex.cpp — host unit test for the JSON-RPC QUANTITY parser that turns
 * a node's balance into the number the pre-flight check compares against a sale
 * (eth_json_hex_quantity, eth_json.h).
 *
 * It matters because both failure directions are silent and opposite. Wrap a
 * uint256 into a uint64 and a funded card is told it has no money — a terminal
 * that refuses working payments. Read garbage as zero and the same thing happens
 * for a different reason. Saturating is the only answer that fails safe: too
 * much reported balance merely lets the sale reach the node, which is exactly
 * where it stood before this check existed.
 *
 * Header-only unit, so this includes it directly. Build & run from the repo root:
 *
 *   g++ -std=c++14 -Imain tests/units/test_eth_hex.cpp -o test_eth_hex \
 *       && ./test_eth_hex
 */

#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#include "eth_json.h"

int main(void)
{
    uint64_t v = 0U;

    /* Ordinary quantities. */
    assert(eth_json_hex_quantity("0x0", &v) && (v == 0U));
    assert(eth_json_hex_quantity("0x1", &v) && (v == 1U));
    assert(eth_json_hex_quantity("0x1a", &v) && (v == 26U));
    assert(eth_json_hex_quantity("0xff", &v) && (v == 255U));
    assert(eth_json_hex_quantity("0xFF", &v) && (v == 255U));   /* case-blind */
    assert(eth_json_hex_quantity("0X10", &v) && (v == 16U));    /* prefix too  */

    /* An eth_call returns a uint256 padded to 64 characters — the ordinary
     * shape of a token balance, and the one a naive length check rejects. */
    assert(eth_json_hex_quantity(
        "0x00000000000000000000000000000000000000000000000000000000000f4240",
        &v) && (v == 1000000U));                                /* 1.000000 USDC */
    assert(eth_json_hex_quantity(
        "0x0000000000000000000000000000000000000000000000000000000000000000",
        &v) && (v == 0U));

    /* Exactly 16 significant digits still fits. */
    assert(eth_json_hex_quantity("0xffffffffffffffff", &v) && (v == UINT64_MAX));

    /* Wider than 64 bits saturates rather than wrapping. 20 ETH is past 2^64
     * wei, so this is the everyday case for a funded account and not a corner:
     * wrapping it would report a rich card as empty. */
    assert(eth_json_hex_quantity("0x10000000000000000", &v) && (v == UINT64_MAX));
    assert(eth_json_hex_quantity(
        "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        &v) && (v == UINT64_MAX));
    /* 20 ETH in wei, written out. */
    assert(eth_json_hex_quantity("0x01158e460913d00000", &v) && (v == UINT64_MAX));

    /* Padding is not value: 63 zeros and a 1 is one, not a saturated balance. */
    assert(eth_json_hex_quantity(
        "0x0000000000000000000000000000000000000000000000000000000000000001",
        &v) && (v == 1U));

    /* Rejected. The last two are the ones that would otherwise read as zero: a
     * call to an address with no code answers "0x", and a truncated or corrupted
     * body must not be mistaken for an empty account. */
    assert(!eth_json_hex_quantity(NULL, &v));
    assert(!eth_json_hex_quantity("0x1", NULL));
    assert(!eth_json_hex_quantity("", &v));
    assert(!eth_json_hex_quantity("1a", &v));         /* no prefix            */
    assert(!eth_json_hex_quantity("0x", &v));         /* no code at that addr */
    assert(!eth_json_hex_quantity("0xzz", &v));       /* not hex              */
    assert(!eth_json_hex_quantity("0x00zz", &v));     /* not hex, behind pad  */
    assert(!eth_json_hex_quantity("0x1 ", &v));       /* trailing junk        */

    /* A rejected parse leaves the caller's variable alone, so a failure cannot
     * be read as a balance of whatever the last call returned. */
    v = 4242U;
    assert(!eth_json_hex_quantity("0x", &v));
    assert(v == 4242U);

    printf("test_eth_hex: all assertions passed\n");
    return 0;
}
