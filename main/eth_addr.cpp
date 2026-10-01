/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file eth_addr.cpp
 * @brief Implementation of the hex Ethereum-address parser, with EIP-55
 *        checksum verification (HARDENING.md §7.1).
 */

#include "eth_addr.h"

#include <string.h>
#include <stdbool.h>

#include "keccak256.h"
#include "CW_Utils.h"   /* SDK hardened primitives: secure_wipe */

/**
 * @brief Decode a single ASCII hex digit.
 *
 * @param[in] c Character to decode.
 * @return Nibble value 0–15, or -1 if @p c is not in [0-9a-fA-F].
 */
static int hex_nibble_val(char c)
{
    if ((c >= '0') && (c <= '9')) { return c - '0'; }
    if ((c >= 'a') && (c <= 'f')) { return (c - 'a') + 10; }
    if ((c >= 'A') && (c <= 'F')) { return (c - 'A') + 10; }
    return -1;
}

bool eth_addr_parse(const char *hex, uint8_t out[ETH_ADDR_LEN])
{
    const char *p = hex;
    if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X'))) {
        p += 2;
    }
    if (strlen(p) != ETH_ADDR_HEX_LEN) {
        return false;
    }

    CW_Utils::secure_wipe(out, ETH_ADDR_LEN);

    /* Single pass: validate every nibble, decode the bytes, lower-case a copy
     * for the checksum hash, and note whether the source is mixed-case. */
    char lower[ETH_ADDR_HEX_LEN];
    bool has_upper = false;
    bool has_lower = false;
    size_t i;
    for (i = 0U; i < ETH_ADDR_HEX_LEN; i++) {
        char c = p[i];
        int  v = hex_nibble_val(c);
        if (v < 0) {
            return false;
        }
        if ((c >= 'a') && (c <= 'f')) {
            has_lower = true;
            lower[i]  = c;
        } else if ((c >= 'A') && (c <= 'F')) {
            has_upper = true;
            lower[i]  = static_cast<char>((c - 'A') + 'a');
        } else {
            lower[i] = c;   /* digit — case-neutral */
        }
        if ((i & 1U) == 0U) {
            out[i / 2U] = static_cast<uint8_t>(static_cast<uint8_t>(v) << 4U);
        } else {
            out[i / 2U] |= static_cast<uint8_t>(v);
        }
    }

    /* The zero address is nobody's: a transfer to it burns the funds, and an
     * all-zero string is what a blank or wiped setting decodes to. It is never
     * a payee or a contract this terminal should act on. */
    uint8_t any = 0U;
    for (i = 0U; i < ETH_ADDR_LEN; i++) { any |= out[i]; }
    if (any == 0U) {
        return false;
    }

    /* EIP-55: a mixed-case address carries a checksum, so verify it; an
     * all-lower or all-upper address is treated as un-checksummed and accepted
     * (standard lenient behaviour). A checksummed ADDR_TO thus rejects any
     * single-nibble corruption or typo of the source string. */
    if (has_upper && has_lower) {
        uint8_t h[32];
        keccak256(reinterpret_cast<const uint8_t *>(lower), ETH_ADDR_HEX_LEN, h);
        for (i = 0U; i < ETH_ADDR_HEX_LEN; i++) {
            char c = p[i];
            bool is_alpha = ((c >= 'a') && (c <= 'f')) ||
                            ((c >= 'A') && (c <= 'F'));
            if (!is_alpha) {
                continue;   /* digits have no case to check */
            }
            uint8_t nib = ((i & 1U) == 0U) ? static_cast<uint8_t>(h[i / 2U] >> 4U)
                                           : static_cast<uint8_t>(h[i / 2U] & 0x0FU);
            bool want_upper = (nib >= 8U);
            bool is_upper   = ((c >= 'A') && (c <= 'F'));
            if (want_upper != is_upper) {
                return false;
            }
        }
    }
    return true;
}

bool eth_addr_format(const uint8_t addr[ETH_ADDR_LEN], char *out, size_t n)
{
    /* "0x" + 40 hex + NUL */
    if ((out == NULL) || (n < (ETH_ADDR_HEX_LEN + 3U))) {
        if ((out != NULL) && (n > 0U)) { out[0] = '\0'; }
        return false;
    }

    static const char LOWER[] = "0123456789abcdef";

    /* Lower-case hex first: EIP-55 hashes exactly that string, so it has to
     * exist before any character can be given its case. */
    char lower[ETH_ADDR_HEX_LEN];
    size_t i;
    for (i = 0U; i < ETH_ADDR_LEN; i++) {
        lower[i * 2U]      = LOWER[(addr[i] >> 4U) & 0x0FU];
        lower[(i * 2U) + 1U] = LOWER[addr[i] & 0x0FU];
    }

    uint8_t h[32];
    keccak256(reinterpret_cast<const uint8_t *>(lower), ETH_ADDR_HEX_LEN, h);

    out[0] = '0';
    out[1] = 'x';
    for (i = 0U; i < ETH_ADDR_HEX_LEN; i++) {
        char c = lower[i];
        if ((c >= 'a') && (c <= 'f')) {
            const uint8_t nib = ((i & 1U) == 0U)
                              ? static_cast<uint8_t>(h[i / 2U] >> 4U)
                              : static_cast<uint8_t>(h[i / 2U] & 0x0FU);
            if (nib >= 8U) { c = static_cast<char>((c - 'a') + 'A'); }
        }
        out[i + 2U] = c;
    }
    out[ETH_ADDR_HEX_LEN + 2U] = '\0';
    return true;
}
