/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file card_status.h
 * @ingroup device
 * @brief What a tapped card is missing, read from its SELECT response.
 *
 * A card straight out of its envelope has no PIN and no key on it. Neither the
 * payment path nor the payout read can do anything with one, and without this
 * both find out three APDUs later — as "Wrong card PIN" or as a sign error —
 * which sends the operator looking for a fault that is not there. The applet
 * publishes both facts in the clear in its SELECT response, before any secure
 * channel or PIN is involved, so the terminal can simply say which.
 *
 * Header-only and free of ESP-IDF, for the same reason addr_check.h is: it is a
 * parser over bytes a card sent, so it is kept where a host test can reach it
 * (tests/units/test_card_status.cpp).
 *
 * The layout is the one cryptnox-sdk-py's factory._select() reads:
 *
 *     r[0]      card type, 'B' (Basic) or 'N' (NFT)
 *     r[1..3]   applet version
 *     r[4..35]  the 32 data bytes; r[5] is the flags byte
 *     r[n-2..]  SW1 SW2
 *
 * and the two flags are the ones it exposes as card.initialized and card.seeded.
 */

#ifndef CARD_STATUS_H
#define CARD_STATUS_H

#include <stddef.h>
#include <stdint.h>

/** @brief SELECT for the Cryptnox applet: CLA INS P1 P2 Lc || AID. */
#define CARD_SELECT_APDU  { 0x00U, 0xA4U, 0x04U, 0x00U, 0x07U, \
                            0xA0U, 0x00U, 0x00U, 0x10U, 0x00U, 0x01U, 0x12U }

/** @brief Offset of the flags byte in a SELECT response. */
#define CARD_FLAGS_OFF    5U

/** @brief Shortest SELECT response this can read: flags plus the status word. */
#define CARD_SELECT_MIN   8U

#define CARD_FLAG_PIN_SET  0x40U   /**< Initialised: PIN and PUK are set.     */
#define CARD_FLAG_SEEDED   0x20U   /**< A key has been generated or loaded.   */

/** @brief What a tapped card is missing. */
typedef enum {
    CARD_READY = 0,   /**< Initialised and seeded — usable.                   */
    CARD_UNKNOWN,     /**< Nothing this can judge; carry on and let the
                           ordinary paths report whatever goes wrong.         */
    CARD_NO_PIN,      /**< Never initialised: no PIN, no key, nothing.        */
    CARD_NO_KEY       /**< Initialised, but no seed loaded — cannot sign.     */
} card_state_t;

/**
 * @brief Judge a card from its SELECT response.
 *
 * @param[in] r Response bytes as the card sent them, status word included.
 * @param[in] n Number of bytes in @p r.
 * @return The state, or @ref CARD_UNKNOWN for anything that is not a successful
 *         SELECT of a card type these flags are known to describe. Unknown is
 *         deliberately not an error: this runs in front of every card operation,
 *         and a parser that guessed would refuse a working card.
 */
static inline card_state_t card_state(const uint8_t *r, size_t n)
{
    if ((r == NULL) || (n < CARD_SELECT_MIN)) { return CARD_UNKNOWN; }
    /* Not 0x9000 means the applet did not answer this SELECT, so there is no
     * layout below to trust. */
    if ((r[n - 2U] != 0x90U) || (r[n - 1U] != 0x00U)) { return CARD_UNKNOWN; }
    /* A card type whose flags byte we have not been told about could have
     * anything in bit 6, and reading it wrong here would refuse a card that
     * works. 'B' is Basic, 'N' is NFT; both carry these two flags. */
    if ((r[0] != (uint8_t)'B') && (r[0] != (uint8_t)'N')) { return CARD_UNKNOWN; }

    const uint8_t flags = r[CARD_FLAGS_OFF];
    if ((flags & CARD_FLAG_PIN_SET) == 0U) { return CARD_NO_PIN; }
    if ((flags & CARD_FLAG_SEEDED)  == 0U) { return CARD_NO_KEY; }
    return CARD_READY;
}

/**
 * @brief The line to put in front of the operator, or NULL if there is none.
 *
 * Short enough for the panel's failure line and the config page's note, and it
 * names the fix rather than the flag: whoever is holding a blank card needs to
 * be told to set it up, not told about bit 6.
 */
static inline const char *card_state_text(card_state_t s)
{
    switch (s) {
        case CARD_NO_PIN: return "Card not set up - initialise it first";
        case CARD_NO_KEY: return "Card has no key - load a seed first";
        default:          return NULL;
    }
}

#endif /* CARD_STATUS_H */
