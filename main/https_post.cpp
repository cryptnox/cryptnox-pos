/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file https_post.cpp
 * @brief HTTPS JSON POST with the authenticated-Date clock cross-check.
 */

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "https_post.h"
#include "civil_time.h"

#include <string.h>
#include <strings.h>    /* strcasecmp */
#include <time.h>       /* time */
#include <inttypes.h>   /* PRId64 */

/* CW_Utils.h pulls in Arduino.h (via platform_compat.h); it must come before
 * any lwip-including IDF header (esp_http_client.h, esp_netif.h, ...) so that
 * IPAddress.h declares INADDR_NONE before lwip defines it as a macro. */
#include "CW_Utils.h"   /* hardened memory primitives (CODING_RULES §1.4) */

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "wdt.h"            /* wdt_feed() — each request can block for a while */

static const char *const TAG = "https";

/* never dump full RPC responses (they can echo credentials embedded
 * in the URL) — log at most this many bytes on failures. */
#define RESP_LOG_MAX  80

/* Tolerated disagreement between our clock and the server's Date header.
 * The header has 1 s granularity and provider clocks are NTP-synced, so only
 * request latency sits in between — 5 min is enormously generous while still
 * catching the real attack (back-dating to revive an expired cert moves the
 * clock by weeks). Same convention as Kerberos. */
#define CLOCK_SKEW_MAX_S  300

/******************************************************************
 * 2. Response headers
 ******************************************************************/

/** @brief Response headers captured during fetch (see @ref http_event_cb). */
typedef struct {
    char date[40];   /**< Date header value, "" if the server sent none.
                          IMF-fixdate is 29 chars; 40 leaves slack.        */
} resp_hdrs_t;

/**
 * @brief HTTP event hook that captures the response Date header.
 *
 * Response headers are ONLY reachable this way. esp_http_client_get_header()
 * looks up client->request->headers — the headers we send — so it can never
 * return the server's Date, however plausible the name looks.
 *
 * @param[in] evt Event; user_data points at the caller's resp_hdrs_t.
 * @return ESP_OK always (never fail a transfer over a header we merely want).
 */
static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    if ((evt == NULL) || (evt->event_id != HTTP_EVENT_ON_HEADER) ||
        (evt->user_data == NULL) || (evt->header_key == NULL)) {
        return ESP_OK;
    }

    /* Field names are case-insensitive (RFC 9110 §5.1). */
    if (strcasecmp(evt->header_key, "Date") == 0) {
        resp_hdrs_t *hdrs = static_cast<resp_hdrs_t *>(evt->user_data);
        const char  *val  = (evt->header_value != NULL) ? evt->header_value : "";
        (void)strncpy(hdrs->date, val, sizeof(hdrs->date) - 1U);
        hdrs->date[sizeof(hdrs->date) - 1U] = '\0';
    }
    return ESP_OK;
}

/**
 * @brief Cross-check the system clock against the server's HTTP Date header.
 *
 * SNTP gave us the clock over plain unauthenticated UDP, so a network
 * attacker can dictate it. This header arrives inside the encrypted,
 * authenticated TLS channel — without the CA's private key it can be neither
 * forged nor altered, which makes it a strictly better time source than the
 * handshake (ServerHello.random's gmt_unix_time is pure random in TLS 1.3 and
 * esp_http_client does not expose it anyway).
 *
 * Must be called after esp_http_client_fetch_headers().
 *
 * A missing or non-conforming header yields "not corroborated", not
 * "disagrees": the response already passed TLS validation, so an absent
 * header means the provider genuinely omitted it, and failing the payment
 * over that would be a self-inflicted outage. An attacker cannot induce this
 * case without breaking TLS.
 *
 * @param[in] date_hdr Captured Date value; "" when the server sent none.
 * @return false only when a parseable Date disagrees with the local clock by
 *         more than @ref CLOCK_SKEW_MAX_S; true otherwise.
 */
