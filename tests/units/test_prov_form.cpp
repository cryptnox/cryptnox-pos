/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 *
 * Host test for main/form_parse.h — the setup portal's urlencoded field
 * extraction. This is the only part of the portal that parses input controlled
 * by whoever is on the setup AP, and one of the fields it extracts is a payout
 * address, so its edge cases are worth pinning down.
 *
 *   g++ -std=c++14 -Wall -Imain tests/units/test_prov_form.cpp -o t && ./t
 */

#include "form_parse.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/** @brief form_field into a fixed buffer, for terser assertions. */
static const char *ff(const char *body, const char *key)
{
    static char out[128];
    (void)form_field(body, key, out, sizeof(out));
    return out;
}

int main(void)
{
    /* ── Plain extraction ──────────────────────────────────────── */
    assert(strcmp(ff("code=1234", "code"), "1234") == 0);
    assert(strcmp(ff("a=1&code=1234&b=2", "code"), "1234") == 0);
    assert(strcmp(ff("code=1234&b=2", "b"), "2") == 0);
    assert(strcmp(ff("x=1&last=9", "last"), "9") == 0);

    /* Missing, empty and absent-value fields all read as empty. */
    assert(strcmp(ff("code=1234", "pass"), "") == 0);
    assert(strcmp(ff("code=&x=1", "code"), "") == 0);
    assert(strcmp(ff("", "code"), "") == 0);
    assert(form_field(NULL, "code", NULL, 0) == 0U);

    /* ── Prefix anchoring ──────────────────────────────────────── */
    /* The bug this guards: "pass" must not be answered by "wifipass", or a
     * crafted body could feed the wrong value into a field that matters. */
    assert(strcmp(ff("wifipass=zzz&pass=yyy", "pass"), "yyy") == 0);
    assert(strcmp(ff("wifipass=zzz", "pass"), "") == 0);
    assert(strcmp(ff("xcode=zzz&code=42", "code"), "42") == 0);
    /* A key that is a prefix of the real one must not match it either. */
    assert(strcmp(ff("address=0xdead", "addr"), "") == 0);

    /* ── Percent and plus decoding ─────────────────────────────── */
    assert(strcmp(ff("ssid=My+Cafe", "ssid"), "My Cafe") == 0);
    assert(strcmp(ff("ssid=My%20Cafe", "ssid"), "My Cafe") == 0);
    assert(strcmp(ff("pass=a%26b", "pass"), "a&b") == 0);
    assert(strcmp(ff("pass=%2B%3D%25", "pass"), "+=%") == 0);
    assert(strcmp(ff("s=caf%C3%A9", "s"), "caf\xC3\xA9") == 0);
    /* Lower- and upper-case hex both decode. */
    assert(strcmp(ff("s=%2f%2F", "s"), "//") == 0);

    /* ── Malformed escapes stop, they do not guess ─────────────── */
    assert(strcmp(ff("s=ab%", "s"), "ab") == 0);
    assert(strcmp(ff("s=ab%4", "s"), "ab") == 0);
    assert(strcmp(ff("s=ab%zz&t=1", "s"), "ab") == 0);
    /* An escape truncated by the field separator stops at the separator. */
    assert(strcmp(ff("s=ab%2&t=1", "s"), "ab") == 0);

    /* ── An embedded NUL: bytes written != string length ───────── */
    /* "%00" is a legal escape and decodes to a real NUL, so the return value and
     * strlen() disagree from that point on. Pinned because the callers in
     * provision.cpp must validate with strlen: a caller trusting the return value
     * would see a 4-digit admin code in "1%002345" or a non-empty SSID in "%00".
     * The value in play is what C reads, which is the shorter one. */
    char nul[16];
    assert(form_field("s=12%003456", "s", nul, sizeof(nul)) == 7U);
    assert(strlen(nul) == 2U);
    assert(strcmp(nul, "12") == 0);
    assert(form_field("s=%00", "s", nul, sizeof(nul)) == 1U);
    assert(strlen(nul) == 0U);

    /* ── Field boundaries ──────────────────────────────────────── */
    /* A decoded '&' must not be mistaken for a separator, and a real one must. */
    assert(strcmp(ff("s=x%26y&t=z", "s"), "x&y") == 0);
    assert(strcmp(ff("s=x&t=z", "s"), "x") == 0);
    /* An encoded '=' inside a value survives. */
    assert(strcmp(ff("s=a%3Db", "s"), "a=b") == 0);

    /* ── Truncation ────────────────────────────────────────────── */
    /* Never writes past the buffer, always NUL-terminates. The caller's own
     * length check is what rejects the truncated value. */
    char small[5];
    const size_t w = form_field("s=abcdefgh", "s", small, sizeof(small));
    assert(w == 4U);
    assert(strcmp(small, "abcd") == 0);

    char one[1];
    assert(form_field("s=abc", "s", one, sizeof(one)) == 0U);
    assert(one[0] == '\0');

    /* A multi-byte escape must not be half-written at the boundary: with room
     * for 4 characters the 5th is dropped whole, not as a stray byte. */
    char four[5];
    (void)form_field("s=%41%42%43%44%45", "s", four, sizeof(four));
    assert(strcmp(four, "ABCD") == 0);

    /* ── A realistic payout-address body ──────────────────────── */
    const char *body =
        "addr=0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed&net=eth";
    assert(strcmp(ff(body, "addr"),
                  "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed") == 0);
    assert(strcmp(ff(body, "net"), "eth") == 0);
    /* Field order must not matter. */
    const char *rev = "net=tron&addr=THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdc";
    assert(strcmp(ff(rev, "addr"), "THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdc") == 0);
    assert(strcmp(ff(rev, "net"), "tron") == 0);

    printf("test_prov_form ... OK\n");
    return 0;
}
