/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ui.cpp
 * @brief Touchscreen UI implementation built on LVGL 8.x.
 *
 * TFT_eSPI is used only as the low-level panel driver (the LVGL flush
 * callback pushes rendered pixels to it); XPT2046_Touchscreen feeds the LVGL
 * pointer input device. All LVGL calls happen on the single UI task — the
 * public ui_show_* entry points (called from the main task) only stage a
 * screen request + its data and raise a dirty flag, which the UI task drains.
 */

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "ui_internal.h"

const char *TAG = "ui";

/* No ambient-light sensing: the enclosure covers the LDR on GPIO 34, so any
 * reading is of the inside of the case. Brightness is the slider only. */

/******************************************************************
 * 5. Shared state (written by ui_show_* on the main task, read by ui_task)
 ******************************************************************/
ui_event_cb_t s_cb = NULL;

static volatile bool        s_screen_dirty = true;
volatile ui_screen_t s_req_screen   = UI_SCREEN_SPLASH;

/* Amount entry — parsed from the keypad string; starts empty (0). */
uint64_t s_amount_units = 0ULL;

/* Confirm-screen payload */
uint64_t s_confirm_amount = 0ULL;
char     s_confirm_addr[64] = "";
char     s_confirm_fee[40]  = "";   /* "Fee up to 0.0013 ETH", or "" */

/* Tx-status payload. The info line is retexted in place via s_tx_info_dirty
 * (the confirmation countdown): a rebuild per tick would restart the spinner. */
ui_tx_state_t s_tx_state    = UI_TX_STATE_PLACE_CARD;
char          s_tx_info[72] = "";   /* holds a 66-char EVM hash */
lv_obj_t     *s_tx_info_lbl = NULL;
static volatile bool s_tx_info_dirty = false;
uint8_t  s_cal_step = 0U;
int16_t  s_cal_raw[2][2] = {{0, 0}, {0, 0}};
bool     s_cal_pressed = false;
int16_t  s_cal_last_x = 0;
int16_t  s_cal_last_y = 0;
static uint16_t s_cal_prev[4] = {0, 0, 0, 0};   /* restored on cancel/timeout */
uint32_t s_cal_deadline = 0U;
lv_obj_t *s_cal_countdown = NULL;

/* The sheet that rises on a swipe up. While set, build_admin_screen() builds
 * into it and does NOT clear the screen, so the sale screen stays underneath.
 * Valid only for one dispatch in render_requested_screen(). */
lv_obj_t     *s_sheet         = NULL;
volatile bool s_sheet_pending = false;

/* The card-wait note. Its own buffer, not the transaction screen's, so a setup
 * message cannot turn up under the spinner on a payment. */
char          s_card_note[64] = "";

/* Amount entry — keypad input string (e.g. "12.50") and its display label. The
 * cents are their own label so they can be set in a smaller font past 100; below
 * that they stay in the main label and this one holds "". */
lv_obj_t *s_amount_label = NULL;
lv_obj_t *s_amount_cents_label = NULL;
/* The flex row the two of them sit in — kept because its placement is not
 * fixed: see amount_row_place(). */
lv_obj_t *s_amount_row = NULL;
uint64_t  s_amount_cents = 0;      /* amount entered, in cents          */

/* The asset selector on the amount row, and the chevron it drops when it has to
 * make room. Both die with the screen — cleared in clear_screen(). */
lv_obj_t *s_asset_btn   = NULL;
lv_obj_t *s_asset_arrow = NULL;

/* The Charge button, kept because it is enabled and disabled as digits arrive
 * and leave — see charge_set_enabled(). */
lv_obj_t *s_charge_btn  = NULL;

/* The status band's clock. Built by build_page(), so only on the sale flow (the
 * admin tab bar and setup headers own that band). Retexted by the status timer. */
lv_obj_t *s_clock_lbl   = NULL;
/* Whether Charge was tapped and is waiting on main. A flag, not the button's
 * state: the keypad stays live and amount_update_display() would otherwise
 * re-enable it on the next digit — see charge_set_busy(). */
bool      s_charge_busy = false;

/* PIN entry — the textarea (password mode) is the live input; s_pin is the
 * handoff buffer read by main via ui_take_pin() and wiped on read. */
lv_obj_t *s_pin_ta      = NULL;
char      s_pin[16]     = {0};
uint8_t   s_pin_len     = 0;
net_wifi_ap_t s_aps[WIFI_MAX_APS];
uint16_t      s_ap_count = 0;
char          s_wifi_ssid[33] = {0};   /* selected network          */
char          s_wifi_pass[65] = {0};   /* entered passphrase (handoff) */
char          s_wifi_note[64] = {0};   /* why the picker reopened (may be empty) */
lv_obj_t     *s_wifi_pass_ta  = NULL;
lv_obj_t     *s_wifi_eye_lbl  = NULL;   /* glyph swapped on reveal/hide */
lv_obj_t     *s_pin_eye_lbl   = NULL;   /* same, on the card-PIN keypad */
lv_obj_t     *s_admin_eye_lbl = NULL;   /* same, on the admin-code keypad */

