/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 *
 * Host test for main/settings_rules.h — what a setting may be, without the NVS
 * around it: the time-zone range and its biased storage form, the DST rule, the
 * config page's gas-fee bounds, the admin-code digest and the address form the
 * dual-stored payout and contract slots compare on.
 *
 * The admin digests are keccak-256 from pycryptodome, via
 * tools/gen_kat_vectors.py — not from the keccak256.cpp under test.
 *
 *   g++ -std=c++14 -Wall -Imain -Icryptnox-sdk-esp32/cryptnox-sdk-cpp \
 *       tests/units/test_settings_rules.cpp -o t && ./t
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* CW_Utils::fill_secure_random is ESP32-specific and never reached from
 * secure_wipe; stub it for the linker. */
#include "CW_Utils.h"
bool CW_Utils::fill_secure_random(uint8_t *dest, size_t len)
{
    (void)dest;
    (void)len;
    return false;
}

#include "CW_Utils.cpp"
#include "keccak256.cpp"
#include "settings_rules.h"

/* Salt 00 01 .. 0f. */
static const uint8_t ADMIN_123456[32] = {
    0x69, 0x12, 0xef, 0x63, 0x0a, 0xd8, 0x3a, 0x07, 0x29, 0xb7, 0xbf, 0x7c,
    0x4e, 0x92, 0x7d, 0xb1, 0xdf, 0xa8, 0x4f, 0x1d, 0x48, 0x2f, 0xf4, 0xc1,
    0x18, 0xb7, 0x10, 0x1b, 0xb6, 0x89, 0x6d, 0xb8,
};
static const uint8_t ADMIN_0000[32] = {
    0x3c, 0x26, 0xa4, 0x4d, 0xae, 0x38, 0x7a, 0x90, 0x58, 0x9a, 0xd0, 0x3e,
    0xef, 0x3a, 0xac, 0xde, 0x2d, 0x16, 0xc2, 0x65, 0x1b, 0x49, 0x58, 0x2d,
    0x86, 0x11, 0x33, 0x1e, 0x28, 0xeb, 0xb8, 0x9d,
};
static const uint8_t ADMIN_987654321[32] = {
    0x2f, 0x63, 0x10, 0x34, 0x37, 0x7d, 0xf4, 0x4d, 0xe3, 0x84, 0xf2, 0xbd,
    0x5c, 0xdb, 0x66, 0x0c, 0xd5, 0xec, 0x50, 0xd8, 0x3b, 0x9a, 0xc4, 0x2f,
    0xd6, 0xc6, 0xb6, 0x59, 0x14, 0x8d, 0xc0, 0x20,
};
/* 40 x 'x': only the first ADMIN_CODE_HASH_MAX (32) reach the digest. */
static const uint8_t ADMIN_LONG[32] = {
    0x1c, 0xa7, 0xe1, 0xbe, 0x55, 0xc5, 0xc1, 0x74, 0xad, 0x44, 0xfc, 0xc0,
    0x7e, 0x00, 0x49, 0x46, 0xe3, 0xf1, 0x9e, 0xe7, 0xc0, 0xa3, 0x0b, 0x52,
    0x7b, 0xed, 0x9a, 0x20, 0x29, 0x25, 0x73, 0x98,
};

static const char *norm(bool tron, const char *addr)
{
    static char out[SETTINGS_PAYOUT_MAX];
    memset(out, 0, sizeof(out));
    return settings_addr_normalise(tron, addr, out) ? out : NULL;
}

