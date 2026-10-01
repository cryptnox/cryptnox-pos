/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file settings.cpp
 * @brief NVS-backed implementation of the persistent device settings.
 */

#include "settings.h"
#include "settings_rules.h"   /* ranges, digest, normalisation — host-tested */
#include "civil_time.h"   /* CIVIL_DST__COUNT */

#include <atomic>     /* the chain / network caches, read across tasks */
#include <stdio.h>    /* snprintf — payout address normalisation */
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"    /* settings_wipe_if_new_build — erases the partition */
#include "esp_log.h"
#include "esp_random.h"

#include "CW_Utils.h"   /* secure_wipe / secure_compare (CODING_RULES §1.4) */

extern "C" {
#include "keccak256.h"
}

#include "config.h"   /* MAX_FEE / MAX_PRIORITY_FEE — compile-time fee defaults */

/* Same guard main.cpp carries: a config.h without the contract compiles to an
 * empty string that fails its decode and disables the asset. */
#ifndef TRON_ADDR_USDT
#define TRON_ADDR_USDT  ""
#endif
/* Mainnet halves of both pairs: empty means the asset is refused on mainnet. */
#ifndef TRON_ADDR_USDT_MAIN
#define TRON_ADDR_USDT_MAIN  ""
#endif
#ifndef ADDR_USDC_MAIN
#define ADDR_USDC_MAIN  ""
#endif

static const char *const TAG = "settings";

#define NS_SETTINGS   "settings"
#define K_BRIGHTNESS  "bright"
#define K_WIFI_SSID   "wifi_ssid"
#define K_WIFI_PASS   "wifi_pass"
#define K_MAX_FEE     "max_fee_gw"
#define K_PRIO_FEE    "prio_fee_gw"
#define K_ADMIN_SALT  "adm_salt"
#define K_ADMIN_HASH  "adm_hash"
#define K_ADMIN_FAILS "adm_fails"
#define K_CHAIN       "chain"
#define K_MAINNET     "mainnet"
/* Minutes east of UTC, stored biased — see settings_get_tz_offset_min(). */
#define K_TZ_OFFSET   "tz_off"
#define K_TZ_DST      "tz_dst"
/* Touch calibration, one key per axis, packed min<<16 | max. */
#define K_TOUCH_X     "touch_x"
#define K_TOUCH_Y     "touch_y"
/* BUILD_ID of the image that last wrote NVS — see settings_wipe_if_new_build. */
#define K_BUILD_ID    "build_id"
#ifndef BUILD_ID
#error "BUILD_ID is set from git by the project CMakeLists.txt"
#endif
/* The sale between broadcast and verdict — see settings_inflight_save. */
#define K_INFLIGHT    "inflight"
/* Payout addresses, each stored twice — see settings_get_payout. */
#define K_PAY_ETH     "pay_eth"
#define K_PAY_ETH2    "pay_eth_e"
#define K_PAY_TRX     "pay_trx"
#define K_PAY_TRX2    "pay_trx_e"
/* Token contracts, same treatment — see settings_get_contract. One key pair per
 * deployment, so a network switch never carries a testnet contract to mainnet. */
#define K_CT_ETH      "ct_eth"
#define K_CT_ETH2     "ct_eth_e"
#define K_CT_TRX      "ct_trx"
#define K_CT_TRX2     "ct_trx_e"
#define K_CT_ETH_M    "ct_eth_m"
#define K_CT_ETH_M2   "ct_eth_me"
#define K_CT_TRX_M    "ct_trx_m"
#define K_CT_TRX_M2   "ct_trx_me"

/* ADMIN_SALT_LEN, ADMIN_HASH_LEN and ADMIN_CODE_HASH_MAX are in settings_rules.h. */

#define DEFAULT_BRIGHTNESS  80U

/* config.h carries the fees in wei; the UI works in Gwei. */
#define WEI_PER_GWEI               1000000000ULL
#define DEFAULT_MAX_FEE_GWEI       (uint32_t)(MAX_FEE / WEI_PER_GWEI)
#define DEFAULT_PRIORITY_FEE_GWEI  (uint32_t)(MAX_PRIORITY_FEE / WEI_PER_GWEI)

/******************************************************************
 * NVS scalar helpers
 *
 * A failed read falls back to the default, never a guess, and is not logged
 * (reads are frequent and the default is correct). A failed write is logged.
 ******************************************************************/
