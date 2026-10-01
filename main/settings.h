/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file settings.h
 * @ingroup device
 * @brief Persistent device settings stored in NVS (backlight, Wi-Fi creds).
 *
 * All getters return sensible defaults when nothing has been written.
 * Requires nvs_flash_init() to have run first.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Which chain (and therefore which asset) the terminal charges in.
 *
 * The numbers are persisted in NVS, so existing ones never move; new assets are
 * appended. settings.cpp rejects anything at or past @ref POS_CHAIN__COUNT, so a
 * downgrade falls back to the default rather than guessing what a number meant.
 */
typedef enum {
    POS_CHAIN_ETH_USDC    = 0,  /**< USDC (ERC-20) on Ethereum (the default). */
    POS_CHAIN_TRON_TRX    = 1,  /**< Native TRX on Tron.                      */
    POS_CHAIN_TRON_USDT   = 2,  /**< USDT (TRC-20) on Tron.                   */
    POS_CHAIN_ETH_USDT    = 3,  /**< USDT (ERC-20) on Ethereum.               */
    POS_CHAIN_POLY_USDC   = 4,  /**< USDC (ERC-20) on Polygon.                */
    POS_CHAIN_POLY_USDT   = 5,  /**< USDT (ERC-20) on Polygon.                */
    POS_CHAIN_TRON_USDC   = 6,  /**< USDC (TRC-20) on Tron — testnet only.    */
    POS_CHAIN_ETH_NATIVE  = 7,  /**< Native ETH on Ethereum.                  */
    POS_CHAIN_POLY_NATIVE = 8,  /**< Native POL on Polygon.                   */
    POS_CHAIN__COUNT            /**< Sentinel — keep last, not a selection.  */
} pos_chain_t;

/* Family/network predicates for each selection live in assets.h. */

/**
 * @brief Ceiling on a native-coin sale, in the keypad's 6-decimal base units.
 *
 * ETH and POL are 18-decimal, so wei = units * 10^12, and eth_tx_t::eth_value is
 * a uint64 — 2^64-1 wei is 18.446744073709551615 of the coin. Past that the
 * multiply wraps and the card would sign a value nobody entered, so the keypad
 * stops at 18.44 and the payment path re-checks it.
 *
 * ponytail: uint64 wei, 18.44 ETH/POL a sale. Widening means carrying
 * eth_value as a 32-byte big-endian buffer through eth_rlp and its two
 * encoders — worth it only if somebody actually needs to charge more.
 */
#define POS_AMOUNT_UNITS_MAX_NATIVE  18446744ULL

/** @brief Selected chain, or @ref POS_CHAIN_ETH_USDC if never set. */
pos_chain_t settings_get_chain(void);

/** @brief Persist the selected chain. */
void settings_set_chain(pos_chain_t chain);

/**
 * @brief true when the terminal is on the production networks.
 *
 * Picks the deployment (Ethereum/Sepolia, Polygon/Amoy, Tron/Nile), not the
 * asset. Defaults to true: a unit coming up on a testnet would report every sale
 * as paid and settle nothing. Endpoints, chain ids and contracts are read once at
 * boot into the dual stores, so @ref settings_set_mainnet takes effect only on a
 * restart, which the caller performs.
 */
bool settings_get_mainnet(void);

/** @brief Persist the production/test network choice. */
void settings_set_mainnet(bool mainnet);

/**
 * @brief Pick the string belonging to the network the terminal is on.
 *
 * One place for every config.h testnet/mainnet pair (RPC URLs, contracts, names),
 * so no call site can leave a mainnet sale pointed at a testnet contract.
 */
const char *settings_net_str(const char *testnet, const char *mainnet);

/** @brief Widest real-world UTC offsets, in minutes: UTC-12:00 to UTC+14:00. */
#define TZ_OFFSET_MIN  (-720)
#define TZ_OFFSET_MAX  (840)

/**
 * @brief The panel clock's standard (winter) offset from UTC, in minutes east.
 *
 * Minutes because half- and quarter-hour zones exist. DST is added on top from
 * settings_get_tz_dst() by civil_time.cpp (newlib's tzset costs 64 KB of flash).
 *
 * @return Offset in minutes, 0 (UTC) when unset or when NVS holds nonsense.
 */
int16_t settings_get_tz_offset_min(void);

/**
 * @brief Store the panel clock's UTC offset.
 *
 * @param[in] minutes Minutes east of UTC, @ref TZ_OFFSET_MIN to
 *                    @ref TZ_OFFSET_MAX.
 * @return false if out of range, in which case nothing is written.
 */
bool settings_set_tz_offset_min(int16_t minutes);

