/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file provision.cpp
 * @brief The config portal: a SoftAP and a captive portal, for setup and for
 *        administration alike, one page for both. See provision.h for the why.
 */

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "provision.h"

#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* Before lwip/sockets.h, and it has to stay there. CW_Utils.h drags in
 * Arduino's IPAddress.h, which declares `extern const IPAddress INADDR_NONE;`.
 * Once lwIP's headers have been seen, INADDR_NONE is a macro expanding to a
 * u32_t cast, and that declaration stops parsing. Arduino first, lwIP second. */
#include "CW_Utils.h"

#include "lwip/sockets.h"

#include "esp_heap_caps.h"   /* largest free block — fragmentation, not just total */
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"   /* esp_restart — the network switch reboots to apply */
#include "esp_timer.h"
#include "nvs.h"
#include "mbedtls/sha256.h"   /* Tron base58check */

#include "addr_check.h"
#include "eth_addr.h"
#include "form_parse.h"
#include "json_out.h"
#include "net.h"
#include "ota.h"
#include "portal_page.h"   /* PAGE_HTML + PAGE_JS — the document page_get serves */
#include "settings.h"
#include "settings_rules.h"   /* fee bounds, time-zone range — host-tested */
#include "civil_time.h"   /* CIVIL_DST__COUNT — the clock's DST rules */

static const char *const TAG = "prov";

/******************************************************************
 * 2. Constants
 ******************************************************************/

/* PORTAL_IP / PORTAL_URL are in portal_page.h: the page prints the URL for an
 * operator to type and the handlers below redirect to it, so the two have to
 * agree and there is one definition of it. */

#define AP_PASS_LEN   10U    /* ~50 bits out of the 32-char alphabet below */

/* No 0/O/1/I/l: this is read off a 2.8" panel and typed by hand when the QR
 * code will not scan, and those four are where that goes wrong. */
static const char AP_PASS_ALPHABET[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

/* Own namespace, so nothing this module keeps is swept up by anything that
 * erases settings for other reasons. settings_factory_reset() erases this one BY
 * NAME — grep "prov" in settings.cpp before renaming it. */
#define NS_PROV       "prov"

/* The AP passphrase, drawn once and kept — see ap_pass_load(). */
#define K_AP_PASS     "ap_pass"

/* Keys nothing writes any more; erased on sight. See ap_pass_load(). */
#define K_TLS_CRT     "tls_crt"
#define K_TLS_KEY     "tls_key"

/* Gas-cap bounds, in Gwei: FEE_GWEI_MIN / _MAX in settings_rules.h. The numbers
 * the panel's +/- steppers used to enforce before the fees moved to this page —
 * kept identical so a value stored here is one the terminal has always been able
 * to hold. The page's own min/max attributes say the same thing to the browser;
 * the two constants are what actually decide. */

/* Read this much of an upload at a time. 4 KB is a flash page-erase unit and one
 * lwIP window's worth, and it lives in .bss rather than on the httpd task's
 * stack — where it would not fit. ota.h refuses a second concurrent upload, so
 * one shared buffer is enough. */
#define UPLOAD_CHUNK  4096U

/* Consecutive recv timeouts before an upload is declared dead. cfg.recv_wait_timeout
 * is 30 s, so this is two minutes of a socket saying nothing at all — far longer
 * than any pause a slow phone puts between chunks, and it is what stops a stalled
 * transfer from holding the admin page open past its window (see the deadline
 * check in ui.cpp, which does not close the page while bytes are arriving). */
#define UPLOAD_MAX_STALLS  4U

#define TOKEN_HEX_LEN  32U   /* 128 bits of session token */

#define PROV_MAX_APS   16U

/* No release-list URL here any more, and no "check for updates" button on the
 * page. The portal is served on the terminal's own SoftAP with the station
 * interface down (see prov_start), so the phone reading this page has no route
 * to a release list — a check button could only ever report a network error.
 * The file picker is the whole update story: fetch the signed image on a device
 * that does have internet, then hand it to the terminal here. */

/******************************************************************
 * 3. Module state
 ******************************************************************/

static httpd_handle_t    s_httpd    = NULL;
static TaskHandle_t      s_dns_task = NULL;
static volatile bool     s_dns_run  = false;
static ui_event_cb_t     s_cb       = NULL;

/* Atomic because prov_stop() has two callers on two tasks — the UI task (the
 * portal card's Done button, a declined firmware image, the window deadline) and
 * the main task (the wizard finishing, a value committed from the admin page). Two
 * of those landing in the same tick would otherwise both get past the "already
 * off?" test and call httpd_stop() twice on one handle. prov_stop() claims the mode
 * with an exchange, so exactly one caller does the teardown. */
static std::atomic<prov_mode_t> s_mode{PROV_MODE_OFF};
static volatile prov_step_t s_step  = PROV_STEP_IDLE;
static int64_t              s_deadline_us = 0;   /* 0 = no self-close */

static char s_ssid[33]  = "";
static char s_pass[AP_PASS_LEN + 1U] = "";
static char s_qr[96]    = "";

static uint8_t s_upload[UPLOAD_CHUNK];

/* Browser session. The token is minted when a browser asks to be authorised and
 * only becomes usable once somebody types the admin code on the panel, so a
 * second browser on the same network gets a token that authorises nothing. */
static char           s_token[TOKEN_HEX_LEN + 1U] = "";
static volatile bool  s_auth_pending = false;
static volatile bool  s_authed       = false;

/* Wi-Fi-only re-join: no admin code, no numbered steps. See prov_set_wifi_only(). */
static volatile bool  s_wifi_only    = false;

/* A line for the page from the one party that knows why something did not work.
 * Written by the main task, read by the HTTP task. */
static char           s_note[128] = "";

/* Guards s_note and the scan list below: both are written by the main task and
 * read by the HTTP task, and a torn scan record is an SSID glued to the wrong
 * signal. A spinlock, not a mutex — the critical sections are plain copies of
 * a few hundred bytes, and every formatting step happens on a local copy after
 * the lock is let go. */
static portMUX_TYPE   s_share_mux = portMUX_INITIALIZER_UNLOCKED;

/* The Wi-Fi list handed over by the main task, and a generation counter so the
 * page knows to refetch it after a rescan without diffing the list itself. */
static net_wifi_ap_t  s_aps[PROV_MAX_APS];
static uint16_t       s_ap_count = 0U;
/* std::atomic rather than volatile: it is incremented, and ++ on a volatile is
 * deprecated in C++20 (and was never the atomic operation it looks like). */
static std::atomic<uint32_t> s_scan_gen{0U};

/* A value a browser has proposed. Written by the HTTP task, read and cleared by
 * the UI task once the operator has accepted or rejected it on the panel — two
 * tasks and a value that decides where money goes, so it takes a lock rather
 * than a hopeful volatile. */
static SemaphoreHandle_t s_ask_lock = NULL;
static prov_ask_t        s_ask      = PROV_ASK_NONE;
static char              s_ask_val[SETTINGS_PAYOUT_MAX] = "";

/******************************************************************
 * 4. AP identity
 ******************************************************************/

/** @brief Fill s_pass with AP_PASS_LEN characters of hardware entropy. */
static void ap_pass_draw(void)
{
    /* esp_random() is the hardware RNG, and it is only a *true* one while the RF
     * subsystem runs — prov_start() calls net_wifi_init() before coming here for
     * exactly that reason. Which is also why bootloader_random_enable() (the SAR
     * ADC source, for entropy with the radio off) is NOT used here: it must not be
     * called with Wi-Fi started, and by this point it is. */
    for (size_t i = 0; i < AP_PASS_LEN; i++) {
        s_pass[i] = AP_PASS_ALPHABET[esp_random() % (sizeof(AP_PASS_ALPHABET) - 1U)];
    }
    s_pass[AP_PASS_LEN] = '\0';
}

/**
 * @brief The AP passphrase: drawn once, then kept until a factory reset.
 *
 * It used to be redrawn per session, so a photograph of the panel expired with
 * the session that showed it. That cost more than it bought: the operator retypes
 * ten characters every single time they open the page, and the passphrase they
 * had written down is wrong every time.
 *
 * What the redraw actually defended is the wifi_only portal, which asks for no
 * admin code — so somebody who once photographed the passphrase can join the
 * setup AP again and change which network the terminal joins. That still needs
 * the terminal to have opened that portal by itself (it only does so when the
 * venue network fails) and needs them standing inside its ~10 m of SoftAP, in
 * front of the panel that is displaying the current passphrase to anyone looking
 * anyway. Everything that moves money is behind the admin code, which is typed on
 * the panel and never on the page.
 *
 * Stored in NS_PROV, which settings_factory_reset() erases by name — so "reset
 * the terminal" is the way to retire a passphrase that has got out.
 */
static void ap_pass_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS_PROV, NVS_READWRITE, &h) != ESP_OK) {
        /* No NVS: a session-only passphrase is still a working portal, and the
         * panel shows whatever this drew. */
        ap_pass_draw();
        return;
    }

    size_t n = sizeof(s_pass);
    if ((nvs_get_str(h, K_AP_PASS, s_pass, &n) != ESP_OK) ||
        (strlen(s_pass) != AP_PASS_LEN)) {
        /* Absent, or a length this build does not issue (an older AP_PASS_LEN, or
         * a truncated read): draw a new one and keep it. */
        ap_pass_draw();
        if (nvs_set_str(h, K_AP_PASS, s_pass) == ESP_OK) {
            (void)nvs_commit(h);
        }
    }

    /* The admin page's self-signed TLS identity, on units provisioned by an
     * earlier build: ~1 KB of a 24 KB NVS that nothing reads any more, because the
     * page is on this AP now and not on the venue LAN. NVS deletes logically — the
     * entry is tombstoned and its bytes leave the page on the next compaction — so
     * this closes the NVS read, not a raw flash dump. */
    static const char *const DEAD[] = { K_TLS_CRT, K_TLS_KEY };
    bool erased = false;
    for (size_t i = 0; i < (sizeof(DEAD) / sizeof(DEAD[0])); i++) {
        if (nvs_erase_key(h, DEAD[i]) == ESP_OK) { erased = true; }
    }
    if (erased) { (void)nvs_commit(h); }
    nvs_close(h);
}

