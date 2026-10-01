/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ui_setup.cpp
 * @ingroup ui
 * @brief First-run setup on the panel: welcome, the browser hand-off, card reads, Wi-Fi.
 */

#include "ui_internal.h"

/* First-run greeting, in the sale flow's card on the ramp so setup and selling
 * read as one product. It waits for a tap: it is the only moment the terminal
 * has the operator's attention before the setup steps start asking for things. */
void build_welcome(void) {
    lv_obj_t *card = build_page(PAY_STEP_NONE, false);

    /* The logo is its own 80px render, not the 120px one zoomed: that RGB565
     * image has no alpha and draws grey edges when scaled. The column is
     * hand-balanced around the Start button (208..252) — moving one offset eats
     * into a neighbour. */
    lv_obj_t *logo = lv_img_create(card);
    lv_img_set_src(logo, &logo_mid);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 12);
    pop_in(logo);   /* settled screen, so the flourish is welcome here */

    /* "Thank you for choosing Cryptnox POS." split over two lines: the product
     * name stays black to carry the sentence, the lead-in is grey. */
    make_label(card, "Thank you for choosing", COL_DIM,
               &font_inter_14, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_t *brand = make_label(card, "Cryptnox POS", COL_TEXT,
                                 &font_pjs_20_medium, LV_ALIGN_TOP_MID, 0, 124);

    /* Anchored under the brand, not at a fixed y, because it may wrap. Width
     * and long mode first, so the measurement sees them. */
    lv_obj_t *sub = make_label(card, s_welcome_sub,
                               COL_DIM, &font_inter_14,
                               LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(sub, CARD_W - (2 * CARD_PAD));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_update_layout(brand);
    lv_obj_align_to(sub, brand, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    (void)make_button(card, "Start", COL_ACTION, COL_BG,
                      CARD_BTN_W, CARD_BTN_H, LV_ALIGN_BOTTOM_MID, 0, CARD_BTN_Y,
                      ACT_WELCOME_OK, &font_pjs_20_semibold);
}

/* Phone setup: the same screen at every step, captioned with the current one.
 *
 * One QR code carrying "WIFI:T:WPA;S:...;P:...;;", which iOS and Android cameras
 * join directly; the captive portal then opens the form, so there is no URL. The
 * SSID and passphrase are printed underneath as a fallback. All three go away
 * once a browser has been let in.
 *
 * Deliberately no on-panel alternative: past the admin code the wizard is a
 * browser flow. The admin code stays on the panel because its value is that it
 * never crosses a network.
 *
 * The last step thanks the operator and offers Finish, which applies the
 * settings (see main). */
void build_prov(void) {
    const prov_step_t step = static_cast<prov_step_t>(s_prov_step);

    /* The end of the wizard gets the whole card: nothing left to scan, and the
     * one thing to say is that it worked. */
    if (step == PROV_STEP_DONE) {
        lv_obj_t *card = build_page(PAY_STEP_NONE, false);   /* setup: no band */
        lv_obj_t *chk = make_label(card, LV_SYMBOL_OK, COL_SUCCESS,
                                   &font_icons_48, LV_ALIGN_TOP_MID, 0, 16);
        pop_in(chk);
        make_label(card, "All set", COL_TEXT, &font_pjs_20_medium,
                   LV_ALIGN_TOP_MID, 0, 76);
        lv_obj_t *b = make_label(card,
                                 "Thank you - your terminal is configured.\n\n"
                                 "It restarts once to apply everything.",
                                 COL_DIM, &font_inter_14,
                                 LV_ALIGN_TOP_MID, 0, 106);
        lv_obj_set_width(b, CARD_W - (2 * CARD_PAD));
        lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 106);

        (void)make_button(card, "Finish", COL_ACTION, COL_BG,
                          CARD_BTN_W, CARD_BTN_H, LV_ALIGN_BOTTOM_MID, 0, CARD_BTN_Y,
                          ACT_PROV_FINISH, &font_pjs_20_semibold);
        return;
    }

    /* Once the browser is in, the screen is only a status line, so it sits in
     * the sale card on the ramp like the other setup screens. The QR step keeps
     * the whole white screen: code, credentials and spinner need every row. */
    const bool authed = prov_authed();
    lv_obj_t  *host   = lv_scr_act();
    if (authed) {
        host = build_page(PAY_STEP_NONE, false);   /* setup: no band */
    } else {
        clear_screen();
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), LV_PART_MAIN);
    }
    const lv_coord_t dy = authed ? 6 : 0;   /* the card's top padding */

    /* The title names the step and is the biggest thing on the screen, so a
     * step advancing is visible while the QR code below stays the same. Step
     * numbers count the whole flow: 1 is the admin code created on the panel.
     * The Wi-Fi-only re-join has no steps to count, so it says what it is. */
    const bool  numbered = !prov_wifi_only();
    const char *eyebrow  = "Setup";
    const char *title    = "Nothing to set";
    /* Each hint describes THIS screen, not the one after it. */
    const char *hint     = "";
    switch (step) {
        case PROV_STEP_AUTH:
            eyebrow = "Step 2";
            title   = "Connect your phone";
            hint    = "Scan to join the terminal's Wi-Fi";
            break;
        case PROV_STEP_ADDR:
            eyebrow = "Step 4";
            title   = "Payout addresses";
            hint    = "Continue on your phone";
            break;
        case PROV_STEP_WIFI:
            eyebrow = numbered ? "Step 5" : "Wi-Fi";
            title   = "Wi-Fi network";
            hint    = numbered ? "Continue on your phone"
                               : "Scan with your phone";
            break;
        default:
            break;
    }

    make_label(host, eyebrow, COL_DIM, &font_inter_14,
               LV_ALIGN_TOP_MID, 0, 6 + dy);
    make_label(host, title, COL_TEXT, &font_pjs_20_medium,
               LV_ALIGN_TOP_MID, 0, 24 + dy);   /* 18px step line ends at 24 */
    make_label(host, hint, COL_DIM,
               &font_inter_14, LV_ALIGN_TOP_MID, 0, 50 + dy);

    /* The QR code and AP credentials are for joining, so they go once the phone
     * is let in; left up they would read as "scan this again". */
    if (!authed) {
        lv_obj_t *qr = lv_qrcode_create(lv_scr_act(), 112, COL_TEXT, COL_BG);
        if (qr != NULL) {
            const char *payload = prov_qr_payload();
            (void)lv_qrcode_update(qr, payload, strlen(payload));
            lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 70);
            /* White quiet zone: a code drawn hard against a coloured edge is a code
             * half the scanners in the world will not see. */
            lv_obj_set_style_border_color(qr, COL_BG, LV_PART_MAIN);
            lv_obj_set_style_border_width(qr, 4, LV_PART_MAIN);
        }

        /* Both labelled: someone typing them into a phone's Wi-Fi sheet is
         * filling in two named boxes. */
        make_label(lv_scr_act(), "Or join manually:", COL_DIM,
                   &font_inter_14, LV_ALIGN_TOP_MID, 0, 194);
        make_label(lv_scr_act(), "Network", COL_DIM, &font_inter_14,
                   LV_ALIGN_TOP_LEFT, 24, 214);
        make_label(lv_scr_act(), prov_ap_ssid(), COL_TEXT,
                   &font_inter_14_medium, LV_ALIGN_TOP_LEFT, 100, 214);
        make_label(lv_scr_act(), "Password", COL_DIM, &font_inter_14,
                   LV_ALIGN_TOP_LEFT, 24, 232);
        make_label(lv_scr_act(), prov_ap_pass(), COL_TEXT,
                   &font_inter_14_medium, LV_ALIGN_TOP_LEFT, 100, 232);
    } else {
        lv_obj_t *on = make_label(host,
                                  "Connected - the setup page is open in the "
                                  "browser.",
                                  COL_DIM, &font_inter_14,
                                  LV_ALIGN_TOP_MID, 0, 86);
        lv_obj_set_width(on, CARD_W - (2 * CARD_PAD));
        lv_label_set_long_mode(on, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(on, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(on, LV_ALIGN_TOP_MID, 0, 86);

        /* Only in this branch: a card is read from the browser's setup page, so
         * there is no route to a failed read that is not already past the QR
         * code. */
        if (s_prov_msg[0] != '\0') {
            lv_obj_t *m = make_label(host, s_prov_msg, COL_DANGER,
                                     &font_inter_14,
                                     LV_ALIGN_TOP_MID, 0, 130);
            lv_obj_set_width(m, CARD_W - (2 * CARD_PAD));
            lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            /* Under the Connected line, however many lines it wrapped to. Both
             * start high (86) and the spinner sits low so three lines of each
             * still end above it — the join error is two lines at 204px. */
            lv_obj_update_layout(on);
            lv_obj_align_to(m, on, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
        }
    }

    /* A spinner: the operator is looking at the browser, and this says the
     * terminal is still with them. "a connection", not "your phone": a laptop
     * can join the AP just as well. */
    lv_obj_t *sp = lv_spinner_create(host, 1000, 60);
    lv_obj_set_size(sp, 24, 24);
    lv_obj_align(sp, LV_ALIGN_BOTTOM_MID, 0, authed ? -12 : -34);
    lv_obj_set_style_arc_width(sp, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sp, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, COL_ACCENT, LV_PART_INDICATOR);
    /* Only before the join: after it the line above already says "Connected",
     * and the spinner alone says the terminal is waiting on the browser. */
    if (!authed) {
        make_label(lv_scr_act(), "Waiting for a connection", COL_DIM,
                   &font_inter_14, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
}

/* A payout address or a token contract proposed from a browser, shown for
 * acceptance here. The modal is the security boundary, not decoration: a browser
 * can propose a value, only the panel in the operator's hands can store one — and
 * that holds for the card-derived route too, which comes through the same
 * handshake rather than writing straight to NVS.
 *
 * The value is drawn wrapped at 14 px rather than elided — the operator is being
 * asked to compare it against their own records character by character, so every
 * character has to be on the screen. */
void build_prov_confirm(void) {
    prov_ask_t kind = PROV_ASK_NONE;
    char label[40] = "";
    char addr[SETTINGS_PAYOUT_MAX] = "";
    if (!prov_pending(&kind, label, sizeof(label), addr, sizeof(addr))) { return; }

    const bool contract = (kind == PROV_ASK_CONTRACT_ETH) ||
                          (kind == PROV_ASK_CONTRACT_TRON);

    /* Stacked and measured with ota_text/ota_fit_card, because the label, the
     * address and the warning all vary in height, and a fixed layout lets a
     * taller block run under the Accept button on the screen that decides where
     * the takings go. The label wraps rather than eliding, for the same reason
     * the address does. */
    lv_obj_t *card = open_modal(OTA_CARD_W, 276);

    lv_obj_t *t = ota_text(card, NULL,
                           contract ? "Set token contract?"
                                    : "Set payout address?",
                           COL_TEXT, &font_pjs_20_medium);
    lv_obj_t *l = ota_text(card, t, label, COL_DIM, &font_inter_14);
    lv_obj_t *a = ota_text(card, l, addr, COL_TEXT, &font_inter_14_medium);
    /* Kept to the sentence that changes a decision: the card grows only up to
     * ota_fit_card's ceiling of one screen, past which the buttons overlap the
     * text. */
    lv_obj_t *warn = ota_text(card, a,
                              contract
                              ? "A wrong contract charges a different asset."
                              : "Takings will be sent here. Check it against "
                                "your own records.",
                              COL_DIM, &font_inter_14);
    ota_fit_card(card, warn, 86);   /* two 40px buttons at -46 and -2 */

    /* Reject is the wide, plainly-labelled one and Accept is the deliberate tap:
     * the safe answer to "a stranger's address appeared on my terminal" is no. */
    (void)make_button(card, "Accept", COL_ACCENT, COL_BG, OTA_TEXT_W, 40,
                      LV_ALIGN_BOTTOM_MID, 0, -46, ACT_PROV_OK,
                      &font_pjs_20_semibold);
    (void)make_button(card, "Reject", COL_SURFACE, COL_TEXT, OTA_TEXT_W, 40,
                      LV_ALIGN_BOTTOM_MID, 0, -2, ACT_PROV_NO,
                      &font_pjs_20_semibold);
}

/* "Hold your card to the reader" while an address is derived from it. Its own
 * screen rather than the transaction one: nothing is being paid, and that screen's
 * wording and its Cancel semantics both belong to a sale. */
void build_card_wait(void) {
    /* Same chrome as the sale, deliberately without a lit dash: reading an
     * address off a card during setup is not a step of anybody's payment. */
    lv_obj_t *card = build_page(PAY_STEP_NONE, false);   /* setup: no band */
    const lv_coord_t VW = CARD_W - (2 * CARD_PAD);

    make_label(card, "Cryptnox card", COL_DIM, &font_inter_14,
               LV_ALIGN_TOP_MID, 0, 10);

    /* Centred in the band between the caption and the line below, computed
     * from TAP_MARK_H because the generator chooses the mark's height. */
    make_tap_mark(card, 32 + ((88 - TAP_MARK_H) / 2));

    make_label(card, "Hold card to reader", COL_TEXT,
               &font_pjs_20_medium, LV_ALIGN_TOP_MID, 0, 124);

    /* 154: "Reading your payout addresses" wraps to two lines at 204px, and
     * they must end above the Cancel button (208). */
    lv_obj_t *info = make_label(card, s_card_note, COL_DIM,
                                &font_inter_14, LV_ALIGN_TOP_MID, 0, 154);
    lv_obj_set_width(info, VW);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 154);

    (void)make_ghost_button(card, "Cancel", CARD_BTN_W,
                            LV_ALIGN_BOTTOM_MID, 0, CARD_BTN_Y, ACT_CANCEL);
}

/* Header with a back arrow (to amount entry) instead of the burger. */
static void build_header_back(const char *title) {
    (void)make_title(title, true);
    make_divider(lv_scr_act(), HDR_DIVIDER_Y);
    (void)make_icon_button(LV_SYMBOL_LEFT, ACT_WIFI_CANCEL);
}

static void wifi_item_cb(lv_event_t *e) {
    intptr_t idx = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    if ((idx < 0) || (idx >= s_ap_count)) { return; }
    strncpy(s_wifi_ssid, s_aps[idx].ssid, sizeof(s_wifi_ssid) - 1);
    s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
    request_screen(UI_SCREEN_WIFI_PASS);
}
void build_wifi_list(void) {
    clear_screen();
    build_header_back("Wi-Fi");

    /* Why the picker opened, otherwise the operator lands in Wi-Fi setup with no
     * idea what failed. */
    lv_coord_t list_y = 48;
    if (s_wifi_note[0] != '\0') {
        lv_obj_t *note = make_label(lv_scr_act(), s_wifi_note, COL_DANGER,
                                    &font_inter_14,
                                    LV_ALIGN_TOP_MID, 0, 50);
        lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(note, 216);
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        /* Measure rather than assume two lines: the list then clears the note at
         * any wrap depth, so a longer message can never overprint it. */
        lv_obj_update_layout(note);
        list_y = 50 + lv_obj_get_height(note) + 6;
    }

    if (s_ap_count == 0U) {
        make_label(lv_scr_act(), "No networks found", COL_DIM,
                   &font_inter_14, LV_ALIGN_CENTER, 0, 0);
        make_button(lv_scr_act(), "Rescan", COL_ACTION, COL_BG, 140, ACT_BTN_H,
                    LV_ALIGN_BOTTOM_MID, 0, ACT_BTN_Y, ACT_WIFI,
                    &font_pjs_20_semibold);
        return;
    }

    lv_obj_t *list = lv_list_create(lv_scr_act());
    lv_obj_set_size(list, SCR_W - 12, SCR_H - 4 - list_y);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, list_y);
    lv_obj_set_style_bg_color(list, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 4, LV_PART_MAIN);

    for (uint16_t i = 0U; i < s_ap_count; i++) {
        lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_WIFI, s_aps[i].ssid);
        lv_obj_set_style_bg_color(btn, COL_SURFACE, LV_PART_MAIN);
        lv_obj_set_style_text_color(btn, COL_TEXT, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, wifi_item_cb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }
}

static void wifi_pass_kb_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {            /* keyboard check-mark */
        if (s_wifi_pass_ta != NULL) {
            const char *p = lv_textarea_get_text(s_wifi_pass_ta);
            strncpy(s_wifi_pass, (p != NULL) ? p : "", sizeof(s_wifi_pass) - 1);
            s_wifi_pass[sizeof(s_wifi_pass) - 1] = '\0';
        }
        set_wifi_progress("Connecting to", s_wifi_ssid);
        request_screen(UI_SCREEN_WIFI_CONNECTING);
        if (s_cb != NULL) { s_cb(UI_EVENT_WIFI_TRY, 0); }
    } else if (code == LV_EVENT_CANCEL) {     /* keyboard close */
        request_screen(UI_SCREEN_WIFI_LIST);
    }
}
void build_wifi_pass(void) {
    clear_screen();
    make_label(lv_scr_act(), s_wifi_ssid, COL_TEXT, &font_inter_14_medium,
               LV_ALIGN_TOP_MID, 0, 6);

    s_wifi_pass_ta = lv_textarea_create(lv_scr_act());
    lv_textarea_set_one_line(s_wifi_pass_ta, true);
    lv_textarea_set_text(s_wifi_pass_ta, "");
    lv_textarea_set_placeholder_text(s_wifi_pass_ta, "Password");
    /* Masked by default: this screen faces the customer. The eye beside it
     * reveals on demand, for checking a long passphrase. */
    lv_textarea_set_password_mode(s_wifi_pass_ta, true);
    /* Echo each character briefly, then mask — long enough to catch a typo,
     * a third of LVGL's 1500 ms default on a customer-facing screen. The eye
     * stays the way to re-read the whole passphrase, on the merchant's terms. */
    lv_textarea_set_password_show_time(s_wifi_pass_ta, 500);
    /* Same sizing rule as the card PIN and admin code fields: field and eye are
     * one group, sized to the full-width keyboard less a margin either side, and
     * centred as a pair. */
    lv_obj_set_width(s_wifi_pass_ta, CODE_FIELD_W(SCR_W - (2 * FIELD_PAD_V)));
    lv_obj_align(s_wifi_pass_ta, LV_ALIGN_TOP_MID, CODE_FIELD_X, 28);
    lv_obj_set_style_pad_ver(s_wifi_pass_ta, FIELD_PAD_V, LV_PART_MAIN);

    /* make_icon_button() places itself top-left for the burger; move it beside
     * the field. Keep the label handle so the glyph can be swapped in place. Same
     * pair on the card-PIN screen — see build_pin(). */
    lv_obj_t *eye = make_icon_button(LV_SYMBOL_EYE_OPEN, ACT_WIFI_PASS_REVEAL);
    lv_obj_align_to(eye, s_wifi_pass_ta, LV_ALIGN_OUT_RIGHT_MID, CODE_EYE_GAP, 0);
    s_wifi_eye_lbl = lv_obj_get_child(eye, 0);

    lv_obj_t *kb = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_textarea(kb, s_wifi_pass_ta);
    lv_obj_add_event_cb(kb, wifi_pass_kb_cb, LV_EVENT_ALL, NULL);
}
void build_wifi_connecting(void) {
    clear_screen();
    build_header("Wi-Fi");

    if (s_wifi_name[0] == '\0') {
        /* Nothing to name ("Scanning..."): the caption is the whole message. */
        make_label(lv_scr_act(), s_wifi_caption, COL_TEXT,
                   &font_pjs_20_medium, LV_ALIGN_CENTER, 0, 0);
    } else {
        make_label(lv_scr_act(), s_wifi_caption, COL_DIM,
                   &font_inter_14, LV_ALIGN_CENTER, 0, -30);

        /* LONG_DOT elides on real glyph metrics, so a 32-char SSID never
         * overflows. Width must be set before the long mode. */
        lv_obj_t *name = make_label(lv_scr_act(), s_wifi_name, COL_TEXT,
                                    &font_pjs_20_medium, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_width(name, SCR_W - 24);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(name, LV_ALIGN_CENTER, 0, 0);   /* re-centre after the resize */
    }
}