static bool clock_corroborated(const char *date_hdr)
{
    int64_t  server_epoch = 0;
    int64_t  local_epoch;
    int64_t  skew;

    if (date_hdr[0] == '\0') {
        ESP_LOGW(TAG, "no Date header - clock not corroborated");
        return true;
    }

    if (!civil_parse_http_date(date_hdr, &server_epoch)) {
        ESP_LOGW(TAG, "unparseable Date header - clock not corroborated");
        return true;
    }

    local_epoch = static_cast<int64_t>(time(NULL));
    skew = local_epoch - server_epoch;
    if (skew < 0) { skew = -skew; }

    if (skew > CLOCK_SKEW_MAX_S) {
        ESP_LOGE(TAG, "clock off by %" PRId64 " s vs server (local %" PRId64
                      ", server %" PRId64 ") - refusing (spoofed NTP?)",
                 skew, local_epoch, server_epoch);
        return false;
    }

    ESP_LOGD(TAG, "clock corroborated (skew %" PRId64 " s)", skew);
    return true;
}

/******************************************************************
 * 3. Public API
 ******************************************************************/

bool https_post_json(const char *url, const char *body,
                     char *resp_buf, size_t resp_buf_size,
                     const char *user, const char *pass, const char *ca_pem)
{
    bool success = false;

    if ((url == NULL) || (body == NULL) || (resp_buf == NULL) ||
        (resp_buf_size < 2U)) {
        return false;
    }
    /* Each socket step below may take up to timeout_ms, and the caller is on the
     * task watchdog: feed once per request so a slow one is not a reset. */
    wdt_feed();

    bool use_auth = ((user != NULL) && (user[0] != '\0') &&
                     (pass != NULL) && (pass[0] != '\0'));

    resp_hdrs_t hdrs;
    (void)memset(&hdrs, 0, sizeof(hdrs));

    esp_http_client_config_t cfg;
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(&cfg), sizeof(cfg));
    cfg.url               = url;
    cfg.method            = HTTP_METHOD_POST;
    cfg.timeout_ms        = 15000;
    cfg.event_handler     = http_event_cb;   /* captures the Date header */
    cfg.user_data         = &hdrs;
    /* if a cert was pinned by the caller, trust ONLY it — otherwise any of the
     * ~150 CAs in the Mozilla bundle could MITM the RPC. */
    if (ca_pem != NULL) {
        cfg.cert_pem = ca_pem;
    } else {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }
    if (use_auth) {
        cfg.username  = user;
        cfg.password  = pass;
        cfg.auth_type = HTTP_AUTH_TYPE_BASIC;
    }

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return false;
    }

    (void)esp_http_client_set_header(client, "Content-Type", "application/json");

    int body_len = static_cast<int>(strlen(body));
    esp_err_t err = esp_http_client_open(client, body_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open: %s", esp_err_to_name(err));
        goto cleanup;
    }

    if (esp_http_client_write(client, body, body_len) != body_len) {
        ESP_LOGE(TAG, "HTTP write incomplete");
        goto cleanup;
    }

    {
        int64_t content_length = esp_http_client_fetch_headers(client);
        (void)content_length;  /* may be -1 for chunked; we read until EOF */

        /* Reject the response before reading the body if the authenticated
         * Date proves our SNTP-supplied clock was spoofed. Consistent with
         * the existing no-network-time path: refuse rather than adopt the
         * header's time, so one wrong provider clock can never silently
         * redefine what this terminal treats as "now". */
        if (!clock_corroborated(hdrs.date)) {
            goto cleanup;   /* success stays false */
        }

        wdt_feed();   /* the handshake and headers are done; the body follows */
        int  total = 0;
        int  read;
        bool full  = false;
        do {
            int space = static_cast<int>(resp_buf_size - 1U) - total;
            if (space <= 0) { full = true; break; }
            read = esp_http_client_read(client, resp_buf + total, space);
            if (read > 0) { total += read; }
        } while (read > 0);

        resp_buf[total] = '\0';

        /* A body that filled the buffer and was not finished is a truncated
         * response, and used to be reported as success — JSON cut mid-string that
         * the parsers then had to be lucky about. Failure instead: the callers
         * already treat "no usable answer" as retry or Unconfirmed. */
        if (full && !esp_http_client_is_complete_data_received(client)) {
            ESP_LOGE(TAG, "response larger than %u bytes - dropped",
                     static_cast<unsigned>(resp_buf_size - 1U));
            goto cleanup;   /* success stays false */
        }

        /* a 4xx/5xx body that happens to contain "result" must not
         * be mistaken for a successful response. */
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGE(TAG, "HTTP status %d: %.*s", status, RESP_LOG_MAX, resp_buf);
        }
        success = ((total > 0) && (status == 200));
    }

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return success;
}