int main(void)
{
    /* ── Time zone: UTC-12:00 .. UTC+14:00 ─────────────────────────── */
    assert(tz_offset_valid(-720));
    assert(tz_offset_valid(0));
    assert(tz_offset_valid(840));
    assert(!tz_offset_valid(-721));
    assert(!tz_offset_valid(841));
    assert(!tz_offset_valid(-2147483647L));

    /* Stored biased by 720, and every valid offset round-trips. */
    assert(tz_offset_encode(-720) == 0U);
    assert(tz_offset_encode(0) == 720U);
    assert(tz_offset_encode(840) == 1560U);
    for (int m = TZ_OFFSET_MIN; m <= TZ_OFFSET_MAX; m++) {
        assert(tz_offset_decode(tz_offset_encode((int16_t)m)) == m);
    }
    /* A missing key (default = the bias) reads as UTC... */
    assert(tz_offset_decode((uint32_t)TZ_OFFSET_BIAS) == 0);
    /* ...and anything past the top of the range is UTC, not a wild clock. */
    assert(tz_offset_decode(1561U) == 0);
    assert(tz_offset_decode(0xFFFFFFFFU) == 0);

    /* ── DST rule ─────────────────────────────────────────────────── */
    for (long r = 0; r < (long)CIVIL_DST__COUNT; r++) {
        assert(tz_dst_valid(r));
        assert(tz_dst_decode((uint8_t)r) == (uint8_t)r);
    }
    assert(!tz_dst_valid(-1));
    assert(!tz_dst_valid((long)CIVIL_DST__COUNT));
    assert(tz_dst_decode((uint8_t)CIVIL_DST__COUNT) == (uint8_t)CIVIL_DST_NONE);
    assert(tz_dst_decode(255U) == (uint8_t)CIVIL_DST_NONE);

    /* ── Gas fees from the config page: 1..500 Gwei, tip <= cap ───── */
    assert(fee_pair_check(1UL, 1UL) == FEE_PAIR_OK);
    assert(fee_pair_check(500UL, 500UL) == FEE_PAIR_OK);
    assert(fee_pair_check(30UL, 2UL) == FEE_PAIR_OK);
    assert(fee_pair_check(0UL, 1UL) == FEE_PAIR_OUT_OF_RANGE);
    assert(fee_pair_check(1UL, 0UL) == FEE_PAIR_OUT_OF_RANGE);
    assert(fee_pair_check(501UL, 1UL) == FEE_PAIR_OUT_OF_RANGE);
    assert(fee_pair_check(500UL, 501UL) == FEE_PAIR_OUT_OF_RANGE);
    assert(fee_pair_check(20UL, 21UL) == FEE_PAIR_TIP_ABOVE_MAX);
    /* Both wrong: the range is what gets reported. */
    assert(fee_pair_check(0UL, 5UL) == FEE_PAIR_OUT_OF_RANGE);
    /* strtoul's answer for "-1" is ULONG_MAX, which must not slip through. */
    assert(fee_pair_check((unsigned long)-1, 1UL) == FEE_PAIR_OUT_OF_RANGE);

    /* ── Admin code digest: keccak256(salt || code[:32]) ──────────── */
    uint8_t salt[ADMIN_SALT_LEN];
    for (size_t i = 0U; i < sizeof(salt); i++) { salt[i] = (uint8_t)i; }
    uint8_t d[ADMIN_HASH_LEN];
    admin_derive("123456", salt, d);
    assert(memcmp(d, ADMIN_123456, sizeof(d)) == 0);
    admin_derive("0000", salt, d);
    assert(memcmp(d, ADMIN_0000, sizeof(d)) == 0);
    admin_derive("987654321", salt, d);
    assert(memcmp(d, ADMIN_987654321, sizeof(d)) == 0);
    admin_derive("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", salt, d);
    assert(memcmp(d, ADMIN_LONG, sizeof(d)) == 0);
    /* A different salt is a different digest for the same code. */
    salt[0] ^= 1U;
    admin_derive("123456", salt, d);
    assert(memcmp(d, ADMIN_123456, sizeof(d)) != 0);

    /* ── Stored address form ──────────────────────────────────────── */
    const char *bare = "5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed";
    const char *hex  = "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed";
    assert(strcmp(norm(false, bare), hex) == 0);
    assert(strcmp(norm(false, hex), hex) == 0);
    assert(strcmp(norm(false, "0X5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed"), hex) == 0);
    /* Tron is stored as given — a leading "0x" is not stripped from it. */
    assert(strcmp(norm(true, "THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdc"),
                  "THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdc") == 0);
    assert(strcmp(norm(true, "0xabc"), "0xabc") == 0);
    /* Refused: nothing, empty, or no room for the NUL. */
    assert(norm(false, NULL) == NULL);
    assert(norm(false, "") == NULL);
    char long_addr[SETTINGS_PAYOUT_MAX + 1];
    memset(long_addr, 'a', SETTINGS_PAYOUT_MAX);
    long_addr[SETTINGS_PAYOUT_MAX] = '\0';
    assert(norm(true, long_addr) == NULL);
    long_addr[SETTINGS_PAYOUT_MAX - 1U] = '\0';          /* 63 characters */
    assert(strlen(norm(true, long_addr)) == SETTINGS_PAYOUT_MAX - 1U);
    /* Pinned, not endorsed: 63 bare characters on the Ethereum side come back
     * with the prefix and lose their last two to the buffer. Nothing valid is
     * that long — an address is 40 — so the parse after this rejects it. */
    assert(strlen(norm(false, long_addr)) == SETTINGS_PAYOUT_MAX - 1U);
    assert(strncmp(norm(false, long_addr), "0xaaa", 5) == 0);

    printf("test_settings_rules ... OK\n");
    return 0;
}
