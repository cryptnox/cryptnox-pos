/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file money.h
 * @ingroup app
 * @brief The sale's arithmetic: keypad cents, base units, wei, fees, calldata.
 *
 * Header-only and free of ESP-IDF dependencies on purpose, like form_parse.h.
 * Every number the customer is charged passes through these few lines, and a
 * mistake in them is silent — a wrapped multiply signs a value nobody entered,
 * a byte out of place in the calldata pays somebody else. So they live where a
 * host test can hold them against vectors from an independent signer
 * (tests/units/test_money.cpp, tools/gen_kat_vectors.py), rather than as
 * statics in main.cpp and ui.cpp where nothing could reach them.
 */

#ifndef MONEY_H
#define MONEY_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "CW_Utils.h"   /* secure_wipe / safe_memcpy (CODING_RULES §1.4) */
#include "eth_addr.h"   /* ETH_ADDR_LEN */
#include "settings.h"   /* POS_AMOUNT_UNITS_MAX_NATIVE */

/******************************************************************
 * Keypad amounts (ui.cpp)
 ******************************************************************/

/* 9999.99: plenty for a counter terminal, and it keeps the figure, the cents
 * and the asset selector inside the amount row. */
#define AMOUNT_CENTS_MAX  999999ULL   /* 9999.99 */
/* 18.44 — POS_AMOUNT_UNITS_MAX_NATIVE expressed in the keypad's cents. */
#define AMOUNT_CENTS_MAX_NATIVE  (POS_AMOUNT_UNITS_MAX_NATIVE / 10000ULL)

/**
 * @brief Ceiling on what the keypad will accept, in cents.
 *
 * ETH and POL are 18-decimal and the signed value is a uint64 of wei, so a sale
 * stops at 18.44 of either (see POS_AMOUNT_UNITS_MAX_NATIVE).
 *
 * @param[in] native true for an EVM network's own coin (ETH, POL).
 */
static inline uint64_t amount_cents_cap(bool native)
{
    return native ? AMOUNT_CENTS_MAX_NATIVE : AMOUNT_CENTS_MAX;
}

/** @brief A digit shifted in from the right; refused whole if it passes @p cap. */
static inline uint64_t amount_key_digit(uint64_t cents, unsigned digit, uint64_t cap)
{
    const uint64_t n = (cents * 10ULL) + static_cast<uint64_t>(digit);
    return (n <= cap) ? n : cents;
}

/** @brief The "00" key: two zeroes shifted in, clamped to @p cap. */
static inline uint64_t amount_key_00(uint64_t cents, uint64_t cap)
{
    const uint64_t n = cents * 100ULL;
    return (n > cap) ? cap : n;
}

/** @brief The backspace key: the last digit dropped. */
static inline uint64_t amount_key_back(uint64_t cents)
{
    return cents / 10ULL;
}

/** @brief Keypad cents to 6-decimal base units. */
static inline uint64_t amount_cents_to_units(uint64_t cents)
{
    return cents * 10000ULL;
}

/** @brief "12.50": 6-decimal base units to two places, truncated. */
static inline void amount_format(uint64_t units, char *out, size_t n)
{
    uint64_t whole = units / 1000000ULL;
    uint64_t cents = (units % 1000000ULL) / 10000ULL;
    snprintf(out, n, "%" PRIu64 ".%02" PRIu64, whole, cents);
}

/******************************************************************
 * Units, wei and fees (main.cpp)
 ******************************************************************/

/**
 * @brief 6-decimal keypad units -> wei, for the 18-decimal coins only.
 *
 * @param[in]  units Sale amount in keypad base units.
 * @param[out] wei   units * 10^12; untouched on refusal.
 * @return false past @ref POS_AMOUNT_UNITS_MAX_NATIVE, where the multiply
 *         would wrap and sign a value nobody entered.
 */
static inline bool evm_units_to_wei(uint64_t units, uint64_t *wei)
{
    if (units > POS_AMOUNT_UNITS_MAX_NATIVE) { return false; }
    *wei = units * 1000000000000ULL;
    return true;
}

/**
 * @brief The EIP-1559 fees one EVM sale will offer, in wei per gas, from the
 *        operator's Gwei settings.
 *
 * @param[in]  max_gwei   The Max fee setting.
 * @param[in]  prio_gwei  The Priority fee setting.
 * @param[in]  polygon    true on Polygon, which has a tip floor of its own.
 * @param[in]  floor_gwei That floor (POLY_MIN_PRIORITY_FEE_GWEI).
 * @param[out] max_fee    Fee cap, wei per gas.
 * @param[out] prio_fee   Tip, wei per gas; never above @p max_fee.
 */
static inline void evm_fees_from_gwei(uint32_t max_gwei, uint32_t prio_gwei,
                                      bool polygon, uint32_t floor_gwei,
                                      uint64_t *max_fee, uint64_t *prio_fee)
{
    /* The user edits Gwei, so scale to wei. Keep the tip <= the cap or the tx
     * is malformed. */
    uint64_t max_fee_wei  = (uint64_t)max_gwei  * 1000000000ULL;
    uint64_t prio_fee_wei = (uint64_t)prio_gwei * 1000000000ULL;
    if (prio_fee_wei > max_fee_wei) { prio_fee_wei = max_fee_wei; }
    /* Polygon drops a transfer whose tip is under ~25 Gwei, and the fee knobs are
     * shared with Ethereum where 20 is right. Raise the floor here rather than
     * asking the operator to retune the Tx tab every time they switch networks —
     * and lift the cap with it, or the clamp above would only put it back. */
    if (polygon) {
        const uint64_t floor_wei = (uint64_t)floor_gwei * 1000000000ULL;
        if (prio_fee_wei < floor_wei) { prio_fee_wei = floor_wei; }
        if (max_fee_wei  < prio_fee_wei) { max_fee_wei = prio_fee_wei; }
    }
    *max_fee  = max_fee_wei;
    *prio_fee = prio_fee_wei;
}

