/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ui.h
 * @ingroup ui
 * @brief Touchscreen UI API: screens, events and transaction states for the
 *        CYD (ILI9341 + XPT2046) payment flow.
 */

#ifndef UI_H
#define UI_H

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "net.h"   /* net_wifi_ap_t for the network picker */

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************
 * 2. Types
 ******************************************************************/

/** @brief Top-level screens of the payment flow. */
typedef enum {
    UI_SCREEN_SPLASH,      /**< Boot splash while WiFi/RPC/wallet come up. */
    UI_SCREEN_AMOUNT,      /**< Amount entry with +/- buttons.             */
    UI_SCREEN_CONFIRM,     /**< Amount + destination review.               */
    UI_SCREEN_PIN,         /**< Numeric keypad to enter the card PIN.      */
    UI_SCREEN_WIFI_LIST,   /**< Scanned Wi-Fi networks to pick from.       */
    UI_SCREEN_WIFI_PASS,   /**< Keyboard to enter the Wi-Fi password.      */
    UI_SCREEN_WIFI_CONNECTING, /**< "Connecting…" while main associates.   */
    UI_SCREEN_SETTINGS,    /**< Full-screen settings (tabs).               */
    UI_SCREEN_TX_STATUS,   /**< Card wait / signing / broadcast progress.  */
    UI_SCREEN_BOOT_ERROR,  /**< Startup fault — names the cause, not "Declined". */
    UI_SCREEN_ADMIN_SET,   /**< First-run creation of the admin code.      */
    UI_SCREEN_ADMIN_UNLOCK,/**< Admin code demanded before the settings.   */
    UI_SCREEN_WELCOME,     /**< Greeting opening first-run setup.          */
    UI_SCREEN_PROV,        /**< QR + AP credentials for phone-based setup. */
    UI_SCREEN_CARD_WAIT,   /**< "Tap your card" while an address is read.  */
    UI_SCREEN_TOUCH_CAL,   /**< Two-point touch calibration (Screen tab).  */
} ui_screen_t;

/** @brief Events emitted by the UI task towards the main task. */
typedef enum {
    UI_EVENT_AMOUNT_CONFIRMED,  /**< CONFIRM tapped; payload = amount.     */
    UI_EVENT_CONFIRM_OK,        /**< Send tapped on the Confirm screen.    */
    UI_EVENT_CONFIRM_CANCEL,    /**< Cancel tapped (Confirm or card wait). */
    UI_EVENT_PIN_ENTERED,       /**< PIN keypad validated; fetch via ui_take_pin. */
    UI_EVENT_WIFI_SCAN,         /**< User opened the Wi-Fi picker; main should scan. */
    UI_EVENT_WIFI_TRY,          /**< Wi-Fi creds entered; fetch via ui_take_wifi_creds. */
    UI_EVENT_TX_RETRY,          /**< New sale after Done/Failed, or Clear on
                                     Unconfirmed.                         */
    UI_EVENT_ADMIN_SET,         /**< Admin code created and stored (first run). */
    UI_EVENT_WELCOME_DONE,      /**< Start tapped on the welcome screen.   */
    UI_EVENT_PROV_AUTH,         /**< A browser asked to be authorised; the admin
                                     code has to be taken on the panel.   */
    UI_EVENT_PROV_VALUE,        /**< A payout address or token contract was
                                     proposed; needs accepting on the panel. */
    UI_EVENT_PROV_VALUE_SET,    /**< That value was accepted and stored.  */
    UI_EVENT_PROV_VALUE_NO,     /**< That value was rejected on the panel. Its own
                                     event so a caller holding a second value to
                                     offer is not left waiting on an accept that
                                     will never come.                     */
    UI_EVENT_PROV_CARD,         /**< The page asked the terminal to read the
                                     payout addresses off a Cryptnox card. */
    UI_EVENT_PROV_SCAN,         /**< The page asked for a fresh Wi-Fi scan. */
    UI_EVENT_PROV_NEXT,         /**< Continue tapped in the browser wizard. */
    UI_EVENT_PROV_FINISH,       /**< Finish tapped on the panel's last screen. */
    UI_EVENT_CARD_PIN,          /**< PIN entered for a card *read*, not a
                                     payment; fetch via ui_take_pin.      */
    UI_EVENT_OTA_STAGED,        /**< Firmware uploaded and verified; needs
                                     accepting on the panel before it boots. */
    UI_EVENT_TX_RECHECK,        /**< Recheck tapped on Unconfirmed: poll the
                                     same sale's receipt again.           */
    UI_EVENT_PROV_STOP,         /**< The UI wants the config portal down
                                     (closed card, refused/failed update,
                                     admin window over). Main calls
                                     prov_stop(): it blocks for up to ~2 s,
                                     which froze the panel on the UI task. */
} ui_event_t;