/** @brief SSID from the SoftAP MAC, so two terminals in a room are tellable apart. */
static void ap_ssid_build(void)
{
    uint8_t mac[6] = { 0 };
    (void)esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    (void)snprintf(s_ssid, sizeof(s_ssid), "Cryptnox-%02X%02X", mac[4], mac[5]);
}

/******************************************************************
 * 5. DNS hijack
 ******************************************************************/

/**
 * @brief Answer every A query with the portal address.
 *
 * Not a DNS server — it does not parse the question beyond finding where it
 * ends, and it answers identically regardless of what was asked. That is the
 * whole job: the phone's connectivity probe has to resolve to us before the
 * HTTP half can fail it on purpose.
 */
static void dns_task(void *arg)
{
    (void)arg;

    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in me;
    memset(&me, 0, sizeof(me));
    me.sin_family      = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_ANY);
    me.sin_port        = htons(53);
    if (bind(sock, reinterpret_cast<struct sockaddr *>(&me), sizeof(me)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* One second, so prov_stop() is noticed promptly instead of on the next
     * query — which on an idle AP may never come. */
    struct timeval tv = { 1, 0 };
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[192];
    while (s_dns_run) {
        struct sockaddr_in from;
        socklen_t          from_len = sizeof(from);
        const int n = recvfrom(sock, buf, sizeof(buf), 0,
                               reinterpret_cast<struct sockaddr *>(&from), &from_len);
        /* 12-byte header + at least a root label and QTYPE/QCLASS. */
        if (n < 17) { continue; }

        /* Walk the QNAME label chain to find where the question ends. Bail on a
         * compression pointer: a query has no business containing one, and
         * following it here is how you write a loop that never returns. */
        size_t p = 12U;
        while ((p < static_cast<size_t>(n)) && (buf[p] != 0U)) {
            if ((buf[p] & 0xC0U) != 0U) { p = 0U; break; }
            p += static_cast<size_t>(buf[p]) + 1U;
        }
        if ((p == 0U) || ((p + 5U) > static_cast<size_t>(n))) { continue; }
        const size_t q_end = p + 5U;   /* NUL + QTYPE(2) + QCLASS(2) */

        if ((q_end + 16U) > sizeof(buf)) { continue; }

        buf[2] = 0x81U;   /* QR=1, RD copied on: a response, recursion available */
        buf[3] = 0x80U;
        buf[6] = 0x00U; buf[7] = 0x01U;   /* ANCOUNT = 1 */
        buf[8] = 0x00U; buf[9] = 0x00U;   /* NSCOUNT = 0 */
        buf[10] = 0x00U; buf[11] = 0x00U; /* ARCOUNT = 0 */

        size_t a = q_end;
        buf[a++] = 0xC0U; buf[a++] = 0x0CU;          /* NAME -> offset 12       */
        buf[a++] = 0x00U; buf[a++] = 0x01U;          /* TYPE  A                 */
        buf[a++] = 0x00U; buf[a++] = 0x01U;          /* CLASS IN                */
        buf[a++] = 0x00U; buf[a++] = 0x00U;
        buf[a++] = 0x00U; buf[a++] = 0x00U;          /* TTL 0 — do not cache us */
        buf[a++] = 0x00U; buf[a++] = 0x04U;          /* RDLENGTH 4              */
        buf[a++] = 192U; buf[a++] = 168U; buf[a++] = 4U; buf[a++] = 1U;

        (void)sendto(sock, buf, a, 0,
                     reinterpret_cast<struct sockaddr *>(&from), from_len);
    }

    close(sock);
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

/******************************************************************
 * 6. Request helpers
 ******************************************************************/

/* form_field() lives in form_parse.h and addr_is_base58()/addr_tron_plausible()
 * in addr_check.h, neither of which has any ESP-IDF dependency, so the host tests
 * in tests/units can include them. Between them they are all the code here that
 * interprets input a stranger on the network controls. */

/** @brief Read a request body into @p out. @return false if it did not fit. */
static bool read_body(httpd_req_t *req, char *out, size_t n)
{
    if (req->content_len >= n) { return false; }
    size_t got = 0U;
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, out + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) { continue; }
        if (r <= 0) { return false; }
        got += static_cast<size_t>(r);
    }
    out[got] = '\0';
    return true;
}

/** @brief True once the portal's own window has closed. */
static bool expired(void)
{
    return (s_deadline_us != 0) && (esp_timer_get_time() >= s_deadline_us);
}

/**
 * @brief Whether the request carries the token of the authorised session.
 *
 * There is no admin code to check here — the code was typed on the panel, which
 * is what turned @ref s_authed on. So this only asks "are you the browser that
 * was let in", and a wrong token is not a guessing attempt worth rate-limiting:
 * it is 128 random bits, and guessing it does not get anybody past the on-screen
 * confirmation that guards every value that matters anyway.
 */
static bool has_token(httpd_req_t *req)
{
    if (s_token[0] == '\0') { return false; }

    char tok[TOKEN_HEX_LEN + 1U] = { 0 };
    if (httpd_req_get_hdr_value_str(req, "X-Prov-Token", tok,
                                    sizeof(tok)) != ESP_OK) {
        return false;
    }
    return CW_Utils::secure_compare(reinterpret_cast<const uint8_t *>(tok),
                                    reinterpret_cast<const uint8_t *>(s_token),
                                    sizeof(tok));
}

/** @brief Whether the request is the browser that was let in. */
static bool authed(httpd_req_t *req)
{
    return s_authed && has_token(req);
}

/** @brief Send a plain-text status line; the page shows it verbatim. */
static esp_err_t reply(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, msg);
}

/** @brief 200 with a plain-text message. */
static esp_err_t ok(httpd_req_t *req, const char *msg)
{
    return reply(req, "200 OK", msg);
}

/** @brief Whether the request's path is @p path — req->uri carries any query
 *         string, which routing ignores and this must too. */
static bool uri_is(const httpd_req_t *req, const char *path)
{
    const size_t n = strcspn(req->uri, "?");
    return (strlen(path) == n) && (strncmp(req->uri, path, n) == 0);
}

/**
 * @brief The gate every mutating endpoint runs first.
 *
 * @param[out] rc Set to the response already sent when this returns false.
 * @return true if the request may proceed.
 */
