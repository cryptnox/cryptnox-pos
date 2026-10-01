/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file net.cpp
 * @brief Wi-Fi station bring-up (init/scan/connect/RSSI) and SNTP time sync.
 */

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "net.h"
#include "civil_time.h"

#include <string.h>
#include <stdlib.h>     /* malloc, free */
#include <time.h>       /* time */
#include <sys/time.h>   /* settimeofday — clearing a back-dated clock */
#include <inttypes.h>   /* PRId64 */

/* CW_Utils.h pulls in Arduino.h (via platform_compat.h); it must come before
 * any lwip-including IDF header (esp_netif.h, ...) so that IPAddress.h
 * declares INADDR_NONE before lwip defines it as a macro. */
#include "CW_Utils.h"   /* hardened memory primitives (CODING_RULES §1.4) */

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_app_desc.h"   /* esp_app_get_description — build timestamp */
#include "esp_timer.h"      /* the background re-join */
#include "wdt.h"            /* wdt_feed() — callers on the task watchdog block here */
#include "esp_log.h"

static const char *const TAG = "net";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
/* Retries inside one connect call. Kept low: main.cpp retries the whole call
 * (WIFI_SAVED_ATTEMPTS), and each of those resets the association properly. */
#define WIFI_MAX_RETRY      2

/* Way out when the association succeeds but no IP ever arrives (hung DHCP):
 * the FAIL bit is never set, so only this timeout ends the call. */
#define WIFI_TIMEOUT_MS     15000

/* Background re-join once a kept association drops and the immediate retries
 * are spent: 2, 4, 8, 16 s, then every 30 s for as long as it takes. A router
 * that reboots mid-shift is back in a minute or two, and the terminal has to be
 * back on it without anybody power-cycling it. */
#define REJOIN_FIRST_S      2U
#define REJOIN_MAX_S        30U

/******************************************************************
 * 2. Module state
 ******************************************************************/

static EventGroupHandle_t s_wifi_event_group = NULL;
static int                s_retry_num        = 0;
static bool               s_wifi_inited      = false;
static bool               s_sntp_running     = false;

/* Whether the station association is one to hold on to. Set by a successful
 * join (and by net_wifi_keep_trying), cleared by net_wifi_disconnect — which is
 * also what net_ap_start calls, so the re-join never runs while the portal is
 * AP-only. Read from the event and timer tasks, hence volatile. */
static volatile bool      s_keep             = false;
static esp_timer_handle_t s_rejoin_timer     = NULL;
static uint32_t           s_rejoin_s         = 0U;   /* next backoff; 0 = first */

/* Whether the SoftAP is currently the radio's only interface. The scan and
 * connect calls below need the station back for a moment, and this is how they
 * know to put the radio back to AP-only afterwards — and how the re-join knows
 * to stay out of the way. */
static volatile bool      s_ap_up            = false;

/******************************************************************
 * 3. WiFi event handler
 ******************************************************************/

/** @brief Background re-join: one association attempt, from the timer task. */
static void rejoin_cb(void *arg)
{
    (void)arg;
    if (!s_keep || s_ap_up) { return; }
    /* Straight to the ceiling: a failed attempt reschedules through the handler
     * instead of spending the immediate retries a second time. */
    s_retry_num = WIFI_MAX_RETRY;
    ESP_LOGI(TAG, "WiFi re-join attempt");
    (void)esp_wifi_connect();
}

/** @brief Schedule the next re-join, doubling the wait up to REJOIN_MAX_S. */
static void rejoin_schedule(void)
{
    if (!s_keep || s_ap_up || (s_rejoin_timer == NULL)) { return; }
    if (s_rejoin_s == 0U) {
        s_rejoin_s = REJOIN_FIRST_S;
    } else {
        s_rejoin_s = ((s_rejoin_s * 2U) > REJOIN_MAX_S) ? REJOIN_MAX_S
                                                         : (s_rejoin_s * 2U);
    }
    (void)esp_timer_stop(s_rejoin_timer);   /* ESP_ERR_INVALID_STATE if idle */
    if (esp_timer_start_once(s_rejoin_timer,
                             static_cast<uint64_t>(s_rejoin_s) * 1000000ULL) == ESP_OK) {
        ESP_LOGW(TAG, "WiFi down - re-join in %u s", static_cast<unsigned>(s_rejoin_s));
    }
}