/* Progress screen (UI_SCREEN_WIFI_CONNECTING), two pieces rather than one
 * preformatted line: the name is stored raw so the label elides it by real
 * glyph width, which no character budget can do for every SSID. */
char          s_wifi_caption[24] = {0};  /* "Scanning..." / "Connecting to" */
char          s_wifi_name[33]    = {0};  /* network name; empty for none    */

/* Splash progress line. Written by the main task, applied by the UI task
 * (LVGL is not thread-safe) — same hand-off shape as request_screen(). */
char          s_boot_step[40]     = {0};
lv_obj_t     *s_boot_step_lbl     = NULL;
static volatile bool s_boot_step_dirty   = false;

/* The Tx tab's two gas rows, same hand-off. The config page (HTTP task) writes
 * the caps while its card is raised over this screen, so the rows are retexted
 * in place: a rebuild would take that card down. */
lv_obj_t     *s_fee_max_lbl       = NULL;
lv_obj_t     *s_fee_prio_lbl      = NULL;
static volatile bool s_fees_dirty        = false;

/* Phone setup (UI_SCREEN_PROV). An int, not a prov_step_t: provision.h
 * includes ui.h. Written by the main task, read by the UI task. */
volatile int  s_prov_step         = 0;
/* Why the last card read came to nothing, shown on the setup screen (the
 * browser gets it too, but the person who tapped is looking at the panel).
 * Cleared whenever a fresh read starts. */
char          s_prov_msg[64]      = "";
static volatile bool s_addr_modal_dirty  = false;
/* Firmware uploaded from a browser and waiting to be accepted here. Same
 * handoff as the address modal above: the HTTP task never touches LVGL. */
static volatile bool s_ota_modal_dirty   = false;

/* Startup fault (UI_SCREEN_BOOT_ERROR). */
ui_boot_err_t s_boot_err            = UI_BOOT_ERR_NFC;
char          s_boot_detail[64]     = {0};
lv_obj_t     *s_admin_ta      = NULL;
lv_obj_t     *s_admin_note_lbl = NULL;
char          s_admin_first[ADMIN_CODE_MAX + 1] = {0};  /* 1st of 2 passes */
char          s_admin_note[48] = {0};
bool          s_admin_confirming = false;   /* 2nd pass of the creation */
static bool          s_welcome_sent     = false;   /* Start already reported */
char          s_welcome_sub[96]  = {0};     /* line under the brand: fits main's update greeting */
/* Penalty clock, monotonic since boot (lv_tick_elaps handles the wrap). The
 * attempt count itself lives in NVS, so power-cycling shortens the current wait
 * but never resets the escalation. */
uint32_t      s_admin_lock_start = 0;
uint32_t      s_admin_lock_ms    = 0;
/* What a correct code on the unlock screen is for (admin panel or portal). One
 * screen serves both so the lockout cannot drift between two copies. */
bool          s_admin_for_portal = false;

/* Whether the PIN keypad is collecting a card PIN for a *read* (deriving a payout
 * address) rather than for a payment. Same reason as above: the card refuses to
 * export a public key without a verified PIN, so the screen is the same one. */
bool          s_pin_for_card     = false;

/* Destination info shown on the settings "Tx" tab (set by main, static). */
const char *s_addr_usdc = NULL;
const char *s_addr_dest = NULL;

/* Settings bottom-bar buttons (Reset shares the line with Close on About). */
lv_obj_t *s_reset_btn = NULL;
lv_obj_t *s_close_btn = NULL;

/* The networks the picker offers are pos_net_t (assets.h); several chains share
 * one (USDC and USDT are both Ethereum). */

static ui_screen_t s_settings_return = UI_SCREEN_AMOUNT;   /* screen to go back to */
/* Which tab a (re)built settings page opens on. Zeroed on a fresh open, kept
 * across rebuilds from inside the page (an asset pick returns to the Tx tab). */
uint16_t s_settings_tab = 0U;

/* The sale's chain is read from NVS wherever needed; there is no UI-side copy. */
/* The asset the admin Tx tab is showing. Its own, not the sale's: looking up
 * another asset there must not change what the next customer is charged in.
 * Seeded from the sale's asset on each fresh open. */
pos_chain_t s_view_chain = POS_CHAIN_ETH_USDC;

/** An asset's row — ticker, standard, caption, network. Default: the sale's. */
const pos_asset_t *asset(pos_chain_t c) {
    return pos_asset_of(c);
}

/** Ticker of the asset being charged, for the selector and the amount screens. */
const char *asset_name(pos_chain_t c) {
    return asset(c)->ticker;
}

/**
 * Which network that asset lives on — the selector's subtitle.
 *
 * Names the deployment, not just the family ("Ethereum" vs "Ethereum Sepolia"):
 * it decides whether a sale settles in real money.
 */