static bool gate(httpd_req_t *req, esp_err_t *rc)
{
    if (expired()) {
        *rc = reply(req, "503 Service Unavailable",
                    "This page has closed. Reopen it on the terminal.");
        return false;
    }
    /* The Wi-Fi-only portal lets a browser in without the admin code, because
     * the terminal has lost its network and only needs a password. It opens by
     * itself when the venue network drops — which anyone can cause — so it must
     * not also be a way to change fees, the network, the clock or the payout
     * without the code. Those stay behind the full admin session. */
    if (s_wifi_only && !uri_is(req, "/api/wifi") && !uri_is(req, "/api/rescan") &&
        !uri_is(req, "/api/scan")) {
        *rc = reply(req, "403 Forbidden",
                    "Only the Wi-Fi network can be set from here while the "
                    "terminal is offline. Use Configure on the terminal for "
                    "anything else.");
        return false;
    }
    if (!authed(req)) {
        *rc = reply(req, "401 Unauthorized",
                    "This browser is not authorized. Enter the admin code on "
                    "the terminal screen.");
        return false;
    }
    return true;
}

/******************************************************************
 * 7. The page
 ******************************************************************/

/* The document itself is portal_page.h — PAGE_HTML and PAGE_JS. It lives next
 * door because it is a document, not a server: ~700 lines of markup, CSS and
 * script that only this one handler ever reads. */

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    /* No-store, or a phone's portal browser serves a stale step back from cache
     * after the terminal has moved on. */
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    (void)httpd_resp_sendstr_chunk(req, PAGE_HTML);
    (void)httpd_resp_sendstr_chunk(req, PAGE_JS);
    return httpd_resp_sendstr_chunk(req, NULL);   /* end of chunked response */
}

/******************************************************************
 * 8. API — state
 ******************************************************************/

/** @brief Label the panel and the page both use for a pending proposal. */
static const char *ask_label(prov_ask_t k)
{
    switch (k) {
        /* Polygon is EVM and spends this same address, and the panel is where an
         * operator decides whether to accept it — so it says so there too, not
         * only in the browser. */
        case PROV_ASK_PAYOUT_ETH:    return "Ethereum / Polygon payout address";
        case PROV_ASK_PAYOUT_TRON:   return "Tron payout address";
        case PROV_ASK_CONTRACT_ETH:  return "ERC-20 token contract";
        case PROV_ASK_CONTRACT_TRON: return "TRC-20 token contract";
        default:                     return "";
    }
}

static const char *step_name(prov_step_t s)
{
    switch (s) {
        case PROV_STEP_AUTH:  return "auth";
        case PROV_STEP_ADDR:  return "addr";
        case PROV_STEP_WIFI:  return "wifi";
        case PROV_STEP_DONE:  return "done";
        case PROV_STEP_ADMIN: return "admin";
        default:              return "idle";
    }
}

/* json_escape() lives in json_out.h, next to form_parse.h and addr_check.h and for
 * the same reason: an SSID is 32 arbitrary bytes chosen by whoever named the router,
 * and a quote in one turns this response into something the page cannot parse. Host
 * test: tests/units/test_json_out.cpp. */

/* Defined with the proposal handlers in §10, where it belongs — the report below
 * borrows it so the page cannot describe as usable a contract it would itself
 * refuse if somebody pasted it in. */
static bool addr_plausible(bool tron, const char *addr);

