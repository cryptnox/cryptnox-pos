/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file form_parse.h
 * @ingroup device
 * @brief application/x-www-form-urlencoded field extraction.
 *
 * Header-only and free of ESP-IDF dependencies on purpose: this is the one piece
 * of the setup portal that parses attacker-shaped input, so it is kept where a
 * host-side test can reach it (tests/units/test_prov_form.cpp).
 */

#ifndef FORM_PARSE_H
#define FORM_PARSE_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/** @brief Hex nibble value, or -1 if @p c is not a hex digit. */
static inline int form_hexval(char c)
{
    if ((c >= '0') && (c <= '9')) { return c - '0'; }
    if ((c >= 'a') && (c <= 'f')) { return (c - 'a') + 10; }
    if ((c >= 'A') && (c <= 'F')) { return (c - 'A') + 10; }
    return -1;
}

/**
 * @brief Pull one field out of a urlencoded body, percent-decoding it.
 *
 * @param[in]  body NUL-terminated request body.
 * @param[in]  key  Field name to find.
 * @param[out] out  Decoded value; always NUL-terminated, empty if not found.
 * @param[in]  n    Capacity of @p out, including the NUL.
 * @return number of bytes WRITTEN, which is not necessarily @c strlen(out): a
 *         submitted "%00" decodes to a real NUL byte, so "12%003456" writes 9
 *         bytes of which C sees 2. Validate with @c strlen, and never treat this
 *         return value as the length of a string — a length check that passes on
 *         9 while the value in play is "12" is exactly the confusion an attacker
 *         submitting escapes is looking for.
 */
static inline size_t form_field(const char *body, const char *key,
                               char *out, size_t n)
{
    if ((out == NULL) || (n == 0U)) { return 0U; }
    out[0] = '\0';
    if ((body == NULL) || (key == NULL)) { return 0U; }

    char pat[32];
    (void)snprintf(pat, sizeof(pat), "%s=", key);
    const size_t patlen = strlen(pat);

    /* Anchored at the body start or just after an '&', so a field called
     * "wifipass" cannot answer a lookup for "pass". */
    const char *p = body;
    while (p != NULL) {
        if ((strncmp(p, pat, patlen) == 0) && ((p == body) || (p[-1] == '&'))) {
            p += patlen;
            break;
        }
        p = strchr(p, '&');
        if (p != NULL) { p++; }
    }
    if (p == NULL) { return 0U; }

    size_t w = 0U;
    while ((*p != '\0') && (*p != '&') && (w < (n - 1U))) {
        if (*p == '+') {
            out[w++] = ' ';
            p++;
        } else if (*p == '%') {
            const int hi = (p[1] != '\0') ? form_hexval(p[1]) : -1;
            const int lo = ((hi >= 0) && (p[2] != '\0')) ? form_hexval(p[2]) : -1;
            /* Malformed escape: stop rather than guess. A truncated value is
             * rejected by the caller's own checks; a guessed one might not be. */
            if (lo < 0) { break; }
            out[w++] = static_cast<char>((hi << 4) | lo);
            p += 3;
        } else {
            out[w++] = *p++;
        }
    }
    out[w] = '\0';
    return w;
}

#endif /* FORM_PARSE_H */
