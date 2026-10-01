/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * test_json_out.cpp — host unit test for main/json_out.h.
 *
 * Why this is worth a test: `GET /api/state` and `GET /api/scan` embed a Wi-Fi SSID
 * in a JSON string, and an SSID is 32 arbitrary bytes chosen by whoever named the
 * router. Get the escaping wrong and the response stops parsing — which on this
 * device means the entire config UI goes blank, during setup, with no console to
 * read the reason from.
 *
 * Assertions are on the escaped output AND on the result parsing as JSON, checked
 * with a deliberately strict miniature parser below rather than by eye.
 *
 * Build & run from the repo root:
 *
 *   g++ -std=c++14 -Wall -Imain tests/units/test_json_out.cpp -o test_json_out \
 *       && ./test_json_out
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "json_out.h"

/*
 * Walk a JSON string body — the bytes that would sit between the quotes — and
 * report whether it is well formed. Deliberately strict: a lone backslash, an
 * unescaped quote or a raw control byte is a malformed document, which is exactly
 * the failure this test exists to catch.
 */
static bool json_string_body_ok(const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        const unsigned char c = (unsigned char)*p;
        if (c == '"') { return false; }            /* would end the string */
        if (c < 0x20U) { return false; }           /* raw control byte */
        if (c == '\\') {
            const char e = p[1];
            if (e == 'u') {
                for (int i = 2; i <= 5; i++) {
                    const char h = p[i];
                    const bool hex = ((h >= '0') && (h <= '9')) ||
                                     ((h >= 'a') && (h <= 'f')) ||
                                     ((h >= 'A') && (h <= 'F'));
                    if (!hex) { return false; }
                }
                p += 5;
            } else if ((e == '"') || (e == '\\') || (e == '/') || (e == 'b') ||
                       (e == 'f') || (e == 'n') || (e == 'r') || (e == 't')) {
                p += 1;
            } else {
                return false;                      /* invalid escape, incl. trailing \ */
            }
        }
    }
    return true;
}

int main(void)
{
    char b[256];

    /* Nothing to escape: byte-for-byte passthrough. */
    assert(json_escape(b, sizeof(b), "My Cafe") == 7U);
    assert(strcmp(b, "My Cafe") == 0);

    /* The two characters that would end the string early. */
    assert(json_escape(b, sizeof(b), "say \"hi\"") > 0U);
    assert(strcmp(b, "say \\\"hi\\\"") == 0);
    assert(json_string_body_ok(b));

    assert(json_escape(b, sizeof(b), "C:\\net") > 0U);
    assert(strcmp(b, "C:\\\\net") == 0);
    assert(json_string_body_ok(b));

    /* A backslash immediately before the closing quote — the classic way to make a
     * parser swallow the quote and run off the end of the document. */
    (void)json_escape(b, sizeof(b), "trailing\\");
    assert(strcmp(b, "trailing\\\\") == 0);
    assert(json_string_body_ok(b));

    /* C0 controls: JSON forbids them raw. A tab and a newline in an SSID are odd
     * but entirely legal over the air. */
    (void)json_escape(b, sizeof(b), "a\tb\nc");
    assert(strcmp(b, "a\\u0009b\\u000ac") == 0);
    assert(json_string_body_ok(b));

    /* DEL (0x7f) is NOT a C0 control and JSON does not require escaping it. */
    (void)json_escape(b, sizeof(b), "a\x7f" "b");
    assert(strcmp(b, "a\x7f" "b") == 0);

    /* UTF-8 passes through as bytes. Re-encoding would be a second way to get an
     * SSID wrong, and an SSID is not required to be valid UTF-8 in the first place. */
    (void)json_escape(b, sizeof(b), "Caf\xc3\xa9");
    assert(strcmp(b, "Caf\xc3\xa9") == 0);
    assert(json_string_body_ok(b));

    /* Empty and NULL. */
    assert(json_escape(b, sizeof(b), "") == 0U);
    assert(b[0] == '\0');
    assert(json_escape(b, sizeof(b), NULL) == 0U);
    assert(b[0] == '\0');

    /* Degenerate buffers must not write past the end. */
    assert(json_escape(b, 0U, "x") == 0U);
    assert(json_escape(NULL, sizeof(b), "x") == 0U);

    /* Truncation. The point is not where it stops but that what it produced is
     * still valid JSON — never clipped mid-escape. Every buffer size from tiny to
     * comfortable, against a value made entirely of expanding characters. */
    for (size_t n = 1U; n <= 40U; n++) {
        char small[64];
        memset(small, 0x7f, sizeof(small));       /* poison, to catch a short NUL */
        const size_t w = json_escape(small, n, "\"\\\t\"\\\t\"\\\t\"\\\t");
        assert(w < n);                            /* room was left for the NUL */
        assert(small[w] == '\0');
        assert(strlen(small) == w);
        assert(json_string_body_ok(small));
        /* And nothing beyond the NUL was touched. */
        for (size_t i = n; i < sizeof(small); i++) { assert(small[i] == 0x7f); }
    }

    /* The real shape: a 32-character SSID full of quotes, into the 132-byte buffer
     * provision.cpp actually gives it. 32 quotes expand to 64 bytes, so this must
     * survive whole rather than truncate. */
    char worst[33];
    memset(worst, '"', 32);
    worst[32] = '\0';
    const size_t w = json_escape(b, 132U, worst);
    assert(w == 64U);
    assert(json_string_body_ok(b));

    puts("json_out unit test OK");
    return 0;
}
