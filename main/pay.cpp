/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file pay.cpp
 * @ingroup app
 * @brief What a sale is paid with and to, and the sale between broadcast and
 *        verdict.
 */

#include "pos_app.h"

/* BIP32 Ethereum derivation path: m/44'/60'/0'/0/0 */
const uint8_t ETH_DERIVE_PATH[20] = {
    0x80U, 0x00U, 0x00U, 0x2CU,
    0x80U, 0x00U, 0x00U, 0x3CU,
    0x80U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U,
};

/* The payout strings the dual stores are built from: operator-set, else config.h.
 * Resolved once at boot; a change during setup applies through a restart, so
 * there is one validated path. Ethereum is "0x"-prefixed for eth_addr_parse. */
char s_payout_eth[SETTINGS_PAYOUT_MAX]   = "";
char s_payout_tron[SETTINGS_PAYOUT_MAX]  = "";

token_t s_token[POS_CHAIN__COUNT];

const token_cfg_t TOKEN_CFG[] = {
    { POS_CHAIN_ETH_USDC,  ADDR_USDC,      ADDR_USDC_MAIN,      "ADDR_USDC"      },
    { POS_CHAIN_ETH_USDT,  ADDR_USDT,      ADDR_USDT_MAIN,      "ADDR_USDT"      },
    { POS_CHAIN_POLY_USDC, POLY_ADDR_USDC, POLY_ADDR_USDC_MAIN, "POLY_ADDR_USDC" },
    { POS_CHAIN_POLY_USDT, POLY_ADDR_USDT, POLY_ADDR_USDT_MAIN, "POLY_ADDR_USDT" },
    { POS_CHAIN_TRON_USDT, TRON_ADDR_USDT, TRON_ADDR_USDT_MAIN, "TRON_ADDR_USDT" },
    { POS_CHAIN_TRON_USDC, TRON_ADDR_USDC, TRON_ADDR_USDC_MAIN, "TRON_ADDR_USDC" },
};

const size_t TOKEN_CFG_COUNT = sizeof(TOKEN_CFG) / sizeof(TOKEN_CFG[0]);


/** @brief The selection's token, or NULL for a native coin. */
token_t *active_token(pos_chain_t chain) {
    if ((unsigned)chain >= (unsigned)POS_CHAIN__COUNT) { return NULL; }
    return pos_asset_of(chain)->native ? NULL : &s_token[chain];
}

/**
 * @brief Parse @p t->str into its dual store, twice and independently.
 *
 * EVM: eth_addr_parse, which also runs the EIP-55 checksum. Tron: base58check,
 * decoded twice by the SDK, so a mistyped contract is refused here rather than
 * charged against the wrong asset.
 */
static bool token_parse(token_t *t, bool tron, CW_CryptoProvider &crypto) {
    if (!tron) {
        t->ok = eth_addr_parse(t->str, t->addr.addr) &&
                eth_addr_parse(t->str, t->addr.addr_echo);
        return t->ok;
    }
    uint8_t c21[CW_TRON_ADDRESS_BYTES];
    uint8_t c21_echo[CW_TRON_ADDRESS_BYTES];
    t->ok = CW_Tron::decodeAddress(t->str, crypto, c21) &&
            CW_Tron::decodeAddress(t->str, crypto, c21_echo);
    if (t->ok) {
        (void)CW_Utils::safe_memcpy(t->addr.addr, sizeof(t->addr.addr),
                                    &c21[1], ETH_ADDR_LEN);
        (void)CW_Utils::safe_memcpy(t->addr.addr_echo, sizeof(t->addr.addr_echo),
                                    &c21_echo[1], ETH_ADDR_LEN);
    }
    return t->ok;
}

/**
 * @brief Load one token at boot: the operator's contract if one is set and
 *        parses, config.h otherwise.
 *
 * Non-fatal by design: a placeholder or a typo leaves @c ok false and that one
 * asset is refused when selected, rather than stopping a terminal that charges
 * in something else from booting.
 */
