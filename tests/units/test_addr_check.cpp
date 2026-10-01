/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * test_addr_check.cpp — host unit test for the config portal's structural Tron
 * address check (main/addr_check.h).
 *
 * Worth its own test because of what it guards: this is the function that decides
 * whether a string a stranger on the network POSTed is offered to the operator as
 * a payout address. It is not the authoritative check — base58check happens on the
 * main task, which has a crypto provider — so its job is narrower and exact:
 * length, prefix, alphabet, and no accidental "yes" on an empty or near-miss
 * string.
 *
 * Header-only unit, no ESP-IDF. Build & run from the repo root:
 *
 *   g++ -std=c++14 -Imain tests/units/test_addr_check.cpp -o test_addr_check \
 *       && ./test_addr_check
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "addr_check.h"

int main(void)
{
    /* A real Nile address (the config.h default recipient) — 34 chars, T, base58. */
    assert(addr_tron_plausible("THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdc"));
    {
        /* Decoded bytes, pinned against an independent base58 decode. */
        static const unsigned char WANT[25] = {
            0x41, 0x51, 0x86, 0x88, 0xfb, 0xb3, 0x9c, 0xcf, 0x12, 0x53, 0xf2,
            0xb1, 0x21, 0x76, 0x79, 0xfb, 0xe3, 0x16, 0x32, 0x92, 0x88, 0xcb,
            0x5e, 0x85, 0x7f };
        unsigned char b[25];
        assert(addr_tron_decode("THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdc", b));
        assert(memcmp(b, WANT, sizeof(b)) == 0);
        /* One character off decodes to other bytes (the checksum catches it). */
        assert(addr_tron_decode("THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdd", b));
        assert(memcmp(b, WANT, sizeof(b)) != 0);
        /* Too large for 25 bytes, or not a Tron address at all. */
        assert(!addr_tron_decode("Tzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz", b));
        assert(!addr_tron_decode("0xHQGuFzL87ZqhxkgqYEryRAd7gqFqL5rd", b));
    }
    assert(addr_tron_plausible("TXYZopYRdj2D9XRtbG411XZZ3kM5VkAeBf"));

    /* Wrong length: 33 and 35 characters. Off-by-one is the likeliest way a
     * truncated paste arrives, and it must not pass. */
    assert(!addr_tron_plausible("THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rd"));
    assert(!addr_tron_plausible("THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdcc"));

    /* Right length and alphabet, wrong prefix — an Ethereum-style or Bitcoin-style
     * string of the same length is not a Tron address. */
    assert(!addr_tron_plausible("aHQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdc"));
    assert(!addr_tron_plausible("1HQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdc"));

    /* Characters base58 deliberately excludes, at the length that would otherwise
     * pass: 0, O, I and l are the four a human mistypes, which is exactly why the
     * alphabet omits them. */
    assert(!addr_tron_plausible("THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rd0"));
    assert(!addr_tron_plausible("THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdO"));
    assert(!addr_tron_plausible("THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdI"));
    assert(!addr_tron_plausible("THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdl"));

    /* Non-base58 punctuation, and a NUL-terminated-early string: form_field can
     * hand back a value whose bytes continue past what C reads as its end. */
    assert(!addr_tron_plausible("THQGuFzL87ZqhxkgqYEryRAd7gqFqL5r/c"));
    assert(!addr_tron_plausible("THQGuFzL87Zqhxkgq YEryRAd7gqFqL5rd"));

    /* Empty and lone-T: the length test carries these, but assert it, because an
     * alphabet loop over an empty string finds nothing wrong with it. */
    assert(!addr_tron_plausible(""));
    assert(!addr_tron_plausible("T"));
    assert(!addr_tron_plausible(NULL));

    /* addr_is_base58 on its own: it is the piece that answers "yes" too readily,
     * so its empty-string behaviour is pinned here rather than left implied. */
    assert(addr_is_base58("abcXYZ123"));
    assert(!addr_is_base58(""));
    assert(!addr_is_base58(NULL));
    assert(!addr_is_base58("abc0"));   /* zero is not in the alphabet */
    assert(!addr_is_base58("abcO"));
    assert(!addr_is_base58("abcI"));
    assert(!addr_is_base58("abcl"));

    puts("addr_check unit test OK");
    return 0;
}