/** @brief States shown on the transaction-status screen. */
typedef enum {
    UI_TX_STATE_PLACE_CARD,  /**< Waiting for the card on the reader. */
    UI_TX_STATE_PROCESSING,  /**< Card tapped — opening the secure channel. */
    UI_TX_STATE_SIGNING,     /**< Card found, signing in progress.    */
    UI_TX_STATE_SENDING,     /**< Broadcasting the signed tx.         */
    UI_TX_STATE_CONFIRMING,  /**< Broadcast — waiting for the mined receipt. */
    UI_TX_STATE_DONE,        /**< Receipt mined with status 0x1.      */
    UI_TX_STATE_FAILED,      /**< Nothing was sent; info line says why. */
    UI_TX_STATE_UNCONFIRMED, /**< Sent, or maybe sent, and no verdict yet —
                                  info is the tx hash. Never "Declined": the
                                  payment may still land.               */
} ui_tx_state_t;

/**
 * @brief Startup faults shown on @ref UI_SCREEN_BOOT_ERROR.
 *
 * Bring-up problems, not declined payments — hence their own screen. Wording
 * lives in ui.cpp; the caller only names the fault.
 */
typedef enum {
    UI_BOOT_ERR_NFC,      /**< PN532 did not answer — wiring/power/I2C.       */
    UI_BOOT_ERR_WALLET,   /**< Reader answered, wallet layer failed to start. */
    UI_BOOT_ERR_CONFIG,   /**< A config.h address in this build is invalid.  */
} ui_boot_err_t;

/**
 * @brief Callback invoked from the UI task on user interaction.
 *
 * Runs in the UI task context — keep it short and non-blocking.
 *
 * @param[in] event   Event identifier.
 * @param[in] payload Amount in USDC base units for
 *                    @ref UI_EVENT_AMOUNT_CONFIRMED, 0 otherwise.
 */
typedef void (*ui_event_cb_t)(ui_event_t event, uint64_t payload);

/******************************************************************
 * 3. Public API
 ******************************************************************/

/**
 * @brief Initialise display + touch and start the UI task.
 *
 * @param[in] cb Event callback; must remain valid for the program lifetime.
 */
void ui_init(ui_event_cb_t cb);

/** @brief Switch to the splash screen. */
void ui_show_splash(void);

/** @brief Switch to the amount-entry screen (forces a value redraw). */
void ui_show_amount_entry(void);

/**
 * @brief Switch to the confirm screen.
 *
 * @param[in] amount_units Amount in USDC base units (6 decimals).
 * @param[in] dest_addr    "0x..."-prefixed destination address; copied
 *                         internally, may be NULL for none.
 * @param[in] fee          The network-fee ceiling as a line ("Fee up to 0.0013
 *                         ETH"), copied; NULL or "" for none.
 */
void ui_show_confirm(uint64_t amount_units, const char *dest_addr,
                     const char *fee);

/**
 * @brief Switch to the transaction-status screen.
 *
 * @param[in] state Transaction state to display.
 * @param[in] info  Optional info line (tx hash, error message); copied
 *                  internally, may be NULL for none.
 */
void ui_show_tx_status(ui_tx_state_t state, const char *info);

/**
 * @brief Replace the transaction screen's info line without rebuilding it.
 *
 * For progress on a state that lasts: the receipt poll can run for two minutes,
 * and calling @ref ui_show_tx_status per pass would restart the spinner the
 * operator is reading as "still working". No-op unless the transaction screen
 * is up in one of its spinner states.
 *
 * @param[in] info Line to show; copied internally, may be NULL to clear.
 */
void ui_set_tx_info(const char *info);