const char *asset_network(pos_chain_t c) {
    const pos_net_info_t *ni = pos_net_info(asset(c)->net);
    return settings_net_str(ni->long_test, ni->long_main);
}

/** Caption for the address row above "Send to": TRX has no contract to show. */
const char *asset_caption(pos_chain_t c) {
    return asset(c)->caption;
}

/* The buffers above are written by the main task (and a few by the HTTP task)
 * through the public ui_* calls, and read by the UI task while it builds a
 * screen; without a lock a screen could show a torn message or Wi-Fi record.
 * The UI task holds this for each pass of its loop (render, the targeted label
 * updates and lv_timer_handler with its event callbacks), and every public call
 * that writes shared state holds it for the copy. Recursive: an event callback
 * that reaches back into a public ui_* call on the UI task already owns it.
 * Writers wait for at most one pass of the loop, a few milliseconds. */
static SemaphoreHandle_t s_ui_mx = NULL;

struct UiLock {
    UiLock()  { if (s_ui_mx != NULL) { (void)xSemaphoreTakeRecursive(s_ui_mx, portMAX_DELAY); } }
    ~UiLock() { if (s_ui_mx != NULL) { (void)xSemaphoreGiveRecursive(s_ui_mx); } }
    UiLock(const UiLock &) = delete;
    UiLock &operator=(const UiLock &) = delete;
};

/* prov_stop() takes up to ~2 s, so main runs it, not the UI task: the UI only
 * asks. Rate-limited to once a second, since the deadline check would otherwise
 * ask every 5 ms; a request lost to a full queue is simply asked again. */
static uint32_t s_prov_stop_at = 0U;

static void request_prov_stop(void) {
    if ((s_prov_stop_at != 0U) && (lv_tick_elaps(s_prov_stop_at) < 1000U)) { return; }
    s_prov_stop_at = lv_tick_get() | 1U;   /* never 0 once asked */
    if (s_cb != NULL) { s_cb(UI_EVENT_PROV_STOP, 0); }
}
void request_screen(ui_screen_t s) {
    s_req_screen   = s;
    s_screen_dirty = true;
}

/* Sets both pieces at once, so no caller can leave a stale name behind. */
void set_wifi_progress(const char *caption, const char *name) {
    strncpy(s_wifi_caption, (caption != NULL) ? caption : "",
            sizeof(s_wifi_caption) - 1);
    s_wifi_caption[sizeof(s_wifi_caption) - 1] = '\0';
    strncpy(s_wifi_name, (name != NULL) ? name : "", sizeof(s_wifi_name) - 1);
    s_wifi_name[sizeof(s_wifi_name) - 1] = '\0';
}

/* amount_format, AMOUNT_CENTS_MAX(_NATIVE) and the keypad arithmetic are in
 * money.h, where test_money can reach them. */

/**
 * Ceiling on what the keypad will accept, for the asset currently selected.
 *
 * ETH and POL are 18-decimal and the signed value is a uint64 of wei, so a sale
 * stops at 18.44 of either (see POS_AMOUNT_UNITS_MAX_NATIVE). Enforced at the
 * keypad rather than at the confirm step, so the mistake is never keyed.
 */
uint64_t amount_cents_max(void) {
    return amount_cents_cap(pos_chain_is_native_evm(settings_get_chain()));
}

/**
 * @brief Open the admin panel's front door — the code screen, not the panel.
 *
 * One lock for the whole menu: Wi-Fi, fee caps and the factory reset are all
 * merchant operations, and the reset in particular must not be one tap away
 * from a customer left alone with the terminal.
 *
 * No code stored means first-run setup has not finished, so the request is
 * ignored rather than let through (reachable by backing out of the first-run
 * Wi-Fi picker onto the amount screen).
 *
 * Called from the swipe gesture, which arms the full lockout penalty: a gesture
 * that skipped it would be a cheaper way in.
 */
static void open_admin_entry(void) {
    s_settings_return = s_req_screen;   /* remember where we came from */
    if (!settings_has_admin_code()) { return; }
    s_settings_tab     = 0U;   /* a fresh open starts on Screen */
    s_view_chain       = settings_get_chain();
    s_admin_confirming = false;
    s_admin_for_portal = false;
    s_admin_note[0]    = '\0';
    /* Re-arm the wait from the persisted attempt count. The wait itself lives
     * in RAM — persisting a deadline would need a trustworthy absolute clock,
     * and the wall clock is what an attacker on the network can move. Deriving
     * it from the NVS count makes the escalation survive reboots, so a power
     * cycle cannot cut the cost of a guess to one reboot. */
    s_admin_lock_ms    = admin_penalty_ms(settings_admin_fail_count());
    s_admin_lock_start = lv_tick_get();
    /* Arrive as a sheet. Set here rather than at the gesture, so the early
     * return above cannot leave the flag armed for an unrelated visit. */
    s_sheet_pending = true;
    request_screen(UI_SCREEN_ADMIN_UNLOCK);
}

/* Runs on the UI task (inside lv_timer_handler), so touching shared state and
 * invoking s_cb (which only posts to a queue) is safe here. */
