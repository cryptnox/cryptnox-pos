/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file main.cpp
 * @ingroup app
 * @brief cryptnox-pos entry point: touchscreen USDC payment terminal.
 *
 * Drives the amount → confirm → sign → broadcast flow on the Cheap Yellow
 * Display, signing EIP-1559 USDC transfers on a Cryptnox card via PN532.
 */


#include "pos_app.h"

/* The main loop wakes at least this often with nothing to do, so it can feed the
 * task watchdog (CONFIG_ESP_TASK_WDT_TIMEOUT_S is 60). */
#define MAIN_LOOP_TICK_MS    5000U
#define HEAP_LOG_PERIOD_US   (10LL * 60LL * 1000000LL)

QueueHandle_t s_ui_queue = NULL;

/* Set by the UI task on Cancel during PLACE_CARD; the main task checks it after
 * signing to skip the FAILED flash. Atomic because it crosses tasks. */
std::atomic<bool> s_user_cancelled{false};

/**
 * @brief UI-task callback: forward a touch event to the main task queue.
 *
 * Runs in the UI task context.  A Cancel tap also raises the atomic
 * @ref s_user_cancelled flag so the in-flight signing flow can abort
 * without waiting for the queue to drain.
 *
 * @param[in] event   UI event identifier.
 * @param[in] payload Event payload (amount in USDC base units, or 0).
 */
void ui_event_dispatch(ui_event_t event, uint64_t payload) {
    if (event == UI_EVENT_CONFIRM_CANCEL) {
        s_user_cancelled = true;
    }
    ui_msg_t msg = { event, payload };
    (void)xQueueSend(s_ui_queue, &msg, 0);
}

/**
 * @brief ESP-IDF application entry point: bring-up, then the main interaction
 *        loop (amount → confirm → sign+broadcast).
 */
