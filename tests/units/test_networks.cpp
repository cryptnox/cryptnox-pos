/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * test_networks.cpp — host unit test for the production network constants in
 * main/config.h.
 *
 * The one thing about the mainnet/testnet switch that can be checked without a
 * device, and the one thing worth checking: whether the addresses in config.h are
 * addresses. The firmware parses each one twice at boot (erc20_load) and disables
 * that asset if either pass fails, so a wrong EIP-55 case is a refusal rather than
 * a wrong charge — but it is a refusal nobody sees until a customer is standing
 * there, and the log line that explains it scrolls past on a device with no
 * console attached. Caught here instead, at build time.
 *
 * eth_addr_parse verifies the EIP-55 checksum on a mixed-case address, so this
 * fails on a single flipped letter — which is exactly how a hand-copied contract
 * goes wrong. What it cannot check is that the address is the RIGHT contract:
 * a well-formed address for the wrong token passes here and moves the wrong asset.
 * That one is a block explorer's job, and config.h says so.
 *
 * The chain ids are asserted distinct from their testnet twins because the chain
 * id is what stops a transaction signed for one being replayed on the other; two
 * that matched would be a config.h whose halves had been copied and not edited.
 *
 * Single translation unit, the same pattern as test_eth_addr.cpp. Build & run
 * from the repo root:
 *
 *   g++ -std=c++14 -Imain -Icryptnox-sdk-esp32/cryptnox-sdk-cpp \
 *       tests/units/test_networks.cpp -o test_networks && ./test_networks
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "CW_Utils.h"
bool CW_Utils::fill_secure_random(uint8_t *dest, size_t len)
{
    (void)dest;
    (void)len;
    return false;
}

#include "CW_Utils.cpp"
#include "keccak256.cpp"
#include "eth_addr.cpp"

#include "config.h"

/* A config.h may legitimately leave an asset unset — the firmware treats an
 * unparseable contract as "this one was never configured" and refuses only that
 * selection. So the test is: set, or a placeholder. Anything else is a typo. */
static bool unset_or_valid(const char *what, const char *hex_no_0x)
{
    char with_0x[64];
    (void)snprintf(with_0x, sizeof(with_0x), "0x%s", hex_no_0x);

    uint8_t parsed[ETH_ADDR_LEN];
    if (eth_addr_parse(with_0x, parsed)) { return true; }

    /* The template's own placeholder, deliberately left in. Nothing else is
     * allowed to fail — "AObc…" with a capital O for a zero parses as neither. */
    if (hex_no_0x[0] == '<') {
        printf("  note: %s is not configured\n", what);
        return true;
    }
    printf("  FAIL: %s = \"%s\" is not a valid EIP-55 address\n", what, hex_no_0x);
    return false;
}

int main(void)
{
    /* The production ERC-20s. Every one of these is a live mainnet contract that
     * real money is charged against, so each has to parse — checksum included. */
    assert(unset_or_valid("ADDR_USDC_MAIN",      ADDR_USDC_MAIN));
    assert(unset_or_valid("ADDR_USDT_MAIN",      ADDR_USDT_MAIN));
    assert(unset_or_valid("POLY_ADDR_USDC_MAIN", POLY_ADDR_USDC_MAIN));
    assert(unset_or_valid("POLY_ADDR_USDT_MAIN", POLY_ADDR_USDT_MAIN));

    /* And the testnet halves, by the same rule — a build that only ever runs on
     * testnets is still a build whose contracts have to be addresses. */
    assert(unset_or_valid("ADDR_USDC",      ADDR_USDC));
    assert(unset_or_valid("ADDR_USDT",      ADDR_USDT));
    assert(unset_or_valid("POLY_ADDR_USDC", POLY_ADDR_USDC));
    assert(unset_or_valid("POLY_ADDR_USDT", POLY_ADDR_USDT));

    /* The recipient fallback, which is fatal at boot rather than merely
     * disabling something — a terminal cannot come up with an unparseable one. */
    assert(unset_or_valid("ADDR_TO", ADDR_TO));

    /* Tron contracts get no checksum test here: base58check needs a crypto
     * provider, which is why the firmware decodes them in the main task and not
     * in the portal. Length and prefix are still worth a look — the mainnet USDT
     * contract is the one asset on Tron that a production terminal charges. */
    assert((strlen(TRON_ADDR_USDT_MAIN) == 34U) &&
           (TRON_ADDR_USDT_MAIN[0] == 'T'));

    /* Distinct chain ids, per network family. Equal ones would mean a config.h
     * half-copied: the signed transaction would carry the wrong network's id,
     * which is the field that makes it replayable on the other. */
    assert(CHAIN_ID_MAINNET != CHAIN_ID_SEPOLIA);
    assert(CHAIN_ID_POLYGON != CHAIN_ID_AMOY);
    /* And across families, or a Polygon sale could be replayed on Ethereum. */
    assert(CHAIN_ID_MAINNET != CHAIN_ID_POLYGON);
    assert(CHAIN_ID_SEPOLIA != CHAIN_ID_AMOY);

    /* Different endpoints too. Same string on both sides means the switch moves
     * the chain id and the contracts and leaves the transaction being broadcast
     * to the network it was not built for. */
    assert(strcmp(RPC_URL,      RPC_URL_MAIN)      != 0);
    assert(strcmp(POLY_RPC_URL, POLY_RPC_URL_MAIN) != 0);
    assert(strcmp(TRON_URL,     TRON_URL_MAIN)     != 0);

    printf("test_networks OK\n");
    return 0;
}