void btn_event_cb(lv_event_t *e) {
    BtnAction act = static_cast<BtnAction>(
        reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));

    /* The coin rows carry their chain in the action itself, so adding an asset is
     * a row in the picker's table and nothing here. */
    if ((act >= ACT_CHAIN_BASE) && (act < ACT_CHAIN_OF(POS_CHAIN__COUNT))) {
        const pos_chain_t picked = static_cast<pos_chain_t>(act - ACT_CHAIN_BASE);
        /* From the admin Tx tab the pick only changes what that tab shows. */
        if (s_req_screen == UI_SCREEN_SETTINGS) {
            s_view_chain = picked;
            close_modal();
            request_screen(UI_SCREEN_SETTINGS);   /* rebuild repoints the rows */
            return;
        }
        settings_set_chain(picked);
        /* The entered amount outlives the picker, so switching to an 18-decimal
         * coin can leave a figure the new asset cannot carry. Clamp it to the
         * new ceiling. */
        if (s_amount_cents > amount_cents_max()) {
            s_amount_cents = amount_cents_max();
        }
        close_modal();
        /* Repoint the contract and payout strings before the rebuild reads
         * them, or the Tx tab shows the previous asset's contract and, across
         * the Ethereum/Tron divide, the previous network's payout address. */
        ui_refresh_addresses();
        /* Rebuild the screen the picker was opened from (s_req_screen: a modal
         * is drawn over it, never instead of it). The amount survives; it lives
         * in s_amount_cents, not in the widgets. */
        request_screen(s_req_screen);
        return;
    }

    switch (act) {
        case ACT_CONFIRM:
            if (s_cb != NULL && s_amount_units > 0ULL) {
                /* Before the callback, not after: main may answer at once or
                 * after a network round trip, and the screen must stop taking
                 * taps before either. */
                charge_set_busy();
                s_cb(UI_EVENT_AMOUNT_CONFIRMED, s_amount_units);
            }
            break;
        case ACT_CANCEL:
            if (s_cb != NULL) { s_cb(UI_EVENT_CONFIRM_CANCEL, 0); }
            break;
        case ACT_SEND:
            /* Collect the PIN on the keypad screen before signing. Cleared
             * explicitly, so a card read abandoned by any route cannot leave the
             * payment keypad reporting the wrong event. */
            s_pin_for_card = false;
            request_screen(UI_SCREEN_PIN);
            break;
        case ACT_PIN_CANCEL:
            CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_pin), sizeof(s_pin));
            s_pin_len = 0;
            if (s_pin_for_card) {
                /* Reported so the waiting main task stops waiting; the card read
                 * has no confirm screen to fall back to. */
                s_pin_for_card = false;
                if (s_cb != NULL) { s_cb(UI_EVENT_CONFIRM_CANCEL, 0); }
                request_screen(UI_SCREEN_PROV);
            } else {
                request_screen(UI_SCREEN_CONFIRM);
            }
            break;
        case ACT_NEW:
            if (s_cb != NULL) { s_cb(UI_EVENT_TX_RETRY, 0); }
            break;
        case ACT_TX_RECHECK:
            if (s_cb != NULL) { s_cb(UI_EVENT_TX_RECHECK, 0); }
            break;
        case ACT_WELCOME_OK:
            /* Guarded: request_screen only takes effect on the UI task's next
             * pass, so a double tap could otherwise emit twice. */
            if (!s_welcome_sent) {
                s_welcome_sent = true;
                if (s_cb != NULL) { s_cb(UI_EVENT_WELCOME_DONE, 0); }
            }
            break;
        case ACT_ADMIN_CANCEL:
            CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_admin_first),
                                  sizeof(s_admin_first));
            if (s_admin_for_portal) {
                /* Refuse the browser rather than leave it polling "waiting for
                 * the admin code". Back to the QR screen in wizard mode. */
                prov_auth_resolve(false);
                s_admin_for_portal = false;
                request_screen((prov_mode() == PROV_MODE_WIZARD)
                               ? UI_SCREEN_PROV : UI_SCREEN_AMOUNT);
            } else {
                request_screen(UI_SCREEN_AMOUNT);
            }
            break;
        case ACT_CLOSE:
            settings_persist();
            request_screen(s_settings_return);
            break;
        case ACT_TOUCH_CAL:
            settings_persist();
            /* Keep what is in force, so a cancel — or an operator who cannot
             * hit anything — gets the working panel back. */
            s_cal_prev[0] = s_cal_xmin; s_cal_prev[1] = s_cal_xmax;
            s_cal_prev[2] = s_cal_ymin; s_cal_prev[3] = s_cal_ymax;
            s_cal_step    = 0U;
            s_cal_pressed = false;
            request_screen(UI_SCREEN_TOUCH_CAL);
            break;
        case ACT_CAL_SAVE:
            settings_set_touch_cal(s_cal_xmin, s_cal_xmax,
                                   s_cal_ymin, s_cal_ymax);
            /* Read back what was actually stored: settings_set_touch_cal
             * refuses a collapsed span, and the panel must then keep running
             * on the numbers in NVS rather than the ones it rejected. */
            touch_cal_load();
            request_screen(UI_SCREEN_SETTINGS);
            break;
        case ACT_CAL_CANCEL:
            s_cal_xmin = s_cal_prev[0]; s_cal_xmax = s_cal_prev[1];
            s_cal_ymin = s_cal_prev[2]; s_cal_ymax = s_cal_prev[3];
            request_screen(UI_SCREEN_SETTINGS);
            break;
        case ACT_WIFI:
            settings_persist();
            set_wifi_progress("Scanning...", NULL);
            request_screen(UI_SCREEN_WIFI_CONNECTING);
            if (s_cb != NULL) { s_cb(UI_EVENT_WIFI_SCAN, 0); }
            break;
        case ACT_WIFI_CANCEL:
            CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_wifi_pass),
                                  sizeof(s_wifi_pass));
            request_screen(UI_SCREEN_AMOUNT);
            break;
        case ACT_WIFI_PASS_REVEAL:
            code_field_reveal(s_wifi_pass_ta, s_wifi_eye_lbl);
            break;
        case ACT_PIN_REVEAL:
            code_field_reveal(s_pin_ta, s_pin_eye_lbl);
            break;
        case ACT_ADMIN_REVEAL:
            code_field_reveal(s_admin_ta, s_admin_eye_lbl);
            break;
        case ACT_RESET:
            open_reset_confirm();            /* ask before wiping */
            break;
        case ACT_RESET_CONFIRM:
            /* Wipe stored settings (brightness, auto, Wi-Fi) and reboot — the
             * device comes back up into first-run Wi-Fi setup. */
            settings_factory_reset();
            esp_restart();
            break;
        case ACT_MODAL_CLOSE:
            close_modal();
            break;
        case ACT_PROV_OK:
        case ACT_PROV_NO:
            /* The panel is the only place a payout address or token contract
             * proposed by the browser can be accepted. Commit (or drop) before
             * closing, so it cannot outlive the card. */
            (void)prov_pending_commit(act == ACT_PROV_OK);
            close_modal();
            break;
        case ACT_PROV_FINISH:
            if (s_cb != NULL) { s_cb(UI_EVENT_PROV_FINISH, 0); }
            break;
        case ACT_PORTAL:
            open_portal_window();
            break;
        case ACT_PORTAL_CLOSE:
            /* Closing the card closes the page: a config endpoint should not
             * outlive the operator standing in front of the terminal. */
            request_prov_stop();
            close_modal();
            break;
        case ACT_OTA_NO:
            /* Refused. The staging is dropped and the page goes with it, so a
             * declined image cannot be re-offered to whoever wanders past next. */
            close_modal();
            (void)ota_commit(false);
            request_prov_stop();
            break;
        case ACT_OTA_OK:
            /* The panel is the only place firmware can be installed; the browser
             * that uploaded it only got as far as this modal. Does not return on
             * success — it reboots into the new slot. */
            close_modal();
            if (!ota_commit(true)) {
                /* Nothing to install: the terminal rebooted since the upload
                 * (staging is RAM), or the slot refused to become bootable. Say
                 * so — a card that just closes looks like a silent success. */
                request_prov_stop();
                open_ota_gone();
            }
            break;
        case ACT_NET_PICK:
            open_network_picker();
            break;
        case ACT_NET_ETH:
        case ACT_NET_POLY:
        case ACT_NET_TRON:
            /* Step 2: which coin on the network just picked. */
            open_coin_picker((act == ACT_NET_TRON) ? POS_NET_TRON :
                             (act == ACT_NET_POLY) ? POS_NET_POLY : POS_NET_ETH);
            break;
        case ACT_CHAIN_BASE:
            /* Unreachable — the chain block above returns. Named only so -Wswitch
             * keeps checking that every other action still has a case here. */
            break;
    }
}