/**
 * @brief Copy the most recently entered PIN out and wipe the UI's copy.
 *
 * Call once after @ref UI_EVENT_PIN_ENTERED. The internal buffer is
 * secure-wiped on read, so a second call returns 0.
 *
 * @param[out] out  Destination buffer (NUL-terminated on return).
 * @param[in]  n    Capacity of @p out.
 * @return number of PIN digits copied (0 if none available).
 */
size_t ui_take_pin(char *out, size_t n);

/**
 * @brief Show the scanned Wi-Fi networks for the user to pick from.
 *
 * @param[in] aps  Array of scanned APs (copied internally).
 * @param[in] n    Number of entries in @p aps.
 * @param[in] note Optional one-line reason the picker (re)opened, shown above
 *                 the list; copied internally, NULL for none. Applied on every
 *                 call, so a stale note cannot survive a later render.
 */
void ui_show_wifi_list(const net_wifi_ap_t *aps, uint16_t n, const char *note);

/**
 * @brief Show a startup fault, naming the cause and what to do about it.
 *
 * @param[in] kind   Which bring-up step failed.
 * @param[in] detail Optional technical detail for a technician (an esp_err_t
 *                   name, say); copied internally, may be NULL.
 */
void ui_show_boot_error(ui_boot_err_t kind, const char *detail);

/**
 * @brief Provide the token contract and destination addresses for the confirm
 *        screen and the settings "Tx" tab (pointers stored as-is; pass
 *        static/literal storage that outlives the call).
 */
void ui_set_addresses(const char *token_contract, const char *dest_addr);

/**
 * @brief Implemented by main: repoint those two rows at the selected chain.
 *
 * The asset picker switches the chain on the UI task and rebuilds the page
 * immediately, so it asks for the new pair here rather than posting an event
 * and racing its own redraw. main owns the per-chain contract and payout
 * strings; this only reads them.
 */
void ui_refresh_addresses(void);

/**
 * @brief Same, for any chain (a pos_chain_t) rather than the one sales use.
 *
 * The admin Tx tab browses another asset's contract and payout without
 * switching what the terminal charges in.
 */
void ui_refresh_addresses_for(uint8_t chain);

/**
 * @brief Show a "Connecting to <ssid>…" screen while main associates.
 *
 * Interactive picker only; unattended boot reports through
 * @ref ui_set_boot_status and stays on the splash.
 */
void ui_show_wifi_connecting(const char *ssid);

/**
 * @brief Set the one-line progress note on the splash screen.
 *
 * Updates the splash in place instead of switching screens. Safe from the main
 * task: applied by the UI task on its next pass.
 *
 * @param[in] step Short label ("Starting NFC reader"), copied internally.
 *                 NULL or "" clears the line.
 */
void ui_set_boot_status(const char *step);

/**
 * @brief Note that the stored gas caps have changed, so the Tx tab can catch up.
 *
 * The caps are the one setting the config page writes straight through, and that
 * page is opened from a card raised over the settings screen — so the two gas
 * rows underneath keep the values they were built with until the operator leaves
 * the screen and comes back. This retexts them in place.
 *
 * Safe from the HTTP task: applied by the UI task on its next pass, and a no-op
 * when the rows are not on screen (any other screen, or Tron, which has no caps).
 */
void ui_fees_changed(void);

/**
 * @brief Note that the stored UTC offset has changed, so the clock can catch up.
 *
 * Same handoff as @ref ui_fees_changed and for the same reason: the config page
 * writes the offset straight through from the HTTP task, and the band's clock
 * caches it rather than opening NVS on every three-second tick. Without this the
 * panel keeps the old hour until something rebuilds the screen.
 *
 * Safe from the HTTP task: applied by the UI task on its next pass.
 */
void ui_clock_changed(void);

/**
 * @brief Greet the operator at the start of first-run setup, or after an update.
 *
 * Shown on a virgin or factory-reset terminal, before the Wi-Fi and admin-code
 * steps, and again on the first boot of a freshly installed image. Emits
 * @ref UI_EVENT_WELCOME_DONE when Start is tapped.
 *
 * @param[in] sub Line under the product name, copied internally. NULL keeps the
 *                first-run wording ("Let's configure your terminal.").
 */
void ui_show_welcome(const char *sub);

/**
 * @brief Run the first-run admin-code creation (enter, then confirm).
 *
 * Deliberately has no way out: everything behind the burger menu — Wi-Fi, fees,
 * factory reset — sits behind this code, so the terminal must not become usable
 * without one. Emits @ref UI_EVENT_ADMIN_SET once stored.
 */
