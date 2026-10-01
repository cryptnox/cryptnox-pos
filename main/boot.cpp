/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file boot.cpp
 * @ingroup app
 * @brief Bring-up: NVS, panel, reader, wallet, addresses, setup wizard, Wi-Fi
 *        and clock — everything before the main loop.
 */

#include "pos_app.h"

/* Quiet CW_Logger: swallows the SDK's verbose connection/retry chatter on the
 * UART. Our own logs go through ESP_LOGx. */
class NullLogger : public CW_Logger {
public:
    bool begin(unsigned long) override { return true; }
    void print(const __FlashStringHelper*) override {}
    void print(const char*) override {}
    void print(char) override {}
    void print(uint8_t, int) override {}
    void print(uint16_t, int) override {}
    void print(uint32_t, int) override {}
    void print(int, int) override {}
    void println() override {}
    void println(const __FlashStringHelper*) override {}
    void println(const char*) override {}
    void println(char) override {}
    void println(uint8_t, int) override {}
    void println(uint16_t, int) override {}
    void println(uint32_t, int) override {}
    void println(int, int) override {}
};

/* PN532 on I²C, wired to the CYD CN1 connector. */
#define PN532_I2C_PORT      0
#define PN532_SDA           27
#define PN532_SCL           22
#define PN532_IRQ           (-1)
#define PN532_RST           (-1)
#define PN532_I2C_HZ        100000U

/* The CYD's onboard RGB LED, common anode: HIGH is off, a floating pin glows.
 * Unused, so it is driven off at boot. */
#define LED_R               GPIO_NUM_4
#define LED_G               GPIO_NUM_16
#define LED_B               GPIO_NUM_17

/* Startup retry budgets. The effort is spent out here rather than inside
 * net_wifi_connect(), since each call resets the association properly:
 * 3 x (1 + WIFI_MAX_RETRY) associations, 45 s worst case. */
#define WIFI_SAVED_ATTEMPTS  3U
#define TIME_SYNC_ATTEMPTS   3U

/* Picker notes, shared by boot and the settings Wi-Fi change. */
const char *const NOTE_JOIN_FAILED =
    "Could not join that network - check the password";
const char *const NOTE_NO_TIME =
    "No network time - this Wi-Fi has no usable internet";

/* Credentials the picker just joined with, held until a clock sync proves the
 * network usable end to end (see wifi_keep_or_drop). Associating is not enough:
 * a captive-portal or offline AP joins fine and would then be reached for on
 * every boot. Deferring the write also means a transient NTP outage never
 * erases a saved network that does work. */
static char s_join_ssid[33] = { 0 };
static char s_join_pass[65] = { 0 };

/**
 * @brief Persist or discard the pending picker credentials, then scrub them.
 *
 * @param[in] keep true once the clock is set — the only proof the network is
 *                 actually usable; false to drop them unpersisted.
 */
void wifi_keep_or_drop(bool keep)
{
    if (keep && (s_join_ssid[0] != '\0')) {
        settings_set_wifi(s_join_ssid, s_join_pass);
        ESP_LOGI(TAG, "saved network '%s' (clock synced)", s_join_ssid);
    }
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_join_pass), sizeof(s_join_pass));
    (void)memset(s_join_ssid, 0, sizeof(s_join_ssid));
}

/**
 * @brief Block until the UI reports @p want, discarding anything else.
 *
 * For the modal first-run steps. The queue is flushed on the way out so a
 * repeated tap is not read by the next stage (wifi_picker() would rescan and
 * throw away a half-typed password).
 */
void wait_for_ui_event(ui_event_t want)
{
    ui_msg_t msg;
    bool     got = false;
    while (!got) {
        if (xQueueReceive(s_ui_queue, &msg, portMAX_DELAY) != pdTRUE) { continue; }
        got = (msg.event == want);
    }
    (void)xQueueReset(s_ui_queue);
}

/**
 * @brief Try the saved credentials, staying on the splash while it happens.
 *
 * Unattended, so it reports through @ref ui_set_boot_status. config.h Wi-Fi
 * credentials are intentionally not used (NVS only).
 *
 * @return true if a saved network was joined.
 */
bool wifi_try_saved(void)
{
    char ssid[33] = { 0 };
    char pass[65] = { 0 };
    if (!settings_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass))) {
        return false;
    }

    ui_set_boot_status("Connecting to Wi-Fi");
    bool ok = false;
    for (uint32_t a = 1U; (a <= WIFI_SAVED_ATTEMPTS) && !ok; a++) {
        ESP_LOGI(TAG, "Wi-Fi '%s': attempt %" PRIu32 "/%u",
                 ssid, a, WIFI_SAVED_ATTEMPTS);
        ok = net_wifi_connect(ssid, pass);
    }
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(pass), sizeof(pass));
    if (!ok) {
        ESP_LOGW(TAG, "saved network '%s' failed %u times",
                 ssid, WIFI_SAVED_ATTEMPTS);
    }
    return ok;
}