/******************************************************************
 * 9. UI task — owns LVGL init and the handler loop
 ******************************************************************/
static void ui_task(void *arg) {
    (void)arg;

    lv_init();

    tft.init();
    tft.setRotation(0);          /* portrait, 240x320 */
    tft.invertDisplay(true);     /* CYD ILI9341 panel renders inverted otherwise */

    /* CYD "milky gamma" fix: the 1-USB ILI9341_2 panels' gamma curve bands
     * anti-aliased greys. Re-select a built-in curve via GAMMASET (0x26) for
     * a clean ramp. See TFT_eSPI discussion #3018. */
    tft.writecommand(0x26);      /* GAMMASET */
    tft.writedata(0x02);
    delay(120);
    tft.writecommand(0x26);
    tft.writedata(0x01);

    /* White, not black: the backlight comes on before LVGL's first frame and
     * the theme is white, so black would flash at power-on. */
    tft.fillScreen(TFT_WHITE);

    touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
    touch.begin(touchSPI);
    touch.setRotation(0);        /* match the panel orientation */
    touch_cal_load();            /* per-unit edge counts, before the first read */

    /* Take over the backlight pin with LEDC PWM (after tft.init has touched
     * it) so brightness is dimmable from the settings menu. Restore the saved
     * level from NVS (defaults to 80% if never set). */
    s_brightness = settings_get_brightness();
    backlight_init(s_brightness);

    lv_disp_draw_buf_init(&s_draw_buf, s_buf, NULL, SCR_W * 40);
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = SCR_W;
    s_disp_drv.ver_res  = SCR_H;
    s_disp_drv.flush_cb = disp_flush;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    /* After the display exists (a theme belongs to one) and before the first
     * screen is built — lv_theme_apply runs at object creation, so anything
     * created earlier would keep the default look. */
    theme_init();
    signal_init();               /* top-right Wi-Fi bars, on the top layer */

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = indev_read;
    lv_indev_drv_register(&s_indev_drv);

    const esp_timer_create_args_t targs = {
        .callback        = &tick_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "lv_tick",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t th;
    if (esp_timer_create(&targs, &th) == ESP_OK) {
        (void)esp_timer_start_periodic(th, LV_TICK_PERIOD_MS * 1000);
    }

    ESP_LOGI(TAG, "UI initialized (LVGL %d.%d + TFT_eSPI/XPT2046)",
             lv_version_major(), lv_version_minor());

    /* On the task watchdog: a render or an event callback that never comes
     * back (a wedged SPI transfer, a touch read stuck on the bus) resets the
     * terminal instead of leaving a frozen panel in front of a customer. */
    if (esp_task_wdt_add(NULL) != ESP_OK) {
        ESP_LOGE(TAG, "UI loop not on the task watchdog");
    }

    while (true) {
        /* Sleep first, unlocked — the writers' window — then one pass of the
         * loop under the lock, released when the pass ends. See s_ui_mx. */
        vTaskDelay(pdMS_TO_TICKS(5));
        wdt_feed();
        UiLock lk;
        if (s_screen_dirty) {
            s_screen_dirty = false;
            render_requested_screen();
        }
        /* Boot-status update from the main task; targeted rather than a splash
         * rebuild, which would restart the logo on every step. */
        if (s_boot_step_dirty) {
            s_boot_step_dirty = false;
            if ((s_req_screen == UI_SCREEN_SPLASH) && (s_boot_step_lbl != NULL)) {
                lv_label_set_text(s_boot_step_lbl, s_boot_step);
            }
        }
        /* Swipe up from the bottom edge, raised by indev_read. Gated to the
         * amount screen: anywhere else (mid-sale, or over the admin panel
         * itself) the gesture is dropped. */
        if (s_swipe_admin) {
            s_swipe_admin = false;
            if (s_req_screen == UI_SCREEN_AMOUNT) { open_admin_entry(); }
        }
        /* Touch calibration: corner taps are read raw, and the old values come
         * back if nobody confirms in time — an operator who cannot hit Keep
         * cannot hit Discard either. */
        if (s_req_screen == UI_SCREEN_TOUCH_CAL) {
            touch_cal_poll();
            if ((s_cal_step >= 2U) && (s_cal_countdown != NULL)) {
                int32_t left = (int32_t)(s_cal_deadline - lv_tick_get());
                if (left <= 0) {
                    s_cal_xmin = s_cal_prev[0]; s_cal_xmax = s_cal_prev[1];
                    s_cal_ymin = s_cal_prev[2]; s_cal_ymax = s_cal_prev[3];
                    request_screen(UI_SCREEN_SETTINGS);
                } else {
                    char c[40];
                    snprintf(c, sizeof(c), "Reverting in %d s",
                             (int)((left + 999) / 1000));
                    lv_label_set_text(s_cal_countdown, c);
                }
            }
        }
        /* Progress on the transaction screen, targeted so the spinner does not
         * restart each poll. The label only exists on the spinner states. */
        if (s_tx_info_dirty) {
            s_tx_info_dirty = false;
            if ((s_req_screen == UI_SCREEN_TX_STATUS) && (s_tx_info_lbl != NULL)) {
                lv_label_set_text(s_tx_info_lbl, s_tx_info);
            }
        }
        /* Gas caps stored from the config page. The labels exist only on the
         * Tx tab (not on Tron); other screens read the caps when built. */
        if (s_fees_dirty) {
            s_fees_dirty = false;
            if ((s_fee_max_lbl != NULL) && (s_fee_prio_lbl != NULL)) {
                char f[16];
                snprintf(f, sizeof(f), "%u",
                         static_cast<unsigned>(settings_get_max_fee_gwei()));
                lv_label_set_text(s_fee_max_lbl, f);
                snprintf(f, sizeof(f), "%u",
                         static_cast<unsigned>(settings_get_priority_fee_gwei()));
                lv_label_set_text(s_fee_prio_lbl, f);
            }
        }
        /* A value proposed from the config page. Built here, on the UI task, and
         * after the screen render above — a modal raised straight from the main or
         * HTTP task would be touching LVGL from two tasks at once. */
        if (s_addr_modal_dirty) {
            s_addr_modal_dirty = false;
            build_prov_confirm();
        }
        /* Same handoff for firmware uploaded from the config page. */
        if (s_ota_modal_dirty) {
            s_ota_modal_dirty = false;
            build_ota_confirm();
        }
        /* The admin page closes on its own deadline, checked here because it
         * may touch LVGL. Deliberately NOT gated on the card still being up: a
         * config server outliving its window because nobody was looking at the
         * card is what the window prevents. Wizard mode has no deadline
         * (prov_window_left_min() is 0 there), hence the mode test. */
        /* ...and not while a firmware image is arriving: stopping httpd would
         * drop the upload. This cannot hold the page open for ever: ota_post()
         * gives up on a socket that has gone quiet (UPLOAD_MAX_STALLS). */
        if ((prov_mode() == PROV_MODE_ADMIN) && (prov_window_left_min() == 0U) &&
            !ota_receiving()) {
            request_prov_stop();
            /* ...but NOT the firmware card (build_ota_confirm also sets
             * s_portal_modal). The window covers the config *page*; a verified
             * image waiting on this screen needs no page to install. */
            if (s_portal_modal && !ota_staged(NULL, 0U, NULL)) { close_modal(); }
        }
        lv_timer_handler();
    }
}

/******************************************************************
 * 10. Public API
 ******************************************************************/
extern "C" void ui_init(ui_event_cb_t cb) {
    s_ui_mx        = xSemaphoreCreateRecursiveMutex();   /* before the task */
    s_cb           = cb;
    s_req_screen   = UI_SCREEN_SPLASH;
    s_screen_dirty = true;
    /* LVGL rendering + nested event callbacks (tabview/modal) + NVS calls
     * are stack-heavy; give the task plenty of headroom. */
    xTaskCreate(ui_task, "ui", 16384, NULL, 4, NULL);
}

extern "C" void ui_show_splash(void) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    request_screen(UI_SCREEN_SPLASH);
}

