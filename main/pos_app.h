/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file pos_app.h
 * @ingroup app
 * @brief Private to the application files (main, boot, card_io, pay, pay_evm,
 *        pay_tron): the state they share and the calls between them.
 */

#ifndef POS_APP_H
#define POS_APP_H

#include <stdio.h>
#include <string.h>
#include <sys/time.h>  /* gettimeofday */
#include <strings.h>   /* strcasecmp */
#include <stdlib.h>
#include <inttypes.h>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "CryptnoxWallet.h"
#include "CW_Utils.h"
#include "Pn532NfcTransport.h"
#include "ESP32Logger.h"
#include "ESP32Platform.h"
#include "esp32_crypto_provider.h"
#include "CW_Tron.h"
#include "settings.h"
#include "assets.h"
#include "provision.h"
#include "ota.h"
#include "ota_version.h"
#include "wdt.h"

extern "C" {
#include "pn532.h"
#include "keccak256.h"
#include "eth_addr.h"
#include "eth_sig.h"
#include "card_status.h"
#include "hardening.h"
#include "eth_rlp.h"
#include "eth_rpc.h"
#include "rpc_error.h"
#include "tron_rpc.h"
#include "tron_tx.h"
#include "net.h"
#include "ui.h"
}

#include "money.h"
#include "config_defaults.h"

static const char *const TAG = "cryptnox_pos";

/* ── Selection ───────────────────────────────────────────────── */

/** @brief true when the operator has switched the terminal to Tron. */
static inline bool chain_is_tron(void) {
    return pos_chain_is_tron(settings_get_chain());
}

/** @brief true when the terminal is charging on Polygon rather than Ethereum. */
static inline bool chain_is_polygon(void) {
    return pos_chain_is_polygon(settings_get_chain());
}

/** @brief true when charging in the network's own coin (ETH / POL), not a token. */
static inline bool chain_is_native_evm(void) {
    return pos_chain_is_native_evm(settings_get_chain());
}

/* ── What a sale is paid with and to (pay.cpp) ───────────────── */

/**
 * @brief A token's contract, dual-stored.
 *
 * The contract decides which asset moves, so it gets the same dual store
 * (§3.2/§7.1) and reconciles as the recipient. One per selection, indexed by
 * pos_chain_t (USDT on Ethereum and Polygon are different deployments). A
 * native coin's slot stays empty.
 */
typedef struct {
    char       str[SETTINGS_PAYOUT_MAX];  /**< as configured: "0x…" or base58   */
    pos_addr_t addr;                      /**< 20 bytes; Tron without the 0x41  */
    bool       ok;                        /**< false until it parsed twice      */
    bool       checked;                   /**< decimals() read and found to be 6 */
} token_t;

/* Where each token's contract comes from in config.h, both deployments. EVM
 * values without the "0x", Tron in base58. USDC on Ethereum and USDT on Tron can
 * also be set from the config page (settings_get_contract), which wins. */
typedef struct {
    pos_chain_t chain;
    const char *test;
    const char *main;
    const char *name;   /**< the macro, for the log line */
} token_cfg_t;

extern const uint8_t     ETH_DERIVE_PATH[20];
extern char              s_payout_eth[SETTINGS_PAYOUT_MAX];
extern char              s_payout_tron[SETTINGS_PAYOUT_MAX];
extern token_t           s_token[POS_CHAIN__COUNT];
extern const token_cfg_t TOKEN_CFG[];
extern const size_t      TOKEN_CFG_COUNT;
extern pos_addr_t        s_dest;
extern pos_addr_t        s_tron_dest;
extern bool              s_payout_bad[2];

token_t          *active_token(pos_chain_t chain = settings_get_chain());
void              token_load(const token_cfg_t *cfg, CW_CryptoProvider &crypto);
bool              token_decimals_ok(pos_chain_t chain, char *err, size_t err_max);
const pos_addr_t *active_dest(void);

/* The fees one sale offers, read ONCE when its confirm screen is built and used
 * for the cap shown, the balance check and the signed transaction. Read
 * separately, a fee changed from the config page in between would make them
 * disagree, and the check could pass a sale the signed tx cannot pay for. */
typedef struct {
    uint64_t max_fee;    /* wei per gas */
    uint64_t prio_fee;   /* wei per gas */
} sale_fee_t;

extern sale_fee_t s_sale_fee;
void sale_fee_text(char *out, size_t n);

/* ── The sale between broadcast and verdict (pay.cpp) ────────── */

/* What a broadcast attempt ended as. UNKNOWN: the node never answered, so the
 * transaction may be on its way. Only the chain can settle it, and until it does
 * the sale is Unconfirmed — never declined, because a declined sale is one the
 * merchant takes again. */
typedef enum {
    BCAST_FAILED = 0,   /**< Refused before or at broadcast: nothing was sent. */
    BCAST_SENT,         /**< The node took it.                                 */
    BCAST_UNKNOWN,      /**< No usable answer: it may or may not be out there. */
} bcast_t;

/* Everything the receipt poll needs, kept here so "Check again" on the
 * Unconfirmed screen resumes the same sale. Persisted as-is to NVS
 * (inflight_persist), so its layout is the record's. */
