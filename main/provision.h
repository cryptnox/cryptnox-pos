/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file provision.h
 * @ingroup device
 * @brief The config portal: one web app for setting a terminal up and for
 *        administering it afterwards, in two modes.
 *
 * ## Why a portal at all
 *
 * Typing a 42-character recipient address on a 240x320 resistive panel is the
 * worst part of setting one of these up, and telling an operator to type an IP
 * address is only marginally better. Scanning a QR code should land them on the
 * form.
 *
 * ## One transport: the terminal's own SoftAP
 *
 * Both modes raise a WPA2 SoftAP with a fresh per-session passphrase and run a
 * captive portal on it: a DNS responder answers every lookup with the AP's
 * address, and the HTTP server deliberately fails each OS's connectivity probe so
 * the phone concludes the network is captive and opens the browser by itself. Miss
 * either half and the phone joins in silence. The QR code on the panel carries
 * "WIFI:T:WPA;S:...;P:...;;", which both iOS and Android cameras join from
 * directly — so there is never a URL to type.
 *
 * The AP is the *only* interface while a portal is up: @ref net_ap_start drops the
 * station association and @ref net_ap_stop restores it. That is not tidiness.
 * esp_http_server binds every interface and offers no bind address, so a portal
 * running beside a station association also answers the venue LAN — where every
 * device holding the venue PSK can reach the payout forms, and where the perimeter
 * this module actually relies on (a passphrase on the panel, in front of the
 * person asking) means nothing at all. So there is no remote administration here,
 * by construction: you are in front of the terminal or you are nowhere.
 *
 * What that buys, besides the smaller attack surface, is one transport instead of
 * two — no TLS server, no self-signed identity in NVS, no certificate warning to
 * explain on a panel, and no second code path through the same forms.
 *
 * ## The two modes
 *
 * @ref PROV_MODE_WIZARD — a blank terminal, or one whose saved network has become
 * unreachable. Walks the setup steps one at a time and stays up until setup ends.
 *
 * @ref PROV_MODE_ADMIN — a configured terminal, opened from behind the admin code
 * in the settings menu. Serves everything at once and closes itself after
 * @ref PROV_WINDOW_MIN, because it has taken a working terminal off its network to
 * do this.
 *
 * ## Authorisation: the panel, never the wire
 *
 * The browser has no admin-code field. It asks to be let in, and the terminal
 * shows its admin-code screen; the operator types the code *on the panel*, and the
 * portal session becomes authorised. So the code never crosses the network in
 * either direction, on either mode's transport, and a browser that reaches the
 * page without the terminal in reach can do nothing at all.
 *
 * One exception, and it is the wizard's alone: @ref prov_set_wifi_only, the flow a
 * configured terminal gets when all it has lost is its network. See there.
 *
 * Everything that changes where money goes goes further than that: a browser may
 * *propose* a payout address or a token contract, and the value is then displayed
 * on the panel for somebody to accept there. Nothing is stored until they do.
 * Firmware follows the same rule — an upload is verified and staged, and only an
 * on-screen accept makes it bootable.
 *
 * ## Why plain HTTP, in both modes
 *
 * Not an oversight:
 *   - A captive-portal probe fetches a bare http:// URL on port 80 and will not
 *     follow us to 443. TLS means the browser never opens by itself, which is the
 *     entire point of a captive portal.
 *   - The link is already encrypted. It is a WPA2 SoftAP whose random per-session
 *     passphrase is on the panel in front of the operator, it admits one station
 *     at a time, and it only exists while the portal does.
 *   - The admin code is not on that link (see above), and the values that are get
 *     confirmed on the panel.
 *   - There is nothing else on the wire to protect it from: the AP is the radio's
 *     only interface for as long as the portal is up.
 * So the AP passphrase is the perimeter, and TLS on top of WPA2 would buy a
 * certificate warning and nothing else.
 */

#ifndef PROVISION_H
#define PROVISION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "net.h"  /* net_wifi_ap_t — the browser is offered the device's own scan */
#include "ui.h"   /* ui_event_cb_t — submissions are reported as UI events */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How long the portal stays up before closing itself, minutes. */
#define PROV_WINDOW_MIN  15U