/** @brief Stop the background re-join and reset its backoff. */
static void rejoin_cancel(void)
{
    if (s_rejoin_timer != NULL) { (void)esp_timer_stop(s_rejoin_timer); }
    s_rejoin_s = 0U;
}

/**
 * @brief WiFi/IP event handler driving the connect retry state machine.
 *
 * Retries the association up to @ref WIFI_MAX_RETRY times, then signals
 * @ref WIFI_FAIL_BIT — and, for an association that is being kept, hands over
 * to the background re-join, which keeps trying with backoff for as long as it
 * takes. Signals @ref WIFI_CONNECTED_BIT once an IP is bound.
 *
 * @param[in] arg        Unused.
 * @param[in] event_base Event base (WIFI_EVENT or IP_EVENT).
 * @param[in] event_id   Event identifier within @p event_base.
 * @param[in] event_data Unused.
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        /* Started — association is initiated explicitly by
         * net_wifi_connect(), not here, so a start with no credentials
         * (e.g. before provisioning) doesn't churn through retries. */
    } else if ((event_base == WIFI_EVENT) &&
               (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        if (s_wifi_event_group != NULL) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "WiFi retry %d/%d", s_retry_num, WIFI_MAX_RETRY);
        } else {
            if (s_wifi_event_group != NULL) {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            rejoin_schedule();   /* no-op unless the association is being kept */
        }
    } else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        s_retry_num = 0;
        rejoin_cancel();
        if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    } else {
        /* other events ignored */
    }
}

/******************************************************************
 * 4. Public API
 ******************************************************************/

void net_wifi_init(void)
{
    if (s_wifi_inited) { return; }

    s_wifi_event_group = xEventGroupCreate();
    s_retry_num = 0;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    (void)esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Persistent handlers (never unregistered) so init/connect can repeat. */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    const esp_timer_create_args_t targs = {
        .callback        = &rejoin_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "wifi_rejoin",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_rejoin_timer));

    s_wifi_inited = true;
}

/* SoftAP netif, created on first net_ap_start() and then kept: destroying and
 * recreating it across setup steps buys nothing and esp_netif teardown while
 * lwIP still holds sockets is a known way to crash. */
static esp_netif_t *s_ap_netif = NULL;

bool net_ap_start(const char *ssid, const char *pass)
{
    if ((ssid == NULL) || (pass == NULL) || (strlen(pass) < 8U)) { return false; }
    net_wifi_init();   /* idempotent — also guarantees esp_wifi_start() has run */

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) { return false; }
    }

    wifi_config_t cfg;
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(&cfg), sizeof(cfg));
    (void)snprintf(reinterpret_cast<char *>(cfg.ap.ssid), sizeof(cfg.ap.ssid),
                   "%s", ssid);
    cfg.ap.ssid_len = static_cast<uint8_t>(strlen(reinterpret_cast<char *>(cfg.ap.ssid)));
    (void)snprintf(reinterpret_cast<char *>(cfg.ap.password),
                   sizeof(cfg.ap.password), "%s", pass);
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.channel  = 1;
    /* One. WPA2-PSK gives every station the same key, so it does not isolate
     * them from each other: a second association is a silent seat on the wire,
     * watching the operator type the venue's Wi-Fi password. At one, an intruder
     * who got the passphrase off the screen instead takes the operator's seat —
     * the operator cannot join, notices immediately, and restarts setup with a
     * new passphrase. Loud beats quiet. A stale association is cleared the same
     * way; net_ap_stop() and the AP's own inactivity timeout free the slot. */
    cfg.ap.max_connection = 1;

    /* AP-only, and the station goes down first. httpd binds every interface, so
     * in APSTA the config portal answers the venue LAN too — the one place its
     * perimeter (a passphrase on the panel in front of the operator) means
     * nothing. The scan and connect calls borrow the station back below, and
     * net_ap_stop() re-associates. */
    net_wifi_disconnect();
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK)             { return false; }
    if (esp_wifi_set_config(WIFI_IF_AP, &cfg) != ESP_OK)       { return false; }
    /* With one slot, a phone that walked away without deauthenticating holds the
     * only seat until the AP gives up on it, and the 300 s default is five minutes
     * of "cannot join" in the middle of setup. Not much shorter than this, though:
     * the auth step deliberately sends the operator from the phone to the panel, a
     * locked phone stops polling, and deauthing it there is how a browser ends up
     * back on cellular. Two minutes is longer than any panel interaction and less
     * than half the lockout. Diagnosable if it fails: silently keeping 300 s with
     * one slot is the lockout nobody could explain. */
    if (esp_wifi_set_inactive_time(WIFI_IF_AP, 120) != ESP_OK) {
        ESP_LOGW(TAG, "AP idle timeout unchanged - a dropped phone may hold the "
                      "only slot for 5 min");
    }
    s_ap_up = true;
    ESP_LOGI(TAG, "SoftAP '%s' up on 192.168.4.1 (AP-only, station down)", ssid);
    return true;
}