void ui_show_admin_set(void);

/**
 * @brief Fetch the selected SSID + entered password and wipe the UI's copy.
 *
 * Call once after @ref UI_EVENT_WIFI_TRY.
 *
 * @param[out] ssid    SSID buffer (>= 33 bytes).
 * @param[in]  ssid_n  Capacity of @p ssid.
 * @param[out] pass    Password buffer (>= 65 bytes).
 * @param[in]  pass_n  Capacity of @p pass.
 * @return length of the SSID (0 if none available).
 */
size_t ui_take_wifi_creds(char *ssid, size_t ssid_n, char *pass, size_t pass_n);

/**
 * @brief Load credentials into the same handoff buffers the picker fills.
 *
 * Lets the setup page hand Wi-Fi credentials to main through the existing
 * @ref UI_EVENT_WIFI_TRY path, so a form submission and a screen tap reach the
 * connect-and-verify loop identically. The caller emits the event afterwards.
 *
 * @param[in] ssid Network name.
 * @param[in] pass Passphrase; the caller wipes its own copy.
 */
void ui_stage_wifi_creds(const char *ssid, const char *pass);

/**
 * @brief Show the setup screen: QR code, AP name and passphrase.
 *
 * The same screen at every wizard step, captioned with whichever one is current.
 * There is deliberately no "use this screen instead" escape any more: the whole
 * wizard past the admin code happens in the browser, so the panel's job here is to
 * carry the QR code and to report progress.
 *
 * @param[in] step The prov_step_t the portal is serving. Typed as int to keep
 *                 this header free of provision.h, which includes this one.
 */
void ui_show_prov(int step);

/**
 * @brief Raise the modal that asks the operator to accept a value a browser
 *        proposed, reading the pending proposal from provision.h.
 *
 * Call after @ref UI_EVENT_PROV_VALUE. Accepting emits
 * @ref UI_EVENT_PROV_VALUE_SET; rejecting emits nothing and drops the proposal, so
 * a caller waiting on the step stays where it is and can be offered another.
 */
void ui_show_prov_confirm(void);

/**
 * @brief Demand the admin code so a browser can be authorised.
 *
 * Same screen as the settings unlock, but a correct code calls
 * prov_auth_resolve(true) instead of opening the menu, and backing out refuses the
 * request rather than silently leaving the browser waiting.
 */
void ui_show_prov_auth(void);

/**
 * @brief Ask for the card PIN before reading an address off a Cryptnox card.
 *
 * The card will not export a public key without a verified PIN, so deriving a
 * payout address needs one exactly as signing does. Emits @ref UI_EVENT_CARD_PIN,
 * or @ref UI_EVENT_CONFIRM_CANCEL if the operator backs out.
 */
void ui_show_card_pin(void);

/**
 * @brief Put a failure line on the setup screen, in red under the step title.
 *
 * For the card read: a refused card, a wrong PIN or sixty seconds with no tap
 * all end with the panel back on the step it started from, and without a reason
 * there that reads as the terminal having reset itself mid-read. The browser
 * gets the same sentence through @c prov_set_note — this is the copy for the
 * person actually holding the card.
 *
 * Cleared by @ref ui_show_card_pin, so each attempt starts clean.
 *
 * @param[in] msg Message, copied. NULL or "" clears it.
 */
void ui_set_prov_note(const char *msg);

/**
 * @brief "Hold your card to the reader" while the address is read.
 *
 * Its own screen rather than the transaction one: nothing is being paid here, and
 * the tx screen's wording and its Cancel semantics both belong to a sale.
 *
 * @param[in] note Optional line under the prompt, copied internally.
 */
void ui_show_card_wait(const char *note);

/**
 * @brief Raise the modal that asks the operator to accept a firmware image the
 *        update page has uploaded, reading it from ota.h.
 *
 * Call after @ref UI_EVENT_OTA_STAGED. Accepting reboots into the new firmware
 * and does not return; declining discards the staging and leaves the running
 * firmware alone. The panel is the only place either can happen — an upload on
 * its own changes nothing about what boots.
 */
void ui_show_ota_confirm(void);

#ifdef __cplusplus
}
#endif

#endif // UI_H