extern "C" void ui_show_amount_entry(void) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_amount_cents = 0U;      /* fresh entry each time */
    s_amount_units = 0U;
    request_screen(UI_SCREEN_AMOUNT);
}

extern "C" void ui_show_confirm(uint64_t amount_units, const char *dest_addr,
                                const char *fee) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_confirm_amount = amount_units;
    (void)snprintf(s_confirm_fee, sizeof(s_confirm_fee), "%s",
                   (fee != NULL) ? fee : "");
    if (dest_addr != NULL) {
        strncpy(s_confirm_addr, dest_addr, sizeof(s_confirm_addr) - 1);
        s_confirm_addr[sizeof(s_confirm_addr) - 1] = '\0';
    } else {
        s_confirm_addr[0] = '\0';
    }
    request_screen(UI_SCREEN_CONFIRM);
}

extern "C" size_t ui_take_pin(char *out, size_t n) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    if ((out == NULL) || (n == 0U)) { return 0U; }
    size_t len = s_pin_len;
    if (len > (n - 1U)) { len = n - 1U; }
    (void)CW_Utils::safe_memcpy(reinterpret_cast<uint8_t *>(out), n,
                                reinterpret_cast<const uint8_t *>(s_pin), len);
    out[len] = '\0';
    /* Handed off — wipe the UI's copy of the PIN. */
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_pin), sizeof(s_pin));
    s_pin_len = 0;
    return len;
}