/**
 * @brief Run the panel network picker (scan → list → keyboard → connect) until
 *        connected.
 *
 * Blocks until a connection succeeds. The fallback to the browser setup
 * (@ref run_wizard): reached from the settings menu, or when the SoftAP could not
 * come up.
 *
 * A network joined here is not persisted: the credentials are staged for
 * @ref wifi_keep_or_drop, which the caller invokes once the clock proves the uplink
 * usable.
 *
 * @param[in] note One-line reason shown above the picker, or NULL.
 * @return true always: the picker took the screen, so restore the splash.
 */
bool wifi_picker(const char *note)
{
    net_wifi_ap_t aps[16];
    uint16_t n = net_wifi_scan(aps, 16);
    ui_show_wifi_list(aps, n, note);

    ui_msg_t msg;
    while (true) {
        if (xQueueReceive(s_ui_queue, &msg, portMAX_DELAY) != pdTRUE) { continue; }

        if (msg.event == UI_EVENT_WIFI_TRY) {
            char w_ssid[33] = { 0 };
            char w_pass[65] = { 0 };
            bool ok = false;
            if (ui_take_wifi_creds(w_ssid, sizeof(w_ssid),
                                   w_pass, sizeof(w_pass)) > 0U) {
                /* Interactive: the operator expects to see the attempt. */
                ui_show_wifi_connecting(w_ssid);
                ok = net_wifi_connect(w_ssid, w_pass);
                if (ok) {
                    /* Staged, not saved — wifi_keep_or_drop() decides once the
                     * clock has proven this network carries real internet. */
                    (void)snprintf(s_join_ssid, sizeof(s_join_ssid), "%s", w_ssid);
                    (void)snprintf(s_join_pass, sizeof(s_join_pass), "%s", w_pass);
                }
            }
            CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(w_pass), sizeof(w_pass));
            if (ok) { return true; }   /* the picker owns the screen */
            note = NOTE_JOIN_FAILED;
        } else if (msg.event == UI_EVENT_WIFI_SCAN) {
            note = NULL;   /* rescan asked for — the old reason is stale */
        }

        /* WIFI_SCAN, a failed connect, or any stray event: re-scan and show
         * the list again so the user stays in setup until connected. */
        n = net_wifi_scan(aps, 16);
        ui_show_wifi_list(aps, n, note);
    }
}

/**
 * @brief Resolve the EVM payout address into its dual store, at boot.
 *
 * Take the operator's value if there is one, probe-parse it, fall back to
 * config.h if that fails, then parse twice into the dual store (§7.1), which
 * also runs the EIP-55 checksum, so a mistyped address is caught at boot.
 *
 * A stored value that will not parse does not stop the boot (the setup page
 * can only check an address structurally), but it is reported through
 * @p rejected, and that payout must refuse every sale: falling back to the
 * config.h recipient would quietly pay an address the operator never chose (a
 * development address, even on mainnet). A bad config.h value IS fatal: it is
 * the integrator's own doing and there is nothing left to fall back to.
 *
 * @param[out] rejected Set true when a stored value was refused.
 * @return false when even ADDR_TO will not parse (does not actually return:
 *         see @ref boot_fault).
 */
static bool resolve_evm_payout(bool *rejected);

/* How long a startup fault stays on the panel before the terminal restarts. */
#define BOOT_FAULT_RESTART_S  30U

/**
 * @brief Show a startup fault, then restart. Does not return.
 *
 * Most boot faults (a reader that missed its first I2C wake-up, a card stack that
 * did not come up) clear on the next boot, and an unattended terminal must not
 * stay stopped. On an unconfirmed update (ota_mark_valid runs after the wallet)
 * the restart rolls back to the image that worked. A persistent fault keeps
 * showing, 30 s at a time.
 */
void boot_fault(ui_boot_err_t kind, const char *detail)
{
    ui_show_boot_error(kind, detail);
    ESP_LOGE(TAG, "startup fault - restarting in %u s",
             static_cast<unsigned>(BOOT_FAULT_RESTART_S));
    vTaskDelay(pdMS_TO_TICKS(BOOT_FAULT_RESTART_S * 1000U));
    esp_restart();
}

static bool resolve_evm_payout(bool *rejected)
{
    if (settings_get_payout(false, s_payout_eth, sizeof(s_payout_eth)) &&
        !eth_addr_parse(s_payout_eth, s_dest.addr)) {
        ESP_LOGE(TAG, "stored Ethereum payout address rejected");
        (void)snprintf(s_payout_eth, sizeof(s_payout_eth), "0x%s", ADDR_TO);
        *rejected = true;
    }
    /* Twice, each pass independent — that is what makes it a dual store. */
    if (!eth_addr_parse(s_payout_eth, s_dest.addr) ||
        !eth_addr_parse(s_payout_eth, s_dest.addr_echo)) {
        ESP_LOGE(TAG, "Bad ADDR_TO in config");
        boot_fault(UI_BOOT_ERR_CONFIG, "Bad ADDR_TO in config");
        return false;   /* not reached */
    }
    return true;
}

