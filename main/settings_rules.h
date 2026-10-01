/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file settings_rules.h
 * @ingroup device
 * @brief What a setting may be, apart from where it is stored.
 *
 * Header-only and free of ESP-IDF dependencies on purpose, like form_parse.h:
 * settings.cpp and provision.cpp wrap these in NVS and HTTP, and the rules
 * themselves — the time-zone range and its storage bias, the gas-fee bounds,
 * the admin-code digest, the address normalisation the echo copy relies on —
 * are what tests/units/test_settings_rules.cpp checks.
 */

#ifndef SETTINGS_RULES_H
#define SETTINGS_RULES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "CW_Utils.h"     /* secure_wipe (CODING_RULES §1.4) */
#include "civil_time.h"   /* CIVIL_DST__COUNT */
#include "keccak256.h"
#include "settings.h"     /* TZ_OFFSET_MIN / _MAX, SETTINGS_PAYOUT_MAX */

/******************************************************************
 * Time zone
 ******************************************************************/

/* Minutes east of UTC are stored biased, so they fit the unsigned NVS helpers
 * and a missing key reads as UTC rather than as UTC-12. */
#define TZ_OFFSET_BIAS 720

/** @brief true for an offset the clock accepts (@ref TZ_OFFSET_MIN .. _MAX). */
static inline bool tz_offset_valid(long minutes)
{
    return (minutes >= TZ_OFFSET_MIN) && (minutes <= TZ_OFFSET_MAX);
}

/** @brief A valid offset in its stored, biased form. */
static inline uint32_t tz_offset_encode(int16_t minutes)
{
    return (uint32_t)((int32_t)minutes + TZ_OFFSET_BIAS);
}

/** @brief The stored form back to minutes; nonsense in NVS is UTC, not a wild clock. */
static inline int16_t tz_offset_decode(uint32_t raw)
{
    if (raw > (uint32_t)(TZ_OFFSET_BIAS + TZ_OFFSET_MAX)) {
        return 0;
    }
    return (int16_t)((int32_t)raw - TZ_OFFSET_BIAS);
}

/** @brief true for a DST rule the clock knows. */
static inline bool tz_dst_valid(long rule)
{
    return (rule >= 0) && (rule < (long)CIVIL_DST__COUNT);
}

/** @brief A stored DST rule, or no DST if it is not one the clock knows. */
static inline uint8_t tz_dst_decode(uint8_t r)
{
    return (r < (uint8_t)CIVIL_DST__COUNT) ? r : (uint8_t)CIVIL_DST_NONE;
}

/******************************************************************
 * Gas fees
 ******************************************************************/

/* Bounds are the ones the panel's steppers used to enforce, so a stored value
 * cannot become something the old UI could not express. */
#define FEE_GWEI_MIN  1UL
#define FEE_GWEI_MAX  500UL

/** @brief Verdict on a (max fee, tip) pair from the config page. */
typedef enum {
    FEE_PAIR_OK = 0,
    FEE_PAIR_OUT_OF_RANGE,   /**< either one outside FEE_GWEI_MIN..MAX    */
    FEE_PAIR_TIP_ABOVE_MAX,  /**< the tip is higher than the cap          */
} fee_pair_t;

/** @brief Check a (max fee, tip) pair in Gwei; the range is checked first. */
static inline fee_pair_t fee_pair_check(unsigned long max_gwei, unsigned long prio_gwei)
{
    if ((max_gwei < FEE_GWEI_MIN) || (max_gwei > FEE_GWEI_MAX) ||
        (prio_gwei < FEE_GWEI_MIN) || (prio_gwei > FEE_GWEI_MAX)) {
        return FEE_PAIR_OUT_OF_RANGE;
    }
    if (prio_gwei > max_gwei) { return FEE_PAIR_TIP_ABOVE_MAX; }
    return FEE_PAIR_OK;
}

/******************************************************************
 * Admin code
 ******************************************************************/

#define ADMIN_SALT_LEN    16U
#define ADMIN_HASH_LEN    32U
/* Longest code that goes into the digest. Deliberately NOT ui.cpp's
 * ADMIN_CODE_MAX (9) — same name, different layer. Anything past this is
 * silently dropped from the hash, so keep it comfortably above the UI's cap. */
#define ADMIN_CODE_HASH_MAX  32U

/**
 * @brief Derive the stored digest: a single keccak256 over salt || code.
 *
 * Not stretched, on purpose. The digest lives in the flash-encrypted NVS, so
 * reading it already means the encryption is defeated — and past that point no
 * KDF cost saves a 4-digit code anyway. The salt is still there so the same code
 * yields a different digest on every unit. Guessing at the panel is what the
 * escalating lockout in ui.cpp is for.
 */
static inline void admin_derive(const char *code, const uint8_t *salt,
                                uint8_t out[ADMIN_HASH_LEN])
{
    uint8_t buf[ADMIN_SALT_LEN + ADMIN_CODE_HASH_MAX];
    const size_t clen = strnlen(code, ADMIN_CODE_HASH_MAX);

    (void)memcpy(buf, salt, ADMIN_SALT_LEN);
    (void)memcpy(buf + ADMIN_SALT_LEN, code, clen);
    keccak256(buf, ADMIN_SALT_LEN + clen, out);
    CW_Utils::secure_wipe(buf, sizeof(buf));
}

/******************************************************************
 * Dual-stored addresses
 ******************************************************************/

/**
 * @brief The form a payout address or contract is stored in: Tron as given,
 *        Ethereum "0x"-prefixed whichever way it arrived.
 *
 * Normalised to the form the getter hands back, so the echo comparison
 * compares like with like on the next boot.
 *
 * @param[in]  tron true for a Tron address.
 * @param[in]  addr As submitted.
 * @param[out] norm @ref SETTINGS_PAYOUT_MAX bytes.
 * @return false for a NULL, empty or over-long @p addr (@p norm untouched).
 */
static inline bool settings_addr_normalise(bool tron, const char *addr,
                                           char norm[SETTINGS_PAYOUT_MAX])
{
    if ((addr == NULL) || (addr[0] == '\0')) { return false; }
    if (strlen(addr) >= SETTINGS_PAYOUT_MAX) { return false; }

    if (tron) {
        (void)snprintf(norm, SETTINGS_PAYOUT_MAX, "%s", addr);
    } else {
        const bool prefixed = (addr[0] == '0') && ((addr[1] == 'x') || (addr[1] == 'X'));
        (void)snprintf(norm, SETTINGS_PAYOUT_MAX, "0x%s", prefixed ? (addr + 2) : addr);
    }
    return true;
}

#endif /* SETTINGS_RULES_H */