static esp_err_t state_get(httpd_req_t *req)
{
    prov_ask_t ask = PROV_ASK_NONE;
    (void)prov_pending(&ask, NULL, 0U, NULL, 0U);

    /* Two bodies, not one with empty fields: an unauthorised browser has no
     * business learning the payout addresses or the venue's network name. The
     * firmware version it can see, because the update page is useless without it
     * and it is stamped on the About screen anyway. */
    char body[928];
    if (!authed(req)) {
        (void)snprintf(body, sizeof(body),
                       "{\"mode\":\"%s\",\"step\":\"%s\",\"authed\":false,"
                       "\"auth_pending\":%s,\"version\":\"%s\"}",
                       (s_mode == PROV_MODE_WIZARD) ? "wizard" : "admin",
                       step_name(s_step),
                       s_auth_pending ? "true" : "false",
                       ota_running_version());
    } else {
        char pay_eth[SETTINGS_PAYOUT_MAX] = "";
        char pay_trx[SETTINGS_PAYOUT_MAX] = "";
        char ct_eth[SETTINGS_PAYOUT_MAX]  = "";
        char ct_trx[SETTINGS_PAYOUT_MAX]  = "";
        /* Payout: the stored value only. A browser asking "who gets paid" must not
         * be shown the compile-time fallback as though somebody had chosen it —
         * that is exactly the confusion that leaves a terminal quietly paying an
         * address its operator never saw, and an unset one refuses every sale.
         * Empty means empty, and the page says "not set" because it is. */
        if (!settings_get_payout(false, pay_eth, sizeof(pay_eth))) { pay_eth[0] = '\0'; }
        if (!settings_get_payout(true,  pay_trx, sizeof(pay_trx))) { pay_trx[0] = '\0'; }

        /* Contracts: the value actually in use, plus whether an operator chose it.
         *
         * The same "stored only" rule was applied here and it was the wrong rule.
         * A terminal with no stored contract is not uncontracted — it charges
         * against the one built into the firmware, which is a real address doing a
         * real job and is on the panel's own Tx tab. Reporting that as "not set"
         * beside a working USDC selection reads as a fault, and the obvious repair
         * is to paste something over a contract that was already correct.
         *
         * The reason the payout rule exists does not carry across: a fallback
         * recipient is somebody else's address and the terminal refuses to spend to
         * it, while a fallback contract is the asset the operator picked. So the
         * page shows it and labels where it came from — which is the distinction
         * the original comment was protecting, said out loud instead of by
         * omission. Nothing is disclosed: the contract is in the signed image, on
         * the panel, and public on-chain, and this body only reaches a browser that
         * has already had the admin code typed on the terminal. */
        const bool ct_eth_own = settings_get_contract(POS_CHAIN_ETH_USDC, ct_eth, sizeof(ct_eth));
        const bool ct_trx_own = settings_get_contract(POS_CHAIN_TRON_USDT, ct_trx, sizeof(ct_trx));
        /* An asset the build never configured falls back to the placeholder still
         * sitting in config.h, which is a string and not an address. main.cpp
         * refuses that asset over it; showing it here as the contract in use would
         * be the page contradicting the terminal. Same plausibility test the
         * proposals are held to, so the page cannot report as usable an address it
         * would itself have rejected. */
        if (!addr_plausible(false, ct_eth)) { ct_eth[0] = '\0'; }
        if (!addr_plausible(true,  ct_trx)) { ct_trx[0] = '\0'; }

        char ssid[33] = "";
        char pass[65] = "";
        (void)settings_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));
        CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(pass), sizeof(pass));
        char ssid_json[132] = "";
        (void)json_escape(ssid_json, sizeof(ssid_json), ssid);

        char note[sizeof(s_note)];
        taskENTER_CRITICAL(&s_share_mux);
        (void)CW_Utils::safe_memcpy(reinterpret_cast<uint8_t *>(note), sizeof(note),
                                    reinterpret_cast<const uint8_t *>(s_note),
                                    sizeof(note));
        taskEXIT_CRITICAL(&s_share_mux);
        note[sizeof(note) - 1U] = '\0';

        (void)snprintf(body, sizeof(body),
                       "{\"mode\":\"%s\",\"step\":\"%s\",\"authed\":true,"
                       "\"auth_pending\":false,\"version\":\"%s\","
                       "\"pay_eth\":\"%s\",\"pay_trx\":\"%s\","
                       "\"ct_eth\":\"%s\",\"ct_trx\":\"%s\","
                       "\"ct_eth_own\":%s,\"ct_trx_own\":%s,"
                       "\"ssid\":\"%s\",\"pending\":\"%s\",\"note\":\"%s\","
                       "\"mainnet\":%s,\"fee_max\":%u,\"fee_prio\":%u,"
                       "\"tz_off\":%d,\"tz_dst\":%u,\"scan_gen\":%u,\"win\":%u}",
                       (s_mode == PROV_MODE_WIZARD) ? "wizard" : "admin",
                       step_name(s_step), ota_running_version(),
                       pay_eth, pay_trx, ct_eth, ct_trx,
                       ct_eth_own ? "true" : "false",
                       ct_trx_own ? "true" : "false",
                       ssid_json, ask_label(ask), note,
                       settings_get_mainnet() ? "true" : "false",
                       static_cast<unsigned>(settings_get_max_fee_gwei()),
                       static_cast<unsigned>(settings_get_priority_fee_gwei()),
                       static_cast<int>(settings_get_tz_offset_min()),
                       static_cast<unsigned>(settings_get_tz_dst()),
                       static_cast<unsigned>(s_scan_gen.load()),
                       prov_window_left_min());
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t scan_get(httpd_req_t *req)
{
    esp_err_t rc;
    if (!gate(req, &rc)) { return rc; }

    /* Chunked: sixteen 32-character SSIDs plus their JSON overhead does not fit a
     * stack buffer worth having on the httpd task. */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    (void)httpd_resp_sendstr_chunk(req, "{\"aps\":[");
    for (uint16_t i = 0U; ; i++) {
        net_wifi_ap_t ap;
        bool          have = false;
        taskENTER_CRITICAL(&s_share_mux);
        if (i < s_ap_count) { ap = s_aps[i]; have = true; }
        taskEXIT_CRITICAL(&s_share_mux);
        if (!have) { break; }
        ap.ssid[sizeof(ap.ssid) - 1U] = '\0';
        char name[132] = "";
        (void)json_escape(name, sizeof(name), ap.ssid);
        char one[200];
        (void)snprintf(one, sizeof(one),
                       "%s{\"ssid\":\"%s\",\"rssi\":%d,\"open\":%s}",
                       (i == 0U) ? "" : ",", name,
                       static_cast<int>(ap.rssi),
                       ap.open ? "true" : "false");
        (void)httpd_resp_sendstr_chunk(req, one);
    }
    (void)httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

/******************************************************************
 * 9. API — authorisation
 ******************************************************************/

static esp_err_t auth_post(httpd_req_t *req)
{
    if (expired()) {
        return reply(req, "503 Service Unavailable",
                     "This page has closed. Reopen it on the terminal.");
    }
    /* The token belongs to one browser. A reload keeps it (the page holds it in
     * sessionStorage and sends it back), so "somebody pressed F5" still gets
     * straight back in — but a request WITHOUT it is somebody else on this
     * access point, and handing them the live token would let any phone that
     * joined the Wi-Fi ride on the operator's session. */
    if (s_authed || s_auth_pending) {
        if (has_token(req)) { return ok(req, s_token); }
        return reply(req, "409 Conflict",
                     s_authed ? "Another browser is already signed in. Close "
                                "the page on the terminal to start over."
                              : "Another browser is waiting for the admin code. "
                                "Cancel it on the terminal, then try again.");
    }

    /* A fresh token per session. The first four digits it maps to are shown on
     * the panel beside the code prompt and on this browser's page, so the
     * operator can see WHICH browser they are about to let in. */
    for (size_t i = 0; i < TOKEN_HEX_LEN; i++) {
        s_token[i] = "0123456789abcdef"[esp_random() & 0x0FU];
    }
    s_token[TOKEN_HEX_LEN] = '\0';

    if (s_wifi_only && !settings_has_admin_code()) {
        /* Nothing to authorise: the only reason this portal is up is that the
         * terminal has lost its network, and whoever is asking read this AP's
         * per-device passphrase off the panel in front of them. Only while no
         * admin code exists, though — once one does, it is the promise that
         * Wi-Fi sits behind it, and the AP passphrase is readable by anyone at
         * the panel, so the code is demanded as on the full wizard. */
        s_authed = true;
        ESP_LOGW(TAG, "browser let in without a code (Wi-Fi-only re-join)");
        if (s_cb != NULL) { s_cb(UI_EVENT_PROV_NEXT, 0); }
        return ok(req, s_token);
    }

    s_auth_pending = true;
    ESP_LOGI(TAG, "browser asked to be authorised - admin code needed on panel");
    if (s_cb != NULL) { s_cb(UI_EVENT_PROV_AUTH, 0); }

    return ok(req, s_token);
}

/******************************************************************
 * 10. API — the values the panel has to confirm
 ******************************************************************/

/**
 * @brief Reject an address that is obviously not one, before it is proposed.
 *
 * Ethereum gets the real check: eth_addr_parse() verifies the EIP-55 checksum, so
 * a single mistyped character in a mixed-case address is caught here.
 *
 * Tron gets the real check too: base58-decoded (addr_check.h) and its 4-byte
 * checksum verified as double SHA-256 of the first 21 bytes. A mistyped address
 * that only looked right used to be stored, fail at boot, and — before that was
 * fixed — pay the config.h recipient instead.
 */
static bool addr_plausible(bool tron, const char *addr)
{
    if (tron) {
        unsigned char b[25];
        uint8_t       h[32];
        if (!addr_tron_decode(addr, b)) { return false; }
        if ((mbedtls_sha256(b, 21U, h, 0) != 0) ||
            (mbedtls_sha256(h, sizeof(h), h, 0) != 0)) {
            return false;
        }
        return memcmp(h, &b[21], 4U) == 0;
    }
    uint8_t parsed[ETH_ADDR_LEN];
    return eth_addr_parse(addr, parsed);
}

/** @brief Shared body of /api/payout and /api/contract. */
static esp_err_t value_post(httpd_req_t *req, bool contract)
{
    esp_err_t rc;
    if (!gate(req, &rc)) { return rc; }

    char body[160] = { 0 };
    if (!read_body(req, body, sizeof(body))) {
        return reply(req, "400 Bad Request", "Bad request.");
    }

    char addr[SETTINGS_PAYOUT_MAX] = { 0 };
    char net[8] = { 0 };
    (void)form_field(body, "addr", addr, sizeof(addr));
    (void)form_field(body, "net", net, sizeof(net));

    const bool tron = (strcmp(net, "tron") == 0);
    if (!addr_plausible(tron, addr)) {
        return reply(req, "400 Bad Request", tron
            ? "That is not a Tron address (34 characters, starts with T)."
            : "That is not a valid Ethereum address. A mixed-case address must "
              "carry a correct EIP-55 checksum.");
    }

    const prov_ask_t kind = contract
        ? (tron ? PROV_ASK_CONTRACT_TRON : PROV_ASK_CONTRACT_ETH)
        : (tron ? PROV_ASK_PAYOUT_TRON   : PROV_ASK_PAYOUT_ETH);

    if (!prov_propose(kind, addr)) {
        return reply(req, "409 Conflict",
                     "Something is already waiting to be accepted on the "
                     "terminal screen. Deal with that one first.");
    }
    return ok(req, "Now check that value on the terminal screen and accept it "
                   "there. It is not stored until you do.");
}

static esp_err_t payout_post(httpd_req_t *req)   { return value_post(req, false); }
static esp_err_t contract_post(httpd_req_t *req) { return value_post(req, true);  }

/**
 * @brief Store the EIP-1559 gas caps (Gwei).
 *
 * The one setting this page writes straight through instead of proposing it on the
 * panel. An address decides *who* gets the money and so has to be read back by a
 * human; a fee cap only decides how much gas the terminal will pay for its own
 * transaction, and a wrong one is self-announcing — the sale is priced out of a
 * block and the panel says so. Bounds are the ones the panel's steppers used to
 * enforce, so a stored value cannot become something the old UI could not express.
 */
static esp_err_t fees_post(httpd_req_t *req)
{
    esp_err_t rc;
    if (!gate(req, &rc)) { return rc; }

    char body[96] = { 0 };
    if (!read_body(req, body, sizeof(body))) {
        return reply(req, "400 Bad Request", "Bad request.");
    }

    char max_s[12] = { 0 };
    char prio_s[12] = { 0 };
    (void)form_field(body, "max", max_s, sizeof(max_s));
    (void)form_field(body, "prio", prio_s, sizeof(prio_s));

    /* strtoul on its own answers 0 for "abc", which would then be refused as
     * out of range anyway — but check the terminator too, so "20x" is a typo
     * that gets reported rather than silently stored as 20. */
    char    *end_max  = NULL;
    char    *end_prio = NULL;
    const unsigned long max_gwei  = strtoul(max_s,  &end_max,  10);
    const unsigned long prio_gwei = strtoul(prio_s, &end_prio, 10);
    if ((max_s[0] == '\0') || (prio_s[0] == '\0') ||
        (*end_max != '\0') || (*end_prio != '\0')) {
        return reply(req, "400 Bad Request", "Both fees have to be whole numbers "
                                             "of Gwei.");
    }
    const fee_pair_t verdict = fee_pair_check(max_gwei, prio_gwei);
    if (verdict == FEE_PAIR_OUT_OF_RANGE) {
        return reply(req, "400 Bad Request",
                     "Each fee has to be between 1 and 500 Gwei.");
    }
    if (verdict == FEE_PAIR_TIP_ABOVE_MAX) {
        return reply(req, "400 Bad Request",
                     "The tip cannot be higher than the max fee.");
    }

    (void)settings_set_fees_gwei(static_cast<uint32_t>(max_gwei),
                                 static_cast<uint32_t>(prio_gwei));   /* checked above */
    /* The panel is very likely showing the Tx tab's two gas rows right now, with
     * this page's card over them. Without this they stay on the old numbers until
     * the operator leaves the settings screen and comes back. */
    ui_fees_changed();
    ESP_LOGI(TAG, "gas caps set from the config page: max %lu, tip %lu Gwei",
             max_gwei, prio_gwei);
    return ok(req, "Gas fees stored. They apply to the next sale.");
}

/**
 * @brief Store the panel clock's standard offset from UTC and its DST rule.
 *
 * Written straight through like the gas fees, and for the same reason: it cannot
 * send money anywhere. The worst a wrong one does is put the wrong hour in the
 * corner of the screen, which announces itself to the first person who looks.
 *
 * The page sends both from one region list; @c dst is a civil_dst_t and a
 * missing one means none, so a fixed offset is still a valid request.
 *
 * The value is validated against the same bounds settings_set_tz_offset_min
 * enforces, so a hand-rolled POST cannot store an offset the picker could not
 * express — and it is rejected rather than clamped, since a clamped offset is a
 * clock that is silently wrong by whatever the clamp moved it.
 */
static esp_err_t clock_post(httpd_req_t *req)
{
    esp_err_t rc;
    if (!gate(req, &rc)) { return rc; }

    char body[64] = { 0 };
    if (!read_body(req, body, sizeof(body))) {
        return reply(req, "400 Bad Request", "Bad request.");
    }

    char off_s[12] = { 0 };
    char dst_s[4]  = { 0 };
    (void)form_field(body, "off", off_s, sizeof(off_s));
    (void)form_field(body, "dst", dst_s, sizeof(dst_s));

    /* strtol answers 0 for "abc", which is a legitimate offset (UTC) — so the
     * terminator is what separates "the operator picked UTC" from "that was not
     * a number at all". */
    char      *end = NULL;
    const long off = strtol(off_s, &end, 10);
    if ((off_s[0] == '\0') || (*end != '\0')) {
        return reply(req, "400 Bad Request",
                     "The offset has to be a whole number of minutes.");
    }
    char      *dend = NULL;
    const long dst  = strtol(dst_s, &dend, 10);   /* "" reads as 0: no DST */
    if ((*dend != '\0') || !tz_dst_valid(dst) || !tz_offset_valid(off)) {
        return reply(req, "400 Bad Request",
                     "That is not a time zone the terminal can use.");
    }
    (void)settings_set_tz_offset_min(static_cast<int16_t>(off));
    (void)settings_set_tz_dst(static_cast<uint8_t>(dst));

    /* The panel is very likely showing a sale screen with this page's card over
     * it. The clock caches the offset rather than reading NVS every tick, so
     * without this it keeps the old hour until something rebuilds the screen. */
    ui_clock_changed();
    ESP_LOGI(TAG, "clock set from the config page: %ld min, DST rule %ld", off, dst);
    return ok(req, "Clock stored. The terminal's time updates in a moment.");
}

/**
 * @brief Switch the terminal between the production and the test networks.
 *
 * Written straight through and followed by a restart, which is the whole of the
 * mechanism: the RPC endpoints, the chain ids and the token contracts are
 * resolved once at boot into the dual stores that every signature is reconciled
 * against, so there is no point at which a running terminal can be moved between
 * networks without a boot. Trying would mean a payment whose nonce came from one
 * chain and whose contract came from the other.
 *
 * Not proposed on the panel like an address, for the reason the gas caps are not:
 * this cannot send money anywhere. It decides where the operator's own payout
 * address is paid — the same address on both — and it announces itself loudly, on
 * the Tx tab and in the asset picker, the moment the terminal comes back up. It is
 * behind the admin code either way, like everything else on this page.
 *
 * The answer goes out before the reboot, with a pause long enough for it to reach
 * the browser: the phone is on this device's own AP, so a restart with the reply
 * still queued reads to the operator as the page having crashed.
 */
static esp_err_t network_post(httpd_req_t *req)
{
    esp_err_t rc;
    if (!gate(req, &rc)) { return rc; }

    char body[48] = { 0 };
    if (!read_body(req, body, sizeof(body))) {
        return reply(req, "400 Bad Request", "Bad request.");
    }

    char net[8] = { 0 };
    (void)form_field(body, "net", net, sizeof(net));

    const bool main_wanted = (strcmp(net, "main") == 0);
    if (!main_wanted && (strcmp(net, "test") != 0)) {
        return reply(req, "400 Bad Request", "Pick production or test.");
    }
    if (main_wanted == settings_get_mainnet()) {
        return reply(req, "409 Conflict", "The terminal is already on that "
                                          "network.");
    }

    settings_set_mainnet(main_wanted);
    ESP_LOGW(TAG, "network switched to %s from the config page - restarting",
             main_wanted ? "PRODUCTION" : "test");

    const esp_err_t sent = ok(req, main_wanted
        ? "Switched to the production networks. The terminal is restarting - "
          "check the asset on its Tx tab when it comes back."
        : "Switched to the test networks. The terminal is restarting - check the "
          "asset on its Tx tab when it comes back.");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return sent;   /* not reached */
}

/** @brief The browser asks the terminal to read the addresses off a card. */
static esp_err_t card_post(httpd_req_t *req)
{
    esp_err_t rc;
    if (!gate(req, &rc)) { return rc; }

    ESP_LOGI(TAG, "card-derived payout addresses requested from the page");
    if (s_cb != NULL) { s_cb(UI_EVENT_PROV_CARD, 0); }
    return ok(req, "Follow the terminal screen.");
}

/******************************************************************
 * 11. API — Wi-Fi and wizard navigation
 ******************************************************************/

static esp_err_t wifi_post(httpd_req_t *req)
{
    esp_err_t rc;
    if (!gate(req, &rc)) { return rc; }

    char body[256] = { 0 };
    if (!read_body(req, body, sizeof(body))) {
        return reply(req, "400 Bad Request", "Bad request.");
    }

    char ssid[33] = { 0 };
    char pass[65] = { 0 };
    (void)form_field(body, "ssid", ssid, sizeof(ssid));
    (void)form_field(body, "pass", pass, sizeof(pass));
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(body), sizeof(body));

    /* strlen, not what form_field returned: "ssid=%00" writes one byte and leaves
     * a string C reads as empty, which would otherwise be staged as a network
     * name and sent to esp_wifi_connect. See form_parse.h. */
    if (strlen(ssid) == 0U) {
        CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(pass), sizeof(pass));
        return reply(req, "400 Bad Request", "A network name is required.");
    }

    /* Staged into the UI's own handoff buffers and reported as the ordinary
     * "credentials entered" event, so main's existing connect-and-verify loop —
     * the connecting screen, the retry note, the keep-or-drop decision once the
     * clock proves the uplink — runs unchanged. */
    ui_stage_wifi_creds(ssid, pass);
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(pass), sizeof(pass));
    ESP_LOGI(TAG, "Wi-Fi '%s' submitted from the config page", ssid);

    /* Answer BEFORE the event: in wizard mode the connect attempt takes the radio
     * down and this response would never reach the phone otherwise. One sentence
     * for both modes — the wizard's page does not show this message at all any
     * more, it replaces itself with its own finished screen. */
    const esp_err_t sent = ok(req, "Trying that network now. Watch the terminal "
                                   "screen.");
    if (s_cb != NULL) { s_cb(UI_EVENT_WIFI_TRY, 0); }
    return sent;
}