extern "C" void ui_show_wifi_list(const net_wifi_ap_t *aps, uint16_t n,
                                  const char *note) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_ap_count = (n > WIFI_MAX_APS) ? WIFI_MAX_APS : n;
    for (uint16_t i = 0U; i < s_ap_count; i++) {
        s_aps[i] = aps[i];
    }
    /* Set on every call, so an earlier failure's note cannot linger. */
    if (note != NULL) {
        strncpy(s_wifi_note, note, sizeof(s_wifi_note) - 1);
        s_wifi_note[sizeof(s_wifi_note) - 1] = '\0';
    } else {
        s_wifi_note[0] = '\0';
    }
    request_screen(UI_SCREEN_WIFI_LIST);
}

extern "C" void ui_set_addresses(const char *token_contract, const char *dest_addr) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_addr_usdc = token_contract;
    s_addr_dest = dest_addr;
}

extern "C" void ui_show_wifi_connecting(const char *ssid) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    set_wifi_progress("Connecting to", ssid);
    request_screen(UI_SCREEN_WIFI_CONNECTING);
}

extern "C" void ui_set_boot_status(const char *step) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    strncpy(s_boot_step, (step != NULL) ? step : "", sizeof(s_boot_step) - 1);
    s_boot_step[sizeof(s_boot_step) - 1] = '\0';
    s_boot_step_dirty = true;   /* applied by the UI task — LVGL is single-thread */
}

