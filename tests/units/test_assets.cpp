/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * test_assets.cpp — host unit test for the asset descriptor table in main/assets.h.
 *
 * The table replaced four parallel switches and three hand-written picker tables
 * in ui.cpp. That trade is only worth making if the table is complete: a switch
 * with a `default` renders SOMETHING for a chain nobody added, and a table with a
 * missing row would too (pos_asset_of falls back), which is the same silent-wrong
 * failure moved to a new address. So the completeness that used to be "did you
 * remember all ten edit sites" is asserted here instead.
 *
 * Header-only unit, no ESP-IDF. Build & run from the repo root:
 *
 *   g++ -std=c++14 -Imain tests/units/test_assets.cpp -o test_assets && ./test_assets
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "assets.h"

int main(void)
{
    /* One row per selection, no more and no less. */
    assert(POS_ASSET_COUNT == (size_t)POS_CHAIN__COUNT);

    /* Every enumerator appears exactly once, and every row is fully filled in —
     * an empty ticker or caption is a row somebody added by copying a neighbour
     * and not finishing. */
    for (int c = 0; c < (int)POS_CHAIN__COUNT; c++) {
        const pos_chain_t chain = (pos_chain_t)c;
        int seen = 0;
        for (size_t i = 0U; i < POS_ASSET_COUNT; i++) {
            if (POS_ASSETS[i].chain == chain) { seen++; }
        }
        assert(seen == 1);

        const pos_asset_t *a = pos_asset_of(chain);
        assert(a->chain == chain);
        assert(a->ticker   != NULL && a->ticker[0]   != '\0');
        assert(a->standard != NULL && a->standard[0] != '\0');
        assert(a->caption  != NULL && a->caption[0]  != '\0');
        assert((int)a->net >= 0 && a->net < POS_NET__COUNT);
    }

    /* The table's `net` must agree with the family predicates settings.h steers
     * the signing paths with. Two sources of truth about which network an asset
     * is on is exactly the bug the table was meant to remove, so they are
     * cross-checked rather than trusted to stay in step. */
    for (int c = 0; c < (int)POS_CHAIN__COUNT; c++) {
        const pos_chain_t chain = (pos_chain_t)c;
        const pos_asset_t *a = pos_asset_of(chain);
        assert((a->net == POS_NET_TRON)  == pos_chain_is_tron(chain));
        assert((a->net == POS_NET_POLY)  == pos_chain_is_polygon(chain));
        /* `native` is the table's own word for it; on the EVM networks it must
         * mean what the signing fork means by it. Native TRX is native here but
         * deliberately not pos_chain_is_native_evm — that predicate only steers
         * the Ethereum-side fork. */
        if (a->net != POS_NET_TRON) {
            assert(a->native == pos_chain_is_native_evm(chain));
        }
    }

    /* A native coin has no contract, so its row names the asset; a token's names
     * the contract. Getting these two the wrong way round puts "Asset" over a
     * 42-character hex string on the screen an operator checks a contract on. */
    for (size_t i = 0U; i < POS_ASSET_COUNT; i++) {
        const pos_asset_t *a = &POS_ASSETS[i];
        if (a->native) {
            assert(strcmp(a->caption, "Asset") == 0);
            assert(strcmp(a->standard, "Native coin") == 0);
        } else {
            assert(strstr(a->caption, "contract") != NULL);
            assert(strcmp(a->standard, "Native coin") != 0);
        }
    }

    /* Exactly one native coin per network — a network with two would put two
     * "Native coin" rows in one picker, and one with none would offer no way to
     * charge in the network's own coin. */
    for (int n = 0; n < (int)POS_NET__COUNT; n++) {
        int natives = 0, rows = 0;
        for (size_t i = 0U; i < POS_ASSET_COUNT; i++) {
            if (POS_ASSETS[i].net != (pos_net_t)n) { continue; }
            rows++;
            if (POS_ASSETS[i].native) {
                /* Native first: the picker renders this table in order. */
                assert(natives == 0 && rows == 1);
                natives++;
            }
        }
        assert(natives == 1);
        assert(rows > 0);
    }

    /* Every network is named in both deployments, and the two differ — the whole
     * point of the long forms is telling a test terminal from a production one. */
    for (int n = 0; n < (int)POS_NET__COUNT; n++) {
        const pos_net_info_t *ni = pos_net_info((pos_net_t)n);
        assert(ni->name != NULL && ni->name[0] != '\0');
        assert(ni->long_test != NULL && ni->long_main != NULL);
        assert(ni->sub_test  != NULL && ni->sub_main  != NULL);
        assert(strcmp(ni->long_test, ni->long_main) != 0);
        assert(strcmp(ni->sub_test,  ni->sub_main)  != 0);
    }

    /* Out-of-range inputs resolve rather than crash, as documented. */
    assert(pos_asset_of((pos_chain_t)POS_CHAIN__COUNT) == &POS_ASSETS[0]);
    assert(pos_net_info((pos_net_t)POS_NET__COUNT) == &POS_NETS[POS_NET_ETH]);

    /* Spot-check the strings the operator actually reads, so a well-formed table
     * that says the wrong thing still fails. */
    assert(strcmp(pos_asset_of(POS_CHAIN_TRON_TRX)->ticker, "TRX") == 0);
    assert(strcmp(pos_asset_of(POS_CHAIN_POLY_NATIVE)->ticker, "POL") == 0);
    assert(strcmp(pos_asset_of(POS_CHAIN_ETH_USDC)->ticker, "USDC") == 0);
    assert(strcmp(pos_asset_of(POS_CHAIN_ETH_USDT)->caption,
                  "USDT contract") == 0);
    assert(strcmp(pos_asset_of(POS_CHAIN_TRON_USDT)->caption,
                  "Token contract") == 0);
    assert(pos_net_of(POS_CHAIN_POLY_USDT) == POS_NET_POLY);

    /* Decimals and marks. 18 is the one that turns a keypad amount into wei with
     * a 10^12 multiply, so only the EVM coins may carry it; and every token wears
     * a mark of its own — the old ticker lookup put USDC on anything not USDT. */
    for (size_t i = 0U; i < POS_ASSET_COUNT; i++) {
        const pos_asset_t *a = &POS_ASSETS[i];
        assert(a->decimals == (pos_chain_is_native_evm(a->chain) ? 18U : 6U));
        assert((a->coin == POS_COIN_NET) == a->native);
        if (a->coin == POS_COIN_USDC) { assert(strcmp(a->ticker, "USDC") == 0); }
        if (a->coin == POS_COIN_USDT) { assert(strcmp(a->ticker, "USDT") == 0); }
    }

    printf("test_assets OK\n");
    return 0;
}
