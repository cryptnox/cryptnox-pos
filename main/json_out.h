/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file json_out.h
 * @ingroup device
 * @brief Escaping for values the config portal puts inside a JSON string.
 *
 * Header-only and free of ESP-IDF dependencies, for the same reason form_parse.h
 * is: one of the values that goes through here is a Wi-Fi SSID, which is 32
 * arbitrary bytes chosen by whoever named their router. A bare quote in one
 * produces a response the page cannot parse — the whole config UI goes blank on a
 * device with no console to read — so it is kept where a host test can reach it
 * (tests/units/test_json_out.cpp).
 *
 * Not a JSON writer. It escapes one string value; the surrounding braces, keys and
 * commas are the caller's snprintf, which is fine because every one of those is a
 * literal in provision.cpp.
 */

#ifndef JSON_OUT_H
#define JSON_OUT_H

#include <stddef.h>
#include <stdio.h>

/**
 * @brief Copy @p val into @p out, escaping what JSON requires.
 *
 * Handles the two characters that would end the string early (`"` and `\`) and the
 * C0 controls, which JSON forbids raw. Everything else is passed through as bytes,
 * including UTF-8 — an SSID is not required to be valid UTF-8 and re-encoding it
 * would be a second way to get it wrong.
 *
 * Truncates rather than overflowing, and never mid-escape: the loop stops while
 * there is still room for the longest single expansion (``, six bytes) plus
 * the NUL, so a clipped value is still parseable JSON.
 *
 * @param[out] out Destination, always NUL-terminated when @p n > 0.
 * @param[in]  n   Capacity of @p out including the NUL.
 * @param[in]  val Value to escape; NULL is treated as empty.
 * @return bytes written, excluding the NUL.
 */
static inline size_t json_escape(char *out, size_t n, const char *val)
{
    if ((out == NULL) || (n == 0U)) { return 0U; }
    out[0] = '\0';
    if (val == NULL) { return 0U; }

    size_t w = 0U;
    for (const char *p = val; *p != '\0'; p++) {
        const unsigned char c = (unsigned char)*p;
        /* Room for the worst case (6) plus the NUL before committing to anything,
         * so the output never ends half way through an escape sequence. */
        if ((w + 7U) > n) { break; }
        if ((c == '"') || (c == '\\')) {
            out[w++] = '\\';
            out[w++] = (char)c;
        } else if (c < 0x20U) {
            const int k = snprintf(&out[w], n - w, "\\u%04x", (unsigned)c);
            if (k < 0) { break; }
            w += (size_t)k;
        } else {
            out[w++] = (char)c;
        }
    }
    out[w] = '\0';
    return w;
}

#endif /* JSON_OUT_H */