void net_wifi_disconnect(void)
{
    if (!s_wifi_inited) { return; }
    /* Past the retry ceiling first: wifi_event_handler() re-associates on every
     * STA_DISCONNECTED below it, so a bare esp_wifi_disconnect() would undo
     * itself three times over. net_wifi_connect() zeroes the counter again, so
     * nothing after this inherits the ceiling. */
    s_retry_num = WIFI_MAX_RETRY;
    s_keep      = false;   /* ...and the background re-join with it */
    rejoin_cancel();
    (void)esp_wifi_disconnect();
    /* Take the interface down with the association while a portal is up: the
     * association is what put the config forms on the venue LAN, and leaving an
     * idle station enabled leaves half of that in place. */
    if (s_ap_up) { (void)esp_wifi_set_mode(WIFI_MODE_AP); }
    ESP_LOGI(TAG, "station association dropped");
}

void net_ap_stop(void)
{
    if (s_ap_netif == NULL) { return; }
    s_ap_up = false;
    (void)esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "SoftAP down");

    /* Put back the association net_ap_start() displaced. The driver still holds
     * the credentials — only the link went away — so this is a reconnect, not a
     * reconfigure. Skipped when the station is already up, which is the wizard
     * case: it joined the venue network through net_wifi_connect() and the portal
     * comes down in the same breath. */
    wifi_ap_record_t joined;
    if (esp_wifi_sta_get_ap_info(&joined) == ESP_OK) { return; }

    wifi_config_t cfg;
    if ((esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) &&
        (cfg.sta.ssid[0] != '\0')) {
        s_retry_num = 0;   /* net_wifi_disconnect() left it at the ceiling */
        s_keep      = true;   /* the terminal's own network: hold on to it */
        const esp_err_t err = esp_wifi_connect();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "re-joining '%s'",
                     reinterpret_cast<const char *>(cfg.sta.ssid));
        } else {
            /* Loud: a terminal that quietly stayed off its network after the
             * config page closed looks working and fails at the first RPC call. */
            ESP_LOGE(TAG, "could not re-join '%s' (%s) - restart the terminal",
                     reinterpret_cast<const char *>(cfg.sta.ssid),
                     esp_err_to_name(err));
        }
    }
    /* The copy carries the passphrase (CODING_RULES §1.4). */
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(&cfg), sizeof(cfg));
}

uint16_t net_wifi_scan(net_wifi_ap_t *out, uint16_t max)
{
    wdt_feed();   /* a blocking all-channel scan */
    net_wifi_init();
    if ((out == NULL) || (max == 0U)) { return 0U; }

    wifi_scan_config_t scan_cfg;
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(&scan_cfg), sizeof(scan_cfg));   /* all channels, all SSIDs */

    /* Scanning is a station operation, and net_ap_start() switched the station
     * off. Borrow it for the scan and the readout, then hand the radio straight
     * back: AP-only is what keeps the portal off the venue LAN, so the window
     * where it is not AP-only has to be this one call and no longer. */
    const bool borrowed = s_ap_up;
    if (borrowed) { (void)esp_wifi_set_mode(WIFI_MODE_APSTA); }

    uint16_t          num  = 0U;
    wifi_ap_record_t *recs = NULL;
    if (esp_wifi_scan_start(&scan_cfg, true) == ESP_OK) {
        (void)esp_wifi_scan_get_ap_num(&num);
        if (num > 0U) {
            recs = static_cast<wifi_ap_record_t *>(
                       malloc(num * sizeof(wifi_ap_record_t)));
            if (recs != NULL) {
                (void)esp_wifi_scan_get_ap_records(&num, recs);
            }
        }
    }

    if (borrowed) { (void)esp_wifi_set_mode(WIFI_MODE_AP); }
    if (recs == NULL) { return 0U; }

    uint16_t count = 0U;
    for (uint16_t i = 0U; (i < num) && (count < max); i++) {
        const char *ssid = reinterpret_cast<const char *>(recs[i].ssid);
        if (ssid[0] == '\0') { continue; }          /* hidden network */

        bool dup = false;                            /* one entry per SSID */
        for (uint16_t j = 0U; j < count; j++) {
            if (strcmp(out[j].ssid, ssid) == 0) { dup = true; break; }
        }
        if (dup) { continue; }

        (void)strncpy(out[count].ssid, ssid, sizeof(out[count].ssid) - 1U);
        out[count].ssid[sizeof(out[count].ssid) - 1U] = '\0';
        out[count].rssi = recs[i].rssi;
        out[count].open = (recs[i].authmode == WIFI_AUTH_OPEN);
        count++;
    }

    free(recs);
    return count;
}

