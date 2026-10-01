/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * test_chain.cpp — host unit test for the chain predicates in main/assets.h.
 *
 * Two things are worth a test here, and both cost money when they are wrong.
 *
 * Which family a selection belongs to decides which signing path runs, which
 * payout address is spent and which endpoint the transaction is broadcast to.
 * While Sepolia was the only EVM chain, "not Sepolia" meant Tron and both
 * main.cpp and ui.cpp said so in their own one-liner; Polygon made that answer
 * wrong in two places at once. So every enumerator is classified here, and
 * classified exactly once — a chain that is neither Tron nor Polygon is on
 * Ethereum by elimination, so a new one nobody added to a predicate silently
 * becomes an Ethereum chain and gets signed with Sepolia's chain id.
 *
 * The numbers are pinned because they are persisted in NVS. Renumbering the
 * enum would leave a terminal that stored 2 charging in whatever now sits at 2 —
 * a different asset, or a different network, with no visible change.
 *
 * Header-only unit, no ESP-IDF. Build & run from the repo root:
 *
 *   g++ -std=c++14 -Imain tests/units/test_chain.cpp -o test_chain && ./test_chain
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "assets.h"

int main(void)
{
    /* The persisted numbers. New assets append; these never move. */
    assert(POS_CHAIN_ETH_USDC == 0);
    assert(POS_CHAIN_TRON_TRX   == 1);
    assert(POS_CHAIN_TRON_USDT   == 2);
    assert(POS_CHAIN_ETH_USDT    == 3);
    assert(POS_CHAIN_POLY_USDC   == 4);
    assert(POS_CHAIN_POLY_USDT   == 5);
    assert(POS_CHAIN_TRON_USDC   == 6);
    assert(POS_CHAIN_ETH_NATIVE  == 7);
    assert(POS_CHAIN_POLY_NATIVE == 8);
    assert(POS_CHAIN__COUNT      == 9);

    /* Tron: the native coin and both TRC-20s. */
    assert(pos_chain_is_tron(POS_CHAIN_TRON_TRX));
    assert(pos_chain_is_tron(POS_CHAIN_TRON_USDT));
    assert(pos_chain_is_tron(POS_CHAIN_TRON_USDC));
    /* Nothing on an EVM network is. Polygon especially: it is not Sepolia, which
     * is exactly what the old one-liner tested. */
    assert(!pos_chain_is_tron(POS_CHAIN_ETH_USDC));
    assert(!pos_chain_is_tron(POS_CHAIN_ETH_USDT));
    assert(!pos_chain_is_tron(POS_CHAIN_POLY_USDC));
    assert(!pos_chain_is_tron(POS_CHAIN_POLY_USDT));
    assert(!pos_chain_is_tron(POS_CHAIN_ETH_NATIVE));
    assert(!pos_chain_is_tron(POS_CHAIN_POLY_NATIVE));

    /* Polygon: its two ERC-20s and its own coin. POL is the one most easily
     * forgotten, and forgetting it signs a Polygon transfer with Sepolia's chain
     * id, against Sepolia's nonce, at Sepolia's endpoint. */
    assert(pos_chain_is_polygon(POS_CHAIN_POLY_USDC));
    assert(pos_chain_is_polygon(POS_CHAIN_POLY_USDT));
    assert(pos_chain_is_polygon(POS_CHAIN_POLY_NATIVE));
    assert(!pos_chain_is_polygon(POS_CHAIN_ETH_USDC));
    assert(!pos_chain_is_polygon(POS_CHAIN_ETH_USDT));
    assert(!pos_chain_is_polygon(POS_CHAIN_ETH_NATIVE));
    assert(!pos_chain_is_polygon(POS_CHAIN_TRON_TRX));
    assert(!pos_chain_is_polygon(POS_CHAIN_TRON_USDT));
    assert(!pos_chain_is_polygon(POS_CHAIN_TRON_USDC));

    /* The EVM coins. Native TRX is deliberately NOT one of them — the predicate
     * only steers the Ethereum-side fork, and answering true for Tron would send
     * a TRX sale down the EIP-1559 path. */
    assert(pos_chain_is_native_evm(POS_CHAIN_ETH_NATIVE));
    assert(pos_chain_is_native_evm(POS_CHAIN_POLY_NATIVE));
    assert(!pos_chain_is_native_evm(POS_CHAIN_TRON_TRX));
    assert(!pos_chain_is_native_evm(POS_CHAIN_ETH_USDC));
    assert(!pos_chain_is_native_evm(POS_CHAIN_ETH_USDT));
    assert(!pos_chain_is_native_evm(POS_CHAIN_POLY_USDC));
    assert(!pos_chain_is_native_evm(POS_CHAIN_POLY_USDT));

    /* The 18-decimal ceiling: units * 10^12 must still fit a uint64 of wei, and
     * one unit more must not. This is the number that keeps the card from
     * signing a wrapped value nobody entered. */
    assert(POS_AMOUNT_UNITS_MAX_NATIVE * 1000000000000ULL / 1000000000000ULL
           == POS_AMOUNT_UNITS_MAX_NATIVE);
    assert(POS_AMOUNT_UNITS_MAX_NATIVE
           == (uint64_t)(UINT64_MAX / 1000000000000ULL));

    /* No selection is in two families, and every one is in at most one — the
     * loop is over the whole enum, so an asset added without touching either
     * predicate still has to come out as a plain Ethereum chain deliberately
     * rather than by having been forgotten. */
    int tron = 0, poly = 0, eth = 0, nat = 0;
    for (int c = 0; c < (int)POS_CHAIN__COUNT; c++) {
        const pos_chain_t chain = (pos_chain_t)c;
        const bool t = pos_chain_is_tron(chain);
        const bool p = pos_chain_is_polygon(chain);
        assert(!(t && p));
        /* A native EVM coin is on exactly one EVM network, never on Tron. */
        if (pos_chain_is_native_evm(chain)) { nat++; assert(!t); }
        if (t)      { tron++; }
        else if (p) { poly++; }
        else        { eth++;  }
    }
    assert(tron == 3);   /* TRX, USDT, USDC   */
    assert(poly == 3);   /* POL, USDC, USDT   */
    assert(eth  == 3);   /* ETH, USDC, USDT   */
    assert(nat  == 2);   /* ETH, POL          */

    printf("test_chain OK\n");
    return 0;
}
