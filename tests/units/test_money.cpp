/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 *
 * Known-answer test for main/money.h — keypad cents, the native-coin ceiling,
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

    /* ── Units -> wei around POS_AMOUNT_UNITS_MAX_NATIVE ──────────── */
    /* 18446743 and 18446744 units fit a uint64 of wei; 18446745 is
     * 18446745000000000000 > 2^64 - 1 and must be refused, not wrapped. */
    uint64_t wei = 0U;
    assert(POS_AMOUNT_UNITS_MAX_NATIVE == 18446744ULL);
    assert(evm_units_to_wei(0U, &wei) && (wei == 0U));
    assert(evm_units_to_wei(1U, &wei) && (wei == 1000000000000ULL));
    assert(evm_units_to_wei(18446743ULL, &wei) && (wei == 18446743000000000000ULL));
    assert(evm_units_to_wei(18446744ULL, &wei) && (wei == 18446744000000000000ULL));
    wei = 42U;
    assert(!evm_units_to_wei(18446745ULL, &wei));
    assert(wei == 42U);   /* untouched on refusal */
    assert(!evm_units_to_wei(UINT64_MAX, &wei));

    /* ── Keypad ceilings ──────────────────────────────────────────── */
    assert(amount_cents_cap(false) == 999999ULL);   /* 9999.99 */
    assert(amount_cents_cap(true)  == 1844ULL);     /* 18.44 */
    /* The native ceiling, keyed, is a value the wei conversion accepts. */
    assert(evm_units_to_wei(amount_cents_to_units(amount_cents_cap(true)), &wei));
    assert(wei == 18440000000000000000ULL);
    /* The token ceiling is not — which is why it is only the token ceiling. */
    assert(!evm_units_to_wei(amount_cents_to_units(amount_cents_cap(false)), &wei));

    /* ── Keypad keys ──────────────────────────────────────────────── */
    const uint64_t cap_n = amount_cents_cap(true);
    uint64_t c = 0U;
    c = amount_key_digit(c, 1U, cap_n);   /* 0.01 */
    c = amount_key_digit(c, 8U, cap_n);   /* 0.18 */
    c = amount_key_digit(c, 4U, cap_n);   /* 1.84 */
    c = amount_key_digit(c, 4U, cap_n);   /* 18.44 — exactly the cap */
    assert(c == 1844U);
    assert(amount_key_digit(c, 0U, cap_n) == 1844U);   /* 184.40 refused whole */
    assert(amount_key_digit(184U, 5U, cap_n) == 184U); /* 18.45 refused */
    assert(amount_key_digit(184U, 4U, cap_n) == 1844U);
    assert(amount_key_00(18U, cap_n) == 1800U);
    assert(amount_key_00(19U, cap_n) == 1844U);        /* 19.00 clamps to 18.44 */
    assert(amount_key_00(999999U, amount_cents_cap(false)) == 999999U);
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
    assert(evm_funds_check(true, 630000000000000ULL, gas, 0U) == EVM_FUNDS_OK);
    assert(evm_funds_check(true, 629999999999999ULL, gas, 0U) == EVM_FUNDS_SHORT_GAS);
    assert(evm_funds_check(true, 631000000000000ULL, gas, 1U) == EVM_FUNDS_OK);
    assert(evm_funds_check(true, 630999999999999ULL, gas, 1U) == EVM_FUNDS_SHORT_VALUE);
    /* The sum that would wrap: 18446744 units + the gas is past 2^64, so no
     * balance can pay it — a check that added instead of subtracting would
     * have said yes. */
    assert(evm_funds_check(true, UINT64_MAX, gas, 18446744ULL) == EVM_FUNDS_SHORT_VALUE);
    assert(evm_funds_check(true, UINT64_MAX, gas, 18446114ULL) == EVM_FUNDS_OK);
    assert(evm_funds_check(true, UINT64_MAX, gas, 18446115ULL) == EVM_FUNDS_SHORT_VALUE);
    /* Past the native cap is not a verdict: the payment path refuses it by name. */
    assert(evm_funds_check(true, UINT64_MAX, gas, 18446745ULL) == EVM_FUNDS_UNKNOWN);
    /* A token only needs the gas in wei; its own balance is a separate read. */
    assert(evm_funds_check(false, gas, gas, 999999999U) == EVM_FUNDS_OK);
    assert(evm_funds_check(false, gas - 1U, gas, 0U) == EVM_FUNDS_SHORT_GAS);

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
