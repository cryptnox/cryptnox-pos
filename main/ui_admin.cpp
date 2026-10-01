/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ui_admin.cpp
 * @ingroup ui
 * @brief The settings page, touch calibration and the admin-code screens.
 */

#include "ui_internal.h"

/******************************************************************
 * 7b. Settings — full-screen page (swipe up from the bottom edge)
 ******************************************************************/
/* Brightness only: the one setting this page changes. The fee caps are written
 * by the config page. */
void settings_persist(void) {
    settings_set_brightness(s_brightness);
}

static void brightness_event_cb(lv_event_t *e) {
    lv_obj_t *sl  = lv_event_get_target(e);
    lv_obj_t *lbl = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    s_brightness  = static_cast<uint8_t>(lv_slider_get_value(sl));
    backlight_set_pct(s_brightness);
    if (lbl != NULL) {
        char b[8];
        snprintf(b, sizeof(b), "%u%%", static_cast<unsigned>(s_brightness));
        lv_label_set_text(lbl, b);
    }
}

/* Show the Reset button (and shrink Close) only on the About tab. Driven by the
 * tab index rather than by an event, so the rebuild path can call it directly
 * instead of faking a tab change. */
static void settings_bottom_bar(uint16_t tab) {
    const bool about = (tab == TAB_ABOUT);
    if (s_reset_btn != NULL) {
        if (about) { lv_obj_clear_flag(s_reset_btn, LV_OBJ_FLAG_HIDDEN); }
        else       { lv_obj_add_flag(s_reset_btn, LV_OBJ_FLAG_HIDDEN); }
    }
    if (s_close_btn != NULL) {
        if (about) {
            lv_obj_set_width(s_close_btn, 108);
            lv_obj_align(s_close_btn, LV_ALIGN_BOTTOM_RIGHT, -10, ACT_BTN_Y);
        } else {
            lv_obj_set_width(s_close_btn, 232);
            lv_obj_align(s_close_btn, LV_ALIGN_BOTTOM_MID, 0, ACT_BTN_Y);
        }
    }
}