/** @brief The clock's DST rule, a civil_dst_t; CIVIL_DST_NONE when unset. */
uint8_t settings_get_tz_dst(void);

/** @brief Store the DST rule. @return false (nothing written) if unknown. */
bool settings_set_tz_dst(uint8_t rule);

/** @brief Backlight level in percent, or 80 if never set. */
uint8_t settings_get_brightness(void);

/** @brief Persist the backlight level (0..100). */
void settings_set_brightness(uint8_t pct);

/**
 * @brief Raw XPT2046 range that maps to the panel's four edges.
 *
 * Resistive overlays vary unit to unit, so this is stored per device. Defaults
 * to 200..3800, close enough to reach the calibration screen uncalibrated.
 */
void settings_get_touch_cal(uint16_t *x_min, uint16_t *x_max,
                            uint16_t *y_min, uint16_t *y_max);

/** @brief Persist a calibration. Rejected (and ignored) if an axis is inverted
 *  or collapsed — a bad store here makes the panel untappable, including the
 *  screen that would fix it. */
void settings_set_touch_cal(uint16_t x_min, uint16_t x_max,
                            uint16_t y_min, uint16_t y_max);

/** @brief true if a Wi-Fi SSID has been stored. */
bool settings_has_wifi(void);

/**
 * @brief Read the stored Wi-Fi credentials.
 *
 * @param[out] ssid     Buffer for the SSID (>= 33 bytes recommended).
 * @param[in]  ssid_n   Capacity of @p ssid.
 * @param[out] pass     Buffer for the password (>= 65 bytes recommended).
 * @param[in]  pass_n   Capacity of @p pass.
 * @return true if both SSID and password were present, false otherwise
 *         (buffers are left NUL-terminated/empty on failure).
 */
bool settings_get_wifi(char *ssid, size_t ssid_n, char *pass, size_t pass_n);

/** @brief Persist Wi-Fi credentials (plaintext — see README threat model). */
void settings_set_wifi(const char *ssid, const char *pass);

/**
 * @brief true once an admin code exists.
 *
 * Not "configured": an interrupted setup leaves a code and no payout address.
 * Use @ref settings_has_payout for "can this unit take money". Cleared by
 * @ref settings_factory_reset.
 */
bool settings_has_admin_code(void);

/**
 * @brief Store a new admin code, with a fresh random salt.
 *
 * Only a salted keccak256 digest is persisted, deliberately not stretched (see
 * settings.cpp). Resets the failure counter.
 *
 * @param[in] code NUL-terminated code; the caller wipes its own copy.
 * @return false if NVS refused the write — the caller must not treat setup as
 *         done, since a terminal with no stored code can never open its menu.
 */
bool settings_set_admin_code(const char *code);

/**
 * @brief Check a candidate code, maintaining the failure counter.
 *
 * @param[in] code NUL-terminated candidate.
 * @return true on match (counter cleared); false on mismatch or when no code is
 *         stored (counter incremented on a genuine mismatch).
 */
bool settings_check_admin_code(const char *code);

/**
 * @brief Consecutive failed unlock attempts, persisted.
 *
 * Kept in NVS so power-cycling does not clear the penalty the UI derives from it.
 */
uint8_t settings_admin_fail_count(void);

/**
 * @brief EIP-1559 max fee per gas, in Gwei.
 * @return the stored override, or the config.h default (MAX_FEE).
 */
uint32_t settings_get_max_fee_gwei(void);

/**
 * @brief EIP-1559 max priority fee (tip) per gas, in Gwei.
 * @return the stored override, or the config.h default (MAX_PRIORITY_FEE).
 */
uint32_t settings_get_priority_fee_gwei(void);

/**
 * @brief Persist the max fee and the tip per gas (Gwei), as a pair.
 * @return false, storing nothing, if @c fee_pair_check (settings_rules.h) refuses it.
 */
bool settings_set_fees_gwei(uint32_t max_gwei, uint32_t prio_gwei);

/** @brief Longest payout address plus NUL — "0x" + 40 hex, or 34 base58 Tron. */
#define SETTINGS_PAYOUT_MAX  64U

/**
 * @brief Read the payout address for a network.
 *
 * Falls back to the config.h recipient when nothing is stored, so callers always
 * get a parseable address. That fallback is for display and boot-time reasoning
 * only: the payment path checks @ref settings_has_payout first and refuses the
 * sale, because an address nobody chose is somebody else's.
 *
 * Dual-stored: the NVS value carries an echo copy compared here (the config.h
 * value lives in the signed image and needs none). A mismatch falls back to
 * config.h rather than paying out to a half-written string.
 *
 * @param[in]  tron  true for the Tron payout address, false for Ethereum.
 * @param[out] out   Buffer, >= @ref SETTINGS_PAYOUT_MAX. Ethereum addresses are
 *                   returned "0x"-prefixed, ready to parse.
 * @param[in]  n     Capacity of @p out.
 * @return true if a stored (operator-set) address was returned, false if the
 *         config.h default was used. Either way @p out is valid.
 */