static esp_err_t rescan_post(httpd_req_t *req)
{
    esp_err_t rc;
    if (!gate(req, &rc)) { return rc; }
    if (s_cb != NULL) { s_cb(UI_EVENT_PROV_SCAN, 0); }
    return ok(req, "Scanning.");
}

static esp_err_t next_post(httpd_req_t *req)
{
    esp_err_t rc;
    if (!gate(req, &rc)) { return rc; }
    /* The portal does not advance its own step: main owns the order, because each
     * step is something main has to do (scan the radio, park on a queue) and not
     * just a section to reveal. */
    if (s_cb != NULL) { s_cb(UI_EVENT_PROV_NEXT, 0); }
    return ok(req, "");
}

/******************************************************************
 * 12. API — firmware upload
 ******************************************************************/

/**
 * @brief Stream a firmware image into the idle slot, without booting it.
 *
 * Straight to flash: the image is bigger than the heap, so there is no version of
 * this that buffers it first. What makes that safe is ota.h's contract — nothing
 * written here can run until the image's SHA-256 has been verified — and its
 * signature too, on a build with Secure Boot (sdkconfig.defaults.release /
 * .flash_encryption); the plain defaults check structure and self-hash only —
 * AND somebody has accepted it on the panel.
 */
static esp_err_t ota_post(httpd_req_t *req)
{
    esp_err_t rc;
    if (!gate(req, &rc)) { return rc; }

    const char *err = "";
    if (!ota_begin(req->content_len, &err)) {
        /* One of these refusals is a dead end unless the panel is asked again: an
         * image already staged is only reachable through the card that
         * UI_EVENT_OTA_STAGED raised, staging lives in RAM, and an operator who
         * dismissed that card without deciding has no way back to it. So raise it
         * again — then "accept or discard it there first" is an instruction the
         * operator can actually follow. */
        if (ota_staged(NULL, 0U, NULL) && (s_cb != NULL)) {
            s_cb(UI_EVENT_OTA_STAGED, 0);
        }
        return reply(req, ota_receiving() ? "409 Conflict" : "400 Bad Request", err);
    }

    const size_t len = req->content_len;
    size_t       got = 0U;
    unsigned     stalls = 0U;
    bool         good = true;
    while (good && (got < len)) {
        const size_t want = ((len - got) < UPLOAD_CHUNK) ? (len - got) : UPLOAD_CHUNK;
        const int    n    = httpd_req_recv(req, reinterpret_cast<char *>(s_upload),
                                           want);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            /* A slow uplink is not a dead one — keep waiting, but not for ever:
             * this loop is what the portal's deadline now waits behind. */
            if (++stalls >= UPLOAD_MAX_STALLS) {
                ESP_LOGW(TAG, "upload stalled at %u/%u bytes - giving up",
                         static_cast<unsigned>(got), static_cast<unsigned>(len));
                good = false;
                break;
            }
            continue;
        }
        stalls = 0U;
        if (n <= 0) {
            ESP_LOGW(TAG, "upload aborted at %u/%u bytes",
                     static_cast<unsigned>(got), static_cast<unsigned>(len));
            good = false;
            break;
        }
        good = ota_write(s_upload, static_cast<size_t>(n));
        got += static_cast<size_t>(n);
    }

    if (!good) {
        ota_abort();
        return reply(req, "400 Bad Request",
                     "The upload did not complete. Nothing was installed.");
    }

    char ver[48] = "?";
    const bool ended = ota_end(ver, sizeof(ver), &err);

    /* The one measurement that matters on this task: ota_end() has just run the
     * signature verification, which is this stack's high-water mark by a wide
     * margin (it is what used to overflow the default 4 KB). Logged either way —
     * a rejection verifies too. If this number ever gets close to zero, raise
     * cfg.stack_size in prov_start() rather than finding out from a panic. */
    ESP_LOGI(TAG, "httpd stack: %u bytes still free after verification",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(NULL)));

    if (!ended) {
        return reply(req, "400 Bad Request", err);
    }

    char msg[144];
    (void)snprintf(msg, sizeof(msg),
                   "Version %s received and verified. Accept it on the terminal "
                   "screen to install it and reboot.", ver);
    /* Answer BEFORE the event, as wifi_post() does and for the same reason: what
     * the event leads to is a modal on the panel whose Install button reboots this
     * device, and a browser that was still waiting for this line reports a
     * successful update as a dropped connection. The image is already staged and
     * ota.h's rules do not depend on the order of these two. */
    const esp_err_t sent = ok(req, msg);
    if (s_cb != NULL) { s_cb(UI_EVENT_OTA_STAGED, 0); }
    return sent;
}