static uint8_t nvs_u8_get(const char *key, uint8_t def)
{
    uint8_t val = def;
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READONLY, &h) == ESP_OK) {
        (void)nvs_get_u8(h, key, &val);
        nvs_close(h);
    }
    return val;
}

static void nvs_u8_set(const char *key, uint8_t val)
{
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_set_u8(h, key, val);
        (void)nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGW(TAG, "%s: nvs_open failed", key);
    }
}

static uint32_t nvs_u32_get(const char *key, uint32_t def)
{
    uint32_t val = def;
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READONLY, &h) == ESP_OK) {
        (void)nvs_get_u32(h, key, &val);
        nvs_close(h);
    }
    return val;
}

static void nvs_u32_set(const char *key, uint32_t val)
{
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_set_u32(h, key, val);
        (void)nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGW(TAG, "%s: nvs_open failed", key);
    }
}

/******************************************************************
 * Chain and network caches
 *
 * Read on nearly every UI pass (per keypad digit), so cached in RAM; -1 means
 * "not read yet". Atomic: the UI task writes, the main task reads mid-payment.
 * The chain cache is written through settings_set_chain(). The network cache
 * keeps the boot value until restart (provision.cpp reboots to apply it).
 ******************************************************************/
static std::atomic<int16_t> s_chain_cache{-1};
static std::atomic<int8_t>  s_mainnet_cache{-1};

/** @brief Forget both, so the next read goes back to flash. For the erase paths. */
static void cache_invalidate(void)
{
    s_chain_cache.store(-1);
    s_mainnet_cache.store(-1);
}

pos_chain_t settings_get_chain(void)
{
    const int16_t cached = s_chain_cache.load();
    if (cached >= 0) { return (pos_chain_t)cached; }

    pos_chain_t chain = POS_CHAIN_ETH_USDC;
    /* Unknown value = a downgrade or a corrupt cell; fall back to the default
     * rather than charge on a chain no code path can handle. */
    const uint8_t stored = nvs_u8_get(K_CHAIN, (uint8_t)POS_CHAIN_ETH_USDC);
    if (stored < (uint8_t)POS_CHAIN__COUNT) {
        chain = (pos_chain_t)stored;
    }
    s_chain_cache.store((int16_t)chain);
    return chain;
}

void settings_set_chain(pos_chain_t chain)
{
    nvs_u8_set(K_CHAIN, (uint8_t)chain);
    /* After the write, not before: a reader that arrives in between gets the old
     * value, which is the one still in flash. */
    s_chain_cache.store((int16_t)chain);
}

bool settings_get_mainnet(void)
{
    const int8_t cached = s_mainnet_cache.load();
    if (cached >= 0) { return (cached != 0); }

    /* Every way of not knowing (no key, unopenable namespace) lands on mainnet:
     * a terminal that guesses "testnet" reports payments that settle nowhere. */
    const bool mainnet = (nvs_u8_get(K_MAINNET, 1U) != 0U);
    s_mainnet_cache.store(mainnet ? 1 : 0);
    return mainnet;
}

void settings_set_mainnet(bool mainnet)
{
    nvs_u8_set(K_MAINNET, mainnet ? 1U : 0U);
    /* Flash only, not the cache: the cache stays the boot value until the
     * restart, so a sale signed in the window before it still gets the chain id,
     * endpoint and contract slot of the deployment it was built against. */
    ESP_LOGW(TAG, "network set to %s", mainnet ? "mainnet" : "testnet");
}

const char *settings_net_str(const char *testnet, const char *mainnet)
{
    return settings_get_mainnet() ? mainnet : testnet;
}

uint8_t settings_get_brightness(void)
{
    return nvs_u8_get(K_BRIGHTNESS, DEFAULT_BRIGHTNESS);
}

void settings_set_brightness(uint8_t pct)
{
    if (pct > 100U) { pct = 100U; }
    nvs_u8_set(K_BRIGHTNESS, pct);
}

int16_t settings_get_tz_offset_min(void)
{
    /* Stored biased by 720 so it fits the unsigned helpers the rest of this
     * file uses, and so a missing key reads as UTC rather than as UTC-12. */
    const uint32_t raw = nvs_u32_get(K_TZ_OFFSET, (uint32_t)TZ_OFFSET_BIAS);
    return tz_offset_decode(raw);   /* nonsense in NVS is UTC, not a wild clock */
}

bool settings_set_tz_offset_min(int16_t minutes)
{
    if (!tz_offset_valid(minutes)) {
        return false;
    }
    nvs_u32_set(K_TZ_OFFSET, tz_offset_encode(minutes));
    return true;
}