void token_load(const token_cfg_t *cfg, CW_CryptoProvider &crypto) {
    token_t   *t    = &s_token[cfg->chain];
    const bool tron = pos_chain_is_tron(cfg->chain);
    if (settings_get_contract(cfg->chain, t->str, sizeof(t->str))) {
        t->checked = false;   /* operator-set: token_decimals_ok asks the chain */
        if (token_parse(t, tron, crypto)) { return; }
        ESP_LOGW(TAG, "stored %s contract not usable - trying config.h",
                 pos_asset_of(cfg->chain)->ticker);
    }
    (void)snprintf(t->str, sizeof(t->str), "%s%s", tron ? "" : "0x",
                   settings_net_str(cfg->test, cfg->main));
    t->checked = true;   /* config.h: part of the signed image, vetted at build */
    if (!token_parse(t, tron, crypto)) {
        const pos_asset_t *a = pos_asset_of(cfg->chain);
        ESP_LOGW(TAG, "%s not usable - %s on %s disabled", cfg->name, a->ticker,
                 pos_net_info(a->net)->name);
    }
}

/**
 * @brief Before the first sale in an operator-set token, read its decimals()
 *        and refuse anything but 6.
 *
 * Every amount is signed in 6-decimal base units, so an 18-decimal contract
 * would be charged 10^-12 of the figure on the screen. Checked here rather than
 * when the contract is proposed: the config page runs with the station down, so
 * no node can be asked then. A read that fails refuses the sale too, and the
 * next sale asks again. Once per boot per token.
 *
 * @return true when the token may be charged in.
 */
bool token_decimals_ok(pos_chain_t chain, char *err, size_t err_max)
{
    token_t *t = active_token(chain);
    if ((t == NULL) || t->checked) { return true; }

    uint64_t dec   = 0U;
    bool     asked = false;
    if (pos_chain_is_tron(chain)) {
        uint8_t c21[CW_TRON_ADDRESS_BYTES];
        c21[0] = CW_TRON_ADDRESS_PREFIX;
        (void)CW_Utils::safe_memcpy(&c21[1], sizeof(c21) - 1U, t->addr.addr,
                                    ETH_ADDR_LEN);
        char hex[TRON_ADDR_HEX_LEN + 1U];
        tron_addr_to_hex(c21, hex, sizeof(hex));
        asked = tron_rpc_get_trc20_decimals(hex, &dec);
    } else {
        eth_rpc_select_for(pos_chain_is_polygon(chain));
        asked = eth_rpc_get_token_decimals(t->str, &dec);
        eth_rpc_select();
    }
    if (asked && (dec == 6U)) {
        t->checked = true;
        return true;
    }
    if (asked) {
        (void)snprintf(err, err_max, "Token contract has %u decimals - set it again",
                       static_cast<unsigned>((dec > 99U) ? 99U : dec));
    } else {
        (void)snprintf(err, err_max, "Could not check the token contract");
    }
    ESP_LOGW(TAG, "contract %s refused: %s", t->str, err);
    return false;
}

/**
 * @brief Point the UI's address rows at the selected chain.
 *
 * Called on every entry to the confirm screen and from the asset picker, since
 * the chain can be switched while this task is parked on its queue (see ui.h).
 */
extern "C" void ui_refresh_addresses_for(uint8_t c) {
    const pos_chain_t chain = static_cast<pos_chain_t>(c);
    const token_t    *token = active_token(chain);
    const char       *payee = pos_chain_is_tron(chain) ? s_payout_tron : s_payout_eth;
    if (token != NULL) {
        ui_set_addresses(token->str, payee);
    } else if (pos_chain_is_tron(chain)) {
        ui_set_addresses("Native TRX (no contract)", payee);
    } else {
        ui_set_addresses(pos_chain_is_polygon(chain) ? "Native POL (no contract)"
                                                     : "Native ETH (no contract)",
                         payee);
    }
}

extern "C" void ui_refresh_addresses(void) {
    ui_refresh_addresses_for(static_cast<uint8_t>(settings_get_chain()));
}

/* Dual-stored recipient (§3.2/§7.1): ADDR_TO parsed twice at boot into two
 * independent copies, reconciled before calldata encode and before signing so
 * a transient flip on the working copy can't redirect funds. */
pos_addr_t s_dest;

/* A stored payout address that failed its checksum at boot, [0] EVM, [1] Tron.
 * The dual store then holds the config.h fallback so the boot can continue,
 * and every sale on that family is refused — see resolve_evm_payout. */
bool s_payout_bad[2] = { false, false };