/******************************************************************
 * 13. Captive-portal probes
 ******************************************************************/

/**
 * @brief Send every probe and stray URL to the portal.
 *
 * This is the half that makes the browser open by itself. Each OS fetches a
 * known URL after joining and only raises the portal UI if the answer is *not*
 * what it expects, so answering correctly here would be the bug.
 */
static esp_err_t redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", PORTAL_URL);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, NULL, 0);
}

/** @brief 404 handler — the catch-all for probe URLs not listed below. */
static esp_err_t redirect_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    return redirect(req);
}

/* The probe URLs worth registering explicitly. The 404 handler would catch them
 * all anyway; these are named because each is a specific OS's decision point and
 * a silent change to one is a portal that stops opening on one platform only.
 *
 * Apple's is served as 200-with-wrong-body rather than a 302: iOS follows the
 * redirect, compares the final body against "Success", and a body it cannot
 * fetch is treated as no network at all. */
static const char *const PROBE_URIS[] = {
    "/generate_204",              /* Android                       */
    "/gen_204",                   /* Android, older                */
    "/connecttest.txt",           /* Windows NCSI                  */
    "/ncsi.txt",                  /* Windows NCSI                  */
    "/canonical.html",            /* Firefox                       */
    "/success.txt",               /* Firefox, newer                */
    "/chat",                      /* some Android builds           */
};

static esp_err_t apple_probe(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    /* Deliberately NOT Apple's expected "<HTML><HEAD><TITLE>Success..." body. */
    return httpd_resp_sendstr(req,
        "<HTML><HEAD><TITLE>Setup</TITLE></HEAD>"
        "<BODY><A href='" PORTAL_URL "'>Cryptnox setup</A></BODY></HTML>");
}

/******************************************************************
 * 14. Handler registration
 ******************************************************************/

static void register_handlers(void)
{
    const httpd_uri_t api[] = {
        { "/",             HTTP_GET,  page_get,      NULL },
        { "/api/state",    HTTP_GET,  state_get,     NULL },
        { "/api/scan",     HTTP_GET,  scan_get,      NULL },
        { "/api/auth",     HTTP_POST, auth_post,     NULL },
        { "/api/payout",   HTTP_POST, payout_post,   NULL },
        { "/api/contract", HTTP_POST, contract_post, NULL },
        { "/api/fees",     HTTP_POST, fees_post,     NULL },
        { "/api/clock",    HTTP_POST, clock_post,    NULL },
        { "/api/network",  HTTP_POST, network_post,  NULL },
        { "/api/card",     HTTP_POST, card_post,     NULL },
        { "/api/wifi",     HTTP_POST, wifi_post,     NULL },
        { "/api/rescan",   HTTP_POST, rescan_post,   NULL },
        { "/api/next",     HTTP_POST, next_post,     NULL },
        { "/api/ota",      HTTP_POST, ota_post,      NULL },
    };
    for (size_t i = 0; i < (sizeof(api) / sizeof(api[0])); i++) {
        (void)httpd_register_uri_handler(s_httpd, &api[i]);
    }

    /* Both modes: they are the same SoftAP with the same page on it, so the admin
     * page opens itself on the phone exactly like the wizard's does. */
    for (size_t i = 0; i < (sizeof(PROBE_URIS) / sizeof(PROBE_URIS[0])); i++) {
        const httpd_uri_t p = { PROBE_URIS[i], HTTP_GET, redirect, NULL };
        (void)httpd_register_uri_handler(s_httpd, &p);
    }
    const httpd_uri_t a1 = { "/hotspot-detect.html", HTTP_GET, apple_probe, NULL };
    const httpd_uri_t a2 = { "/library/test/success.html", HTTP_GET, apple_probe, NULL };
    (void)httpd_register_uri_handler(s_httpd, &a1);
    (void)httpd_register_uri_handler(s_httpd, &a2);

    (void)httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, redirect_404);
}

