/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 *
 * Known-answer test for main/money.h — keypad cents, the keypad ceiling,
 * units to wei, the EIP-1559 fees, the pre-flight funds check, the fee text and
 * the USDC transfer calldata. Every figure the customer is charged is one of
 * these, and every mistake in them is silent.
 *
 * Expected values are from tools/gen_kat_vectors.py (eth-abi for the calldata,
 * Python's unbounded ints and Decimal for the arithmetic), not from money.h.
 *
 *   g++ -std=c++14 -Wall -Imain -Icryptnox-sdk-esp32/cryptnox-sdk-cpp \
 *       tests/units/test_money.cpp -o t && ./t
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* CW_Utils::fill_secure_random is ESP32-specific and never reached from
 * safe_memcpy / secure_wipe; stub it for the linker. */
#include "CW_Utils.h"
bool CW_Utils::fill_secure_random(uint8_t *dest, size_t len)
{
    (void)dest;
    (void)len;
    return false;
}

#include "CW_Utils.cpp"
#include "money.h"

/* 0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed, the EIP-55 spec address. */
static const uint8_t PAYEE[20] = {
    0x5a, 0xae, 0xb6, 0x05, 0x3f, 0x3e, 0x94, 0xc9, 0xb9, 0xa0,
    0x9f, 0x33, 0x66, 0x94, 0x35, 0xe7, 0xef, 0x1b, 0xea, 0xed,
};

/* eth_abi.encode(["address", "uint256"], [PAYEE, amount]) behind a9059cbb. */
static const uint8_t CALLDATA_0[68] = {
    0xa9, 0x05, 0x9c, 0xbb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x5a, 0xae, 0xb6, 0x05, 0x3f, 0x3e, 0x94, 0xc9,
    0xb9, 0xa0, 0x9f, 0x33, 0x66, 0x94, 0x35, 0xe7, 0xef, 0x1b, 0xea, 0xed,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t CALLDATA_1[68] = {
    0xa9, 0x05, 0x9c, 0xbb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x5a, 0xae, 0xb6, 0x05, 0x3f, 0x3e, 0x94, 0xc9,
    0xb9, 0xa0, 0x9f, 0x33, 0x66, 0x94, 0x35, 0xe7, 0xef, 0x1b, 0xea, 0xed,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
};
static const uint8_t CALLDATA_bebc20[68] = {
    0xa9, 0x05, 0x9c, 0xbb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x5a, 0xae, 0xb6, 0x05, 0x3f, 0x3e, 0x94, 0xc9,
    0xb9, 0xa0, 0x9f, 0x33, 0x66, 0x94, 0x35, 0xe7, 0xef, 0x1b, 0xea, 0xed,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xbe, 0xbc, 0x20,
};
static const uint8_t CALLDATA_ffffffffffffffff[68] = {
    0xa9, 0x05, 0x9c, 0xbb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x5a, 0xae, 0xb6, 0x05, 0x3f, 0x3e, 0x94, 0xc9,
    0xb9, 0xa0, 0x9f, 0x33, 0x66, 0x94, 0x35, 0xe7, 0xef, 0x1b, 0xea, 0xed,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/* units * 10^12 as a uint256: 1 unit, both sides of the old 2^64 wall, and
 * the 9999.99 ceiling. */
static const uint8_t WEI_1[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xe8, 0xd4, 0xa5, 0x10, 0x00,
};
static const uint8_t WEI_18446744[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xee, 0xd6, 0x91, 0x80, 0x00,
};
static const uint8_t WEI_18446745[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0xd7, 0xab, 0x36, 0x90, 0x00,
};
static const uint8_t WEI_9999990000[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x1e,
    0x19, 0xbd, 0x42, 0xc8, 0x42, 0x7f, 0x00, 0x00,
};

/* Balances past 2^64: the 9999.99 sale plus 21000 gas at 30 Gwei, and 1 wei less. */
static const uint8_t HAVE_21e19bf7fc390b46000[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x1e,
    0x19, 0xbf, 0x7f, 0xc3, 0x90, 0xb4, 0x60, 0x00,
};
static const uint8_t HAVE_21e19bf7fc390b45fff[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x1e,
    0x19, 0xbf, 0x7f, 0xc3, 0x90, 0xb4, 0x5f, 0xff,
};

/* Fee ceilings, Decimal(v) / 10^dec rounded toward +inf at six places. */
static const struct {
    uint64_t    v;
    unsigned    dec;
    const char *text;
} COIN_TEXT[] = {
    { 630000000000000ULL, 18U, "0.00063" },
    { 50000000000000000ULL, 18U, "0.05" },
    { 21000000000000ULL, 18U, "0.000021" },
    { 1ULL, 18U, "0.000001" },
    { 1000000000000ULL, 18U, "0.000001" },
    { 1000000000001ULL, 18U, "0.000002" },
    { 100000000ULL, 6U, "100" },
    { 0ULL, 18U, "0" },
    { 18446744073709551615ULL, 18U, "18.446745" },
};

static void calldata_is(uint64_t amount, const uint8_t exp[USDC_CALLDATA_LEN])
{
    uint8_t out[USDC_CALLDATA_LEN];
    memset(out, 0xA5, sizeof(out));   /* the builder must overwrite all of it */
    build_usdc_calldata(out, PAYEE, amount);
    assert(memcmp(out, exp, USDC_CALLDATA_LEN) == 0);
}

int main(void)
{
    /* ── USDC transfer(address,uint256) calldata ──────────────────── */
    assert(USDC_CALLDATA_LEN == 68U);
    calldata_is(0U, CALLDATA_0);
    calldata_is(1U, CALLDATA_1);
    calldata_is(12500000U, CALLDATA_bebc20);            /* 12.50 USDC */
    calldata_is(UINT64_MAX, CALLDATA_ffffffffffffffff); /* every amount byte */

    /* ── Units -> wei (uint256) ────────────────────────────────────── */
    /* 18446745 units is the first value past 2^64 wei — where a uint64 used to
     * wrap — and 9999.99 is ~2^73. All exact, none refused. */
    uint8_t wei[WEI_LEN];
    const uint8_t zero[WEI_LEN] = { 0U };
    assert(AMOUNT_UNITS_MAX == 9999990000ULL);
    assert(evm_units_to_wei(0U, wei) && (memcmp(wei, zero, WEI_LEN) == 0));
    assert(evm_units_to_wei(1U, wei) && (memcmp(wei, WEI_1, WEI_LEN) == 0));
    assert(evm_units_to_wei(18446744ULL, wei) && (memcmp(wei, WEI_18446744, WEI_LEN) == 0));
    assert(evm_units_to_wei(18446745ULL, wei) && (memcmp(wei, WEI_18446745, WEI_LEN) == 0));
    assert(evm_units_to_wei(AMOUNT_UNITS_MAX, wei) &&
           (memcmp(wei, WEI_9999990000, WEI_LEN) == 0));
    /* The keypad ceiling, keyed, is exactly that value. */
    assert(amount_cents_to_units(AMOUNT_CENTS_MAX) == AMOUNT_UNITS_MAX);
    /* Past it is refused, and the output untouched. */
    memset(wei, 0x42, WEI_LEN);
    assert(!evm_units_to_wei(AMOUNT_UNITS_MAX + 1U, wei));
    assert(!evm_units_to_wei(UINT64_MAX, wei));
    assert((wei[0] == 0x42U) && (wei[WEI_LEN - 1U] == 0x42U));

    /* ── Keypad keys ──────────────────────────────────────────────── */
    const uint64_t cap_n = AMOUNT_CENTS_MAX;
    uint64_t c = 0U;
    for (int i = 0; i < 6; i++) {
        c = amount_key_digit(c, 9U, cap_n);   /* 0.09 ... 9999.99 */
    }
    assert(c == 999999U);                                  /* exactly the cap */
    assert(amount_key_digit(c, 0U, cap_n) == 999999U);     /* 99999.90 refused whole */
    assert(amount_key_digit(1844U, 5U, cap_n) == 18445U);  /* 184.45: past the old 18.44 */
    assert(amount_key_00(18U, cap_n) == 1800U);
    assert(amount_key_00(10000U, cap_n) == 999999U);       /* 10000.00 clamps */
    assert(amount_key_00(9999U, cap_n) == 999900U);
    assert(amount_key_back(1844U) == 184U);
    assert(amount_key_back(0U) == 0U);
    assert(amount_cents_to_units(1250U) == 12500000U);

    /* ── Amount text: two places, truncated ───────────────────────── */
    char buf[32];
    amount_format(12500000U, buf, sizeof(buf));
    assert(strcmp(buf, "12.50") == 0);
    amount_format(0U, buf, sizeof(buf));
    assert(strcmp(buf, "0.00") == 0);
    amount_format(999999ULL * 10000ULL, buf, sizeof(buf));
    assert(strcmp(buf, "9999.99") == 0);
    amount_format(18446744ULL, buf, sizeof(buf));
    assert(strcmp(buf, "18.44") == 0);
    amount_format(19999ULL, buf, sizeof(buf));        /* 0.019999: truncated */
    assert(strcmp(buf, "0.01") == 0);

    /* ── EIP-1559 fees from the Gwei settings ─────────────────────── */
    uint64_t mf = 0U;
    uint64_t pf = 0U;
    evm_fees_from_gwei(30U, 2U, false, 30U, &mf, &pf);
    assert((mf == 30000000000ULL) && (pf == 2000000000ULL));
    /* A tip above the cap is clamped to it, or the transaction is malformed. */
    evm_fees_from_gwei(10U, 20U, false, 30U, &mf, &pf);
    assert((mf == 10000000000ULL) && (pf == 10000000000ULL));
    /* Polygon: the floor raises the tip, and the cap with it. */
    evm_fees_from_gwei(20U, 2U, true, 30U, &mf, &pf);
    assert((mf == 30000000000ULL) && (pf == 30000000000ULL));
    /* ...but not above what the operator already asked for. */
    evm_fees_from_gwei(50U, 40U, true, 30U, &mf, &pf);
    assert((mf == 50000000000ULL) && (pf == 40000000000ULL));
    evm_fees_from_gwei(50U, 2U, true, 30U, &mf, &pf);
    assert((mf == 50000000000ULL) && (pf == 30000000000ULL));
    /* The whole uint32 range scales without wrapping. */
    evm_fees_from_gwei(UINT32_MAX, UINT32_MAX, false, 30U, &mf, &pf);
    assert((mf == 4294967295000000000ULL) && (pf == 4294967295000000000ULL));

    /* ── Pre-flight funds ─────────────────────────────────────────── */
    const uint64_t gas = 21000ULL * 30000000000ULL;   /* 630000000000000 wei */
    uint8_t have[WEI_LEN];
    wei_from_u64(630000000000000ULL, have);
    assert(evm_funds_check(true, have, gas, 0U) == EVM_FUNDS_OK);
    wei_from_u64(629999999999999ULL, have);
    assert(evm_funds_check(true, have, gas, 0U) == EVM_FUNDS_SHORT_GAS);
    wei_from_u64(631000000000000ULL, have);
    assert(evm_funds_check(true, have, gas, 1U) == EVM_FUNDS_OK);
    wei_from_u64(630999999999999ULL, have);
    assert(evm_funds_check(true, have, gas, 1U) == EVM_FUNDS_SHORT_VALUE);
    /* Past 2^64 on both sides: 9999.99 + the gas, exactly and 1 wei short. */
    assert(evm_funds_check(true, HAVE_21e19bf7fc390b46000, gas, AMOUNT_UNITS_MAX)
           == EVM_FUNDS_OK);
    assert(evm_funds_check(true, HAVE_21e19bf7fc390b45fff, gas, AMOUNT_UNITS_MAX)
           == EVM_FUNDS_SHORT_VALUE);
    /* 2^64 - 1 wei (~18.44) no longer passes for a balance that pays 18.45. */
    wei_from_u64(UINT64_MAX, have);
    assert(evm_funds_check(true, have, gas, 18450000ULL) == EVM_FUNDS_SHORT_VALUE);
    /* Past the keypad cap is not a verdict: the payment path refuses it by name. */
    assert(evm_funds_check(true, HAVE_21e19bf7fc390b46000, gas, AMOUNT_UNITS_MAX + 1U)
           == EVM_FUNDS_UNKNOWN);
    /* A token only needs the gas in wei; its own balance is a separate read. */
    wei_from_u64(gas, have);
    assert(evm_funds_check(false, have, gas, 999999999U) == EVM_FUNDS_OK);
    wei_from_u64(gas - 1U, have);
    assert(evm_funds_check(false, have, gas, 0U) == EVM_FUNDS_SHORT_GAS);

    /* ── Fee ceiling text, rounded up ─────────────────────────────── */
    for (size_t i = 0U; i < (sizeof(COIN_TEXT) / sizeof(COIN_TEXT[0])); i++) {
        char exp[48];
        (void)snprintf(exp, sizeof(exp), "%s ETH", COIN_TEXT[i].text);
        fmt_coin(buf, sizeof(buf), COIN_TEXT[i].v, COIN_TEXT[i].dec, "ETH");
        if (strcmp(buf, exp) != 0) {
            printf("fmt_coin(%llu, %u): got \"%s\", want \"%s\"\n",
                   (unsigned long long)COIN_TEXT[i].v, COIN_TEXT[i].dec, buf, exp);
            assert(false);
        }
    }

    printf("test_money ... OK\n");
    return 0;
}