/* The Tron recipient's own dual store. */
pos_addr_t s_tron_dest;

/** @brief The reconciled recipient for the chain currently selected. */
const pos_addr_t *active_dest(void) {
    return chain_is_tron() ? &s_tron_dest : &s_dest;
}

sale_fee_t s_sale_fee;

/* fmt_coin() — the fee ceiling as text, rounded up — is in money.h. */

/**
 * @brief The most network fee the customer's card can be charged on top of the
 *        sale, for the confirm screen. "" where there is no cap to state: a
 *        native TRX transfer burns bandwidth, not a fee limit.
 */
void sale_fee_text(char *out, size_t n)
{
    out[0] = '\0';
    char amt[32];
    if (chain_is_tron()) {
        if (active_token() == NULL) { return; }
        fmt_coin(amt, sizeof(amt), TRON_TRC20_FEE_LIMIT_SUN, 6U, "TRX");
    } else {
        /* gas limit ~1e5 at most, max fee a uint32 of Gwei x 1e9: ~25 bits of
         * headroom in the product, as in evm_balance_ok. */
        const uint64_t wei = (uint64_t)(chain_is_native_evm() ? GAS_LIMIT_NATIVE
                                                              : GAS_LIMIT_ERC20)
                             * s_sale_fee.max_fee;
        fmt_coin(amt, sizeof(amt), wei, 18U, chain_is_polygon() ? "POL" : "ETH");
    }
    (void)snprintf(out, n, "Fee up to %s", amt);
}

inflight_t s_inflight;

/**
 * @brief Write the sale to NVS just before it leaves the terminal.
 *
 * So a brownout or panic during the receipt poll does not lose whether the
 * customer paid. Written as "broadcast not known": a record that survives a
 * reset is by definition one whose answer nobody saw. One write per sale.
 */
void inflight_persist(const inflight_t *fl)
{
    inflight_t rec = *fl;
    rec.active          = true;
    rec.broadcast_known = false;
    (void)settings_inflight_save(&rec, sizeof(rec));
}

/** @brief Unix time in ms, 0 while the clock is unset. */
uint64_t wall_ms(void)
{
    struct timeval tv;
    if ((gettimeofday(&tv, NULL) != 0) || (tv.tv_sec < 1600000000)) { return 0U; }
    return (static_cast<uint64_t>(tv.tv_sec) * 1000U) +
           (static_cast<uint64_t>(tv.tv_usec) / 1000U);
}

/**
 * @brief Poll the in-flight sale for up to 120 s and show what the chain says.
 *
 * Only three answers end a sale: mined and ours (Approved), mined and reverted
 * (nothing moved), or — Tron only — expired without ever being included
 * (nothing can ever move). Everything else, including a receipt that does
 * not match the transfer and a broadcast whose answer was lost, is
 * Unconfirmed: the screen keeps the hash, offers Check again, and never says
 * Declined, because a declined sale is one the merchant charges a second time.
 */