/******************************************************************
 * 15. Public API
 ******************************************************************/

bool prov_start(prov_mode_t mode, ui_event_cb_t cb)
{
    if (s_mode == mode) {
        /* Idempotent, but restart the clock: the operator asked again. */
        if (mode == PROV_MODE_ADMIN) {
            s_deadline_us = esp_timer_get_time() +
                            ((int64_t)PROV_WINDOW_MIN * 60LL * 1000000LL);
        }
        return s_httpd != NULL;
    }
    if (s_mode != PROV_MODE_OFF) {
        ESP_LOGE(TAG, "portal already up in mode %d - stop it first",
                 static_cast<int>(s_mode.load()));
        return false;
    }

    if (s_ask_lock == NULL) {
        s_ask_lock = xSemaphoreCreateMutex();
        if (s_ask_lock == NULL) { return false; }
    }
    s_cb           = cb;
    s_authed       = false;
    s_auth_pending = false;
    s_wifi_only    = false;
    s_token[0]     = '\0';

    const bool wizard = (mode == PROV_MODE_WIZARD);

    /* Both modes are the same SoftAP with the same page on it. Not a convenience:
     * httpd binds every interface, so a portal running beside a station
     * association answers the venue LAN too — and every device holding the venue
     * PSK is on that LAN. The AP is the perimeter, so it has to be the only
     * interface there is. net_ap_start() drops the station for exactly that
     * reason, and net_ap_stop() puts it back. */

    /* Before the passphrase, not after: on the first portal this device ever opens
     * there is one to draw, and esp_random() is only properly seeded once the RF
     * subsystem is running — net_wifi_init() is what starts it. Drawing first
     * would take the bootloader's entropy, which is the one thing this AP relies
     * on. Every portal after that reads the stored one. */
    net_wifi_init();
    ap_ssid_build();
    ap_pass_load();   /* drawn once, kept until a factory reset */
    (void)snprintf(s_qr, sizeof(s_qr), "WIFI:T:WPA;S:%s;P:%s;;", s_ssid, s_pass);

    if (!net_ap_start(s_ssid, s_pass)) {
        ESP_LOGE(TAG, "SoftAP failed to start");
        /* prov_stop() never runs for a portal that never came up, so the
         * passphrase it would have wiped is wiped here instead. */
        CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_pass), sizeof(s_pass));
        CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_qr), sizeof(s_qr));
        return false;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers  = 24;

    /* 16 KB, not the default 4 KB, and the firmware upload is the only reason.
     *
     * esp_ota_end() verifies the image it has just written, and on a Secure Boot
     * build that means an RSA-3072 PSS verification inside mbedTLS — which needs
     * several KB of stack and runs on whichever task called it. That is this one,
     * the httpd task serving /api/ota, so a completed upload ended in
     *
     *   secure_boot_v2: Verifying with RSA-PSS...
     *   ***ERROR*** A stack overflow in task httpd has been detected.
     *
     * and a reboot: the file arrived, the signature was genuine, and the terminal
     * panicked between the two every single time. Nothing was ever staged, and from
     * the browser it looked like the connection dropped, because it had.
     *
     * 16 KB is the size the UI task already runs at, and it does the same
     * verification from esp_ota_set_boot_partition() when Install is tapped.
     *
     * Measured, not guessed: a 1.0.2 image verified with 11684 of these 16384 bytes
     * still free, so the peak is ~4.7 KB and the old default was about 600 bytes
     * short — which is why it failed every time rather than occasionally. Do not
     * trim this to the measurement: mbedTLS's RSA path is the tallest thing either
     * task does, and ~11 KB of headroom on a transient server is cheaper than
     * another panic between a verified image and the operator's screen. ota_post
     * logs the figure after every upload if it ever needs checking again. */
    cfg.stack_size        = 16384;

    /* The upload is one request that holds a socket for minutes, and LRU purge
     * picks the least recently used socket — which is exactly that one, since its
     * request began before every poll and captive-portal probe that followed. A PC
     * is the case that shows it: Windows probes for internet the whole time it is on
     * an AP that has none, and each probe is another connection. First the socket
     * table ran dry (87 x "error in accept (23)" = ENFILE in one session), then the
     * upload was sacrificed for the next probe and stalled at 33%.
     *
     * So: no purging, and TCP keepalive to reap what purging used to. A dead peer is
     * dropped in ~20s, a live upload is never dropped, and a probe that finds the
     * table full is refused — which costs a Windows machine nothing it was going to
     * get anyway. The page also stops its 1.5s poll while sending (PAGE_JS). */
    cfg.lru_purge_enable  = false;
    cfg.keep_alive_enable = true;
    cfg.keep_alive_idle     = 5;
    cfg.keep_alive_interval = 5;
    cfg.keep_alive_count    = 3;
    /* Port 80 is not optional: a captive-portal probe fetches a bare http:// URL
     * and will not follow us anywhere else. Plain HTTP over WPA2 is the whole
     * transport story now — see provision.h. */
    cfg.server_port       = 80;
    /* A firmware image is 1.9 MB and each erase-and-write pause inside the
     * upload is seconds. The default 5 s would drop the socket mid-image;
     * ota_post() also retries on timeout, and between the two a slow phone
     * survives. */
    cfg.recv_wait_timeout = 30;
    cfg.send_wait_timeout = 30;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd failed to start");
        s_httpd = NULL;
        net_ap_stop();
        CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_pass), sizeof(s_pass));
        CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_qr), sizeof(s_qr));
        return false;
    }

    register_handlers();

    s_mode = mode;
    s_step = wizard ? PROV_STEP_AUTH : PROV_STEP_ADMIN;

    /* No self-close for the wizard: a terminal halfway through setup that shut its
     * own setup page after a quarter of an hour would strand whoever went to fetch
     * the Wi-Fi password. main stops this one when setup ends. Admin mode does
     * close itself — it has taken a working terminal off its network to do this. */
    s_deadline_us = wizard ? 0
                           : (esp_timer_get_time() +
                              ((int64_t)PROV_WINDOW_MIN * 60LL * 1000000LL));

    s_dns_run = true;
    if (xTaskCreate(dns_task, "prov_dns", 3072, NULL, 4, &s_dns_task) != pdPASS) {
        /* The forms still work for anyone who types the IP, but the portal will
         * not open by itself — which is the entire point, so say so. */
        ESP_LOGE(TAG, "DNS task failed - captive portal will NOT auto-open");
        s_dns_run  = false;
        s_dns_task = NULL;
    }
    ESP_LOGW(TAG, "%s portal up: SSID '%s', pass '%s', %s",
             wizard ? "setup" : "admin", s_ssid, s_pass, PORTAL_URL);
    /* The tight moment for heap on this board: httpd, its sockets and the DNS task
     * are now up alongside LVGL and the Wi-Fi driver, and a firmware upload is
     * about to ask for buffers on top. The largest free block, not just the total —
     * a TLS or upload allocation fails on fragmentation long before the sum runs
     * out, and without a number a field failure cannot be attributed at all. */
    ESP_LOGI(TAG, "heap after portal start: %u free, %u largest block",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    return true;
}