/** @brief What the pre-flight balance check concluded. */
typedef enum {
    EVM_FUNDS_OK = 0,      /**< Covered (for a token: the gas is).          */
    EVM_FUNDS_SHORT_GAS,   /**< Not even the network fee.                   */
    EVM_FUNDS_SHORT_VALUE, /**< The fee, but not the fee plus the amount.   */
    EVM_FUNDS_UNKNOWN,     /**< Amount past the native cap: not ours to say. */
} evm_funds_t;

/**
 * @brief Can @p have_wei pay for this sale?
 *
 * @param[in] native   true for ETH/POL; for a token only the gas is in wei.
 * @param[in] have_wei Account balance.
 * @param[in] gas_cost Gas limit * max fee.
 * @param[in] units    Sale amount in keypad base units (native only).
 */
static inline evm_funds_t evm_funds_check(bool native, uint64_t have_wei,
                                          uint64_t gas_cost, uint64_t units)
{
    if (have_wei < gas_cost) { return EVM_FUNDS_SHORT_GAS; }
    if (!native) { return EVM_FUNDS_OK; }
    uint64_t value_wei = 0U;
    if (!evm_units_to_wei(units, &value_wei)) { return EVM_FUNDS_UNKNOWN; }
    /* Subtracting rather than adding: value is capped at just under 2^64
     * wei, so value + gas_cost is the one sum here that could overflow —
     * and the gas is already known to be covered. */
    if ((have_wei - gas_cost) < value_wei) { return EVM_FUNDS_SHORT_VALUE; }
    return EVM_FUNDS_OK;
}

/**
 * @brief "0.0013 ETH": @p v base units of a @p dec-decimal coin, to six places,
 *        rounded UP — it is a ceiling, and rounding down would understate it.
 */
static inline void fmt_coin(char *out, size_t n, uint64_t v, unsigned dec,
                            const char *coin)
{
    uint64_t div = 1U;
    for (unsigned i = 6U; i < dec; i++) { div *= 10U; }
    const uint64_t micro = (v / div) + (((v % div) != 0U) ? 1U : 0U);
    char frac[8];
    (void)snprintf(frac, sizeof(frac), "%06" PRIu64, micro % 1000000U);
    size_t f = strlen(frac);
    while ((f > 0U) && (frac[f - 1U] == '0')) { frac[--f] = '\0'; }
    (void)snprintf(out, n, "%" PRIu64 "%s%s %s", micro / 1000000U,
                   (f > 0U) ? "." : "", frac, coin);
}

/******************************************************************
 * USDC transfer calldata (main.cpp)
 ******************************************************************/

/* ── ERC-20 transfer(address,uint256) selector + calldata ── */
static const uint8_t TRANSFER_SELECTOR[4] = { 0xa9U, 0x05U, 0x9cU, 0xbbU };
#define ABI_SELECTOR_LEN    4U     /* transfer(address,uint256) selector      */
#define ABI_WORD_LEN        32U    /* one ABI-encoded argument word           */
#define USDC_CALLDATA_LEN   (ABI_SELECTOR_LEN + (2U * ABI_WORD_LEN))  /* 68 */
#define ABI_TO_OFFSET       (ABI_SELECTOR_LEN + (ABI_WORD_LEN - ETH_ADDR_LEN))

/**
 * @brief Build the 68-byte ABI-encoded calldata for a USDC @c transfer call.
 *
 * Encodes the ERC-20 @c transfer(address,uint256) selector followed by the
 * ABI-encoded arguments:
 * @code
 * selector(4) | zeroes(12) | to(20) | zeroes(24) | amount_be(8)
 * @endcode
 *
 * @param[out] out    Output buffer of #USDC_CALLDATA_LEN bytes.
 * @param[in]  to     Recipient address, #ETH_ADDR_LEN bytes (already
 *                    parsed/validated).
 * @param[in]  amount Transfer amount in USDC base units (6 decimals).
 */
static inline void build_usdc_calldata(uint8_t out[USDC_CALLDATA_LEN],
                                       const uint8_t to[ETH_ADDR_LEN],
                                       uint64_t amount)
{
    CW_Utils::secure_wipe(out, USDC_CALLDATA_LEN);
    (void)CW_Utils::safe_memcpy(out, USDC_CALLDATA_LEN,
                                TRANSFER_SELECTOR, ABI_SELECTOR_LEN);
    (void)CW_Utils::safe_memcpy(out + ABI_TO_OFFSET,
                                USDC_CALLDATA_LEN - ABI_TO_OFFSET,
                                to, ETH_ADDR_LEN);

    size_t j;
    for (j = 0U; j < sizeof(amount); j++) {
        out[(USDC_CALLDATA_LEN - 1U) - j] =
            static_cast<uint8_t>((amount >> (8U * j)) & 0xFFU);
    }
}

#endif /* MONEY_H */