void settle_inflight(void)
{
    inflight_t *fl = &s_inflight;
    /* The sale's own endpoint. A no-op straight after a sale; after a reset it
     * is what points a resumed EVM poll at the network the hash is on. */
    if (!fl->tron) { eth_rpc_select_for(fl->polygon); }
    ui_show_tx_status(UI_TX_STATE_CONFIRMING,
                      fl->broadcast_known ? "Waiting for the block"
                                          : "Checking it was sent");
    const eth_receipt_expect_t want = {
        fl->hash, fl->to, fl->token ? fl->payee : NULL, fl->amount
    };
    eth_rpc_receipt_result_t rc = ETH_RPC_RECEIPT_PENDING;
    bool expired = false;
    const int64_t deadline = esp_timer_get_time() + 120LL * 1000000LL;
    for (;;) {
        wdt_feed();   /* two minutes of polling is on purpose */
        /* Count down so the wait does not look like a hang. Set in place:
         * ui_show_tx_status here would restart the spinner. */
        char left[32];
        snprintf(left, sizeof(left), "%d s remaining",
                 static_cast<int>((deadline - esp_timer_get_time() + 999999LL)
                                  / 1000000LL));
        ui_set_tx_info(left);
        rc = fl->tron ? tron_receipt_as_eth(tron_rpc_get_receipt(fl->hash))
                      : eth_rpc_get_tx_receipt(&want);
        if ((rc == ETH_RPC_RECEIPT_SUCCESS) || (rc == ETH_RPC_RECEIPT_REVERTED) ||
            (rc == ETH_RPC_RECEIPT_MISMATCH)) {
            break;
        }
        /* Tron: the chain refuses a transaction past raw_data.expiration, so a
         * node that still has never heard of it 10 s after that (one more
         * block) means it will never land. Only on "not found" — an RPC error
         * says nothing either way. */
        const uint64_t now = wall_ms();
        if (fl->tron && (rc == ETH_RPC_RECEIPT_PENDING) && (now != 0U) &&
            (now > (fl->expiration_ms + 10000U))) {
            expired = true;
            break;
        }
        if (esp_timer_get_time() >= deadline) { break; }
        vTaskDelay(pdMS_TO_TICKS(4000));
    }

    if (rc == ETH_RPC_RECEIPT_SUCCESS) {
        /* §4: render PAID only if the monotonic gate holds — the on-chain
         * APPROVED verdict AND amount/recipient still self-consistent through
         * the decide→render window. */
        fl->active = false;
        settings_inflight_clear();
        bool32 decision = run_payment_decision(&fl->decided, active_dest(),
                                               POS_VERDICT_APPROVED);
        if (IS_TRUE32(decision)) {
            ESP_LOGI(TAG, "Tx confirmed on-chain");
            ui_show_tx_status(UI_TX_STATE_DONE, fl->hash);
        } else {
            /* Mined OK but the integrity gate failed — never show PAID on a
             * corrupted decision. */
            pos_handle_anomaly("final decision gate");
            ESP_LOGE(TAG, "Integrity gate failed post-receipt: %s", fl->hash);
            ui_show_tx_status(UI_TX_STATE_FAILED, "Integrity check failed");
        }
    } else if (rc == ETH_RPC_RECEIPT_REVERTED) {
        /* A transfer the node accepted and the chain then rejected is, for a
         * till, almost always the card not holding enough of the token.
         * ponytail: a guess, not a reason. Reading the revert data back needs
         * an eth_call replay at the mined block. */
        fl->active = false;
        settings_inflight_clear();
        (void)run_payment_decision(&fl->decided, active_dest(),
                                   POS_VERDICT_DECLINED);
        ESP_LOGE(TAG, "Tx reverted on-chain: %s", fl->hash);
        ui_show_tx_status(UI_TX_STATE_FAILED,
                          "Reverted - check the card's balance");
    } else if (expired) {
        fl->active = false;
        settings_inflight_clear();
        ESP_LOGW(TAG, "Tron tx %s expired unseen - not sent", fl->hash);
        ui_show_tx_status(UI_TX_STATE_FAILED, "Not sent - nothing was charged");
    } else {
        if (rc == ETH_RPC_RECEIPT_MISMATCH) {
            /* The node's receipt is not our transfer. Whether ours landed is
             * unknown — the node that would say is the one lying. */
            pos_handle_anomaly("receipt mismatch");
        }
        ESP_LOGW(TAG, "Tx %s unconfirmed after 120 s", fl->hash);
        ui_show_tx_status(UI_TX_STATE_UNCONFIRMED, fl->hash);
    }
}

/* The family is read once per payment: the operator can switch it between
 * sales, but never mid-sale. */
bcast_t pay_sign_and_broadcast(CryptnoxWallet &wallet, Pn532NfcTransport &transport,
                               CW_CryptoProvider &crypto, const pos_amount_t *amount,
                               const char *pin, size_t pin_chars, inflight_t *fl,
                               char *err_out, size_t err_max)
{
    /* Again here, not only at the confirm step: free once checked, and no route
     * to the card skips it. */
    if (!token_decimals_ok(settings_get_chain(), err_out, err_max)) {
        return BCAST_FAILED;
    }
    return chain_is_tron()
        ? sign_and_broadcast_tron(wallet, transport, crypto, amount, &s_tron_dest,
                                  active_token(), pin, pin_chars, fl,
                                  err_out, err_max)
        : sign_and_broadcast(wallet, transport, amount, &s_dest, pin, pin_chars,
                             fl, err_out, err_max);
}