bool settings_get_payout(bool tron, char *out, size_t n);

/**
 * @brief Whether an operator has actually set the payout address for a network.
 *
 * Decides whether an asset is offered at all: a unit set up for Ethereum only
 * must not offer Tron payments to the compile-time recipient, which is somebody
 * else's address.
 */
bool settings_has_payout(bool tron);

/**
 * @brief Persist a payout address, writing both the value and its echo copy.
 *
 * Does not validate: the caller checks the EIP-55 / base58 checksum first and
 * shows the address on the device screen for the operator to accept.
 *
 * @param[in] tron true for the Tron address, false for Ethereum.
 * @param[in] addr NUL-terminated address; Ethereum with or without "0x".
 * @return false if NVS refused either write (nothing is left half-applied
 *         that a later read would trust — the echo comparison catches it).
 */
bool settings_set_payout(bool tron, const char *addr);

/**
 * @brief Read the token-contract address for a network.
 *
 * Same dual store and config.h fallback as @ref settings_get_payout: the contract
 * decides which asset moves, so a half-written NVS string must not redirect it.
 * Only USDC on Ethereum and USDT on Tron are settable; any other @p chain returns
 * false with @p out empty.
 *
 * Stored separately per network (@ref settings_get_mainnet): the same token is a
 * different deployment on each, and one slot would leave a mainnet terminal
 * calling a testnet contract that holds nothing.
 *
 * @param[in]  chain The token's selection.
 * @param[out] out  Buffer, >= @ref SETTINGS_PAYOUT_MAX. Ethereum contracts are
 *                  returned "0x"-prefixed.
 * @param[in]  n    Capacity of @p out.
 * @return true if a stored (operator-set) contract was returned, false if the
 *         config.h default (or, for an unsettable @p chain, nothing) was.
 */
bool settings_get_contract(pos_chain_t chain, char *out, size_t n);

/**
 * @brief Persist a token-contract address, value and echo copy.
 *
 * Does not validate — same contract as @ref settings_set_payout: the caller
 * checks the address and has it accepted on the device screen first.
 */
bool settings_set_contract(pos_chain_t chain, const char *addr);

/** @brief Erase all stored settings (brightness, auto, Wi-Fi creds, fees). */
void settings_factory_reset(void);

/**
 * @brief Persist the sale between broadcast and verdict (opaque record).
 *
 * Written just before the transaction leaves the terminal and cleared on the
 * final verdict, so a brownout or panic while polling does not lose whether the
 * customer paid: the next boot resumes polling the same hash. Layout is
 * main.cpp's. Survives a power cut, not a firmware update (see
 * @ref settings_wipe_if_new_build) or a factory reset.
 *
 * @return true once committed.
 */
bool settings_inflight_save(const void *rec, size_t n);

/** @brief Read the persisted sale back. false if none, or not @p n bytes. */
bool settings_inflight_load(void *rec, size_t n);

/** @brief Drop the persisted sale. Writes nothing when there is none. */
void settings_inflight_clear(void);

/*
 * BUILD_ID is set by the build to `git rev-list --count --first-parent HEAD`
 * (project CMakeLists.txt), so every commit gets a new one. A plain counter
 * survives every route onto a unit (browser update, `idf.py flash`, factory
 * image). See @ref settings_wipe_if_new_build.
 */

/**
 * @brief Erase NVS unless this exact build is the one that wrote it.
 *
 * Call once at the top of @c app_main, right after @c nvs_flash_init() and
 * before anything else opens NVS (@c nvs_flash_erase() cannot run with handles
 * outstanding). Handles its own re-init and stamps @ref BUILD_ID.
 *
 * A security control: the firmware is open source, so any other image could
 * have written anything into NVS, a payout address above all. The test is
 * equality — newer, older and absent stamps all erase. A rollback therefore
 * erases again; settings are cheaper to re-enter than a payout address to lose.
 *
 * The stamp does not authenticate itself: a hostile image that runs can forge
 * it. Secure Boot (and Flash Encryption in RELEASE) stops foreign images; this
 * covers state left by non-hostile ones (old releases, engineering builds).
 *
 * Erases the whole partition, Wi-Fi and provision.cpp namespaces included, so
 * the unit comes up in first-run setup and stays unowned until an operator
 * re-enters Wi-Fi and re-accepts the payout address.
 *
 * @return true if the settings were erased, so the caller can say so on the panel.
 */
bool settings_wipe_if_new_build(void);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H */