uint8_t settings_get_tz_dst(void)
{
    return tz_dst_decode(nvs_u8_get(K_TZ_DST, (uint8_t)CIVIL_DST_NONE));
}

bool settings_set_tz_dst(uint8_t rule)
{
    if (!tz_dst_valid(rule)) { return false; }
    nvs_u8_set(K_TZ_DST, rule);
    return true;
}

/* Packed two per u32 (min<<16 | max) — two keys instead of four, and an axis
 * can never be half-written. */
#define TOUCH_CAL_DEF_MIN  200U
#define TOUCH_CAL_DEF_MAX  3800U

void settings_get_touch_cal(uint16_t *x_min, uint16_t *x_max,
                            uint16_t *y_min, uint16_t *y_max)
{
    const uint32_t def = (TOUCH_CAL_DEF_MIN << 16) | TOUCH_CAL_DEF_MAX;
    uint32_t x = nvs_u32_get(K_TOUCH_X, def);
    uint32_t y = nvs_u32_get(K_TOUCH_Y, def);
    *x_min = (uint16_t)(x >> 16); *x_max = (uint16_t)(x & 0xFFFFU);
    *y_min = (uint16_t)(y >> 16); *y_max = (uint16_t)(y & 0xFFFFU);
}

void settings_set_touch_cal(uint16_t x_min, uint16_t x_max,
                            uint16_t y_min, uint16_t y_max)
{
    /* A smaller span is a double-tap on one spot; storing it would leave the
     * panel, calibration screen included, untappable. */
    const uint16_t MIN_SPAN = 500U;
    if ((x_max < x_min + MIN_SPAN) || (y_max < y_min + MIN_SPAN)) {
        ESP_LOGW(TAG, "touch cal rejected: span too small");
        return;
    }
    nvs_u32_set(K_TOUCH_X, ((uint32_t)x_min << 16) | x_max);
    nvs_u32_set(K_TOUCH_Y, ((uint32_t)y_min << 16) | y_max);
}

bool settings_has_wifi(void)
{
    bool present = false;
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READONLY, &h) == ESP_OK) {
        char ssid[33] = {0};
        size_t len = sizeof(ssid);
        present = (nvs_get_str(h, K_WIFI_SSID, ssid, &len) == ESP_OK) &&
                  (ssid[0] != '\0');
        nvs_close(h);
    }
    return present;
}

bool settings_get_wifi(char *ssid, size_t ssid_n, char *pass, size_t pass_n)
{
    if ((ssid == NULL) || (pass == NULL) || (ssid_n == 0U) || (pass_n == 0U)) {
        return false;
    }
    ssid[0] = '\0';
    pass[0] = '\0';

    bool ok = false;
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READONLY, &h) == ESP_OK) {
        size_t ls = ssid_n;
        size_t lp = pass_n;
        if ((nvs_get_str(h, K_WIFI_SSID, ssid, &ls) == ESP_OK) &&
            (nvs_get_str(h, K_WIFI_PASS, pass, &lp) == ESP_OK) &&
            (ssid[0] != '\0')) {
            ok = true;
        } else {
            ssid[0] = '\0';
            pass[0] = '\0';
        }
        nvs_close(h);
    }
    return ok;
}

void settings_set_wifi(const char *ssid, const char *pass)
{
    if ((ssid == NULL) || (pass == NULL)) { return; }
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_set_str(h, K_WIFI_SSID, ssid);
        (void)nvs_set_str(h, K_WIFI_PASS, pass);
        (void)nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGW(TAG, "wifi: nvs_open failed");
    }
}

/* Fee bounds (settings_rules.h) are enforced here, not by the caller: these feed
 * tx.max_fee, the gas ceiling the customer's card pays. Out of range in flash is
 * a corrupt cell and reads as the default. */
static uint32_t fee_get(const char *key, uint32_t dflt)
{
    const uint32_t v = nvs_u32_get(key, dflt);
    return ((v < FEE_GWEI_MIN) || (v > FEE_GWEI_MAX)) ? dflt : v;
}

uint32_t settings_get_max_fee_gwei(void)
{
    return fee_get(K_MAX_FEE, DEFAULT_MAX_FEE_GWEI);
}

uint32_t settings_get_priority_fee_gwei(void)
{
    return fee_get(K_PRIO_FEE, DEFAULT_PRIORITY_FEE_GWEI);
}