typedef struct {
    bool     active;
    bool     tron;
    char     hash[72];        /* EVM: "0x"+64, hashed HERE from the signed
                                 bytes; Tron: the txID of verified raw_data   */
    uint8_t  to[ETH_ADDR_LEN];     /* EVM: the tx's `to` (contract or payee)  */
    uint8_t  payee[ETH_ADDR_LEN];  /* EVM token: the Transfer recipient       */
    bool     token;                /* EVM: an ERC-20 call, not a native send  */
    uint64_t amount;               /* EVM token: the Transfer value           */
    uint64_t expiration_ms;        /* Tron: after this it can never land      */
    bool     broadcast_known;      /* false: the broadcast answer was lost    */
    bool     polygon;              /* EVM: which endpoint the receipt is on   */
    pos_amount_t decided;          /* for the final decision gate             */
} inflight_t;

extern inflight_t s_inflight;
void     inflight_persist(const inflight_t *fl);
uint64_t wall_ms(void);
void     settle_inflight(void);

/**
 * @brief Sign and broadcast the reconciled sale on whichever family is selected.
 *        The family is read once, here.
 */
bcast_t pay_sign_and_broadcast(CryptnoxWallet &wallet, Pn532NfcTransport &transport,
                               CW_CryptoProvider &crypto, const pos_amount_t *amount,
                               const char *pin, size_t pin_chars, inflight_t *fl,
                               char *err_out, size_t err_max);

/* ── EVM (pay_evm.cpp) ───────────────────────────────────────── */

void eth_rpc_select_for(bool polygon);
void eth_rpc_select(void);
void evm_fees_wei(bool polygon, uint64_t *max_fee, uint64_t *prio_fee);
bool evm_balance_ok(const pos_amount_t *amount, char *err, size_t err_max);
bcast_t sign_and_broadcast(CryptnoxWallet &wallet, Pn532NfcTransport &transport,
                           const pos_amount_t *amount, const pos_addr_t *to,
                           const char *pin, size_t pin_chars, inflight_t *fl,
                           char *err_out, size_t err_max);

/* ── Tron (pay_tron.cpp) ─────────────────────────────────────── */

void tron_addr_to_hex(const uint8_t *addr21, char *out, size_t n);
bcast_t sign_and_broadcast_tron(CryptnoxWallet &wallet, Pn532NfcTransport &transport,
                                CW_CryptoProvider &crypto, const pos_amount_t *amount,
                                const pos_addr_t *to, const token_t *token,
                                const char *pin, size_t pin_chars, inflight_t *fl,
                                char *err_out, size_t err_max);
eth_rpc_receipt_result_t tron_receipt_as_eth(tron_receipt_t r);

/* ── The card (card_io.cpp) ──────────────────────────────────── */

/**
 * @brief Scrubs a buffer with CW_Utils::secure_wipe when it leaves scope.
 *
 * The payment paths have many early returns; a guard declared next to each
 * sensitive artifact scrubs it on every exit.
 */
struct WipeGuard {
    uint8_t *buf;
    size_t   len;
    explicit WipeGuard(uint8_t *b, size_t n) : buf(b), len(n) {}
    ~WipeGuard() { CW_Utils::secure_wipe(buf, len); }
    WipeGuard(const WipeGuard &) = delete;
    WipeGuard &operator=(const WipeGuard &) = delete;
};

extern const char *s_card_fault;
const char *pin_fail_text(Pn532NfcTransport &transport, const char *wrong);
bool card_connect(CryptnoxWallet &wallet, Pn532NfcTransport &transport,
                  CW_SecureSession &session, bool setup = false);
bool card_sign(CryptnoxWallet &wallet, CW_SecureSession &session,
               const uint8_t *hash, uint8_t hash_len,
               const uint8_t *path, uint8_t path_len,
               const char *pin, size_t pin_chars,
               uint8_t rs_out[64], char *err_out, size_t err_max);
bool card_read_payouts(CryptnoxWallet &wallet, Pn532NfcTransport &transport,
                       CW_CryptoProvider &crypto, const char *pin, size_t pin_chars,
                       char *eth_out, size_t eth_n, char *tron_out, size_t tron_n,
                       char *err, size_t err_n);

/* ── UI ↔ main task (main.cpp) ───────────────────────────────── */

typedef struct {
    ui_event_t event;
    uint64_t   payload;
} ui_msg_t;

extern QueueHandle_t     s_ui_queue;
extern std::atomic<bool> s_user_cancelled;
void ui_event_dispatch(ui_event_t event, uint64_t payload);

/* ── Bring-up (boot.cpp) ─────────────────────────────────────── */

/** @brief The card stack pos_boot() brought up; lives for the program. */
struct pos_hw_t {
    CryptnoxWallet    &wallet;
    Pn532NfcTransport &transport;
    CW_CryptoProvider &crypto;
};

pos_hw_t pos_boot(void);

extern const char *const NOTE_JOIN_FAILED;
extern const char *const NOTE_NO_TIME;
[[noreturn]] void boot_fault(ui_boot_err_t kind, const char *detail);
void wifi_keep_or_drop(bool keep);
void wait_for_ui_event(ui_event_t want);
bool wifi_try_saved(void);
bool wifi_picker(const char *note);
bool run_wizard(CryptnoxWallet &wallet, Pn532NfcTransport &transport,
                CW_CryptoProvider &crypto, bool wifi_only);
bool sync_time(void);

#endif /* POS_APP_H */