extern "C" void ui_fees_changed(void) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_fees_dirty = true;   /* applied by the UI task — LVGL is single-thread */
}

extern "C" void ui_clock_changed(void) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    /* Only the cache is invalidated; the status timer retexts the label on the
     * UI task within three seconds. */
    s_tz_dirty = true;
}

extern "C" void ui_show_boot_error(ui_boot_err_t kind, const char *detail) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_boot_err = kind;
    if (detail != NULL) {
        strncpy(s_boot_detail, detail, sizeof(s_boot_detail) - 1);
        s_boot_detail[sizeof(s_boot_detail) - 1] = '\0';
    } else {
        s_boot_detail[0] = '\0';
    }
    request_screen(UI_SCREEN_BOOT_ERROR);
}

extern "C" void ui_show_welcome(const char *sub) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    strncpy(s_welcome_sub,
            ((sub != NULL) && (sub[0] != '\0')) ? sub
                                                : "Let's configure your terminal.",
            sizeof(s_welcome_sub) - 1U);
    s_welcome_sub[sizeof(s_welcome_sub) - 1U] = '\0';
    s_welcome_sent = false;
    request_screen(UI_SCREEN_WELCOME);
}

extern "C" void ui_show_admin_set(void) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_admin_confirming = false;
    s_admin_note[0]    = '\0';
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_admin_first),
                          sizeof(s_admin_first));
    request_screen(UI_SCREEN_ADMIN_SET);
}

extern "C" size_t ui_take_wifi_creds(char *ssid, size_t ssid_n,
                                     char *pass, size_t pass_n) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    if ((ssid == NULL) || (pass == NULL) || (ssid_n == 0U) || (pass_n == 0U)) {
        return 0U;
    }
    strncpy(ssid, s_wifi_ssid, ssid_n - 1U);
    ssid[ssid_n - 1U] = '\0';
    strncpy(pass, s_wifi_pass, pass_n - 1U);
    pass[pass_n - 1U] = '\0';
    /* Wipe the UI's copy of the passphrase. */
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_wifi_pass), sizeof(s_wifi_pass));
    return strlen(ssid);
}

extern "C" void ui_stage_wifi_creds(const char *ssid, const char *pass) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    strncpy(s_wifi_ssid, (ssid != NULL) ? ssid : "", sizeof(s_wifi_ssid) - 1U);
    s_wifi_ssid[sizeof(s_wifi_ssid) - 1U] = '\0';
    strncpy(s_wifi_pass, (pass != NULL) ? pass : "", sizeof(s_wifi_pass) - 1U);
    s_wifi_pass[sizeof(s_wifi_pass) - 1U] = '\0';
}

extern "C" void ui_show_prov(int step) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_prov_step = step;
    request_screen(UI_SCREEN_PROV);
}

extern "C" void ui_show_prov_confirm(void) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_addr_modal_dirty = true;
}

extern "C" void ui_show_prov_auth(void) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_admin_for_portal = true;
    s_admin_confirming = false;
    s_admin_note[0]    = '\0';
    /* Re-arm the wait from the persisted attempt count, exactly as the swipe
     * does: same code, same guessing budget, so not a cheaper door. */
    s_admin_lock_ms    = admin_penalty_ms(settings_admin_fail_count());
    s_admin_lock_start = lv_tick_get();
    request_screen(UI_SCREEN_ADMIN_UNLOCK);
}

extern "C" void ui_show_card_pin(void) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_pin_for_card = true;
    s_prov_msg[0]  = '\0';   /* a new attempt starts; drop the last one's reason */
    request_screen(UI_SCREEN_PIN);
}

extern "C" void ui_set_prov_note(const char *msg) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    strncpy(s_prov_msg, (msg != NULL) ? msg : "", sizeof(s_prov_msg) - 1);
    s_prov_msg[sizeof(s_prov_msg) - 1] = '\0';
}

extern "C" void ui_show_card_wait(const char *note) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    strncpy(s_card_note, (note != NULL) ? note : "", sizeof(s_card_note) - 1);
    s_card_note[sizeof(s_card_note) - 1] = '\0';
    request_screen(UI_SCREEN_CARD_WAIT);
}

extern "C" void ui_show_ota_confirm(void) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_ota_modal_dirty = true;
}

extern "C" void ui_show_tx_status(ui_tx_state_t state, const char *info) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    s_tx_state = state;
    if (info != NULL) {
        strncpy(s_tx_info, info, sizeof(s_tx_info) - 1);
        s_tx_info[sizeof(s_tx_info) - 1] = '\0';
    } else {
        s_tx_info[0] = '\0';
    }
    request_screen(UI_SCREEN_TX_STATUS);
}

extern "C" void ui_set_tx_info(const char *info) {
    UiLock lk;   /* shared with the UI task - see s_ui_mx */
    strncpy(s_tx_info, (info != NULL) ? info : "", sizeof(s_tx_info) - 1);
    s_tx_info[sizeof(s_tx_info) - 1] = '\0';
    s_tx_info_dirty = true;
}