/** @brief Which of the two portals is running. */
typedef enum {
    PROV_MODE_OFF,      /**< Not running.                                     */
    PROV_MODE_WIZARD,   /**< Setup: one step at a time, no self-close.        */
    PROV_MODE_ADMIN,    /**< Administration: everything at once, times out.   */
} prov_mode_t;

/**
 * @brief Where the wizard has got to.
 *
 * The order is the flow: create the admin code on the panel, show the QR code,
 * have the browser authorised from the panel, set the payout addresses, join the
 * venue network, done. Only the wizard walks these; admin mode sits on
 * @ref PROV_STEP_ADMIN and serves everything at once.
 */
typedef enum {
    PROV_STEP_IDLE,   /**< Nothing to do — the page says so.                */
    PROV_STEP_AUTH,   /**< Waiting for the admin code to be typed on the panel. */
    PROV_STEP_ADDR,   /**< Payout addresses.                                */
    PROV_STEP_WIFI,   /**< Join the venue's Wi-Fi.                          */
    PROV_STEP_DONE,   /**< Setup finished; thank the operator.              */
    PROV_STEP_ADMIN,  /**< Not a wizard step — the full admin page.         */
} prov_step_t;

/** @brief What the panel is being asked to accept, for @ref prov_pending. */
typedef enum {
    PROV_ASK_NONE,
    PROV_ASK_PAYOUT_ETH,     /**< Ethereum payout address.   */
    PROV_ASK_PAYOUT_TRON,    /**< Tron payout address.       */
    PROV_ASK_CONTRACT_ETH,   /**< ERC-20 token contract.     */
    PROV_ASK_CONTRACT_TRON,  /**< TRC-20 token contract.     */
} prov_ask_t;

/**
 * @brief Raise the portal.
 *
 * Idempotent for the same mode; a different mode is refused rather than silently
 * switched, since the two serve different steps to the same page. Stop it first.
 *
 * Raising the portal takes the terminal off its network for the duration (see
 * above); @ref prov_stop puts it back.
 *
 * A new AP passphrase is drawn on every call and never stored: it is shown on a
 * screen a customer can see, so a photograph of it must not still open the
 * wifi_only portal — which asks for no admin code — weeks later. prov_stop()
 * wipes it, and a reboot mid-setup simply shows the next one.
 *
 * @param[in] mode Which portal to run.
 * @param[in] cb   Where submissions are reported; the same callback the UI task
 *                 uses, so a form and a screen tap are indistinguishable to the
 *                 main task. Must outlive the call.
 * @return true if the portal is up and reachable; false if the SoftAP or the HTTP
 *         server would not start.
 */
bool prov_start(prov_mode_t mode, ui_event_cb_t cb);

/**
 * @brief Stop the portal, drop the AP, and withdraw anything unaccepted.
 *
 * Also re-joins the network the AP displaced, so a configured terminal is back
 * online when the page closes rather than at the next reboot.
 */
void prov_stop(void);

/** @brief Which mode is running, or @ref PROV_MODE_OFF. */
prov_mode_t prov_mode(void);

/** @brief Tell the portal which wizard step is current. */
void prov_set_step(prov_step_t step);

/** @brief The current step. */
prov_step_t prov_step(void);

/** @brief Minutes left before the portal closes itself, 0 once it has. */
unsigned prov_window_left_min(void);

/** @brief AP SSID, or "" while no portal is up. */
const char *prov_ap_ssid(void);

/** @brief AP passphrase, or "" while no portal is up. */
const char *prov_ap_pass(void);

/**
 * @brief What the panel's QR code should carry.
 *
 * "WIFI:T:WPA;S:<ssid>;P:<pass>;;" — a camera joins the AP from it and the captive
 * portal takes over, so one code is enough and there is no URL to read off the
 * panel. Empty while no portal is up.
 */
const char *prov_qr_payload(void);

/**
 * @brief Whether a browser has asked to be authorised.
 *
 * Set by @c POST /api/auth. The panel answers by taking the admin code and calling
 * @ref prov_auth_resolve. Reported as @ref UI_EVENT_PROV_AUTH.
 */
