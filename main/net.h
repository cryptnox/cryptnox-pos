/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file net.h
 * @ingroup device
 * @brief Network bring-up: Wi-Fi station (init/scan/connect/RSSI) and SNTP
 *        time sync.  No application-protocol logic lives here — the Ethereum
 *        JSON-RPC client is in eth_rpc.h.
 */

#ifndef NET_H
#define NET_H

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************
 * 2. Types
 ******************************************************************/

/** @brief A scanned access point (subset of fields the UI needs). */
typedef struct {
    char   ssid[33];   /**< NUL-terminated SSID (max 32 chars + NUL). */
    int8_t rssi;       /**< Signal strength, dBm (closer to 0 = stronger). */
    bool   open;       /**< true if the network is open (no password). */
} net_wifi_ap_t;

/******************************************************************
 * 3. Public API
 ******************************************************************/

/**
 * @brief Bring up the WiFi driver in station mode (idempotent).
 *
 * Initialises netif, the event loop and the WiFi driver and starts the STA.
 * Call once before scanning or connecting. Safe to call repeatedly.
 */
void net_wifi_init(void);

/**
 * @brief Scan for nearby access points (blocking).
 *
 * @param[out] out  Array to fill with de-duplicated APs.
 * @param[in]  max  Capacity of @p out.
 * @return number of APs written (0 on error or none found).
 */
uint16_t net_wifi_scan(net_wifi_ap_t *out, uint16_t max);

/**
 * @brief Connect to a WiFi network and block until an IP is obtained
 *        (up to 30 s). May be called repeatedly to switch networks.
 *
 * @param[in] ssid     Network SSID.
 * @param[in] password Passphrase (empty string for an open network).
 * @return true on success, false on timeout or repeated association failure.
 */
bool net_wifi_connect(const char *ssid, const char *password);

/**
 * @brief Drop the station association, without the retry loop pulling it back.
 *
 * For a join that worked but is not being kept: the setup portal's HTTP server
 * binds every interface, so an associated station puts the setup forms on the
 * venue LAN as well as on the SoftAP. Leaving a test association up while that
 * portal is still running hands the forms to everyone holding the venue PSK.
 * With a SoftAP up this also returns the radio to AP-only, so the station
 * interface goes down with the association rather than lingering idle.
 * Idempotent, and safe before any connect.
 */
void net_wifi_disconnect(void);

/**
 * @brief Raise a WPA2 SoftAP as the radio's *only* interface, for phone-based
 *        configuration.
 *
 * AP-only, not APSTA, and the station association is dropped on the way in. The
 * config portal's HTTP server binds every interface (esp_http_server offers no
 * bind address), so an APSTA terminal answers the payout forms on the venue LAN
 * as well as on the AP — which is exactly where the AP passphrase guards
 * nothing. Taking the station down makes the AP the only door there is.
 *
 * The station is borrowed back where it is genuinely needed and only for as long
 * as that takes: @ref net_wifi_scan flips to APSTA for the scan itself, and
 * @ref net_wifi_connect for the join the operator asked for. @ref net_ap_stop
 * re-associates afterwards.
 *
 * One radio, one channel: when the station associates, the SoftAP is dragged
 * onto the station's channel and any joined phone is dropped. That is expected —
 * the setup page warns before it submits Wi-Fi credentials, and the device
 * screen, not the phone, reports the outcome.
 *
 * @param[in] ssid AP SSID.
 * @param[in] pass WPA2 passphrase; must be at least 8 characters.
 * @return true once the AP interface is configured and up.
 */
bool net_ap_start(const char *ssid, const char *pass);

/**
 * @brief Drop the SoftAP, return the radio to station-only, and re-join.
 *
 * The association @ref net_ap_start displaced is put back if it is not already
 * up: the driver still holds the credentials, so only the association went away.
 * Without it, closing the config page would leave a working terminal offline
 * until somebody rebooted it.
 */
void net_ap_stop(void);

/**
 * @brief Hold on to the network last configured, even though it is down now.
 *
 * A join that succeeds already does this: from then on a drop past the immediate
 * retries hands over to a background re-join (2 s doubling to every 30 s, for as
 * long as it takes). This is for the unit that boots while its router is still
 * coming up — the saved network was tried and failed, and should be tried again
 * in the background rather than never. No-op while the SoftAP is up, and undone
 * by @ref net_wifi_disconnect and @ref net_wifi_connect.
 */
void net_wifi_keep_trying(void);

/** @brief true while the station holds an IP address. */
bool net_wifi_online(void);

/** @brief true while a kept network is down and the re-join is running. */
bool net_wifi_reconnecting(void);

/**
 * @brief Read the RSSI of the currently associated access point.
 *
 * @param[out] rssi_out Signal strength in dBm (closer to 0 = stronger);
 *                      untouched when not associated.
 * @return true if associated and @p rssi_out was written, false otherwise.
 */
bool net_wifi_rssi(int8_t *rssi_out);

/**
 * @brief Block until the system clock has been set via SNTP and sanity-checked.
 *
 * Must be called after net_wifi_connect() and before any HTTPS request:
 * without real time, TLS certificate validity-period checks are meaningless.
 * Those checks are only actually compiled in when CONFIG_MBEDTLS_HAVE_TIME_DATE
 * is set (see sdkconfig.defaults) — HAVE_TIME alone is not enough.
 * SNTP keeps running in the background for periodic resyncs.
 *
 * Callable repeatedly: each call re-subscribes and waits for a fresh packet, so
 * it doubles as a probe for whether the network just joined reaches the internet.
 *
 * SNTP is unauthenticated, so a synced time earlier than the firmware's own
 * build timestamp is rejected: back-dating is what an attacker needs to make
 * an expired certificate look valid again. Moving the clock forward is
 * harmless and is not restricted. A rejection returns false like any other
 * failure, so the caller's existing "no network time" path applies.
 *
 * @param[in] timeout_ms Maximum time to wait for the sync.
 * @return true once the clock is set and passes the lower-bound check, false on
 *         init error, timeout, or a back-dated clock.
 */
bool net_time_sync(uint32_t timeout_ms);

/**
 * @brief Subscribe SNTP without waiting, so the clock is set whenever the
 *        network comes back.
 *
 * For a boot that could not sync in the foreground. lwIP retries on its own;
 * the build-time floor @ref net_time_sync enforces is applied to every update,
 * so a back-dated packet clears the clock rather than setting it. Idempotent.
 */
void net_time_background(void);

#ifdef __cplusplus
}
#endif

#endif // NET_H