void prov_stop(void)
{
    /* Claim the teardown. Whoever gets a non-OFF value here owns it; anyone else
     * arriving concurrently sees OFF and returns. */
    const prov_mode_t was = s_mode.exchange(PROV_MODE_OFF);
    if (was == PROV_MODE_OFF) { return; }

    s_step = PROV_STEP_IDLE;

    s_dns_run = false;
    /* The task closes its socket and deletes itself within one recv timeout. */
    for (int i = 0; (i < 20) && (s_dns_task != NULL); i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (s_httpd != NULL) {
        (void)httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    /* Both modes raise the AP, and net_ap_stop() also re-joins the network the AP
     * displaced — so closing the admin page puts a working terminal back online. */
    net_ap_stop();

    /* An upload half-received does not outlive the page it arrived through.
     *
     * An image that is already *staged* does, and that is deliberate: it has been
     * verified, it is on the panel waiting for somebody to accept it, and
     * installing it needs no page at all. This used to call ota_forget() here, and
     * it was the bug that made "the update did not install" reproducible — a
     * 1.9 MB upload over the SoftAP eats minutes of the 15-minute admin window, so
     * the window routinely expired while the operator was reading the Install
     * button, prov_stop() withdrew the offer from under them, and the tap then did
     * nothing at all. Nothing about that made the device safer: an image can only
     * be here at all if it carries a signature from the key this firmware trusts,
     * and the panel is still the only thing that can install it. Discard on the
     * panel is what drops a staged image (ota_commit(false)). */
    ota_abort();

    s_authed       = false;
    s_auth_pending = false;
    s_wifi_only    = false;
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_token), sizeof(s_token));

    /* The AP passphrase too: the AP it opened is down, the next portal reads it
     * back from NVS, and the QR screen is only ever painted while a portal is up —
     * so nothing needs it in RAM in between. A proposed value IS dropped as well:
     * unaccepted means unwanted. */
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_pass), sizeof(s_pass));
    if (s_ask_lock != NULL) {
        if (xSemaphoreTake(s_ask_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_ask = PROV_ASK_NONE;
            CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_ask_val),
                                  sizeof(s_ask_val));
            (void)xSemaphoreGive(s_ask_lock);
        }
    }
    /* s_qr holds the passphrase, so clear all of it, not just byte 0. */
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_qr), sizeof(s_qr));
    s_deadline_us = 0;
    ESP_LOGI(TAG, "config portal down");
}

prov_mode_t prov_mode(void) { return s_mode; }

void prov_set_step(prov_step_t step) { s_step = step; }

prov_step_t prov_step(void) { return s_step; }

unsigned prov_window_left_min(void)
{
    if ((s_httpd == NULL) || (s_deadline_us == 0)) { return 0U; }
    const int64_t left = s_deadline_us - esp_timer_get_time();
    if (left <= 0) { return 0U; }
    /* Round up: "1 min left" should not read as 0 for the last 59 seconds. */
    return static_cast<unsigned>((left + (59LL * 1000000LL)) / 60000000LL);
}

const char *prov_ap_ssid(void)    { return s_ssid; }
const char *prov_ap_pass(void)    { return s_pass; }
const char *prov_qr_payload(void) { return s_qr; }

bool prov_auth_pending(void) { return s_auth_pending; }

void prov_auth_resolve(bool grant)
{
    if (!s_auth_pending) { return; }
    s_auth_pending = false;
    s_authed       = grant;
    if (grant) {
        ESP_LOGW(TAG, "browser authorised from the panel");
        /* Reported as "move on", because that is what it is: nothing else happens
         * when a browser is let in, and the wizard is parked on its queue waiting
         * for something to happen. Without this the code would be accepted and the
         * flow would sit on the authorise step until somebody pressed Continue. */
        if (s_cb != NULL) { s_cb(UI_EVENT_PROV_NEXT, 0); }
    } else {
        CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_token), sizeof(s_token));
        ESP_LOGW(TAG, "browser authorisation refused");
    }
}

bool prov_authed(void) { return s_authed; }

bool prov_pair_code(char *out, size_t out_size)
{
    if ((out == NULL) || (out_size < 5U) || (s_token[0] == '\0')) { return false; }
    unsigned v = 0U;
    for (size_t i = 0U; i < 4U; i++) {
        const char c = s_token[i];
        v = (v << 4) | static_cast<unsigned>((c <= '9') ? (c - '0') : (c - 'a' + 10));
    }
    (void)snprintf(out, out_size, "%04u", v % 10000U);
    return true;
}

void prov_set_wifi_only(void) { s_wifi_only = true; }

bool prov_wifi_only(void) { return s_wifi_only; }

void prov_set_note(const char *note)
{
    /* Sanitised rather than JSON-escaped. Every note is one of this firmware's own
     * sentences, so there is nothing to escape today — but it lands in a JSON
     * string literal, and a future caller pasting an SSID or an error string in
     * here should get a dropped character rather than a response the page cannot
     * parse. */
    char   clean[sizeof(s_note)];
    size_t w = 0U;
    if (note != NULL) {
        for (const char *p = note; (*p != '\0') && (w < (sizeof(clean) - 1U)); p++) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if ((c == '"') || (c == '\\') || (c < 0x20U)) { continue; }
            clean[w++] = static_cast<char>(c);
        }
    }
    clean[w] = '\0';
    taskENTER_CRITICAL(&s_share_mux);
    (void)CW_Utils::safe_memcpy(reinterpret_cast<uint8_t *>(s_note), sizeof(s_note),
                                reinterpret_cast<const uint8_t *>(clean), w + 1U);
    taskEXIT_CRITICAL(&s_share_mux);
}

void prov_set_scan(const net_wifi_ap_t *aps, uint16_t n)
{
    if (aps == NULL) { n = 0U; }
    if (n > PROV_MAX_APS) { n = PROV_MAX_APS; }
    taskENTER_CRITICAL(&s_share_mux);
    for (uint16_t i = 0U; i < n; i++) { s_aps[i] = aps[i]; }
    s_ap_count = n;
    taskEXIT_CRITICAL(&s_share_mux);
    s_scan_gen++;   /* the page refetches when this moves */
}

bool prov_propose(prov_ask_t kind, const char *addr)
{
    if ((s_ask_lock == NULL) || (addr == NULL) || (kind == PROV_ASK_NONE)) {
        return false;
    }
    if (xSemaphoreTake(s_ask_lock, pdMS_TO_TICKS(500)) != pdTRUE) { return false; }
    if (s_ask != PROV_ASK_NONE) {
        (void)xSemaphoreGive(s_ask_lock);
        return false;
    }
    s_ask = kind;
    (void)snprintf(s_ask_val, sizeof(s_ask_val), "%s", addr);
    (void)xSemaphoreGive(s_ask_lock);

    ESP_LOGW(TAG, "%s proposed: %s - awaiting on-screen accept",
             ask_label(kind), addr);
    if (s_cb != NULL) { s_cb(UI_EVENT_PROV_VALUE, 0); }
    return true;
}

bool prov_pending(prov_ask_t *kind, char *label, size_t label_n,
                  char *value, size_t value_n)
{
    if (s_ask_lock == NULL) { return false; }
    if (xSemaphoreTake(s_ask_lock, pdMS_TO_TICKS(100)) != pdTRUE) { return false; }

    const bool waiting = (s_ask != PROV_ASK_NONE);
    if (waiting) {
        if (kind != NULL) { *kind = s_ask; }
        if ((label != NULL) && (label_n > 0U)) {
            (void)snprintf(label, label_n, "%s", ask_label(s_ask));
        }
        if ((value != NULL) && (value_n > 0U)) {
            (void)snprintf(value, value_n, "%s", s_ask_val);
        }
    } else if (kind != NULL) {
        *kind = PROV_ASK_NONE;
    }
    (void)xSemaphoreGive(s_ask_lock);
    return waiting;
}

bool prov_pending_commit(bool accept)
{
    if (s_ask_lock == NULL) { return false; }
    if (xSemaphoreTake(s_ask_lock, pdMS_TO_TICKS(100)) != pdTRUE) { return false; }

    bool stored = false;
    if ((s_ask != PROV_ASK_NONE) && accept) {
        switch (s_ask) {
            case PROV_ASK_PAYOUT_ETH:
                stored = settings_set_payout(false, s_ask_val);   break;
            case PROV_ASK_PAYOUT_TRON:
                stored = settings_set_payout(true, s_ask_val);    break;
            case PROV_ASK_CONTRACT_ETH:
                stored = settings_set_contract(POS_CHAIN_ETH_USDC, s_ask_val);  break;
            case PROV_ASK_CONTRACT_TRON:
                stored = settings_set_contract(POS_CHAIN_TRON_USDT, s_ask_val); break;
            default: break;
        }
    }
    const bool had = (s_ask != PROV_ASK_NONE);
    s_ask = PROV_ASK_NONE;
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_ask_val), sizeof(s_ask_val));
    (void)xSemaphoreGive(s_ask_lock);

    if (s_cb != NULL) {
        if (stored)    { s_cb(UI_EVENT_PROV_VALUE_SET, 0); }
        else if (had)  { s_cb(UI_EVENT_PROV_VALUE_NO, 0); }
    }
    return stored;
}
