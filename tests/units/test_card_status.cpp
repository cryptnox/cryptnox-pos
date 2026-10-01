/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * test_card_status.cpp — host unit test for main/card_status.h, the check that
 * reads a tapped card's SELECT response and says what the card is missing.
 *
 * Worth its own test because of what a wrong answer costs in each direction. A
 * false "not set up" refuses a working card at a till with a customer standing
 * at it. A false "ready" puts the old behaviour back: the holder of a blank card
 * is told the PIN is wrong. So the two flags are asserted from both sides, and
 * every response this cannot judge has to come out UNKNOWN rather than guessed.
 *
 * Header-only unit, no ESP-IDF. Build & run from the repo root:
 *
 *   g++ -std=c++14 -Imain tests/units/test_card_status.cpp -o test_card_status \
 *       && ./test_card_status
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "card_status.h"

/* A SELECT response as the applet sends it: type, three applet-version bytes,
 * 32 data bytes (the second of which is the flags), then 90 00. */
static size_t reply(uint8_t *out, uint8_t type, uint8_t flags,
                    uint8_t sw1 = 0x90U, uint8_t sw2 = 0x00U)
{
    memset(out, 0, 38U);
    out[0] = type;
    out[1] = 0x02U; out[2] = 0x00U; out[3] = 0x00U;   /* applet 2.0.0 */
    out[5] = flags;
    out[36] = sw1;
    out[37] = sw2;
    return 38U;
}

int main(void)
{
    uint8_t r[38];

    /* The three cards. Flag values are the ones cryptnox-sdk-py reads:
     * 0x40 initialised, 0x20 seeded. A card in the field has both. */
    assert(card_state(r, reply(r, 'B', 0x60U)) == CARD_READY);
    assert(card_state(r, reply(r, 'B', 0x00U)) == CARD_NO_PIN);
    assert(card_state(r, reply(r, 'B', 0x40U)) == CARD_NO_KEY);

    /* Other flags in the byte (PIN-auth, PIN-less, extended public key) say
     * nothing about either question and must not move the answer. */
    assert(card_state(r, reply(r, 'B', 0x60U | 0x1CU)) == CARD_READY);
    assert(card_state(r, reply(r, 'B', 0x1CU))         == CARD_NO_PIN);
    assert(card_state(r, reply(r, 'B', 0x40U | 0x1CU)) == CARD_NO_KEY);

    /* An NFT card carries the same two flags, and is a card this terminal can
     * take a payment from — so it is judged, not waved through. */
    assert(card_state(r, reply(r, 'N', 0x60U)) == CARD_READY);
    assert(card_state(r, reply(r, 'N', 0x00U)) == CARD_NO_PIN);

    /* Everything this cannot judge. Each of these used to be a plausible way to
     * refuse a working card, so each has to be UNKNOWN — which the caller treats
     * as "carry on and let the ordinary paths report whatever happens". */
    assert(card_state(NULL, 38U) == CARD_UNKNOWN);
    assert(card_state(r, 0U)     == CARD_UNKNOWN);
    /* Truncated: the flags byte is not there to read. */
    assert(card_state(r, reply(r, 'B', 0x00U)) == CARD_NO_PIN);   /* full: judged */
    assert(card_state(r, 7U) == CARD_UNKNOWN);                    /* short: not   */
    /* A failed SELECT — file not found, applet locked, anything not 90 00. The
     * bytes before the status word are not a layout to trust. */
    assert(card_state(r, reply(r, 'B', 0x00U, 0x6AU, 0x82U)) == CARD_UNKNOWN);
    assert(card_state(r, reply(r, 'B', 0x00U, 0x90U, 0x01U)) == CARD_UNKNOWN);
    /* A card type whose flags byte is not documented here. Bit 6 could mean
     * anything on it, so it is not read. */
    assert(card_state(r, reply(r, 'Z', 0x00U)) == CARD_UNKNOWN);
    assert(card_state(r, reply(r, 0x00U, 0x00U)) == CARD_UNKNOWN);

    /* The operator-facing lines. A refusal has to carry one — silence would be a
     * card that stops working with nothing on the screen — and a usable card has
     * to carry none, or every payment would show a message. */
    assert(card_state_text(CARD_NO_PIN) != NULL);
    assert(card_state_text(CARD_NO_KEY) != NULL);
    assert(strcmp(card_state_text(CARD_NO_PIN),
                  card_state_text(CARD_NO_KEY)) != 0);
    assert(card_state_text(CARD_READY)   == NULL);
    assert(card_state_text(CARD_UNKNOWN) == NULL);
    /* Short enough for the panel's failure line and the config page's note,
     * which is a 48-byte buffer in main.cpp. */
    assert(strlen(card_state_text(CARD_NO_PIN)) < 48U);
    assert(strlen(card_state_text(CARD_NO_KEY)) < 48U);

    printf("test_card_status OK\n");
    return 0;
}