bool settings_set_fees_gwei(uint32_t max_gwei, uint32_t prio_gwei)
{
    if (fee_pair_check(max_gwei, prio_gwei) != FEE_PAIR_OK) { return false; }
    nvs_u32_set(K_MAX_FEE, max_gwei);
    nvs_u32_set(K_PRIO_FEE, prio_gwei);
    return true;
}

/* admin_derive() — keccak256(salt || code) — is in settings_rules.h. */

static void admin_set_fails(uint8_t n)
{
    nvs_u8_set(K_ADMIN_FAILS, n);
}

bool settings_has_admin_code(void)
{
    bool present = false;
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = 0U;
        present = (nvs_get_blob(h, K_ADMIN_HASH, NULL, &len) == ESP_OK) &&
                  (len == ADMIN_HASH_LEN);
        nvs_close(h);
    }
    return present;
}

bool settings_set_admin_code(const char *code)
{
    if (code == NULL) { return false; }

    uint8_t salt[ADMIN_SALT_LEN];
    esp_fill_random(salt, sizeof(salt));

    uint8_t hash[ADMIN_HASH_LEN];
    admin_derive(code, salt, hash);

    /* Reported rather than swallowed: the menu — factory reset included — is
     * unreachable without a stored code, so a silent write failure would leave
     * a terminal only a USB erase can rescue. */
    bool ok = false;
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READWRITE, &h) == ESP_OK) {
        ok = (nvs_set_blob(h, K_ADMIN_SALT, salt, sizeof(salt)) == ESP_OK) &&
             (nvs_set_blob(h, K_ADMIN_HASH, hash, sizeof(hash)) == ESP_OK) &&
             (nvs_set_u8(h, K_ADMIN_FAILS, 0U) == ESP_OK) &&
             (nvs_commit(h) == ESP_OK);
        nvs_close(h);
        ESP_LOGI(TAG, "admin code set: %s", ok ? "ok" : "FAILED");
    } else {
        ESP_LOGW(TAG, "admin code: nvs_open failed");
    }
    CW_Utils::secure_wipe(hash, sizeof(hash));
    return ok;
}

bool settings_check_admin_code(const char *code)
{
    if (code == NULL) { return false; }

    uint8_t salt[ADMIN_SALT_LEN];
    uint8_t stored[ADMIN_HASH_LEN];
    bool    have = false;

    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READONLY, &h) == ESP_OK) {
        size_t ls = sizeof(salt);
        size_t lh = sizeof(stored);
        have = (nvs_get_blob(h, K_ADMIN_SALT, salt, &ls) == ESP_OK) &&
               (nvs_get_blob(h, K_ADMIN_HASH, stored, &lh) == ESP_OK) &&
               (ls == ADMIN_SALT_LEN) && (lh == ADMIN_HASH_LEN);
        nvs_close(h);
    }
    if (!have) { return false; }   /* no code stored — nothing to match */

    uint8_t calc[ADMIN_HASH_LEN];
    admin_derive(code, salt, calc);
    const bool ok = CW_Utils::secure_compare(calc, stored, ADMIN_HASH_LEN);
    CW_Utils::secure_wipe(calc, sizeof(calc));

    if (ok) {
        if (settings_admin_fail_count() != 0U) { admin_set_fails(0U); }
    } else {
        const uint8_t n = settings_admin_fail_count();
        admin_set_fails((n < 255U) ? (uint8_t)(n + 1U) : 255U);
        ESP_LOGW(TAG, "admin unlock failed (%u consecutive)", (unsigned)(n + 1U));
    }
    return ok;
}

uint8_t settings_admin_fail_count(void)
{
    return nvs_u8_get(K_ADMIN_FAILS, 0U);
}

/* Money-carrying addresses (payout recipient, token contract): a value and an
 * echo copy, compared on read, so a torn write or flipped bit in NVS cannot
 * silently redirect a payment or switch the asset. */

/**
 * @brief Read a dual-stored address, falling back to @p def on any doubt.
 *
 * @param[in]  k_val  NVS key holding the value.
 * @param[in]  k_echo NVS key holding its echo copy.
 * @param[in]  def    Compile-time fallback, already in the returned form.
 * @param[in]  what   Label for the log line when the copies disagree.
 * @return true if the stored pair agreed and was returned.
 */
