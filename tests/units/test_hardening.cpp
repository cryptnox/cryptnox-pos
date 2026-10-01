/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * test_hardening.cpp — host unit test for the decision-integrity gate
 * (HARDENING.md §3, §4). Covers the pure, header-only part of hardening.h;
 * anomaly persistence (hardening.cpp) needs NVS and stays firmware-only.
 *
 * Build & run from the repo root:
 *
 *   g++ -std=c++14 -Imain -Icryptnox-sdk-esp32/cryptnox-sdk-cpp \
 *       tests/units/test_hardening.cpp -o test_hardening && ./test_hardening
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ESP32-specific; never reached from secure_compare. Stub for the linker. */
#include "CW_Utils.h"
bool CW_Utils::fill_secure_random(uint8_t *dest, size_t len)
{
    (void)dest;
    (void)len;
    return false;
}

#include "CW_Utils.cpp"
#include "hardening.h"

/** @brief Fill both address stores with the same recognisable pattern. */
static void set_addr(pos_addr_t *to, uint8_t seed)
{
    size_t i;
    for (i = 0U; i < ETH_ADDR_LEN; i++) {
        to->addr[i]      = static_cast<uint8_t>(seed + i);
        to->addr_echo[i] = static_cast<uint8_t>(seed + i);
    }
}

int main(void)
{
    /* Sentinels are true bitwise complements (also asserted at compile time). */
    assert(IS_TRUE32(TRUE32));
    assert(!IS_TRUE32(FALSE32));
    assert(!IS_TRUE32(0U) && !IS_TRUE32(1U));   /* 0/1 must never read true */

    pos_amount_t amt;
    pos_addr_t   to;
    pos_amount_set(&amt, 1000000ULL);           /* 1.000000 USDC */
    set_addr(&to, 0x11U);

    /* Happy path, and the declined verdict. */
    assert(IS_TRUE32(run_payment_decision(&amt, &to, POS_VERDICT_APPROVED)));
    assert(!IS_TRUE32(run_payment_decision(&amt, &to, POS_VERDICT_DECLINED)));

    /* A garbage verdict token is not "approved" either — fail-closed. */
    assert(!IS_TRUE32(run_payment_decision(&amt, &to, (pos_verdict_t)0U)));

    /* Bit-flip on the amount echo. */
    amt.amount_minor_inv ^= 1ULL;
    assert(!IS_TRUE32(amount_consistent(&amt)));
    assert(!IS_TRUE32(run_payment_decision(&amt, &to, POS_VERDICT_APPROVED)));

    /* Bit-flip on the primary amount. */
    pos_amount_set(&amt, 1000000ULL);
    amt.amount_minor ^= 0x100ULL;
    assert(!IS_TRUE32(run_payment_decision(&amt, &to, POS_VERDICT_APPROVED)));

    /* Bit-flip on the recipient echo, first and last byte. */
    pos_amount_set(&amt, 1000000ULL);
    to.addr_echo[0] ^= 1U;
    assert(!IS_TRUE32(address_consistent(&to)));
    assert(!IS_TRUE32(run_payment_decision(&amt, &to, POS_VERDICT_APPROVED)));

    set_addr(&to, 0x11U);
    to.addr_echo[ETH_ADDR_LEN - 1U] ^= 0x80U;
    assert(!IS_TRUE32(run_payment_decision(&amt, &to, POS_VERDICT_APPROVED)));

    puts("hardening unit test OK");
    return 0;
}
