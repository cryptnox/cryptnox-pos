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

/******************************************************************
 * Keypad amounts (ui.cpp)
 ******************************************************************/

/* 9999.99 of any asset: plenty for a counter terminal, and it keeps the
 * figure, the cents and the asset selector inside the amount row. */
#define AMOUNT_CENTS_MAX  999999ULL   /* 9999.99 */
/* The same ceiling in 6-decimal base units. */
#define AMOUNT_UNITS_MAX  (AMOUNT_CENTS_MAX * 10000ULL)

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

/** @brief How wei is carried: a uint256, 32 bytes big-endian, as on chain. */
#define WEI_LEN  32U

/** @brief @p v as a uint256. */
static inline void wei_from_u64(uint64_t v, uint8_t out[WEI_LEN])
{
    for (size_t i = 0U; i < WEI_LEN; i++) {
        out[(WEI_LEN - 1U) - i] =
            (i < 8U) ? static_cast<uint8_t>((v >> (8U * i)) & 0xFFU) : 0U;
    }
}

/** @brief a += b. A carry out of the top byte is dropped; every caller here
 *         stays below 2^80. */
static inline void wei_add(uint8_t a[WEI_LEN], const uint8_t b[WEI_LEN])
{
    unsigned carry = 0U;
    for (size_t i = WEI_LEN; i > 0U; i--) {
        const unsigned s = static_cast<unsigned>(a[i - 1U]) + b[i - 1U] + carry;
        a[i - 1U] = static_cast<uint8_t>(s & 0xFFU);
        carry = s >> 8;
    }
}

/** @brief a < b. Equal-length big-endian, so byte order is numeric order. */
static inline bool wei_lt(const uint8_t a[WEI_LEN], const uint8_t b[WEI_LEN])
{
    return memcmp(a, b, WEI_LEN) < 0;
}

/**
 * @brief 6-decimal keypad units -> wei, for the 18-decimal coins only.
 *
 * 9999.99 is ~2^73 wei, past a uint64, hence the uint256 result. Under the
 * ceiling units * 10^6 still fits a uint64; the second 10^6 goes over its two
 * 32-bit halves so neither product wraps.
 *
 * @param[in]  units Sale amount in keypad base units.
 * @param[out] wei   units * 10^12; untouched on refusal.
 * @return false past @ref AMOUNT_UNITS_MAX, a value the keypad cannot key.
 */
static inline bool evm_units_to_wei(uint64_t units, uint8_t wei[WEI_LEN])
{
    if (units > AMOUNT_UNITS_MAX) { return false; }
    const uint64_t x = units * 1000000ULL;                 /* < 2^54 */
    uint8_t lo[WEI_LEN];
    uint8_t hi[WEI_LEN];
    wei_from_u64((x & 0xFFFFFFFFULL) * 1000000ULL, lo);   /* < 2^52 */
    wei_from_u64((x >> 32) * 1000000ULL, hi);             /* < 2^42 */
    /* hi << 32: four bytes towards the front */
    (void)memmove(hi, hi + 4U, WEI_LEN - 4U);
    (void)memset(hi + (WEI_LEN - 4U), 0, 4U);
    wei_add(lo, hi);
    (void)CW_Utils::safe_memcpy(wei, WEI_LEN, lo, WEI_LEN);
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
    EVM_FUNDS_UNKNOWN,     /**< Amount past the keypad cap: not ours to say. */
} evm_funds_t;

/**
 * @brief Can @p have_wei pay for this sale?
 *
 * @param[in] native   true for ETH/POL; for a token only the gas is in wei.
 * @param[in] have_wei Account balance, uint256.
 * @param[in] gas_cost Gas limit * max fee.
 * @param[in] units    Sale amount in keypad base units (native only).
 */
static inline evm_funds_t evm_funds_check(bool native,
                                          const uint8_t have_wei[WEI_LEN],
                                          uint64_t gas_cost, uint64_t units)
{
    uint8_t need[WEI_LEN];
    wei_from_u64(gas_cost, need);
    if (wei_lt(have_wei, need)) { return EVM_FUNDS_SHORT_GAS; }
    if (!native) { return EVM_FUNDS_OK; }
    uint8_t value_wei[WEI_LEN];
    if (!evm_units_to_wei(units, value_wei)) { return EVM_FUNDS_UNKNOWN; }
    /* Under 2^64 + 2^74: the uint256 sum cannot wrap. */
    wei_add(need, value_wei);
    if (wei_lt(have_wei, need)) { return EVM_FUNDS_SHORT_VALUE; }
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