bool net_wifi_connect(const char *ssid, const char *password)
{
    wdt_feed();   /* up to WIFI_TIMEOUT_MS below */
    net_wifi_init();

    /* The station is off while the SoftAP is up (net_ap_start), and joining
     * obviously needs it. APSTA rather than AP-only for the duration: the phone
     * that submitted the form is on the AP and has to be told what happened, and
     * a mid-join mode flip would drop it before it could be. The caller either
     * keeps the join and stops the portal, or drops the association again —
     * see UI_EVENT_WIFI_TRY in main.cpp. */
    if (s_ap_up) { (void)esp_wifi_set_mode(WIFI_MODE_APSTA); }

    /* Not kept while it is being tried: a join that fails must not turn into a
     * background loop on a network nobody has proven. */
    s_keep = false;
    rejoin_cancel();
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_retry_num = 0;

    wifi_config_t wifi_cfg;
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(&wifi_cfg), sizeof(wifi_cfg));
    (void)strncpy(reinterpret_cast<char *>(wifi_cfg.sta.ssid),     ssid,     sizeof(wifi_cfg.sta.ssid)     - 1U);
    (void)strncpy(reinterpret_cast<char *>(wifi_cfg.sta.password), password, sizeof(wifi_cfg.sta.password) - 1U);
    /* Open networks have an empty passphrase; otherwise require WPA2+. */
    wifi_cfg.sta.threshold.authmode =
        (password[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    /* The WiFi driver keeps its own copy — scrub the stack copy of the
     * credentials immediately (CODING_RULES §1.4). */
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(&wifi_cfg),
                          sizeof(wifi_cfg));

    (void)esp_wifi_disconnect();   /* drop any current AP before (re)connecting */
    (void)esp_wifi_connect();

    ESP_LOGI(TAG, "Connecting to \"%s\"...", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_TIMEOUT_MS));

    bool connected = ((bits & WIFI_CONNECTED_BIT) != 0U);
    if (connected) {
        s_keep = true;   /* drops from here on re-join in the background */
        ESP_LOGI(TAG, "WiFi connected");
    } else {
        ESP_LOGE(TAG, "WiFi connect failed");
    }
    return connected;
}

void net_wifi_keep_trying(void)
{
    if (!s_wifi_inited || s_ap_up) { return; }
    s_keep = true;
    rejoin_schedule();
}

bool net_wifi_online(void)
{
    return (s_wifi_event_group != NULL) &&
           ((xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0U);
}

bool net_wifi_reconnecting(void)
{
    return s_keep && !net_wifi_online();
}

bool net_wifi_rssi(int8_t *rssi_out)
{
    if (rssi_out == NULL) { return false; }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return false;   /* not associated */
    }
    *rssi_out = ap.rssi;
    return true;
}

/* __DATE__/__TIME__ are the build machine's LOCAL time but are read back here
 * as if UTC, so the derived floor can sit up to ~14 h ahead of the real build
 * instant. One day of slack absorbs every real-world TZ offset; it costs the
 * attacker nothing, since reviving an expired certificate needs weeks of
 * back-dating, not hours.
 * ponytail: fixed slack, not a TZ database. */
#define BUILD_FLOOR_SLACK_S  86400

/**
 * @brief Earliest system time this firmware will accept, as a Unix epoch.
 *
 * Derived from the firmware's own build timestamp: a clock at or after the
 * build instant cannot make an already-expired certificate look valid, and
 * pushing the clock *forward* is harmless for the same reason.
 *
 * @return Epoch-second floor, or 0 (no floor) if the build stamp is
 *         unparseable — fail-open, so a toolchain that deviates from the
 *         standard __DATE__ format cannot brick the terminal. The host
 *         self-check (fuzz/test_civil_time.cpp) asserts our own stamp parses.
 */