static bool dual_get(const char *k_val, const char *k_echo, const char *def,
                     const char *what, char *out, size_t n)
{
    if ((out == NULL) || (n == 0U)) { return false; }
    out[0] = '\0';

    char val[SETTINGS_PAYOUT_MAX]  = { 0 };
    char echo[SETTINGS_PAYOUT_MAX] = { 0 };
    bool stored = false;

    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READONLY, &h) == ESP_OK) {
        size_t lv = sizeof(val);
        size_t le = sizeof(echo);
        stored = (nvs_get_str(h, k_val, val, &lv) == ESP_OK) &&
                 (nvs_get_str(h, k_echo, echo, &le) == ESP_OK);
        nvs_close(h);
    }

    /* secure_compare, not strcmp: this decides where money goes, so the
     * comparison must not leak on length or short-circuit on the first byte. */
    if (stored && !CW_Utils::secure_compare(reinterpret_cast<const uint8_t *>(val),
                                            reinterpret_cast<const uint8_t *>(echo),
                                            sizeof(val))) {
        ESP_LOGE(TAG, "%s: stored copies disagree - using config.h", what);
        stored = false;
    }

    (void)snprintf(out, n, "%s", stored ? val : def);
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(val), sizeof(val));
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(echo), sizeof(echo));
    return stored;
}

/** @brief Write a dual-stored address, normalising Ethereum to "0x"-prefixed. */
static bool dual_set(const char *k_val, const char *k_echo, bool tron,
                     const char *what, const char *addr)
{
    /* Normalise to the form the getter hands back, so the echo comparison
     * compares like with like on the next boot. */
    char norm[SETTINGS_PAYOUT_MAX];
    if (!settings_addr_normalise(tron, addr, norm)) { return false; }

    bool         ok = false;
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READWRITE, &h) == ESP_OK) {
        ok = (nvs_set_str(h, k_val,  norm) == ESP_OK) &&
             (nvs_set_str(h, k_echo, norm) == ESP_OK) &&
             (nvs_commit(h) == ESP_OK);
        nvs_close(h);
    }
    if (ok) {
        ESP_LOGW(TAG, "%s set to %s", what, norm);
    } else {
        ESP_LOGE(TAG, "%s: NVS write failed", what);
    }
    return ok;
}

bool settings_get_payout(bool tron, char *out, size_t n)
{
    /* Ethereum addresses are handed out "0x"-prefixed so every caller can parse
     * them directly; config.h stores them bare, the setup form accepts either. */
    return tron
        ? dual_get(K_PAY_TRX, K_PAY_TRX2, TRON_ADDR_TO, "payout(tron)", out, n)
        : dual_get(K_PAY_ETH, K_PAY_ETH2, "0x" ADDR_TO, "payout(eth)",  out, n);
}

bool settings_has_payout(bool tron)
{
    char scratch[SETTINGS_PAYOUT_MAX];
    const bool stored = settings_get_payout(tron, scratch, sizeof(scratch));
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(scratch), sizeof(scratch));
    return stored;
}

bool settings_set_payout(bool tron, const char *addr)
{
    return tron
        ? dual_set(K_PAY_TRX, K_PAY_TRX2, true,  "payout(tron)", addr)
        : dual_set(K_PAY_ETH, K_PAY_ETH2, false, "payout(eth)",  addr);
}

bool settings_get_contract(pos_chain_t chain, char *out, size_t n)
{
    const bool m = settings_get_mainnet();
    const bool tron = (chain == POS_CHAIN_TRON_USDT);
    if (!tron && (chain != POS_CHAIN_ETH_USDC)) {
        if ((out != NULL) && (n > 0U)) { out[0] = '\0'; }
        return false;
    }
    return tron
        ? dual_get(m ? K_CT_TRX_M : K_CT_TRX, m ? K_CT_TRX_M2 : K_CT_TRX2,
                   m ? TRON_ADDR_USDT_MAIN : TRON_ADDR_USDT,
                   "contract(tron)", out, n)
        : dual_get(m ? K_CT_ETH_M : K_CT_ETH, m ? K_CT_ETH_M2 : K_CT_ETH2,
                   m ? "0x" ADDR_USDC_MAIN : "0x" ADDR_USDC,
                   "contract(eth)",  out, n);
}

bool settings_set_contract(pos_chain_t chain, const char *addr)
{
    const bool m = settings_get_mainnet();
    const bool tron = (chain == POS_CHAIN_TRON_USDT);
    if (!tron && (chain != POS_CHAIN_ETH_USDC)) { return false; }
    return tron
        ? dual_set(m ? K_CT_TRX_M : K_CT_TRX, m ? K_CT_TRX_M2 : K_CT_TRX2,
                   true,  "contract(tron)", addr)
        : dual_set(m ? K_CT_ETH_M : K_CT_ETH, m ? K_CT_ETH_M2 : K_CT_ETH2,
                   false, "contract(eth)",  addr);
}