static void tab_change_cb(lv_event_t *e) {
    lv_obj_t *tabbar = lv_event_get_target(e);
    /* Only a real press names a button; anything else reports
     * LV_BTNMATRIX_BTN_NONE (0xFFFF), which must not be mistaken for a tab. */
    uint16_t sel = lv_btnmatrix_get_selected_btn(tabbar);
    if (sel >= SETTINGS_TAB_COUNT) { return; }
    s_settings_tab = sel;
    settings_bottom_bar(sel);
}
void build_settings(void) {
    clear_screen();   /* white, full screen — see the note by paint_page() */

    lv_obj_t *tv = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 42);
    lv_obj_set_size(tv, SCR_W, SCR_H - 54);
    lv_obj_align(tv, LV_ALIGN_TOP_MID, 0, 0);
    /* Tabview and tab bar are styled by the theme, which paints them COL_BG.
     * The tabview, tab bar and each page go transparent here (not in the theme,
     * so other tabviews stay white) to show the ramp behind them. */
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_tabview_get_tab_btns(tv), LV_OPA_TRANSP,
                            LV_PART_MAIN);

    lv_obj_t *t_screen = lv_tabview_add_tab(tv, "Screen");
    lv_obj_t *t_wifi   = lv_tabview_add_tab(tv, "Wi-Fi");
    lv_obj_t *t_tx     = lv_tabview_add_tab(tv, "Tx");
    lv_obj_t *t_about  = lv_tabview_add_tab(tv, "About");
    lv_obj_t *pages[4] = { t_screen, t_wifi, t_tx, t_about };
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_opa(pages[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(pages[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(pages[i], TAB_PAD, LV_PART_MAIN);
        lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    /* About and Tx can overflow — let them scroll vertically with a scrollbar. */
    lv_obj_t *scrollable[2] = { t_about, t_tx };
    for (int i = 0; i < 2; i++) {
        lv_obj_add_flag(scrollable[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(scrollable[i], LV_DIR_VER);
        lv_obj_set_scrollbar_mode(scrollable[i], LV_SCROLLBAR_MODE_AUTO);
    }
    /* Three of the four tabs are a stack of rows, so they are flex columns and
     * every row is appended rather than placed: conditional and wrapping rows
     * push the rest down by themselves. Screen is a layout, not a stack. */
    lv_obj_t *columns[3] = { t_wifi, t_tx, t_about };
    for (int i = 0; i < 3; i++) {
        lv_obj_set_flex_flow(columns[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(columns[i], 14, LV_PART_MAIN);
    }
    /* About is centred on its logo; the other two read left. */
    lv_obj_set_flex_align(t_about, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /* ── Screen tab: brightness ── */
    make_label(t_screen, "Brightness", COL_TEXT, &font_inter_14_medium,
               LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *pct = make_label(t_screen, "", COL_DIM, &font_inter_14,
                               LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *sl = lv_slider_create(t_screen);
    lv_obj_set_width(sl, 196);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, 32);
    lv_slider_set_range(sl, 10, 100);   /* never fully dark */
    lv_slider_set_value(sl, s_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, COL_ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, brightness_event_cb, LV_EVENT_VALUE_CHANGED, pct);

    char b[8];
    snprintf(b, sizeof(b), "%u%%", static_cast<unsigned>(s_brightness));
    lv_label_set_text(pct, b);

    /* Resistive overlays vary board to board, so the operator can calibrate
     * without a rebuild. */
    lv_obj_t *calpill = make_pill(t_screen, "Touch", "Calibrate the panel",
                                  TAB_W, 76, ACT_TOUCH_CAL);
    lv_obj_align(make_glyph_disc(calpill, LV_SYMBOL_EDIT, COL_ACCENT, COIN_SZ),
                 LV_ALIGN_LEFT_MID, PILL_ICON_X, 0);

    /* ── Wi-Fi tab: show the currently configured network, then a Scan button ── */
    char cur_ssid[33] = {0};
    char cur_pass[65];
    bool have_wifi = settings_get_wifi(cur_ssid, sizeof(cur_ssid),
                                       cur_pass, sizeof(cur_pass));
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(cur_pass), sizeof(cur_pass));

    /* Read-only caption/value pair, not a tappable pill: the network is set from
     * the Configure page, so nobody types a passphrase on a resistive panel. The
     * on-device picker appears only when the terminal cannot re-join (wifi_picker). */
    lv_obj_t *wl = make_field(t_wifi, "Network",
                              have_wifi ? cur_ssid : "Not configured");
    /* An SSID is 32 arbitrary characters; elide it on real glyph widths. */
    lv_label_set_long_mode(wl, LV_LABEL_LONG_DOT);

    /* Link quality of the live association (snapshot at settings open),
     * as a caption/value pair like every other field. */
    int8_t rssi = 0;
    if (net_wifi_rssi(&rssi)) {
        const char *qual = (rssi >= -50) ? "Excellent" :
                           (rssi >= -60) ? "Good"      :
                           (rssi >= -70) ? "Fair"      : "Weak";
        char sig[32];
        snprintf(sig, sizeof(sig), "%s (%d dBm)", qual,
                 static_cast<int>(rssi));
        (void)make_field(t_wifi, "Signal", sig);
    }

    /* Everything else lives in a browser: this row raises the config page and
     * shows a QR code to scan (the same action as About's Update row). The
     * subtitle stays under make_pill's 140px cap, which dot-elides the rest. */
    lv_obj_t *cpill = make_pill(t_wifi, "Configure", "Open in a browser",
                                TAB_W, 0, ACT_PORTAL);
    lv_obj_align(make_glyph_disc(cpill, LV_SYMBOL_SETTINGS, COL_ACCENT, COIN_SZ),
                 LV_ALIGN_LEFT_MID, PILL_ICON_X, 0);

    /* ── Transaction tab: which asset, what it will call, where the funds go and
     * what gas it will pay.
     *
     * All read-only: settings are proposed from the config page and accepted on
     * this panel, since a resistive screen is the wrong place to retype an
     * address. The selector picks which asset's rows to show (s_view_chain). ── */
    const bool tron = pos_chain_is_tron(s_view_chain);
    ui_refresh_addresses_for(static_cast<uint8_t>(s_view_chain));
    /* Ticker over network, split across the pill's two lines so both fit
     * make_pill's 140px cap at full length. */
    lv_obj_t *apill = make_pill(t_tx, asset_name(s_view_chain),
                                asset_network(s_view_chain),
                                TAB_W, 0, ACT_NET_PICK);
    lv_obj_align(make_asset_badge(apill, s_view_chain),
                 LV_ALIGN_LEFT_MID, PILL_ICON_X, 0);

    (void)make_field(t_tx, asset_caption(s_view_chain),
                     (s_addr_usdc != NULL) ? s_addr_usdc : "-");
    (void)make_field(t_tx, "Send to",
                     (s_addr_dest != NULL) ? s_addr_dest : "-");

    /* Say out loud when the recipient is the compile-time one rather than an
     * address somebody chose: this is the row an operator checks to answer "where
     * does my money go". The sale is refused at the confirm step, so say that. */
    if (!settings_has_payout(tron)) {
        lv_obj_t *w = make_label(t_tx, "Not configured - this terminal cannot "
                                       "take payments. Set it from the "
                                       "Configure page.",
                                 COL_DANGER, &font_inter_14,
                                 LV_ALIGN_DEFAULT, 0, 0);
        lv_obj_set_width(w, TAB_W);
        lv_label_set_long_mode(w, LV_LABEL_LONG_WRAP);
    }

    /* The gas the terminal is willing to pay, as two read-only rows. Tron has no
     * such setting (bandwidth, compile-time energy cap), so the rows are absent. */
    if (!tron) {
        char fee[16];
        snprintf(fee, sizeof(fee), "%u",
                 static_cast<unsigned>(settings_get_max_fee_gwei()));
        s_fee_max_lbl = make_field(t_tx, "Max fee (Gwei)", fee);

        snprintf(fee, sizeof(fee), "%u",
                 static_cast<unsigned>(settings_get_priority_fee_gwei()));
        s_fee_prio_lbl = make_field(t_tx, "Priority fee (Gwei)", fee);
    }

    /* The way out of a read-only tab: the config page, one tap from the rows it
     * sets (same action as the Wi-Fi and About rows). The subtitle names the fees
     * where there are any; both forms fit make_pill's 140px subtitle cap. */
    lv_obj_t *tpill = make_pill(t_tx, "Configure",
                                tron ? "Open in a browser" : "Fees in a browser",
                                TAB_W, 0, ACT_PORTAL);
    lv_obj_align(make_glyph_disc(tpill, LV_SYMBOL_SETTINGS, COL_ACCENT, COIN_SZ),
                 LV_ALIGN_LEFT_MID, PILL_ICON_X, 0);

    /* ── About tab: small C logo, name, version, info ── */
    lv_obj_t *blogo = lv_img_create(t_about);
    lv_img_set_src(blogo, &logo_small);   /* 40px dedicated image */

    make_label(t_about, "cryptnox-pos", COL_TEXT, &font_pjs_20_medium,
               LV_ALIGN_DEFAULT, 0, 0);
    /* Straight out of the running image's header rather than a #define, so that
     * after an update this reads as the firmware that is actually executing. */
    char about_ver[OTA_VERSION_SHOWN_MAX];
    make_label(t_about,
               ota_version_display(ota_running_version(), about_ver,
                                   sizeof(about_ver)),
               COL_DIM, &font_inter_14, LV_ALIGN_DEFAULT, 0, 0);

    /* An update that booted and was then reverted would leave this tab on the
     * old version with no explanation, so say so in one red line under it. It
     * clears itself when the next update overwrites the slot it is read from. */
    if (ota_last_update_failed()) {
        make_label(t_about, "Last update rolled back", COL_DANGER,
                   &font_inter_14, LV_ALIGN_DEFAULT, 0, 0);
    }

    /* The update row: opens the config page with a QR code to scan, so it sits
     * behind the admin code with the rest of the settings. Subtitle kept under
     * make_pill's 140px cap, which dot-elides the rest. */
    lv_obj_t *upill = make_pill(t_about, "Update", "From a browser",
                                TAB_W, 0, ACT_PORTAL);
    lv_obj_align(make_glyph_disc(upill, LV_SYMBOL_DOWNLOAD, COL_ACCENT, COIN_SZ),
                 LV_ALIGN_LEFT_MID, PILL_ICON_X, 0);

    /* Deliberately says nothing about which assets or networks: that list goes
     * stale with the firmware, and the Tx tab answers it from live settings. */
    lv_obj_t *about = make_label(t_about,
                                 "Crypto payment terminal\n"
                                 "for Cryptnox cards\n\n"
                                 "Based on cryptnox-sdk-esp32 1.0.0\n"
                                 "(c) Cryptnox 2026 - Educational use only\n\n"
                                 "Licensed under LGPL-3.0-or-later\n\n"
                                 "Third-party: ESP-IDF (Apache-2.0),\n"
                                 "LVGL (MIT), TFT_eSPI (FreeBSD/MIT),\n"
                                 "XPT2046_Touchscreen (MIT)",
                                 COL_DIM, &font_inter_14,
                                 LV_ALIGN_DEFAULT, 0, 0);
    lv_label_set_long_mode(about, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(about, 210);
    lv_obj_set_style_text_align(about, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    /* Bottom bar: Close always; on the About tab a Reset joins it on the same
     * line (Reset left, Close right). On other tabs Close is full-width. */
    s_close_btn = make_button(lv_scr_act(), "Close", COL_ACTION, COL_BG, 232, ACT_BTN_H,
                              LV_ALIGN_BOTTOM_MID, 0, ACT_BTN_Y, ACT_CLOSE,
                              &font_pjs_20_semibold);
    s_reset_btn = make_button(lv_scr_act(), "Reset", COL_DANGER, COL_TEXT, 108, ACT_BTN_H,
                              LV_ALIGN_BOTTOM_LEFT, 10, ACT_BTN_Y, ACT_RESET,
                              &font_pjs_20_semibold);
    /* Shape comes from make_button (BTN_RADIUS), like every other button. */
    lv_obj_add_event_cb(lv_tabview_get_tab_btns(tv), tab_change_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Back to the tab the operator was on: a rebuild is how this page applies a
     * changed asset, and it should not look like the page reopened. */
    if (s_settings_tab != 0U) {
        lv_tabview_set_act(tv, s_settings_tab, LV_ANIM_OFF);
    }
    settings_bottom_bar(s_settings_tab);
}

/******************************************************************
 * 7c. Touch calibration
 *
 * Resistive overlays vary unit to unit. Two targets, then the operator confirms
 * the result by tapping a button drawn with it.
 ******************************************************************/


/* Crosshair rather than a filled dot: the target is the centre, and a dot
 * hides it under the finger that is aiming at it. */
static void cal_target(lv_coord_t cx, lv_coord_t cy) {
    const lv_coord_t arm = 12;
    static lv_point_t h[2], v[2];
    h[0] = { (lv_coord_t)(cx - arm), cy }; h[1] = { (lv_coord_t)(cx + arm), cy };
    v[0] = { cx, (lv_coord_t)(cy - arm) }; v[1] = { cx, (lv_coord_t)(cy + arm) };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *l = lv_line_create(lv_scr_act());
        lv_line_set_points(l, (i == 0) ? h : v, 2);
        lv_obj_set_style_line_width(l, 2, LV_PART_MAIN);
        lv_obj_set_style_line_color(l, COL_DANGER, LV_PART_MAIN);
        lv_obj_set_pos(l, 0, 0);
    }
}

/* Turn the two captured corners into the edge-to-edge range touch_to_screen()
 * maps against, and put it in force without storing it. Returns false on a
 * pair too close together to be two deliberate taps — which is what a stuck
 * panel or an impatient double-tap on one spot looks like. */
static bool cal_apply(void) {
    touch_cal_t c;
    if (!touch_cal_from_corners(s_cal_raw[0][0], s_cal_raw[0][1],
                                s_cal_raw[1][0], s_cal_raw[1][1],
                                CAL_INSET, SCR_W, SCR_H, &c)) {
        return false;
    }
    s_cal_xmin = c.x_min; s_cal_xmax = c.x_max;
    s_cal_ymin = c.y_min; s_cal_ymax = c.y_max;
    return true;
}
void build_touch_cal(void) {
    clear_screen();

    if (s_cal_step < 2U) {
        const bool first = (s_cal_step == 0U);
        /* No header on these two: the targets sit in the corners, and the one
         * at the top left lands under a title bar's divider. Nothing is drawn
         * near a corner except the cross being aimed at. */
        lv_obj_t *ask = make_label(lv_scr_act(),
                   first ? "Tap the cross\nat the top left"
                         : "Now the one at\nthe bottom right",
                   COL_TEXT, &font_pjs_20_medium, LV_ALIGN_CENTER, 0, -10);
        lv_obj_set_style_text_align(ask, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(ask, LV_ALIGN_CENTER, 0, -10);
        lv_obj_t *hint = make_label(lv_scr_act(),
                                    "Use a stylus or a fingernail - the centre "
                                    "of the cross, not near it.",
                                    COL_DIM, &font_inter_14,
                                    LV_ALIGN_CENTER, 0, 60);
        lv_obj_set_width(hint, 200);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 60);

        cal_target(first ? CAL_INSET : (SCR_W - 1 - CAL_INSET),
                   first ? CAL_INSET : (SCR_H - 1 - CAL_INSET));
        return;
    }

    /* Both corners captured. */
    build_header("Calibrate touch");
    if (!cal_apply()) {
        lv_obj_t *no = make_label(lv_scr_act(),
                   "Those two taps were\ntoo close together.\n\n"
                   "Nothing was changed.",
                   COL_DANGER, &font_pjs_20_medium, LV_ALIGN_CENTER, 0, -20);
        lv_obj_set_style_text_align(no, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(no, LV_ALIGN_CENTER, 0, -20);
        make_button(lv_scr_act(), "Back", COL_ACTION, COL_BG, 232, ACT_BTN_H,
                    LV_ALIGN_BOTTOM_MID, 0, ACT_BTN_Y, ACT_CAL_CANCEL,
                    &font_pjs_20_semibold);
        return;
    }

    /* The new map is live from here. Tapping Keep with it IS the test. */
    lv_obj_t *q = make_label(lv_scr_act(), "Keep this\ncalibration?", COL_TEXT,
                             &font_pjs_20_medium, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_text_align(q, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(q, LV_ALIGN_TOP_MID, 0, 70);
    s_cal_countdown = make_label(lv_scr_act(), "", COL_DIM,
                                 &font_inter_14,
                                 LV_ALIGN_TOP_MID, 0, 140);
    s_cal_deadline = lv_tick_get() + CAL_VERIFY_MS;

    make_button(lv_scr_act(), "Discard", COL_ACTION, COL_BG, 104, ACT_BTN_H,
                LV_ALIGN_BOTTOM_LEFT, 10, ACT_BTN_Y, ACT_CAL_CANCEL,
                &font_pjs_20_semibold);
    make_button(lv_scr_act(), "Keep", COL_ACTION, COL_BG, 104, ACT_BTN_H,
                LV_ALIGN_BOTTOM_RIGHT, -10, ACT_BTN_Y, ACT_CAL_SAVE,
                &font_pjs_20_semibold);
}

/* Sampling runs on the UI task, outside LVGL's input device: the calibration
 * screen is the one place that must read the panel before the mapping it is
 * measuring. The sample taken is the last one before release — a resistive
 * panel's first reading as the finger lands is its worst. */
void touch_cal_poll(void) {
    if (s_cal_step >= 2U) { return; }

    int16_t rx, ry;
    if (touch_raw(&rx, &ry)) {
        s_cal_pressed = true;
        s_cal_last_x  = rx;
        s_cal_last_y  = ry;
        return;
    }
    if (!s_cal_pressed) { return; }

    s_cal_pressed = false;
    s_cal_raw[s_cal_step][0] = s_cal_last_x;
    s_cal_raw[s_cal_step][1] = s_cal_last_y;
    s_cal_step++;
    request_screen(UI_SCREEN_TOUCH_CAL);   /* next target, then the verify */
}

/* Modal overlays (factory-reset confirmation, network picker). One at a time:
 * both are full-screen and both are opened from the settings page. */
static lv_obj_t *s_modal = NULL;

/* Whether the modal on screen belongs to the config portal. The portal closes
 * itself after PROV_WINDOW_MIN, and a leftover overlay swallows every touch and
 * would block payments until a power cycle, so the UI task clears it then. */
bool s_portal_modal = false;
void close_modal(void) {
    if (s_modal != NULL) {
        lv_obj_del(s_modal);
        s_modal = NULL;
    }
    s_portal_modal = false;
}

/* Dimmed overlay + centred card on the top layer, so it floats above the
 * settings page. Returns the card for the caller to fill. */
lv_obj_t *open_modal(lv_coord_t w, lv_coord_t h) {
    close_modal();

    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_modal, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *card = lv_obj_create(s_modal);
    lv_obj_set_size(card, w, h);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 14, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    return card;
}
void open_reset_confirm(void) {
    lv_obj_t *card = open_modal(224, 210);

    lv_obj_t *msg = make_label(card,
                               "Erase all settings\n"
                               "(Wi-Fi, brightness, fees)\n"
                               "and reboot?",
                               COL_TEXT, &font_inter_14,
                               LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    make_button(card, "Erase", COL_DANGER, COL_TEXT, 196, 40,
                LV_ALIGN_BOTTOM_MID, 0, -50, ACT_RESET_CONFIRM, &font_pjs_20_semibold);
    make_button(card, "Cancel", COL_ACTION, COL_BG, 196, 40,
                LV_ALIGN_BOTTOM_MID, 0, -2, ACT_MODAL_CLOSE, &font_pjs_20_semibold);
}
#define OTA_GAP      8
#define OTA_CARD_PAD 8   /* open_modal()'s pad_all */

/** Wrapped, centred body text stacked under @p above (or the card top). */
lv_obj_t *ota_text(lv_obj_t *card, lv_obj_t *above, const char *txt,
                          lv_color_t col, const lv_font_t *font) {
    lv_obj_t *l = make_label(card, txt, col, font, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_width(l, OTA_TEXT_W);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    if (above == NULL) {
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 4);
    } else {
        /* align_to() reads current coordinates, and a freshly sized label has
         * not been laid out yet: without this its wrapped height is stale. */
        lv_obj_update_layout(card);
        lv_obj_align_to(l, above, LV_ALIGN_OUT_BOTTOM_MID, 0, OTA_GAP);
    }
    return l;
}

/**
 * Resize the card to the text it ended up with, leaving room for the buttons.
 *
 * @p buttons_h is how far the button block reaches up from the content bottom —
 * 42 for one 40 px button at -2, 86 for two. Clamped to the screen, so a
 * pathological string elides off the bottom rather than drawing a card taller
 * than the panel.
 */
void ota_fit_card(lv_obj_t *card, lv_obj_t *last, lv_coord_t buttons_h) {
    lv_obj_update_layout(card);
    lv_coord_t need = lv_obj_get_y(last) + lv_obj_get_height(last)
                      + OTA_GAP + buttons_h + (2 * OTA_CARD_PAD);
    if (need > (SCR_H - 16)) { need = SCR_H - 16; }
    lv_obj_set_height(card, need);
    lv_obj_center(card);
}
void open_portal_window(void) {
    /* Started here, on the UI task. The event callback is handed over so a
     * submission can come back to the panel. */
    const bool up = prov_start(PROV_MODE_ADMIN, s_cb);

    if (!up) {
        lv_obj_t *card = open_modal(OTA_CARD_W, 200);
        lv_obj_t *m = ota_text(card, NULL,
                 "The terminal's own Wi-Fi would not come up, so there is "
                 "nothing for a phone to join.\n\nRestart and try again.",
                 COL_TEXT, &font_inter_14);
        ota_fit_card(card, m, 42);
        make_button(card, "Close", COL_ACTION, COL_BG, OTA_TEXT_W, 40,
                    LV_ALIGN_BOTTOM_MID, 0, -2, ACT_MODAL_CLOSE,
                    &font_pjs_20_semibold);
        return;
    }

    lv_obj_t *card = open_modal(OTA_CARD_W, 300);

    /* A "WIFI:..." join code: the page is on the terminal's own AP, so joining
     * is the whole journey. 104 px is 26 modules at 4 px. The white quiet zone
     * is not optional: many scanners miss a code drawn hard against an edge. */
    lv_obj_t *qr = lv_qrcode_create(card, 104, COL_TEXT, COL_BG);
    if (qr != NULL) {
        const char *payload = prov_qr_payload();
        (void)lv_qrcode_update(qr, payload, strlen(payload));
        lv_obj_set_style_border_color(qr, COL_BG, LV_PART_MAIN);
        lv_obj_set_style_border_width(qr, 4, LV_PART_MAIN);
        /* Top of the card, the same 4px inset ota_text() gives a first block. */
        lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 4);
    }

    /* The credentials in text too — a laptop has no camera to point, and a code
     * that will not scan in a dim bar still leaves ten characters to type. */
    char creds[80];
    snprintf(creds, sizeof(creds), "SSID: %s\nPassword: %s",
             prov_ap_ssid(), prov_ap_pass());
    lv_obj_t *url = ota_text(card, qr, creds, COL_TEXT, &font_inter_14_medium);

    /* Three hand-broken lines, each under OTA_TEXT_W: under ota_fit_card's
     * ceiling the code, credentials and Done button leave room for no more, so
     * anything added here must replace one. The address is for a real browser:
     * the Wi-Fi sign-in window cannot pick a firmware file. */
    char note[96];
    snprintf(note, sizeof(note),
             "Or open 192.168.4.1\nVenue Wi-Fi off while open\nCloses in %u min",
             prov_window_left_min());
    lv_obj_t *n = ota_text(card, url, note, COL_DIM, &font_inter_14);
    ota_fit_card(card, n, 42);

    make_button(card, "Done", COL_ACTION, COL_BG, OTA_TEXT_W, 40,
                LV_ALIGN_BOTTOM_MID, 0, -2, ACT_PORTAL_CLOSE,
                &font_pjs_20_semibold);

    s_portal_modal = true;   /* set last: open_modal() cleared it */
}

/**
 * Accept or refuse firmware that the update page has uploaded.
 *
 * The upload is already verified (SHA-256, and the signature on a signed build),
 * so this asks whether the person holding the terminal wants this version, which
 * a browser cannot answer. A downgrade is called out: it is properly signed too,
 * and returning to firmware with a known fault is something to be talked into.
 */
/**
 * Install was tapped and there was nothing left to install.
 *
 * Says so, so a terminal that stayed on the old firmware does not look updated.
 */
void open_ota_gone(void) {
    lv_obj_t *card = open_modal(OTA_CARD_W, 200);
    lv_obj_t *m = ota_text(card, NULL,
             "That firmware is no longer waiting to be installed.\n\n"
             "Nothing changed - this terminal is still on the version it was. "
             "Open Update again and send the file once more.",
             COL_TEXT, &font_inter_14);
    ota_fit_card(card, m, 42);
    make_button(card, "Close", COL_ACTION, COL_BG, OTA_TEXT_W, 40,
                LV_ALIGN_BOTTOM_MID, 0, -2, ACT_MODAL_CLOSE,
                &font_pjs_20_semibold);
}
void build_ota_confirm(void) {
    char version[40] = "?";
    bool older = false;
    if (!ota_staged(version, sizeof(version), &older)) { return; }

    lv_obj_t *card = open_modal(OTA_CARD_W, 252);

    /* The downgrade warning is the caption, one red line above the version, so
     * the body and the card's height stay the same either way. */
    lv_obj_t *cap = ota_text(card, NULL,
                             older ? "This is an OLDER version" : "New firmware",
                             older ? COL_DANGER : COL_DIM,
                             &font_inter_14);
    /* 28 pt fits about twelve characters (the 'v' included); a longer
     * `git describe` string steps down a size rather than wrapping. */
    char staged_ver[OTA_VERSION_SHOWN_MAX];
    (void)ota_version_display(version, staged_ver, sizeof(staged_ver));
    lv_obj_t *ver = ota_text(card, cap, staged_ver, COL_TEXT,
                             (strlen(staged_ver) > 12U) ? &font_pjs_20_medium
                                                        : &font_pjs_28_semibold);

    char running_ver[OTA_VERSION_SHOWN_MAX];
    char body[128];
    snprintf(body, sizeof(body),
             "Running %s.\n\nThe terminal restarts now. Not during a payment.",
             ota_version_display(ota_running_version(), running_ver,
                                 sizeof(running_ver)));
    lv_obj_t *m = ota_text(card, ver, body, COL_DIM, &font_inter_14);
    ota_fit_card(card, m, 86);

    make_button(card, "Install", COL_ACTION, COL_BG, OTA_TEXT_W, 40,
                LV_ALIGN_BOTTOM_MID, 0, -46, ACT_OTA_OK, &font_pjs_20_semibold);
    make_button(card, "Discard", COL_ACTION, COL_BG, OTA_TEXT_W, 40,
                LV_ALIGN_BOTTOM_MID, 0, -2, ACT_OTA_NO, &font_pjs_20_semibold);

    s_portal_modal = true;
}

/* Asset selection, in two steps: the network, then the coin on it, since a chain
 * is a (network, coin) pair. The card is 228 wide (212 inside the 8px pad), so
 * the pills run to 206. */
#define PICK_W  206

/**
 * Grey a row out and make it inert.
 *
 * For a network whose payout address nobody has set. Shown rather than hidden,
 * so the dimmed row tells the operator what to go and do.
 */
static void pill_disable(lv_obj_t *p) {
    lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(p, LV_OPA_50, LV_PART_MAIN);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(p); i++) {
        lv_obj_set_style_text_color(lv_obj_get_child(p, i), COL_DIM, LV_PART_MAIN);
    }
}

/** Step 1 — the network. */
/* Which action opens step 2 for a network, in pos_net_t order. Kept here
 * rather than in assets.h because an action is a UI concept. */
static const BtnAction NET_ACT[POS_NET__COUNT] = {
    ACT_NET_ETH, ACT_NET_POLY, ACT_NET_TRON
};
void open_network_picker(void) {
    /* Same growth rule as step 2 — header + rows + the Cancel button — so a
     * fourth network added to assets.h widens the card rather than clipping. */
    lv_obj_t *card = open_modal(228,
        static_cast<lv_coord_t>(86 + (POS_NET__COUNT * (PILL_H + 2))));

    make_label(card, "Network", COL_DIM, &font_inter_14,
               LV_ALIGN_TOP_MID, 0, 2);

    for (int i = 0; i < (int)POS_NET__COUNT; i++) {
        const pos_net_info_t *ni = pos_net_info(static_cast<pos_net_t>(i));
        /* Read here, not cached: the subtitle names the deployment, which is a
         * runtime setting. */
        const char *sub = settings_net_str(ni->sub_test, ni->sub_main);
        /* A network with no payout address of its own is not offered. Otherwise a
         * terminal set up for Ethereum and interrupted before Tron would quietly
         * take Tron payments to the compile-time recipient — somebody else's
         * address — and look entirely normal doing it. Polygon shares the
         * Ethereum payout address (same EVM account). */
        const bool have = settings_has_payout(i == (int)POS_NET_TRON);
        lv_coord_t y = static_cast<lv_coord_t>(22 + (i * (PILL_H + 2)));
        /* Kept under this pill's 130px subtitle cap
         * (PICK_W - PILL_TEXT_X - PILL_TEXT_PAD_R) so it is not elided. */
        lv_obj_t  *p = make_pill(card, ni->name, have ? sub : "No payout set",
                                 PICK_W, y, NET_ACT[i]);
        lv_obj_align(make_net_badge(p, static_cast<pos_net_t>(i)),
                     LV_ALIGN_LEFT_MID, PILL_ICON_X, 0);
        if (!have) { pill_disable(p); }
    }

    make_button(card, "Cancel", COL_SURFACE, COL_TEXT, PICK_W, 40,
                LV_ALIGN_BOTTOM_MID, 0, -2, ACT_MODAL_CLOSE,
                &font_pjs_20_semibold);
}

/** Step 2 — the coin on the network chosen in step 1. */
void open_coin_picker(pos_net_t net) {
    /* The rows are assets.h's, filtered on the network, in table order (grouped
     * by network, native coin first). The chain IS the action (ACT_CHAIN_OF). */
    size_t n = 0;
    for (size_t i = 0U; i < POS_ASSET_COUNT; i++) {
        if (POS_ASSETS[i].net == net) { n++; }
    }

    char title[24];
    (void)snprintf(title, sizeof(title), "Coin on %s", pos_net_info(net)->name);

    /* Card grows with the row count: header + rows + the Back button. */
    lv_obj_t *card = open_modal(228,
        static_cast<lv_coord_t>(86 + (n * (PILL_H + 2))));

    make_label(card, title, COL_DIM,
               &font_inter_14, LV_ALIGN_TOP_MID, 0, 2);

    size_t row = 0;
    for (size_t i = 0U; i < POS_ASSET_COUNT; i++) {
        const pos_asset_t *a = &POS_ASSETS[i];
        if (a->net != net) { continue; }
        lv_coord_t y = static_cast<lv_coord_t>(22 + (row * (PILL_H + 2)));
        lv_obj_t  *p = make_pill(card, a->ticker, a->standard, PICK_W, y,
                                 ACT_CHAIN_OF(a->chain), true /* leaf */);
        lv_obj_align(make_asset_badge(p, a->chain),
                     LV_ALIGN_LEFT_MID, PILL_ICON_X, 0);
        row++;
    }

    /* Back, not Cancel: step 2 of two, so the way out is step 1. */
    make_button(card, "Back", COL_SURFACE, COL_TEXT, PICK_W, 40,
                LV_ALIGN_BOTTOM_MID, 0, -2, ACT_NET_PICK,
                &font_pjs_20_semibold);
}

/**
 * @brief Wait imposed after repeated wrong admin codes.
 *
 * Free for the first three tries, then doubling — 1 s, 2 s, 4 s ... — and
 * still doubling past a minute, up to an hour per attempt from the fifteenth
 * wrong code on. The count is in NVS, so a power cycle restarts the current wait
 * rather than resetting it. Never a permanent lock: the code gates the factory
 * reset too, so locking for good would leave no way back in short of a USB
 * reflash.
 */
#define ADMIN_PENALTY_MAX_S  3600U
uint32_t admin_penalty_ms(uint8_t fails) {
    if (fails < 3U) { return 0U; }
    uint32_t shift = static_cast<uint32_t>(fails) - 3U;
    if (shift > 12U) { shift = 12U; }        /* 4096 s: past the cap, no overflow */
    uint32_t secs = 1U << shift;
    if (secs > ADMIN_PENALTY_MAX_S) { secs = ADMIN_PENALTY_MAX_S; }
    return secs * 1000U;
}

/* "45s" or "12 min": the penalty runs to an hour, and nobody at a counter wants
 * to divide "wait 3417s" by sixty. */
static void wait_text(char *out, size_t n, uint32_t secs) {
    if (secs < 120U) { (void)snprintf(out, n, "%us", static_cast<unsigned>(secs)); }
    else { (void)snprintf(out, n, "%u min", static_cast<unsigned>((secs + 59U) / 60U)); }
}

/* Seconds left on the penalty, 0 once it has elapsed. */
static uint32_t admin_lock_remaining_s(void) {
    if (s_admin_lock_ms == 0U) { return 0U; }
    const uint32_t elapsed = lv_tick_elaps(s_admin_lock_start);
    if (elapsed >= s_admin_lock_ms) {
        s_admin_lock_ms = 0U;
        return 0U;
    }
    return ((s_admin_lock_ms - elapsed) + 999U) / 1000U;
}

static void admin_set_note(const char *msg) {
    strncpy(s_admin_note, (msg != NULL) ? msg : "", sizeof(s_admin_note) - 1);
    s_admin_note[sizeof(s_admin_note) - 1] = '\0';
    if (s_admin_note_lbl != NULL) {
        lv_label_set_text(s_admin_note_lbl, s_admin_note);
    }
}

static void admin_submit(void) {
    if (s_admin_ta == NULL) { return; }

    char code[ADMIN_CODE_MAX + 1] = {0};
    const char *txt = lv_textarea_get_text(s_admin_ta);
    strncpy(code, (txt != NULL) ? txt : "", sizeof(code) - 1);
    code[sizeof(code) - 1] = '\0';

    if (s_req_screen == UI_SCREEN_ADMIN_SET) {
        if (!s_admin_confirming) {
            if (strlen(code) < ADMIN_CODE_MIN) {
                char msg[sizeof(s_admin_note)];
                (void)snprintf(msg, sizeof(msg), "At least %d digits",
                               ADMIN_CODE_MIN);
                admin_set_note(msg);   /* built, so it cannot drift from the limit */
            } else {
                strncpy(s_admin_first, code, sizeof(s_admin_first) - 1);
                s_admin_first[sizeof(s_admin_first) - 1] = '\0';
                s_admin_confirming = true;
                admin_set_note("");
                request_screen(UI_SCREEN_ADMIN_SET);   /* rebuild, confirm pass */
            }
        } else if (strcmp(code, s_admin_first) == 0) {
            if (!settings_set_admin_code(code)) {
                /* Stay put and say so. Reporting success here would send main on
                 * to Wi-Fi setup and leave a terminal whose menu — factory reset
                 * included — no code can ever open. */
                admin_set_note("Storage error - try again");
                lv_textarea_set_text(s_admin_ta, "");
                CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(code),
                                      sizeof(code));
                return;
            }
            CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_admin_first),
                                  sizeof(s_admin_first));
            s_admin_confirming = false;
            admin_set_note("");
            /* Not the amount screen: on this first-run path main raises the setup
             * access point next, so say that rather than flash the keypad. */
            set_wifi_progress("Starting setup", NULL);
            request_screen(UI_SCREEN_WIFI_CONNECTING);
            if (s_cb != NULL) { s_cb(UI_EVENT_ADMIN_SET, 0); }
        } else {
            CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_admin_first),
                                  sizeof(s_admin_first));
            s_admin_confirming = false;
            admin_set_note("Codes did not match");
            request_screen(UI_SCREEN_ADMIN_SET);
        }
    } else {
        const uint32_t wait_s = admin_lock_remaining_s();
        char msg[sizeof(s_admin_note)];
        if (strlen(code) < ADMIN_CODE_MIN_STORED) {
            /* Too short to be any stored code, so don't spend an attempt on it.
             * Otherwise a few stray taps on OK push the counter into the penalty
             * and the merchant waits a minute for a menu nobody attacked. */
            admin_set_note("Enter your code");
        } else if (wait_s > 0U) {
            char w[16];
            wait_text(w, sizeof(w), wait_s);
            (void)snprintf(msg, sizeof(msg), "Too many tries - wait %s", w);
            admin_set_note(msg);
        } else if (settings_check_admin_code(code)) {
            s_admin_lock_ms = 0U;
            if (s_admin_for_portal) {
                /* This is the whole authorisation mechanism: the code was typed
                 * here, so the browser is let in without it ever having been on
                 * the network.
                 *
                 * Admin mode returns to the settings page (the code was just
                 * verified); the setup screen would show AP credentials that only
                 * exist during setup. */
                s_admin_for_portal = false;
                s_view_chain       = settings_get_chain();
                prov_auth_resolve(true);
                request_screen((prov_mode() == PROV_MODE_WIZARD)
                               ? UI_SCREEN_PROV : UI_SCREEN_SETTINGS);
            } else {
                request_screen(UI_SCREEN_SETTINGS);
            }
        } else {
            const uint32_t penalty = admin_penalty_ms(settings_admin_fail_count());
            if (penalty > 0U) {
                s_admin_lock_start = lv_tick_get();
                s_admin_lock_ms    = penalty;
                char w[16];
                wait_text(w, sizeof(w), penalty / 1000U);
                (void)snprintf(msg, sizeof(msg), "Wrong code - wait %s", w);
                admin_set_note(msg);
            } else {
                admin_set_note("Wrong code");
            }
            lv_textarea_set_text(s_admin_ta, "");
        }
    }
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(code), sizeof(code));
}

static void admin_kbd_cb(lv_event_t *e) {
    lv_obj_t *bm = lv_event_get_target(e);
    uint32_t id = lv_btnmatrix_get_selected_btn(bm);
    const char *txt = lv_btnmatrix_get_btn_text(bm, id);
    if ((txt == NULL) || (s_admin_ta == NULL)) { return; }

    if (strcmp(txt, LV_SYMBOL_OK) == 0) {
        admin_submit();
    } else if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_del_char(s_admin_ta);
    } else if ((txt[0] >= '0') && (txt[0] <= '9') && (txt[1] == '\0')) {
        lv_textarea_add_char(s_admin_ta, static_cast<uint32_t>(txt[0]));
    }
}

/* Shared body of both admin screens; only the title and the way out differ. */
static void build_admin_screen(const char *title, bool allow_cancel,
                               const char *hint) {
    /* Into the rising sheet when there is one, WITHOUT clearing the screen: the
     * sale screen underneath is what the sheet slides over. Otherwise a normal
     * rebuild. Either way it takes the card PIN's look (card on the ramp). */
    lv_obj_t *host;
    if (s_sheet != NULL) {
        paint_page(s_sheet);
        host = make_card(s_sheet);
    } else {
        host = build_page(PAY_STEP_NONE, false);
    }

    (void)make_title(title, allow_cancel, host);
    if (allow_cancel) {
        (void)make_icon_button(LV_SYMBOL_LEFT, ACT_ADMIN_CANCEL, host);
    }

    /* The card is 262 tall, not 320, so the note tucks under the field
     * (44..76) and the keypad gives up height, as the PIN screen's does. */
    lv_obj_t *kb = make_numeric_keypad(admin_kbd_cb, host, CODE_KBD_W,
                                       160);

    /* The reveal eye, as on the setup PIN: the code is typed blind on a resistive
     * panel, and getting it wrong costs a doubling lockout on the only door to
     * the factory reset. */
    s_admin_ta = make_code_row(host, kb, ADMIN_CODE_MAX, hint, ACT_ADMIN_REVEAL,
                               &s_admin_eye_lbl);

    /* Note band above the keypad: wrong code, mismatch, or the remaining wait. */
    s_admin_note_lbl = make_label(host, s_admin_note, COL_DANGER,
                                  &font_inter_14, LV_ALIGN_TOP_MID, 0,
                                  78);
}
void build_admin_set(void) {
    /* No way out: first-run setup is mandatory, since the whole menu — including
     * the factory reset — hides behind this code. */
    build_admin_screen(s_admin_confirming ? "Confirm code" : "Set admin code",
                       false,
                       s_admin_confirming ? "Type it again" : "New admin code");
}
void build_admin_unlock(void) {
    /* Same screen either way (lockout, note band, keypad); the title says which
     * door, the hint says the key is the admin code. For a browser, the title
     * carries its pairing code — the page shows the same four digits, so the
     * operator lets in the phone in their hand and not whichever one on the
     * access point asked first. */
    static char title[24];
    char code[8];
    if (s_admin_for_portal && prov_pair_code(code, sizeof(code))) {
        (void)snprintf(title, sizeof(title), "Browser %s", code);
    } else {
        (void)snprintf(title, sizeof(title), "%s",
                       s_admin_for_portal ? "Authorize browser" : "Control panel");
    }
    build_admin_screen(title, true, "Admin code");
}