static int64_t build_time_floor(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    int64_t stamp = 0;

    if ((desc == NULL) ||
        !civil_parse_build_stamp(desc->date, desc->time, &stamp)) {
        ESP_LOGE(TAG, "build stamp unparseable - clock lower bound DISABLED");
        return 0;
    }
    return stamp - BUILD_FLOOR_SLACK_S;
}

/**
 * @brief Every SNTP update, foreground or background, lands here first.
 *
 * The same floor @ref net_time_sync applies after its wait, applied to the
 * background resyncs too: once SNTP is left subscribed, lwIP sets the clock on
 * its own, and a back-dated packet arriving then would otherwise be adopted
 * with nobody checking. Refused by putting the clock back to "unset" — every
 * TLS request then fails closed, and the sale path says it has no time.
 */
static void on_time_sync(struct timeval *tv)
{
    const int64_t floor_epoch = build_time_floor();
    if ((tv != NULL) && (static_cast<int64_t>(tv->tv_sec) < floor_epoch)) {
        ESP_LOGE(TAG, "SNTP time %" PRId64 " precedes firmware build floor - "
                      "clock cleared (spoofed NTP?)",
                 static_cast<int64_t>(tv->tv_sec));
        const struct timeval zero = { 0, 0 };
        (void)settimeofday(&zero, NULL);
    }
}

/** @brief Subscribe the SNTP client, without waiting for it. */
static bool sntp_start(void)
{
    /* Several reliable servers — phone hotspots often slow or drop NTP (UDP
     * 123) to pool.ntp.org, so fall back to Google / Cloudflare time. */
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
        ESP_SNTP_SERVER_LIST("time.google.com", "pool.ntp.org", "time.cloudflare.com"));
    config.sync_cb = on_time_sync;
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP init: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void net_time_background(void)
{
    if (s_sntp_running) { return; }   /* already subscribed: lwIP is retrying */
    s_sntp_running = sntp_start();
    if (s_sntp_running) { ESP_LOGI(TAG, "SNTP running in the background"); }
}

bool net_time_sync(uint32_t timeout_ms)
{
    /* A successful sync leaves SNTP subscribed, so a second call would fail on
     * ESP_ERR_INVALID_STATE. Drop it first: every call then waits for a fresh
     * packet, which is what makes this usable as a per-network probe rather than
     * a one-shot at boot. */
    wdt_feed();   /* up to timeout_ms below */
    if (s_sntp_running) {
        esp_netif_sntp_deinit();
        s_sntp_running = false;
    }

    if (!sntp_start()) { return false; }

    /* Poll across the budget so a slow first response doesn't fail us. */
    const int   attempts = 3;
    uint32_t    per_wait = timeout_ms / static_cast<uint32_t>(attempts);
    if (per_wait < 2000U) { per_wait = 2000U; }
    bool synced = false;
    for (int i = 0; (i < attempts) && !synced; i++) {
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(per_wait)) == ESP_OK) {
            synced = true;
        } else {
            ESP_LOGW(TAG, "SNTP: no time yet (%d/%d)", i + 1, attempts);
        }
    }

    /* Stay subscribed on failure too: lwIP then retries on its own (15 s, backing
     * off to 150 s) and sets the clock if the network comes back. A deinit here
     * would leave no client running at all. */
    s_sntp_running = true;

    if (!synced) {
        ESP_LOGE(TAG, "SNTP sync timed out");
        return false;
    }

    /* SNTP is unauthenticated (plain UDP/123), so anyone on the path can
     * dictate the time. Reject a clock earlier than our own build: that is
     * the direction an attacker needs to resurrect an expired certificate. */
    const int64_t floor_epoch = build_time_floor();
    const int64_t now_epoch   = static_cast<int64_t>(time(NULL));
    if (now_epoch < floor_epoch) {
        ESP_LOGE(TAG, "SNTP time %" PRId64 " precedes firmware build floor %"
                      PRId64 " - refusing (spoofed NTP?)",
                 now_epoch, floor_epoch);
        /* Unsubscribe here, unlike the timeout path above: a server feeding
         * back-dated time is not one to leave resyncing us in the background.
         * Keep the flag honest so the next call re-inits rather than double-
         * deiniting. */
        esp_netif_sntp_deinit();
        s_sntp_running = false;
        return false;
    }

    ESP_LOGI(TAG, "System time synced via SNTP (epoch %" PRId64 ")", now_epoch);
    /* Leave SNTP running for periodic background resyncs. */
    return true;
}