bool settings_wipe_if_new_build(void)
{
    nvs_handle_t h;
    uint32_t     stored = 0U;    /* no key yet reads as 0, i.e. older than any build */
    if (nvs_open(NS_SETTINGS, NVS_READONLY, &h) == ESP_OK) {
        (void)nvs_get_u32(h, K_BUILD_ID, &stored);
        nvs_close(h);
    }
    /* Only equality keeps the settings. Newer, older or no stamp means another
     * image wrote this NVS, possibly one built to plant a payout address. An
     * ordering test would let such an image stamp a large number and keep it.
     * Logged either way so both numbers are on the console. */
    if (stored == BUILD_ID) {
        ESP_LOGI(TAG, "build %u - settings written by this build, kept",
                 (unsigned)BUILD_ID);
        return false;
    }

    ESP_LOGW(TAG, "stamped %u, running build %u - erasing NVS",
             (unsigned)stored, (unsigned)BUILD_ID);
    esp_err_t err = nvs_flash_erase();
    if (err == ESP_OK) { err = nvs_flash_init(); }
    if (err != ESP_OK) {
        /* No stamp written, so the next boot retries. Meanwhile the unit keeps
         * its old settings: one that still takes payments beats a brick. */
        ESP_LOGE(TAG, "NVS erase failed (%s) - settings kept", esp_err_to_name(err));
        return false;
    }

    /* Stamp straight away: without it every boot would wipe again. */
    if (nvs_open(NS_SETTINGS, NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_set_u32(h, K_BUILD_ID, (uint32_t)BUILD_ID);
        (void)nvs_commit(h);
        nvs_close(h);
    }
    /* The caches describe the erased partition. Nothing has read them yet at
     * this point, but invalidating does not rely on that ordering. */
    cache_invalidate();
    return true;
}

void settings_factory_reset(void)
{
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READWRITE, &h) == ESP_OK) {
        /* Drops brightness, Wi-Fi creds, the admin code and any operator-set
         * payout address, so the terminal comes back up in first-run setup. */
        (void)nvs_erase_all(h);
        /* Restore the build stamp, or the next boot would wipe again and report
         * an update that never happened. */
        (void)nvs_set_u32(h, K_BUILD_ID, (uint32_t)BUILD_ID);
        (void)nvs_commit(h);
        nvs_close(h);
        ESP_LOGW(TAG, "settings: factory reset");
    }
    cache_invalidate();   /* the chain and network flag went with the erase */

    /* provision.cpp's namespace, cleared here because this function means
     * "forget the operator" and a new one must not inherit any stored keys. */
    if (nvs_open("prov", NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_erase_all(h);
        (void)nvs_commit(h);
        nvs_close(h);
        ESP_LOGW(TAG, "settings: portal namespace cleared");
    }
}

bool settings_inflight_save(const void *rec, size_t n)
{
    if ((rec == NULL) || (n == 0U)) { return false; }
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "inflight: nvs_open failed - sale not persisted");
        return false;
    }
    const bool ok = (nvs_set_blob(h, K_INFLIGHT, rec, n) == ESP_OK) &&
                    (nvs_commit(h) == ESP_OK);
    nvs_close(h);
    if (!ok) { ESP_LOGE(TAG, "inflight: write failed - sale not persisted"); }
    return ok;
}

bool settings_inflight_load(void *rec, size_t n)
{
    if ((rec == NULL) || (n == 0U)) { return false; }
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READONLY, &h) != ESP_OK) { return false; }
    size_t len = n;
    const esp_err_t err = nvs_get_blob(h, K_INFLIGHT, rec, &len);
    nvs_close(h);
    /* A size mismatch is a different layout; settings_wipe_if_new_build already
     * prevents inheriting one, so this is belt and braces, not migration. */
    return (err == ESP_OK) && (len == n);
}

void settings_inflight_clear(void)
{
    nvs_handle_t h;
    if (nvs_open(NS_SETTINGS, NVS_READWRITE, &h) != ESP_OK) { return; }
    /* NOT_FOUND (no broadcast happened) is the ordinary case; writes nothing. */
    if (nvs_erase_key(h, K_INFLIGHT) == ESP_OK) { (void)nvs_commit(h); }
    nvs_close(h);
}