/**
 * @brief Run the browser wizard until the operator presses Finish.
 *
 *   1. admin code   on the panel  — done by the caller; its value is that it
 *                                   never crosses a network.
 *   2. QR code      on the panel  — a camera joins the SoftAP and the captive
 *                                   portal opens the page.
 *   3. authorise    both          — the browser asks; the panel takes the code.
 *   4. addresses    in the browser
 *   5. Wi-Fi        in the browser
 *   6. Finish       on the panel  — restarts, which applies everything.
 *
 * @param[in] wifi_only Steps 5 and 6 only, for a configured terminal that lost its
 *                      network. No admin code: the form can only propose what the
 *                      panel must accept, and the AP passphrase on that panel is
 *                      the perimeter (see prov_set_wifi_only).
 * @return false if the portal could not be raised; the caller falls back to the
 *         panel.
 */
bool run_wizard(CryptnoxWallet &wallet, Pn532NfcTransport &transport,
                       CW_CryptoProvider &crypto, bool wifi_only)
{
    if (!prov_start(PROV_MODE_WIZARD, ui_event_dispatch)) {
        ESP_LOGE(TAG, "setup portal unavailable - falling back to the panel");
        return false;
    }

    /* The Wi-Fi scan runs on THIS task as the step opens: it hops the radio
     * across channels and briefly drops SoftAP clients, so inside an HTTP handler
     * it would drop the very browser asking. */
    auto enter_step = [](prov_step_t step) {
        prov_set_step(step);
        ui_show_prov(step);
        if (step == PROV_STEP_WIFI) {
            net_wifi_ap_t aps[16];
            const uint16_t n_aps = net_wifi_scan(aps, 16);
            prov_set_scan(aps, n_aps);
        }
    };

    /* A configured terminal that only lost its network opens on the Wi-Fi step
     * with nothing to authorise. The page still cannot store anything without the
     * panel, and the AP passphrase it was reached through is on that panel. */
    if (wifi_only) { prov_set_wifi_only(); }
    enter_step(wifi_only ? PROV_STEP_WIFI : PROV_STEP_AUTH);

    /* One tap yields both addresses but only one value can wait on the panel, so
     * the second is parked here until the first is resolved. */
    char card_eth[SETTINGS_PAYOUT_MAX]  = "";
    char card_tron[SETTINGS_PAYOUT_MAX] = "";

    ui_msg_t msg;
    while (true) {
        if (xQueueReceive(s_ui_queue, &msg, portMAX_DELAY) != pdTRUE) { continue; }

        switch (msg.event) {
            case UI_EVENT_PROV_AUTH:
                /* A browser asks to be let in: demand the admin code on the panel.
                 * prov_auth_resolve() reports a grant back as PROV_NEXT; a refusal
                 * reports nothing and the browser can ask again. */
                ui_show_prov_auth();
                break;

            /* One event moves the flow on, whether it came from the browser's
             * Continue button or from prov_auth_resolve() letting it in. */
            case UI_EVENT_PROV_NEXT:
                if (!prov_authed()) { break; }
                if (prov_step() == PROV_STEP_AUTH) {
                    prov_set_note("");
                    enter_step(wifi_only ? PROV_STEP_WIFI : PROV_STEP_ADDR);
                } else if (prov_step() == PROV_STEP_ADDR) {
                    /* Refuse to leave the address step with nothing set: with no
                     * payout address the wizard would end on a till that cannot
                     * take a payment. */
                    if (!settings_has_payout(false) && !settings_has_payout(true)) {
                        prov_set_note("Set at least one payout address first.");
                        break;
                    }
                    prov_set_note("");
                    /* A terminal that already has a network is finished here; the
                     * addresses were all that was missing. The AP goes down with the
                     * portal, so the last word is on the panel. */
                    if (settings_has_wifi()) {
                        prov_stop();
                        ui_show_prov(PROV_STEP_DONE);
                    } else {
                        ui_set_prov_note("");   /* the address step's reason, if any */
                        enter_step(PROV_STEP_WIFI);
                    }
                } else {
                    /* Nowhere left to go, but refresh the panel: this is also how
                     * the Wi-Fi-only flow reports that a browser walked in. */
                    ui_show_prov(prov_step());
                }
                break;

            case UI_EVENT_PROV_SCAN: {
                net_wifi_ap_t aps[16];
                const uint16_t n_aps = net_wifi_scan(aps, 16);
                prov_set_scan(aps, n_aps);
                break;
            }

            case UI_EVENT_PROV_CARD:
                /* Collect the PIN first: the card will not export a key without
                 * it. */
                card_eth[0]  = '\0';
                card_tron[0] = '\0';
                s_user_cancelled = false;
                prov_set_note("Follow the terminal screen.");
                ui_show_card_pin();
                break;

            case UI_EVENT_CARD_PIN: {
                char   pin[16]   = { 0 };
                size_t pin_chars = ui_take_pin(pin, sizeof(pin));
                char   err[48]   = { 0 };
                const bool got = card_read_payouts(wallet, transport, crypto,
                                                   pin, pin_chars,
                                                   card_eth, sizeof(card_eth),
                                                   card_tron, sizeof(card_tron),
                                                   err, sizeof(err));
                CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(pin), sizeof(pin));

                ui_show_prov(prov_step());   /* back to the QR/step screen */
                if (!got) {
                    prov_set_note(err);
                    ui_set_prov_note(err);   /* and on the panel they tapped */
                    break;
                }
                prov_set_note("Accept each address on the terminal screen.");
                /* Ethereum first if there is one; the Tron one waits in card_tron
                 * and is offered when this proposal is resolved. */
                if (card_eth[0] != '\0') {
                    (void)prov_propose(PROV_ASK_PAYOUT_ETH, card_eth);
                    card_eth[0] = '\0';
                } else if (card_tron[0] != '\0') {
                    (void)prov_propose(PROV_ASK_PAYOUT_TRON, card_tron);
                    card_tron[0] = '\0';
                }
                break;
            }

            case UI_EVENT_PROV_VALUE:
                ui_show_prov_confirm();   /* decimals: see token_decimals_ok */
                break;

            case UI_EVENT_PROV_VALUE_SET:
            case UI_EVENT_PROV_VALUE_NO:
                /* Accepted or refused, the panel slot is free again — so offer the
                 * second card-derived address if one is still parked. */
                if (card_tron[0] != '\0') {
                    (void)prov_propose(PROV_ASK_PAYOUT_TRON, card_tron);
                    card_tron[0] = '\0';
                }
                break;

            case UI_EVENT_WIFI_TRY: {
                if (!prov_authed()) { break; }
                ui_set_prov_note("");   /* a new attempt drops the last reason */
                char w_ssid[33] = { 0 };
                char w_pass[65] = { 0 };
                bool joined = false;
                if (ui_take_wifi_creds(w_ssid, sizeof(w_ssid),
                                       w_pass, sizeof(w_pass)) > 0U) {
                    ui_show_wifi_connecting(w_ssid);
                    /* Associating proves nothing (a captive-portal or offline AP
                     * joins fine). The clock is the proof, and TLS needs it too. */
                    if (!net_wifi_connect(w_ssid, w_pass)) {
                        prov_set_note(NOTE_JOIN_FAILED);
                        ui_set_prov_note(NOTE_JOIN_FAILED);   /* and on the panel */
                        /* It may still be half-associated (DHCP times out in
                         * net_wifi_connect(), not the driver); a late lease would
                         * put the setup forms on that LAN. Back to AP-only. */
                        net_wifi_disconnect();
                    } else if (!net_time_sync(15000U)) {
                        prov_set_note(NOTE_NO_TIME);
                        ui_set_prov_note(NOTE_NO_TIME);
                        /* Joined but not kept, and the portal stays up. Its HTTP
                         * server binds every interface, so the association would
                         * leave the setup forms open to the venue LAN, which the
                         * AP passphrase does not guard. Drop it. */
                        net_wifi_disconnect();
                    } else {
                        settings_set_wifi(w_ssid, w_pass);
                        joined = true;
                    }
                }
                CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(w_pass),
                                      sizeof(w_pass));
                if (joined) {
                    /* Done. The AP goes down with the portal (the phone was
                     * dropped when we joined), so the last word is on the panel. */
                    prov_stop();
                    ui_show_prov(PROV_STEP_DONE);
                } else {
                    /* Back on the setup AP alone. Rescan: the list is a minute old
                     * and the network that would not join may have gone. */
                    enter_step(PROV_STEP_WIFI);
                }
                break;
            }

            case UI_EVENT_PROV_STOP:
                /* Asked for by the UI (see request_prov_stop in ui.cpp), done
                 * here because it blocks for up to ~2 s. */
                if (prov_mode() != PROV_MODE_OFF) { prov_stop(); }
                break;

            case UI_EVENT_PROV_FINISH:
                /* Restart to apply. The recipient and contract dual stores are
                 * built at boot from validated strings; rebuilding them in place
                 * would be a second, re-validating path into the money code. */
                prov_stop();
                ESP_LOGW(TAG, "setup finished - restarting to apply it");
                ui_set_boot_status("Applying settings");
                vTaskDelay(pdMS_TO_TICKS(600));   /* let the screen land */
                esp_restart();
                break;

            default:
                break;
        }
    }
}