bool prov_auth_pending(void);

/**
 * @brief Answer a pending authorisation request from the panel.
 *
 * @param[in] grant true if the operator entered the correct admin code.
 */
void prov_auth_resolve(bool grant);

/** @brief Whether the browser session is authorised to change anything. */
bool prov_authed(void);

/**
 * @brief The 4-digit pairing code of the browser asking to be let in.
 *
 * Derived from the session token (its first 16 bits, mod 10000), so the page can
 * compute the same number from the token it holds. Shown on the panel's admin
 * prompt and on the page, so the operator approves the browser in their hand
 * and not whichever one on the access point asked first.
 *
 * @param[out] out      NUL-terminated code on success.
 * @param[in]  out_size >= 5.
 * @return false when no browser has asked.
 */
bool prov_pair_code(char *out, size_t out_size);

/**
 * @brief Cut the wizard down to the Wi-Fi step, with no admin code.
 *
 * For a terminal that is already configured and has only lost its network. Call it
 * right after @ref prov_start (which clears it) and before any browser arrives; the
 * first browser to ask is then let in without the panel demanding the code, and the
 * panel drops the step numbering, since steps 1 and 3-4 do not happen.
 *
 * The relaxation is bounded: the perimeter here is the AP's per-device passphrase,
 * which is on the panel in front of whoever is asking, and everything that decides
 * where money goes still has to be accepted on that panel. What it buys is an
 * operator whose till has moved venues typing a password instead of walking three
 * screens to be allowed to.
 */
void prov_set_wifi_only(void);

/** @brief Whether the portal is the cut-down Wi-Fi-only flow. */
bool prov_wifi_only(void);

/**
 * @brief Put a one-line message on the page.
 *
 * For the things only the device can know — a Wi-Fi network that would not join, a
 * step that cannot be left yet. The browser is where the operator is looking, so
 * that is where the reason has to appear; the panel is showing a QR code.
 *
 * @param[in] note Message, copied. NULL or "" clears it.
 */
void prov_set_note(const char *note);

/**
 * @brief Hand the portal a Wi-Fi scan for the browser to choose from.
 *
 * The scan itself stays on the main task — scanning makes the radio hop channels,
 * which briefly drops anyone joined to the SoftAP, so it happens deliberately at
 * known moments (entering the Wi-Fi step, or a rescan the browser asked for) and
 * never inside an HTTP handler.
 *
 * @param[in] aps Scanned networks; copied.
 * @param[in] n   How many.
 */
void prov_set_scan(const net_wifi_ap_t *aps, uint16_t n);

/**
 * @brief Fetch the value a browser proposed but nobody has accepted.
 *
 * @param[out] kind    What is being asked, may be NULL.
 * @param[out] label   Human name for the panel ("Ethereum payout"), may be NULL.
 * @param[in]  label_n Capacity of @p label.
 * @param[out] value   The proposed address, may be NULL.
 * @param[in]  value_n Capacity of @p value.
 * @return true if a proposal is pending.
 */
bool prov_pending(prov_ask_t *kind, char *label, size_t label_n,
                  char *value, size_t value_n);

/**
 * @brief Resolve a pending proposal from the panel.
 *
 * @param[in] accept true to commit it to NVS, false to discard it.
 * @return true if a value was committed — the caller then has to restart to apply
 *         it, since the recipient and contract dual stores are built at boot.
 */
bool prov_pending_commit(bool accept);

/**
 * @brief Propose a value on behalf of the panel itself.
 *
 * Used by the card-derived route: the terminal reads an address off a Cryptnox
 * card, then puts it through the very same accept-on-the-panel handshake a browser
 * submission goes through, so there is one code path that stores an address and one
 * screen that approves one.
 *
 * @param[in] kind What the value is.
 * @param[in] addr The address; checked by the caller.
 * @return false if another proposal is already waiting.
 */
bool prov_propose(prov_ask_t kind, const char *addr);

#ifdef __cplusplus
}
#endif

#endif /* PROVISION_H */