extern "C" void app_main(void)
{
    const pos_hw_t hw = pos_boot();
    CryptnoxWallet    &wallet         = hw.wallet;
    Pn532NfcTransport &nfcTransport   = hw.transport;
    CW_CryptoProvider &cryptoProvider = hw.crypto;

    /* ── Main interaction loop ────────────────────────────────── */
    /* A sale that was between broadcast and verdict when the terminal went down
     * (brownout, panic, somebody pulling the plug) is picked up where it stopped:
     * same hash, same checks, and the verdict on the panel — Unconfirmed with
     * "Check again" if the network is not back yet. */
    if (settings_inflight_load(&s_inflight, sizeof(s_inflight)) && s_inflight.active) {
        ESP_LOGW(TAG, "resuming the sale in flight at reset: %s", s_inflight.hash);
        settle_inflight();
    } else {
        settings_inflight_clear();
        ui_show_amount_entry();
    }

    pos_amount_t pending_amount;
    pos_amount_set(&pending_amount, 0U);
    ui_msg_t msg;

    /* A card read from the admin page yields both addresses, but the panel shows
     * one proposal at a time: the Tron one waits here until the Ethereum one is
     * resolved, and the restart that applies a stored address waits for both. */
    char card_tron[SETTINGS_PAYOUT_MAX] = "";
    bool restart_due = false;

    /* On the task watchdog from here on: a loop that stops coming back to its
     * queue is a hung terminal, and resetting it beats a frozen panel. Boot above
     * is not subscribed — the wizard and the picker wait on people. */
    if (esp_task_wdt_add(NULL) != ESP_OK) {
        ESP_LOGE(TAG, "main loop not on the task watchdog");
    }
    int64_t next_heap_log_us = esp_timer_get_time() + HEAP_LOG_PERIOD_US;

    while (true) {
        wdt_feed();
        /* Heap fragmentation is the likely way TLS handshakes start failing after
         * days (fresh handshake per call, no PSRAM); log it every ten minutes. */
        if (esp_timer_get_time() >= next_heap_log_us) {
            next_heap_log_us = esp_timer_get_time() + HEAP_LOG_PERIOD_US;
            ESP_LOGI(TAG, "heap: %u free, %u largest block",
                     static_cast<unsigned>(esp_get_free_heap_size()),
                     static_cast<unsigned>(
                         heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)));
        }
        if (xQueueReceive(s_ui_queue, &msg, pdMS_TO_TICKS(MAIN_LOOP_TICK_MS)) != pdTRUE) {
            continue;
        }

        switch (msg.event) {
            case UI_EVENT_AMOUNT_CONFIRMED: {
                pos_amount_set(&pending_amount, msg.payload);
                /* Refuse before the customer taps: offline, every request the sale
                 * makes would fail after the card and PIN; without a clock TLS
                 * cannot validate a certificate. */
                if (!net_wifi_online() || (wall_ms() == 0U)) {
                    ui_show_tx_status(UI_TX_STATE_FAILED,
                                      !net_wifi_online()
                                          ? "Offline - re-joining the Wi-Fi"
                                          : "Waiting for network time");
                    pos_amount_set(&pending_amount, 0U);
                    break;
                }
                /* Refuse rather than fall back. settings_get_payout() answers with
                 * the config.h recipient when nobody set one, which would send a
                 * customer's money to an address the operator never chose while
                 * looking entirely normal. A till never told where the money goes
                 * declines the sale, as it does for an unset token contract. */
                if (!settings_has_payout(chain_is_tron())) {
                    ui_show_tx_status(UI_TX_STATE_FAILED,
                                      "Payout address not configured");
                    pos_amount_set(&pending_amount, 0U);
                    break;
                }
                /* Set, but it failed the checksum at boot: the dual store holds
                 * the config.h fallback, which nobody chose for this till. */
                if (s_payout_bad[chain_is_tron() ? 1 : 0]) {
                    ui_show_tx_status(UI_TX_STATE_FAILED,
                                      "Payout address invalid - set it again");
                    pos_amount_set(&pending_amount, 0U);
                    break;
                }
                const token_t *tok = active_token();
                /* Say so here rather than after the customer has tapped a card:
                 * a placeholder contract means this asset was never set up. */
                if ((tok != NULL) && !tok->ok) {
                    ui_show_tx_status(UI_TX_STATE_FAILED,
                                      "Token contract not configured");
                    pos_amount_set(&pending_amount, 0U);
                    break;
                }
                char dec_err[64];
                if (!token_decimals_ok(settings_get_chain(), dec_err,
                                       sizeof(dec_err))) {
                    ui_show_tx_status(UI_TX_STATE_FAILED, dec_err);
                    pos_amount_set(&pending_amount, 0U);
                    break;
                }
                /* Reconcile amount + recipient (+ token contract) before they
                 * are shown to the customer — displayed value must equal what
                 * gets signed. */
                if (!IS_TRUE32(amount_consistent(&pending_amount)) ||
                    !IS_TRUE32(address_consistent(active_dest())) ||
                    ((tok != NULL) &&
                     !IS_TRUE32(address_consistent(&tok->addr)))) {
                    pos_handle_anomaly("pre-display reconcile");
                    ui_show_tx_status(UI_TX_STATE_FAILED, "Integrity check failed");
                    pos_amount_set(&pending_amount, 0U);
                    break;
                }
                /* No balance check here: the payer is the card, and nobody has
                 * tapped yet. evm_balance_ok / tron_balance_ok run once it is
                 * present. */
                ui_refresh_addresses();
                /* Show the resolved recipient, NOT the config.h literal: they
                 * differ on a terminal provisioned through the setup page, and the
                 * operator checks the address against this row. s_payout_* is the
                 * string s_dest / s_tron_dest were parsed from, so what is
                 * displayed is what gets signed. */
                evm_fees_wei(chain_is_polygon(), &s_sale_fee.max_fee,
                             &s_sale_fee.prio_fee);
                char fee_txt[40];
                sale_fee_text(fee_txt, sizeof(fee_txt));
                ui_show_confirm(pending_amount.amount_minor,
                                chain_is_tron() ? s_payout_tron : s_payout_eth,
                                fee_txt);
                break;
            }

            case UI_EVENT_CONFIRM_CANCEL:
                ui_show_amount_entry();
                break;

            case UI_EVENT_PIN_ENTERED: {
                if (pending_amount.amount_minor == 0U) {
                    /* No fresh AMOUNT_CONFIRMED preceded this — likely a stale
                     * event from a stuck touch or queue replay. Drop it. */
                    ESP_LOGW(TAG, "stale PIN_ENTERED ignored");
                    break;
                }
                /* Fetch the keypad PIN (UI wipes its own copy on read). */
                char   pin[16]   = { 0 };
                size_t pin_chars = ui_take_pin(pin, sizeof(pin));

                s_user_cancelled = false;
                char err_msg[64] = { 0 };
                inflight_t *fl = &s_inflight;
                CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(fl), sizeof(*fl));
                /* The decided amount, for the final gate — in the record before
                 * the sign, because the record is persisted before the broadcast
                 * (inflight_persist) and a resumed poll needs it too. */
                fl->decided = pending_amount;
                const bcast_t sent =
                    pay_sign_and_broadcast(wallet, nfcTransport, cryptoProvider,
                                           &pending_amount, pin, pin_chars, fl,
                                           err_msg, sizeof(err_msg));
                /* scrub our copy of the PIN as soon as signing is done. */
                CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(pin), sizeof(pin));

                /* Clear pending so the next sign needs a fresh New Payment flow. */
                pos_amount_set(&pending_amount, 0U);
                if (sent != BCAST_FAILED) {
                    /* Out of our hands now, whatever the cancel flag says: a
                     * signed transaction the node may hold is settled by the
                     * chain, not by a tap on the panel. */
                    fl->active          = true;
                    fl->broadcast_known = (sent == BCAST_SENT);
                    settle_inflight();
                } else if (!s_user_cancelled) {
                    settings_inflight_clear();   /* refused: nothing went out */
                    ui_show_tx_status(UI_TX_STATE_FAILED, err_msg);
                } else {
                    /* Cancelled — before the broadcast, so nothing went out
                     * either; drop any record so a reboot does not resume it. */
                    settings_inflight_clear();
                    /* Cancel during PLACE_CARD — UI already on amount entry. */
                }
                break;
            }

            case UI_EVENT_TX_RECHECK:
                /* "Check again" on the Unconfirmed screen: same sale, same hash. */
                if (s_inflight.active) {
                    settle_inflight();
                } else {
                    ui_show_amount_entry();
                }
                break;

            case UI_EVENT_TX_RETRY:
                /* New sale — or, from Unconfirmed, the operator's explicit
                 * "I have checked" Clear. The record goes either way: the nonce
                 * is read from "latest", so if the old one is still pending a
                 * new sale replaces it rather than landing beside it. */
                s_inflight.active = false;
                settings_inflight_clear();
                ui_show_amount_entry();
                break;

            case UI_EVENT_PROV_STOP:
                /* Run here, not on the UI task: prov_stop() blocks for up to ~2 s
                 * and would freeze the panel. */
                if (prov_mode() != PROV_MODE_OFF) { prov_stop(); }
                break;

            case UI_EVENT_OTA_STAGED:
                /* Uploaded and verified firmware; nothing boots until it is
                 * accepted on the panel. Routed through this queue so the offer
                 * waits behind a payment rather than appearing over one. */
                ui_show_ota_confirm();
                break;

            /* ── The admin page, open beside a running terminal ──
             * Same handlers as the wizard's, and for the same reason: a browser may
             * propose, only the panel may accept. Routed through this queue so an
             * offer waits behind a payment in progress rather than appearing over
             * a customer's transaction. */
            case UI_EVENT_PROV_AUTH:
                ui_show_prov_auth();
                break;

            case UI_EVENT_PROV_VALUE:
                ui_show_prov_confirm();   /* decimals: see token_decimals_ok */
                break;

            case UI_EVENT_PROV_VALUE_SET:
            case UI_EVENT_PROV_VALUE_NO:
                if (msg.event == UI_EVENT_PROV_VALUE_SET) { restart_due = true; }
                /* The panel slot is free again: offer the parked Tron address. */
                if (card_tron[0] != '\0') {
                    (void)prov_propose(PROV_ASK_PAYOUT_TRON, card_tron);
                    card_tron[0] = '\0';
                    break;
                }
                if (!restart_due) { break; }
                /* Stored. The recipient and contract dual stores are built at boot
                 * from validated strings, so the change applies through a restart
                 * rather than a second, re-validating path into the money code. */
                ESP_LOGW(TAG, "config changed from the admin page - restarting");
                prov_stop();
                ui_set_boot_status("Applying settings");
                vTaskDelay(pdMS_TO_TICKS(600));
                esp_restart();
                break;

            case UI_EVENT_PROV_CARD: {
                /* Same flow as the wizard: PIN on the keypad, tap, then the
                 * accept-on-the-panel handshake. */
                s_user_cancelled = false;
                prov_set_note("Follow the terminal screen.");
                ui_show_card_pin();
                break;
            }

            case UI_EVENT_CARD_PIN: {
                char   pin[16]   = { 0 };
                size_t pin_chars = ui_take_pin(pin, sizeof(pin));
                char   c_eth[SETTINGS_PAYOUT_MAX]  = "";
                char   c_tron[SETTINGS_PAYOUT_MAX] = "";
                char   err[48] = { 0 };
                const bool got = card_read_payouts(wallet, nfcTransport,
                                                   cryptoProvider,
                                                   pin, pin_chars,
                                                   c_eth, sizeof(c_eth),
                                                   c_tron, sizeof(c_tron),
                                                   err, sizeof(err));
                CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(pin), sizeof(pin));
                if (!got) {
                    /* No setup screen mid-shift: show the reason on the failure
                     * screen rather than dropping silently back to the keypad. */
                    prov_set_note(err);
                    ui_show_tx_status(UI_TX_STATE_FAILED, err);
                    break;
                }
                ui_show_amount_entry();
                prov_set_note("Accept each address on the terminal screen.");
                /* Ethereum first if there is one; Tron waits in card_tron and is
                 * offered when that proposal is resolved (see PROV_VALUE_SET). */
                card_tron[0] = '\0';
                if (c_eth[0] != '\0') {
                    (void)prov_propose(PROV_ASK_PAYOUT_ETH, c_eth);
                    (void)snprintf(card_tron, sizeof(card_tron), "%s", c_tron);
                } else if (c_tron[0] != '\0') {
                    (void)prov_propose(PROV_ASK_PAYOUT_TRON, c_tron);
                }
                break;
            }

            case UI_EVENT_PROV_SCAN: {
                net_wifi_ap_t aps[16];
                const uint16_t n_aps = net_wifi_scan(aps, 16);
                prov_set_scan(aps, n_aps);
                break;
            }

            case UI_EVENT_WIFI_SCAN: {
                /* Scan runs in this task so the UI stays responsive. */
                net_wifi_ap_t aps[16];
                uint16_t n = net_wifi_scan(aps, 16);
                /* Opened from settings, not after a failure — no note. */
                ui_show_wifi_list(aps, n, NULL);
                break;
            }

            case UI_EVENT_WIFI_TRY: {
                char w_ssid[33] = { 0 };
                char w_pass[65] = { 0 };
                if (ui_take_wifi_creds(w_ssid, sizeof(w_ssid),
                                       w_pass, sizeof(w_pass)) > 0U) {
                    ui_show_wifi_connecting(w_ssid);

                    /* Same rule as boot: associating proves nothing, so a clock
                     * sync must prove the uplink before saved credentials are
                     * overwritten. One round only; the operator can tap again. */
                    const char *why = NULL;
                    if (!net_wifi_connect(w_ssid, w_pass)) {
                        why = NOTE_JOIN_FAILED;
                    } else if (!net_time_sync(15000U)) {
                        ESP_LOGW(TAG, "'%s' joined but has no network time -"
                                      " not saved", w_ssid);
                        why = NOTE_NO_TIME;
                    } else {
                        settings_set_wifi(w_ssid, w_pass);   /* persist for next boot */
                        /* From the config page: the terminal must never be on a
                         * network *and* serving that page (httpd binds every
                         * interface), so close it rather than hand the forms to
                         * the new LAN. */
                        if (prov_mode() != PROV_MODE_OFF) { prov_stop(); }
                        ui_show_amount_entry();
                    }

                    if (why != NULL) {
                        /* The radio is still on the rejected network, so the
                         * terminal would look ready while every payment fails.
                         * Roll back to the saved network boot already proved. */
                        char b_ssid[33] = { 0 };
                        char b_pass[65] = { 0 };
                        if (settings_get_wifi(b_ssid, sizeof(b_ssid),
                                              b_pass, sizeof(b_pass)) &&
                            (b_ssid[0] != '\0')) {
                            ESP_LOGW(TAG, "rolling back to saved network '%s'",
                                     b_ssid);
                            ui_show_wifi_connecting(b_ssid);
                            if (!net_wifi_connect(b_ssid, b_pass) &&
                                (prov_mode() == PROV_MODE_OFF)) {
                                /* The saved network is down too; keep retrying
                                 * it in the background. */
                                net_wifi_keep_trying();
                            }
                        }
                        CW_Utils::secure_wipe(
                            reinterpret_cast<uint8_t *>(b_pass), sizeof(b_pass));

                        /* prov_stop() re-joins with the restored credentials, but
                         * no association may stay up while the config page is
                         * served: drop it, back to AP-only. */
                        if (prov_mode() != PROV_MODE_OFF) { net_wifi_disconnect(); }

                        /* Back to the picker with the reason, so another network
                         * can be chosen instead of a dead end. */
                        net_wifi_ap_t aps[16];
                        uint16_t n = net_wifi_scan(aps, 16);
                        ui_show_wifi_list(aps, n, why);
                    }
                }
                CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(w_pass), sizeof(w_pass));
                break;
            }

            default:
                break;
        }
    }
}