/**
 * @brief Block on an SNTP sync so TLS certificate validity-period checks run
 *        against real time instead of the 1970 epoch.
 *
 * @return true once the clock is set, false after @ref TIME_SYNC_ATTEMPTS
 *         rounds — flaky uplinks often need a second try.
 */
bool sync_time(void)
{
    for (uint32_t a = 1U; a <= TIME_SYNC_ATTEMPTS; a++) {
        ESP_LOGI(TAG, "SNTP sync: attempt %" PRIu32 "/%u", a, TIME_SYNC_ATTEMPTS);
        if (net_time_sync(15000U)) { return true; }
    }
    return false;
}

/**
 * @brief Everything before the main loop. Returns the card stack, which lives
 *        for the life of the program.
 */
pos_hw_t pos_boot(void)
{
    ESP_LOGI(TAG, "===== cryptnox-pos boot =====");

    /* Before anything slow: the LED is lit from reset until this runs. */
    for (gpio_num_t p : { LED_R, LED_G, LED_B }) {
        (void)gpio_set_direction(p, GPIO_MODE_OUTPUT);
        (void)gpio_set_level(p, 1);
    }
#ifdef CRYPTNOX_POS_DEV_BUILD
    /* build timestamp helps firmware fingerprinting — dev builds only. */
    ESP_LOGI(TAG, "Build: %s %s", __DATE__, __TIME__);
#endif

    /* pn532 at INFO emits only a few useful init lines; the adapters stay at
     * WARN because their per-APDU chatter is verbose. */
    esp_log_level_set("pn532", ESP_LOG_INFO);
    esp_log_level_set("pn532_adapter", ESP_LOG_WARN);
    esp_log_level_set("Pn532NfcTransport", ESP_LOG_WARN);

    /* The PN532 NACKs its address while busy (normal protocol), and i2c.master
     * logs an error burst for every poll. Mute it; real I2C failures still
     * propagate through the SDK's return codes. */
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    /* ── NVS first: required by the WiFi driver AND by the UI task, which
     * reads the saved backlight level / Wi-Fi credentials at startup. ── */
    esp_err_t nvs_ret = nvs_flash_init();
    if ((nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    /* Before the UI task, Wi-Fi driver and recipient: erasing the partition needs
     * every NVS handle shut. Wipes only when this image's BUILD_ID differs from
     * the one that last ran here (settings.h). */
    const bool wiped = settings_wipe_if_new_build();

    /* ── UI: splash visible while the rest boots ───────── */
    s_ui_queue = xQueueCreate(8, sizeof(ui_msg_t));
    ui_init(ui_event_dispatch);
    ui_refresh_addresses();
    /* No ui_show_splash() here — ui_init() already selects it, and asking twice
     * races the UI task into rebuilding the screen and replaying the logo. */

    /* Who is paid on EVM: operator-set if there is one, config.h otherwise,
     * dual-stored either way — see resolve_evm_payout for why a stored value is
     * allowed to fail and a config.h one is not. */
    (void)resolve_evm_payout(&s_payout_bad[0]);   /* false does not return: boot_fault */
    /* The panel clock's zone: SNTP sets UTC and the band adds this operator
     * setting. Logged only. */
    ESP_LOGI(TAG, "clock: UTC%+d:%02d, DST rule %u",
             settings_get_tz_offset_min() / 60,
             (settings_get_tz_offset_min() < 0 ? -settings_get_tz_offset_min()
                                               : settings_get_tz_offset_min()) % 60,
             static_cast<unsigned>(settings_get_tz_dst()));

    ESP_LOGI(TAG, "networks: %s",
             settings_get_mainnet() ? "PRODUCTION" : "test");

    /* Warn if the recipient carries no EIP-55 checksum (no upper-case hex
     * letter) — the boot-time typo check above is a no-op on an all-lowercase
     * address. Skip the "0x" so the 'x' is never read as hex. */
    bool addr_checksummed = false;
    for (const char *pc = s_payout_eth + 2; *pc != '\0'; ++pc) {
        if ((*pc >= 'A') && (*pc <= 'F')) { addr_checksummed = true; break; }
    }
    if (!addr_checksummed) {
        ESP_LOGW(TAG, "recipient is all-lowercase: no EIP-55 checksum verified");
    }

    /* ── PN532 NFC reader ──────────────────────────────────────── */
    ui_set_boot_status("Starting NFC reader");
    static pn532_t nfc;   /* this and the stack below outlive pos_boot() */
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(&nfc), sizeof(nfc));

    pn532_config_t nfc_cfg;
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(&nfc_cfg), sizeof(nfc_cfg));
    nfc_cfg.transport     = PN532_TRANSPORT_I2C;
    nfc_cfg.i2c_port      = PN532_I2C_PORT;
    nfc_cfg.pin_sda       = PN532_SDA;
    nfc_cfg.pin_scl       = PN532_SCL;
    nfc_cfg.pin_irq       = PN532_IRQ;
    nfc_cfg.pin_rst       = PN532_RST;
    nfc_cfg.i2c_clock_hz  = PN532_I2C_HZ;

    /* Keep the error code — unplugged reader and misconfigured bus look
     * identical on screen otherwise. */
    esp_err_t nfc_ret = pn532_init(&nfc, &nfc_cfg);
    if (nfc_ret != ESP_OK) {
        ESP_LOGE(TAG, "PN532 bus init failed: %s", esp_err_to_name(nfc_ret));
        boot_fault(UI_BOOT_ERR_NFC, esp_err_to_name(nfc_ret));
    }

    /* pn532_init() only brings up the bus and ignores its own probe results
     * (pn532.h), so it returns ESP_OK with no reader attached. Probe here —
     * 0 means no answer — or an absent reader is reported as a wallet fault. */
    uint32_t nfc_fw = pn532_get_firmware_version(&nfc);
    if (nfc_fw == 0U) {
        ESP_LOGE(TAG, "PN532 did not answer GetFirmwareVersion - reader absent?");
        boot_fault(UI_BOOT_ERR_NFC, "No answer to GetFirmwareVersion");
    }
    ESP_LOGI(TAG, "PN532 firmware: IC 0x%02X, version %u.%u",
             (unsigned)((nfc_fw >> 24) & 0xFFU),
             (unsigned)((nfc_fw >> 16) & 0xFFU),
             (unsigned)((nfc_fw >> 8) & 0xFFU));

    /* ── Wallet ────────────────────────────────────────────────── */
    ui_set_boot_status("Opening wallet");
    static NullLogger logger;
    (void)logger.begin(115200UL);
    static ESP32CryptoProvider cryptoProvider;

    /* Tron recipient: base58check-decoded by the SDK, which validates the
     * checksum, so a mistyped config.h address fails the boot instead of sending
     * TRX to a stranger. Decoded twice, independently, into the dual store (§7.1). */
    uint8_t tron_to21[CW_TRON_ADDRESS_BYTES];
    uint8_t tron_to21_echo[CW_TRON_ADDRESS_BYTES];
    /* Same fallback as the Ethereum recipient. This is the authoritative
     * base58check on a stored Tron address: the setup page only checks length,
     * prefix and alphabet (the decoder needs the crypto provider). */
    if (settings_get_payout(true, s_payout_tron, sizeof(s_payout_tron)) &&
        !CW_Tron::decodeAddress(s_payout_tron, cryptoProvider, tron_to21)) {
        ESP_LOGE(TAG, "stored Tron payout address rejected - sales refused");
        (void)snprintf(s_payout_tron, sizeof(s_payout_tron), "%s", TRON_ADDR_TO);
        s_payout_bad[1] = true;
    }
    if (!CW_Tron::decodeAddress(s_payout_tron, cryptoProvider, tron_to21) ||
        !CW_Tron::decodeAddress(s_payout_tron, cryptoProvider, tron_to21_echo)) {
        ESP_LOGE(TAG, "Bad TRON_ADDR_TO in config");
        boot_fault(UI_BOOT_ERR_CONFIG, "Bad TRON_ADDR_TO in config");
    }
    (void)CW_Utils::safe_memcpy(s_tron_dest.addr, sizeof(s_tron_dest.addr),
                                &tron_to21[1], ETH_ADDR_LEN);
    (void)CW_Utils::safe_memcpy(s_tron_dest.addr_echo,
                                sizeof(s_tron_dest.addr_echo),
                                &tron_to21_echo[1], ETH_ADDR_LEN);
    /* The resolved address, not the literal: the boot log shows where takings
     * actually go. */
    ESP_LOGI(TAG, "Tron recipient: %s", s_payout_tron);

    /* Every token's contract, parsed twice into its own store. Non-fatal: a
     * placeholder contract must still boot; selecting that asset is refused. */
    for (size_t i = 0U; i < TOKEN_CFG_COUNT; i++) {
        token_load(&TOKEN_CFG[i], cryptoProvider);
    }

    /* Refuse to come up selling an asset whose payout address nobody set.
     *
     * The picker blocks *switching* to such a network, but the stored chain may
     * already be one (Ethereum configured, Tron not, chain still Tron), which
     * would look normal and pay the compile-time recipient. Corrected here, where
     * both the stored chain and the resolved addresses are in hand. */
    if (!settings_has_payout(chain_is_tron())) {
        if (settings_has_payout(!chain_is_tron())) {
            const pos_chain_t to = chain_is_tron() ? POS_CHAIN_ETH_USDC
                                                   : POS_CHAIN_TRON_TRX;
            ESP_LOGW(TAG, "selected chain has no payout address - switching to %d",
                     static_cast<int>(to));
            settings_set_chain(to);
            ui_refresh_addresses();
        } else {
            /* Neither network is configured: every sale would be refused at
             * UI_EVENT_AMOUNT_CONFIRMED. The setup below runs the address step
             * before the main loop; logged for the "won't take payments" case. */
            ESP_LOGW(TAG, "no payout address configured - running setup");
        }
    }
    static Pn532NfcTransport nfcTransport(&nfc, logger);
    static ESP32Platform     platform;
    static CryptnoxWallet    wallet(nfcTransport, logger, cryptoProvider, platform);

    if (!wallet.begin()) {
        ESP_LOGE(TAG, "Wallet begin failed");
        boot_fault(UI_BOOT_ERR_WALLET, NULL);
    }

    /* The bar for keeping a firmware update: the panel, the card reader and the
     * wallet layer all came up on this image. Before this line any reset sends
     * the bootloader back to the previous slot, which is right for a build that
     * cannot drive its own hardware — see ota.h.
     *
     * Deliberately NOT after the network, the setup wizard or an RPC round-trip.
     * A new build wipes the settings, so its first boot runs the whole wizard; a
     * power cut, a router still booting or an RPC outage during setup would then
     * roll a good image back and wipe the settings again. Those are the venue's
     * problems, not the image's, and they are retried below. */
    const bool fresh_update = ota_mark_valid();

    /* The first boot on a new image would look like a plain power-cycle, so greet
     * and name the version on the first screen an operator sees. The wipe itself
     * happened at the top of this function. */
    char greeting[96] = "";
    if (fresh_update || wiped) {
        char shown[OTA_VERSION_SHOWN_MAX];
        (void)snprintf(greeting, sizeof(greeting), "Updated to %s.%s",
                       ota_version_display(ota_running_version(), shown,
                                           sizeof(shown)),
                       wiped ? " Settings are cleared - set the terminal up again."
                             : "");
    }

    /* ── WiFi + RPC ────────────────────────────────────────────── */
    /* URL, credentials and pinned certificate for the selected EVM network.
     * Re-applied at each payment; here it only aims the readiness probe below. */
    eth_rpc_select();
    tron_rpc_init(settings_net_str(TRON_URL, TRON_URL_MAIN));
#ifdef TRON_CA_CERT_PEM
    /* Optional: the Tron node is not trusted anyway (every transaction it
     * serialises is re-derived and compared before signing, tron_tx.h), but
     * pinning makes an attacker beat both that check and TLS. */
    tron_rpc_set_ca_cert(TRON_CA_CERT_PEM);
#endif
    /* Name the unpinned endpoints: each trusts any of the ~150 CAs in the
     * bundle. A release build should normally have none of these lines. */
#ifndef RPC_CA_CERT_PEM
    ESP_LOGW(TAG, "TLS: Ethereum RPC not pinned (RPC_CA_CERT_PEM) - full CA bundle");
#endif
#ifndef POLY_CA_CERT_PEM
    ESP_LOGW(TAG, "TLS: Polygon RPC not pinned (POLY_CA_CERT_PEM) - full CA bundle");
#endif
#ifndef TRON_CA_CERT_PEM
    ESP_LOGW(TAG, "TLS: Tron RPC not pinned (TRON_CA_CERT_PEM) - full CA bundle");
#endif
    /* ── Not configured: greet, take the admin code, then hand over to the browser ──
     *
     * Either one missing runs setup:
     *
     *   - the admin code: a virgin or factory-reset terminal. Gets the greeting
     *     and the code screen.
     *   - a payout address: a *half*-configured terminal, the worse state. It
     *     looks normal, boots to the amount screen and refuses every sale with
     *     nothing on the panel saying why. Testing for the code alone would skip
     *     the wizard on such a terminal (an admin code survives an update).
     *
     * The admin code is the one step that stays on this panel: its value is that
     * it is never on a network. It must also exist first, because the Wi-Fi
     * picker's back arrow (honoured by the UI task alone) lands on the amount
     * screen with the burger menu while main is still in setup; with no code, that
     * menu would open the settings freely. The creation screen has no way out.
     *
     * run_wizard() ends in a restart, which applies the addresses. It only returns
     * if the portal could not be raised; the panel picker below is the fallback. */
    const bool no_code   = !settings_has_admin_code();
    const bool no_payout = !settings_has_payout(false) && !settings_has_payout(true);
    const bool setup_run = no_code || no_payout;
    if (no_code) {
        ESP_LOGI(TAG, "no admin code - first-run setup");
        /* First-run wording, or the update greeting: shown here, since the
         * wizard ends in a restart and would never reach it. */
        ui_show_welcome((greeting[0] != '\0') ? greeting : NULL);
        greeting[0] = '\0';
        wait_for_ui_event(UI_EVENT_WELCOME_DONE);

        ui_show_admin_set();
        wait_for_ui_event(UI_EVENT_ADMIN_SET);
    } else if (no_payout) {
        ESP_LOGW(TAG, "admin code stored but no payout address - setup resumes at "
                      "the addresses");
    }
    if (setup_run) {
        (void)run_wizard(wallet, nfcTransport, cryptoProvider, false);
    }

    ui_set_boot_status("Starting network");
    net_wifi_init();

    /* Wi-Fi and a valid clock are one bring-up step, since TLS needs both: a
     * failed sync sends the operator back to setup with the reason.
     *
     * A configured terminal (a payout address, not merely an admin code) that only
     * lost its network gets the wizard cut short to the Wi-Fi step. The panel
     * picker is only reached when the SoftAP would not come up. */
    bool        try_saved  = true;
    /* The block above already ran the full wizard, so it has had its turn. */
    bool        offer_setup = !setup_run;
    const char *net_note   = NULL;
    bool        synced     = false;
    /* A fully set-up terminal boots unattended, often before the router after a
     * power cut. It comes up offline rather than into setup: Wi-Fi re-join and SNTP
     * retry in the background, and sales are refused until both are back (see
     * UI_EVENT_AMOUNT_CONFIRMED). */
    const bool unattended = settings_has_wifi() &&
                            (settings_has_payout(false) || settings_has_payout(true));
    while (true) {
        const bool joined = try_saved && wifi_try_saved();
        if (!joined && try_saved && unattended) {
            ESP_LOGW(TAG, "saved network down - coming up offline, re-joining "
                          "in the background");
            net_wifi_keep_trying();
            net_time_background();
            break;
        }
        if (!joined) {
            /* No usable saved network. The browser flow first, once; then the panel
             * picker, which is also where a failed clock sync sends us. */
            if (offer_setup) {
                offer_setup = false;
                /* Cut short only for a terminal with a payout address: an admin
                 * code proves setup started, not that it finished, and a terminal
                 * with no address needs the whole wizard. */
                const bool configured = settings_has_payout(false) ||
                                        settings_has_payout(true);
                (void)run_wizard(wallet, nfcTransport, cryptoProvider, configured);
                /* Only reached if the SoftAP would not come up. */
                ESP_LOGW(TAG, "no setup portal - panel Wi-Fi picker");
            }
            /* Only blame the saved network if there was one — on a terminal that
             * has never been on a network this is the first screen, not a failure. */
            if ((net_note == NULL) && settings_has_wifi()) {
                net_note = "Could not join the saved network";
            }
            (void)wifi_picker(net_note);
            ui_show_splash();   /* the picker took the screen */
        }

        ui_set_boot_status("Syncing clock");
        if (sync_time()) {
            wifi_keep_or_drop(true);    /* proven usable — safe to persist */
            synced = true;
            break;
        }
        ESP_LOGE(TAG, "SNTP time sync failed on this network");
        if (joined && unattended) {
            /* The saved network, up but without time yet — a WAN link that is
             * slower to return than the LAN. Keep it; SNTP retries by itself.
             * (Re-subscribed in case the failure was the back-dated-clock
             * refusal, which unsubscribes.) */
            ESP_LOGW(TAG, "no network time yet - retrying in the background");
            net_time_background();
            break;
        }
        /* Drop the staged credentials, but leave an already-saved network alone:
         * the outage is often transient. */
        wifi_keep_or_drop(false);
        /* Force the picker: retrying the same network loops straight back here. */
        try_saved = false;
        net_note  = NOTE_NO_TIME;
    }

    /* One RPC round-trip at boot: it proves the endpoint is reachable, and its
     * authenticated Date header can contradict the unauthenticated SNTP clock.
     * A clock wrong in the *forward* direction (carrying a certificate past its
     * notAfter) would otherwise only surface mid-payment.
     *
     * A warning, not a gate: every payment request re-does the Date check, so
     * nothing is waved through, and an RPC provider's bad minute must not leave
     * the till dead. Retried, since one failure is usually a flaky uplink.
     * Skipped with no clock: TLS cannot succeed yet. */
    bool rpc_ok = false;
    for (int attempt = 0; synced && (attempt < 3) && !rpc_ok; attempt++) {
        uint64_t boot_nonce = 0U;
        rpc_ok = eth_rpc_get_nonce(&boot_nonce);
    }
    if (synced && !rpc_ok) {
        ESP_LOGE(TAG, "RPC unreachable or clock rejected at boot - carrying on, "
                      "each sale re-checks");
    }

    ESP_LOGI(TAG, "Ready%s", synced ? "" : " (offline - re-joining in the background)");
    /* Heap baseline: everything is up and nothing transient has run. Compare with
     * prov_start()'s line to see what the config page and an upload cost. */
    ESP_LOGI(TAG, "heap at ready: %u free, %u largest block",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    /* The update greeting, unless the first-run welcome already carried it. */
    if (greeting[0] != '\0') {
        ui_show_welcome(greeting);
        wait_for_ui_event(UI_EVENT_WELCOME_DONE);
    }


    return pos_hw_t{ wallet, nfcTransport, cryptoProvider };
}
