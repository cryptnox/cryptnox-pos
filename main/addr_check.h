/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file addr_check.h
 * @ingroup device
 * @brief Structural address checks for the config portal.
 *
 * Header-only and free of ESP-IDF dependencies, for the same reason
 * form_parse.h is: this is the code that judges a string a stranger on the
 * network claims is where the money should go, so it is kept where a host-side
 * test can reach it (tests/units/test_addr_check.cpp).
 *
 * The Ethereum side is NOT here — eth_addr_parse() already verifies the EIP-55
 * checksum. For Tron this has the structural check and the base58 decode; the
 * checksum over the decoded bytes (double SHA-256) is done in provision.cpp,
 * which has mbedtls — see addr_plausible() there.
 */

#ifndef ADDR_CHECK_H
#define ADDR_CHECK_H

#include <stddef.h>
#include <string.h>

/** @brief Length of a base58 Tron address, without the NUL. */
#define ADDR_TRON_LEN  34U

/** @brief true if every character of @p s is in the Bitcoin/Tron base58 set. */
static inline bool addr_is_base58(const char *s)
{
    static const char *const B58 =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    if (s == NULL) { return false; }
    /* An empty string has no character outside the alphabet, which would make a
     * plain loop answer "yes". Callers check the length too, but this is the
     * function whose name promises the answer, so it answers correctly. */
    if (*s == '\0') { return false; }
    for (const char *p = s; *p != '\0'; p++) {
        if (strchr(B58, *p) == NULL) { return false; }
    }
    return true;
}

/**
 * @brief Structural check on a Tron address: 34 characters, 'T', base58.
 *
 * Deliberately not the checksum — see the file comment. A value that passes here
 * is still decoded (and can still be refused) on the main task before it is used.
 */
static inline bool addr_tron_plausible(const char *s)
{
    return (s != NULL) && (strlen(s) == ADDR_TRON_LEN) && (s[0] == 'T') &&
           addr_is_base58(s);
}

/**
 * @brief Decode a base58 Tron address into its 25 bytes
 *        (0x41 || 20-byte key hash || 4-byte checksum).
 *
 * @return false if @p s is not a plausible Tron address or does not decode to
 *         exactly 25 bytes. The checksum is NOT verified here.
 */
static inline bool addr_tron_decode(const char *s, unsigned char out[25])
{
    static const char *const B58 =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    if (!addr_tron_plausible(s)) { return false; }
    memset(out, 0, 25U);
    for (const char *p = s; *p != '\0'; p++) {
        unsigned carry = static_cast<unsigned>(strchr(B58, *p) - B58);
        for (int j = 24; j >= 0; j--) {
            carry += 58U * out[j];
            out[j] = static_cast<unsigned char>(carry & 0xFFU);
            carry >>= 8;
        }
        if (carry != 0U) { return false; }   /* more than 25 bytes */
    }
    return out[0] == 0x41U;
}

#endif /* ADDR_CHECK_H */
