/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ui_widgets.cpp
 * @ingroup ui
 * @brief Widget helpers shared by every screen, and the Wi-Fi signal mark.
 */

#include "ui_internal.h"

/******************************************************************
 * 7. Widget helpers
 ******************************************************************/
lv_obj_t *make_button(lv_obj_t *parent, const char *label, lv_color_t bg,
                             lv_color_t fg, lv_coord_t w, lv_coord_t h,
                             lv_align_t align, lv_coord_t x, lv_coord_t y,
                             BtnAction act, const lv_font_t *font) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_align(btn, align, x, y);

    /* Base look — FLAT fill (kill the default theme's gradient, which washes a
     * dark fill toward light), no default border. BTN_RADIUS 6 keeps an action
     * button a softened rectangle; the full-radius pill is for the asset
     * selector and chips, which are not actions. */
    lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, BTN_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);

    /* Flat — no drop shadow (light, minimal look). */
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);

    /* Secondary (surface) buttons get a hairline border to lift them off the bg.
     * (Compare the raw RGB565 value — lv_color_eq isn't in this LVGL build.) */
    if (bg.full == COL_SURFACE.full) {
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, COL_BORDER, LV_PART_MAIN);
    }

    /* Pressed feedback: darken the fill — except on the near-black COL_ACTION,
     * where darker than #101F2E is no visible change at all, so it lightens
     * instead. The press is the only thing a resistive panel says back. */
    lv_obj_set_style_bg_color(btn,
                              (bg.full == COL_ACTION.full)
                                  ? lv_color_mix(lv_color_white(), bg, 40)
                                  : lv_color_mix(lv_color_black(), bg, 70),
                              LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(act)));

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, fg, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_center(lbl);
    return btn;
}
lv_obj_t *make_label(lv_obj_t *parent, const char *txt, lv_color_t color,
                            const lv_font_t *font, lv_align_t align,
                            lv_coord_t x, lv_coord_t y) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_align(lbl, align, x, y);
    return lbl;
}

/* The sale's secondary button: card-white fill with a hairline border. Cancel
 * costs nothing, so it should read as available without competing with the
 * button that commits; an outline is the weakest thing that still reads as one. */
lv_obj_t *make_ghost_button(lv_obj_t *parent, const char *label,
                                   lv_coord_t w, lv_align_t align,
                                   lv_coord_t x, lv_coord_t y, BtnAction act) {
    lv_obj_t *b = make_button(parent, label, COL_BG, COL_TEXT, w,
                              CARD_BTN_H,
                              align, x, y, act, &font_pjs_20_semibold);
    lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(b, COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    return b;
}

/* Thin horizontal rule under a screen title, for visual structure. */
void make_divider(lv_obj_t *parent, lv_coord_t y) {
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_set_size(d, SCR_W - 48, 2);
    lv_obj_align(d, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(d, COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(d, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(d, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(d, 0, LV_PART_MAIN);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
}
#define CHIP_SZ   16
#define BADGE_SZ  (COIN_SZ + 6)   /* room for the chip to hang off the corner */
lv_obj_t *make_glyph_disc(lv_obj_t *parent, const char *sym,
                                 lv_color_t bg, lv_coord_t sz) {
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, sz, sz);
    lv_obj_set_style_bg_color(d, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = lv_label_create(d);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_color(l, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &font_inter_14, LV_PART_MAIN);
    lv_obj_center(l);
    return d;
}

/* Coin mark, optionally with a network chip on its bottom-right corner, the way
 * wallets badge a token with the chain it lives on — so "USDC" (Sepolia) and a
 * TRC-20 are told apart by the icon and not only by the subtitle. The chips
 * carry their white ring in the bitmap (tools/gen_chain_icons.py).
 *
 * @p chip may be NULL, and the box then shrinks to COIN_SZ. The coin is centred
 * in the box and the chip hangs off the corner, so centring the box centres the
 * mark with no per-site nudge. Returns the box for the caller to position. */
static lv_obj_t *make_icon_box(lv_obj_t *parent, const lv_img_dsc_t *coin_src,
                               const lv_img_dsc_t *chip_src) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, (chip_src != NULL) ? BADGE_SZ : COIN_SZ,
                         (chip_src != NULL) ? BADGE_SZ : COIN_SZ);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *coin = lv_img_create(box);
    lv_img_set_src(coin, coin_src);
    lv_obj_center(coin);

    if (chip_src != NULL) {
        lv_obj_t *chip = lv_img_create(box);
        lv_img_set_src(chip, chip_src);
        lv_obj_align(chip, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }
    return box;
}

/* Network marks and chips, indexed by pos_net_t (order follows assets.h). Kept
 * out of the asset table because they are LVGL types and that table is
 * host-testable. */
static const lv_img_dsc_t *const NET_ICON[POS_NET__COUNT] = {
    &icon_eth, &icon_poly, &icon_tron
};
static const lv_img_dsc_t *const NET_CHIP[POS_NET__COUNT] = {
    &chip_eth, &chip_poly, &chip_tron
};

/* A token's own mark, by the row's pos_coin_t. */
static const lv_img_dsc_t *coin_icon(const pos_asset_t *a) {
    switch (a->coin) {
        case POS_COIN_USDC: return &icon_usdc;
        case POS_COIN_USDT: return &icon_usdt;
        default:            return NET_ICON[(int)a->net];
    }
}

/* The selected asset. A native coin IS its network, so it carries no chip —
 * the chip only says "this token lives over there". */
lv_obj_t *make_asset_badge(lv_obj_t *parent, pos_chain_t chain) {
    const pos_asset_t *a = pos_asset_of(chain);
    const int n = (int)a->net;
    if (a->native) { return make_icon_box(parent, NET_ICON[n], NULL); }
    return make_icon_box(parent, coin_icon(a), NET_CHIP[n]);
}

/* The asset selector itself: the badge above turned into a tappable pill, at the
 * right-hand end of the amount's row — mark, ticker, chevron. A flex row at
 * LV_SIZE_CONTENT because the ticker's width varies: LVGL measures it, the align
 * pins the right edge, and amount_row_place() reads back the result. */
lv_obj_t *make_asset_button(lv_obj_t *parent) {
    s_asset_btn = lv_btn_create(parent);
    lv_obj_set_size(s_asset_btn, LV_SIZE_CONTENT, ASSET_BTN_H);
    lv_obj_align(s_asset_btn, LV_ALIGN_TOP_RIGHT, ASSET_BTN_X, ASSET_BTN_Y);
    lv_obj_set_style_bg_color(s_asset_btn, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(s_asset_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_asset_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_asset_btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_asset_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_asset_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_asset_btn,
                              lv_color_mix(lv_color_black(), COL_SURFACE, 20),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_asset_btn, btn_event_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(
                            static_cast<intptr_t>(ACT_NET_PICK)));

    /* Pads and gap set explicitly: the flex row obeys them, and the theme's
     * button padding would otherwise push the badge onto the chevron. */
    lv_obj_set_flex_flow(s_asset_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_asset_btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s_asset_btn, ASSET_BTN_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_asset_btn, 5, LV_PART_MAIN);
    lv_obj_clear_flag(s_asset_btn, LV_OBJ_FLAG_SCROLLABLE);

    (void)make_asset_badge(s_asset_btn, settings_get_chain());
    make_label(s_asset_btn, asset_name(), COL_TEXT, &font_inter_14_medium,
               LV_ALIGN_DEFAULT, 0, 0);
    s_asset_arrow = make_label(s_asset_btn, LV_SYMBOL_RIGHT, COL_DIM,
                               &font_inter_14, LV_ALIGN_DEFAULT, 0, 0);
    return s_asset_btn;
}

/* Chevron off once there is an amount, so the digits get its width back; the
 * ticker stays, as it says which money. Hidden, not deleted: the right-aligned
 * pill re-measures and only its left edge moves. */
void asset_btn_set_compact(bool compact) {
    if (s_asset_arrow == NULL) { return; }   /* not the amount screen */
    if (compact) { lv_obj_add_flag(s_asset_arrow, LV_OBJ_FLAG_HIDDEN); }
    else         { lv_obj_clear_flag(s_asset_arrow, LV_OBJ_FLAG_HIDDEN); }
}

/* The bare network mark, for the network picker's own rows — no coin is chosen
 * at that step, so there is nothing to badge it with. */
lv_obj_t *make_net_badge(lv_obj_t *parent, pos_net_t net) {
    return make_icon_box(parent, NET_ICON[(int)net], NULL);
}

/* "Tap here" mark — the contactless waves and a hand presenting a card, in an
 * oval. The artwork is assets/contactless-icon.png; tools/gen_tap_icon.py
 * downsamples it into main/tap_icon.c, so change the asset and re-run the
 * script. An asset rather than geometry: a hand drawn from rectangles and arcs
 * at this size does not read as a hand.
 *
 * Provenance: the drawing is UXWing's, licensed for commercial use without
 * attribution (a licence, not a PD dedication; this file is the copy those terms
 * applied to). EMVCo's Contactless Indicator is a trademark, which no copyright
 * licence covers; it is licensed through the product's scheme certification.
 *
 * ALPHA_8BIT, recoloured from img_recolor. LVGL's fast path for that format draws
 * nothing under angle or zoom, so this must stay untransformed. Aligned TOP_MID
 * at @p y. TAP_MARK_W/H are set by the generator from the artwork, for reading
 * only; build_card_wait() centres the mark using them. */
#define TAP_MARK_W    109

/* The mark's ink, and the one place to change it. It tracks the artwork's
 * weight: on this artwork grey reads as washed-out or disabled, which is wrong
 * for the one thing the customer is being asked to act on, so it is black. */
#define COL_TAP_MARK  COL_TEXT
void make_tap_mark(lv_obj_t *parent, lv_coord_t y) {
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, &tap_icon);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    /* The mask carries shape only; this is where its colour comes from. */
    lv_obj_set_style_img_recolor(img, COL_TAP_MARK, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, y);
}
#define PILL_TEXT_X  52          /* clears the BADGE_SZ icon at PILL_ICON_X */
/* Right-hand reserve for the chevron (drawn at -14, ~8px wide). 24 clears the
 * glyph without dot-eliding "Tron Nile TRC-20" in a 196px pill. */
#define PILL_TEXT_PAD_R  24
lv_obj_t *make_pill(lv_obj_t *parent, const char *title, const char *sub,
                           lv_coord_t w, lv_coord_t y, BtnAction act,
                           bool leaf) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, PILL_H);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(btn, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn,
                              lv_color_mix(lv_color_black(), COL_SURFACE, 20),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(act)));

    if (sub != NULL) {
        make_label(btn, title, COL_TEXT, &font_pjs_20_medium,
                   LV_ALIGN_TOP_LEFT, PILL_TEXT_X, 7);
        lv_obj_t *sl = make_label(btn, sub, COL_DIM, &font_inter_14,
                                  LV_ALIGN_TOP_LEFT, PILL_TEXT_X, 30);
        /* A leaf row's subtitle is left at LV_SIZE_CONTENT — it is one of our
         * own fixed strings and it must be readable in full. Only the rows that
         * open something get a width cap, because those show an SSID, which can
         * be 32 characters and has to elide somewhere. */
        if (!leaf) {
            lv_obj_set_width(sl, w - PILL_TEXT_X - PILL_TEXT_PAD_R);
            lv_label_set_long_mode(sl, LV_LABEL_LONG_DOT);
        }
    } else {
        make_label(btn, title, COL_TEXT, &font_pjs_20_medium,
                   LV_ALIGN_LEFT_MID, PILL_TEXT_X, 0);
    }
    /* The chevron means "this opens something". A leaf row is the choice
     * itself, so it gets no chevron — and hands the space to the subtitle. */
    if (!leaf) {
        make_label(btn, LV_SYMBOL_RIGHT, COL_DIM, &font_inter_14,
                   LV_ALIGN_RIGHT_MID, -14, 0);
    }
    return btn;
}

/* A read-only row on the settings tabs: dim caption over its value, as one
 * flex item. The tabs are flex columns (see build_settings), so a row sits where
 * it was appended. Returns the value label, for callers that update it in place.
 *
 * Values wrap by default: these are addresses. An SSID wants LV_LABEL_LONG_DOT
 * instead, which the caller sets. */
lv_obj_t *make_field(lv_obj_t *parent, const char *caption,
                            const char *value, lv_color_t col) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, TAB_W, LV_SIZE_CONTENT);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 4, LV_PART_MAIN);

    make_label(box, caption, COL_DIM, &font_inter_14,
               LV_ALIGN_DEFAULT, 0, 0);
    lv_obj_t *v = make_label(box, value, col, &font_inter_14_medium,
                             LV_ALIGN_DEFAULT, 0, 0);
    lv_obj_set_width(v, TAB_W);
    lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
    return v;
}

static void sheet_y_cb(void *obj, int32_t v) {
    lv_obj_set_y(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(v));
}

/* Full-screen opaque panel parked one screen-height below the fold. Square
 * corners: a sheet flush with all four screen edges would show the page behind
 * through each rounded corner. The rise is what reads as a sheet. */
static lv_obj_t *sheet_open(void) {
    lv_obj_t *sh = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(sh);   /* radius 0 among the rest — see above */
    lv_obj_set_size(sh, SCR_W, SCR_H);
    lv_obj_set_pos(sh, 0, SCR_H);
    lv_obj_set_style_bg_color(sh, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sh, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(sh, LV_OBJ_FLAG_SCROLLABLE);
    /* Clickable (the LVGL default) on purpose: it must swallow taps meant for
     * the screen it is covering, which is still fully built underneath. */
    return sh;
}

static void sheet_slide_in(lv_obj_t *sh) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, sh);
    lv_anim_set_exec_cb(&a, sheet_y_cb);
    lv_anim_set_values(&a, SCR_H, 0);
    lv_anim_set_time(&a, 260);
    /* Decelerating, like every sheet anybody has used on a phone — a linear
     * slide reads as mechanical at this size. */
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* Scrub a secret field's heap strings before lv_obj_clean frees them unwiped:
 * the real text (pwd_tmp while masked, the label while revealed) and the label,
 * which holds the last digit in clear during its echo. Copies LVGL's per-digit
 * reallocs left behind are out of reach. */
static void code_field_wipe(lv_obj_t *ta) {
    if (ta == NULL) { return; }
    char *t = const_cast<char *>(lv_textarea_get_text(ta));
    if (t != NULL) { CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(t), strlen(t)); }
    char *l = lv_label_get_text(lv_textarea_get_label(ta));
    if (l != NULL) { CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(l), strlen(l)); }
}
void clear_screen(void) {
    lv_obj_t *scr = lv_scr_act();
    code_field_wipe(s_pin_ta);
    code_field_wipe(s_admin_ta);
    code_field_wipe(s_wifi_pass_ta);
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, COL_BG, LV_PART_MAIN);
    /* build_page() leaves a gradient on the screen, and the screen object
     * outlives the screens built on it — without this the ramp follows onto
     * the flat white pages. */
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    s_amount_label = NULL;
    s_amount_cents_label = NULL;
    s_amount_row   = NULL;
    s_asset_btn    = NULL;
    s_asset_arrow  = NULL;
    s_charge_btn   = NULL;
    s_charge_busy  = false;   /* the button the flag was about is gone */
    s_clock_lbl    = NULL;
    s_pin_ta       = NULL;   /* deleted by lv_obj_clean — drop the dangling ref */
    s_pin_eye_lbl  = NULL;
    s_wifi_pass_ta = NULL;
    s_wifi_eye_lbl   = NULL;
    s_boot_step_lbl  = NULL;
    s_tx_info_lbl    = NULL;
    s_cal_countdown  = NULL;
    s_page_card      = NULL;
    s_fee_max_lbl    = NULL;
    s_fee_prio_lbl   = NULL;
    s_admin_ta       = NULL;
    s_admin_note_lbl = NULL;
    s_reset_btn    = NULL;
    s_close_btn    = NULL;
}

/* The UTC offset the clock adds, cached: settings_get_tz_offset_min() opens
 * NVS and clock_refresh() runs every three seconds. Reloaded through
 * ui_clock_changed() when the config page stores a new one. */
static int16_t       s_tz_off_min = 0;
static uint8_t       s_tz_dst     = 0;
volatile bool s_tz_dirty   = true;

/**
 * @brief Retext the band's clock from the system clock plus the stored offset.
 *
 * "--:--" until SNTP has set the clock: a till showing a confident wrong time
 * (1970) is worse than one admitting it has none.
 *
 * gmtime_r on an already-offset instant, not localtime_r: newlib's timezone
 * machinery costs 64 KB of the app slot. DST comes from civil_local_offset_min().
 * Minute resolution, so the 3 s Wi-Fi status timer drives it.
 */
static void clock_refresh(void) {
    if (s_clock_lbl == NULL) { return; }

    if (s_tz_dirty) {
        s_tz_dirty   = false;
        s_tz_off_min = settings_get_tz_offset_min();
        s_tz_dst     = settings_get_tz_dst();
    }

    const time_t utc = time(NULL);
    /* Nov 2023, before any real sale. Tested on the unshifted instant, so a
     * negative offset cannot drag a just-set clock back under the threshold. */
    if (utc < (time_t)1700000000) {
        lv_label_set_text(s_clock_lbl, "--:--");
        return;
    }

    const time_t local = utc + ((time_t)civil_local_offset_min(
                                    utc, s_tz_off_min, s_tz_dst) * 60);
    struct tm    tmv;
    if (gmtime_r(&local, &tmv) == NULL) {
        lv_label_set_text(s_clock_lbl, "--:--");
        return;
    }
    char b[8];
    (void)snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    lv_label_set_text(s_clock_lbl, b);
}

/**
 * @brief Lay the sale-flow chrome and return the white card to build into.
 *
 * @param step Which step of the sale this is (PAY_STEP_*). Currently unused.
 * @return The card. Children use ITS coordinates: (0,0) is the card's top-left,
 *         CARD_W x CARD_H.
 */
/* The page ramp, on whatever acts as the page (the screen, or the rising admin
 * sheet). LVGL defaults are opaque, so anything over it that should let the
 * ramp show through must say so — see build_settings(). */
void paint_page(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, COL_PAGE, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(obj, COL_PAGE_GRAD, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
}

/* Deliberately no paint_panel(): the admin panel is white, which clear_screen()
 * already leaves, gradient reset included. */

/* The sale flow's white card, on @p parent (the screen, or the admin sheet). */
lv_obj_t *make_card(lv_obj_t *parent) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_pos(card, CARD_X, CARD_Y);
    lv_obj_set_style_bg_color(card, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 14, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

/* @p band false for the first-run setup screens that borrow the card: no
 * clock and no home bar. Nothing is configured yet — the clock would read
 * --:-- and the swipe the bar advertises is off anywhere but the amount screen. */
lv_obj_t *build_page(int step, bool band) {
    (void)step;   /* ponytail: no progress indicator on the band */
    clear_screen();
    paint_page(lv_scr_act());

    /* The clock, left end of the status band; retexted by the status timer
     * rather than by rebuilding the screen every minute. */
    if (band) {
        s_clock_lbl = make_label(lv_scr_act(), "", COL_TITLE,
                                 &font_inter_14_medium,
                                 LV_ALIGN_TOP_LEFT, CLOCK_X, CLOCK_Y);
        clock_refresh();
    }

    lv_obj_t *card = make_card(lv_scr_act());

    /* The handle the admin panel is behind, drawn because a gesture with
     * nothing on screen saying it exists is a gesture nobody finds. */
    if (band) {
        lv_obj_t *home = lv_obj_create(lv_scr_act());
        lv_obj_remove_style_all(home);
        lv_obj_set_size(home, 84, 5);
        lv_obj_align(home, LV_ALIGN_BOTTOM_MID, 0, -9);
        lv_obj_set_style_radius(home, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(home, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(home, COL_HOME_BAR, LV_PART_MAIN);
    }

    s_page_card = card;
    return card;
}

/******************************************************************
 * 8. Screen builders
 ******************************************************************/
/* Borderless icon button (top-left) — just the glyph, no box/shadow. */
lv_obj_t *make_icon_button(const char *sym, BtnAction act,
                                  lv_obj_t *parent) {
    lv_obj_t *btn = lv_btn_create((parent != NULL) ? parent : lv_scr_act());
    lv_obj_set_size(btn, MENU_BTN_W, MENU_BTN_H);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, MENU_BTN_X, MENU_BTN_Y);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(act)));
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, sym);
    lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &font_pjs_20_medium, LV_PART_MAIN);
    lv_obj_center(lbl);
    return btn;
}

/* Which deployment, on the screen that takes the money. Nothing else in the
 * payment flow says test or production, and a firmware update with a new
 * BUILD_ID wipes NVS and brings the unit back on mainnet — so a terminal
 * somebody configured for testnet can start taking real payments with every
 * screen looking normal. Production stays silent, the same rule
 * settings_net_str follows: mainnet names carry no suffix, so the chip's
 * presence IS the warning.
 *
 * In the status band, not on the amount's row: amount_row_place() gives the
 * figure whatever the pill leaves, so a chip there would take width off the
 * digits. */
void add_test_chip(void) {
    if (settings_get_mainnet()) { return; }
    lv_obj_t *chip = lv_label_create(lv_scr_act());
    lv_label_set_text(chip, "TEST");
    lv_obj_set_style_text_color(chip, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_text_font(chip, &font_inter_14_medium, LV_PART_MAIN);
    lv_obj_set_style_bg_color(chip, COL_DANGER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(chip, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(chip, 1, LV_PART_MAIN);   /* 18px line: 20 tall */
    lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    /* Straight after the clock. Not right-aligned: that end is the Wi-Fi mark's,
     * which is on the top layer and would draw over the chip. */
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, CHIP_X, CHIP_Y);
}

/**
 * @brief Screen title, centred but never underneath the top-left icon button.
 *
 * The title gets the gap between two symmetric gutters; if it does not fit at
 * 20px it drops to the 14px face rather than overlapping the icon or eliding.
 * The width cap and LONG_DOT are the backstop for a title too long even at 14px.
 *
 * @param has_icon true when make_icon_button() has put a glyph in the top-left.
 */
lv_obj_t *make_title(const char *txt, bool has_icon,
                            lv_obj_t *parent) {
    /* Symmetric, so the title stays optically centred. Width comes from the
     * parent when there is one (the sale flow's card), not from SCR_W. */
    lv_obj_t *host = (parent != NULL) ? parent : lv_scr_act();
    lv_obj_update_layout(host);
    lv_coord_t hw = lv_obj_get_width(host);
    if (hw <= 0) { hw = SCR_W; }   /* not laid out yet — assume full width */
    /* Cleared from the ~20px glyph, not the 42px hit box: clearing the whole box
     * leaves too little width for "Authorize browser" even at 14px. */
    const lv_coord_t gutter = has_icon ? (MENU_BTN_X + ((MENU_BTN_W + 20) / 2) + 4)
                                       : 8;
    const lv_coord_t avail  = hw - (2 * gutter);

    const bool small = lv_txt_get_width(txt, strlen(txt), &font_pjs_20_medium,
                                        0, LV_TEXT_FLAG_NONE) > avail;
    /* +2 keeps the shorter 14px cap optically level with the 20px arrow glyph
     * beside it (Inter's 18px line box already puts its cap 2 lower). */
    lv_obj_t *l = make_label(host, txt, COL_TITLE,
                             small ? &font_inter_14_medium
                                   : &font_pjs_20_medium,
                             LV_ALIGN_TOP_MID, 0,
                             small ? (HDR_TITLE_Y + 2) : HDR_TITLE_Y);
    lv_obj_set_width(l, avail);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    return l;
}

/* Title + divider only — the burger (settings) lives solely on the amount
 * screen so settings can't be opened mid-transaction. */
void build_header(const char *title) {
    (void)make_title(title, false);
    make_divider(lv_scr_act(), HDR_DIVIDER_Y);
}

/******************************************************************
 * 8b. Wi-Fi signal — the wave, top right of every screen
 *
 * Three arcs over a dot, read at a glance from across a counter. The dot is at
 * least 4px: LVGL clamps the radius to half the side, so 3px draws a square.
 *
 * The sizes are one system. The two gaps must be equal, or the dot reads as
 * stuck to the first ring:
 *   dot to first ring:  SIG_R0 - (SIG_AW / 2) - (SIG_DOT / 2)
 *   ring to ring:       SIG_RSTEP - SIG_AW
 *
 * On the top layer so it survives screen swaps; a modal, created on the same
 * layer later, covers it.
 ******************************************************************/
#define SIG_ARCS   3
#define SIG_AW     2      /* arc thickness                                  */
/* Mark is 22x13; both gaps above are 1px. At r=4 the inner ring's 140-degree
 * cut still bends 2.6px, so it reads as an arc rather than as a dash. */
#define SIG_R0     4      /* innermost arc radius; each ring adds SIG_RSTEP */
#define SIG_RSTEP  3
/* Must stay even: an arc object is 2r + SIG_AW wide, so the fan's centre is a
 * whole coordinate, and only an even dot centres on it (odd sits half a pixel
 * off). */
#define SIG_DOT    4
/* Per-ring sweep, innermost first, centred on 12 o'clock. Inner rings get more
 * of their circle so their sagitta r·(1 - cos(sweep/2)) reads as a curve, not a
 * dash. The sweeps also set the icon's width (2r·sin(sweep/2)) without growing
 * its height. Past ~140° the inner ring looks like most of a circle; to widen
 * further, raise SIG_R0. */
static const uint16_t SIG_SWEEP[SIG_ARCS] = { 140, 110, 100 };

/* The outermost ring's outer edge, which is the icon's half-width and, with the
 * dot's bottom half, its height. Everything below is derived from it. */
#define SIG_REACH  (SIG_R0 + ((SIG_ARCS - 1) * SIG_RSTEP) + (SIG_AW / 2))
#define SIG_W      (2 * SIG_REACH)
#define SIG_H      (SIG_REACH + ((SIG_DOT + 1) / 2))
/* Inset from the right edge. */
#define SIG_INSET  14

static lv_obj_t *s_sig_arc[SIG_ARCS];
static lv_obj_t *s_sig_dot = NULL;
static lv_obj_t *s_sig_box = NULL;

static void signal_refresh(lv_timer_t *t) {
    (void)t;
    int8_t rssi = 0;
    /* net_wifi_rssi() fails when the station is not associated — grey dot, no
     * arc, which is the answer the operator needs before blaming the card.
     * The cuts match the words on the settings page (Good / Fair / Weak), so
     * the icon and the "Signal" field there cannot disagree. */
    const bool up  = net_wifi_rssi(&rssi);
    const int  lit = !up ? 0 : (rssi >= -60) ? 3 : (rssi >= -70) ? 2 : 1;
    for (int i = 0; i < SIG_ARCS; i++) {
        /* Unlit is COL_DIM, not COL_BORDER: the sale flow's page IS COL_BORDER,
         * so a COL_BORDER arc would vanish into it. See COL_PAGE. */
        lv_obj_set_style_arc_color(s_sig_arc[i],
                                   (i < lit) ? COL_TEXT : COL_DIM,
                                   LV_PART_MAIN);
    }
    /* Amber while the network is down and being re-joined in the background
     * (net.cpp): it tells the operator to wait rather than to go and fix
     * something. */
    lv_obj_set_style_bg_color(s_sig_dot,
                              up ? COL_TEXT
                                 : net_wifi_reconnecting() ? COL_WARN : COL_DIM,
                              LV_PART_MAIN);
    /* Hidden on the splash (nothing connected yet), the calibration screen (it
     * would cover a corner target), and the admin screens, whose tab bar owns
     * the top 42px; the Wi-Fi tab reports the signal in words instead. */
    const bool show = (s_req_screen != UI_SCREEN_SPLASH) &&
                      (s_req_screen != UI_SCREEN_TOUCH_CAL) &&
                      (s_req_screen != UI_SCREEN_SETTINGS) &&
                      (s_req_screen != UI_SCREEN_ADMIN_UNLOCK) &&
                      (s_req_screen != UI_SCREEN_ADMIN_SET) &&
                      /* the first-run setup flow: nothing to report yet */
                      (s_req_screen != UI_SCREEN_WELCOME) &&
                      (s_req_screen != UI_SCREEN_PROV) &&
                      (s_req_screen != UI_SCREEN_WIFI_CONNECTING) &&
                      (s_req_screen != UI_SCREEN_WIFI_LIST) &&
                      (s_req_screen != UI_SCREEN_WIFI_PASS) &&
                      (s_req_screen != UI_SCREEN_CARD_WAIT) &&
                      !((s_req_screen == UI_SCREEN_PIN) && s_pin_for_card);
    if (show) { lv_obj_clear_flag(s_sig_box, LV_OBJ_FLAG_HIDDEN); }
    else      { lv_obj_add_flag(s_sig_box, LV_OBJ_FLAG_HIDDEN); }

    /* Same cadence, same reason — the band's two occupants refresh together. */
    clock_refresh();
}
void signal_init(void) {
    s_sig_box = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_sig_box);
    lv_obj_set_size(s_sig_box, SIG_W, SIG_H);
    lv_obj_align(s_sig_box, LV_ALIGN_TOP_RIGHT, -SIG_INSET, 6);
    lv_obj_clear_flag(s_sig_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_sig_box, LV_OBJ_FLAG_CLICKABLE);

    /* Every ring is struck from the same point — the dot — so each arc object is
     * sized to its own ring and then centred on that point, not on the box. */
    const lv_coord_t cx = SIG_W / 2, cy = SIG_REACH;
    for (int i = 0; i < SIG_ARCS; i++) {
        const lv_coord_t d = (2 * (SIG_R0 + (i * SIG_RSTEP))) + SIG_AW;
        lv_obj_t *a = lv_arc_create(s_sig_box);
        lv_obj_remove_style_all(a);          /* also kills the knob and the
                                              * value indicator: both draw at
                                              * the default arc width of 0 */
        lv_obj_set_size(a, d, d);
        lv_obj_set_pos(a, cx - (d / 2), cy - (d / 2));
        /* 0° is 3 o'clock and angles run clockwise, so 270 is 12 o'clock and each
         * ring is cut symmetrically about it. */
        lv_arc_set_bg_angles(a, 270 - (SIG_SWEEP[i] / 2), 270 + (SIG_SWEEP[i] / 2));
        lv_obj_set_style_arc_width(a, SIG_AW, LV_PART_MAIN);
        /* Square-cut: a rounded cap on a 2px stroke anti-aliases into a darker
         * pixel past each tip. */
        lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
        lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
        s_sig_arc[i] = a;
    }

    s_sig_dot = lv_obj_create(s_sig_box);
    lv_obj_remove_style_all(s_sig_dot);
    lv_obj_set_size(s_sig_dot, SIG_DOT, SIG_DOT);
    lv_obj_set_pos(s_sig_dot, cx - (SIG_DOT / 2), cy - (SIG_DOT / 2));
    lv_obj_set_style_bg_opa(s_sig_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_sig_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(s_sig_dot, LV_OBJ_FLAG_CLICKABLE);

    /* ponytail: 3 s poll. An event-driven update would mean a Wi-Fi handler
     * poking LVGL from the event task — add one only if the lag shows. */
    (void)lv_timer_create(signal_refresh, 3000, NULL);
    signal_refresh(NULL);
}
void render_requested_screen(void) {
    /* A modal lives on the top layer, so it would survive the screen swap and
     * sit there swallowing every touch. Nothing wants that. */
    close_modal();

    /* The swipe's answer: build the admin code screen into a sheet below the
     * fold and slide it up over the sale screen. Only for the gesture; every
     * other route to this screen is an ordinary rebuild. */
    lv_obj_t *sheet = NULL;
    if (s_sheet_pending) {
        s_sheet_pending = false;
        if (s_req_screen == UI_SCREEN_ADMIN_UNLOCK) {
            sheet   = sheet_open();
            s_sheet = sheet;
        }
    }

    switch (s_req_screen) {
        case UI_SCREEN_SPLASH:    build_splash();    break;
        case UI_SCREEN_AMOUNT:    build_amount();    break;
        case UI_SCREEN_CONFIRM:   build_confirm();   break;
        case UI_SCREEN_PIN:       build_pin();       break;
        case UI_SCREEN_WIFI_LIST: build_wifi_list(); break;
        case UI_SCREEN_WIFI_PASS: build_wifi_pass(); break;
        case UI_SCREEN_WIFI_CONNECTING: build_wifi_connecting(); break;
        case UI_SCREEN_SETTINGS:  build_settings();  break;
        case UI_SCREEN_TX_STATUS: build_tx_status(); break;
        case UI_SCREEN_BOOT_ERROR:   build_boot_error();   break;
        case UI_SCREEN_ADMIN_SET:    build_admin_set();    break;
        case UI_SCREEN_ADMIN_UNLOCK: build_admin_unlock(); break;
        case UI_SCREEN_WELCOME:      build_welcome();      break;
        case UI_SCREEN_PROV:         build_prov();         break;
        case UI_SCREEN_CARD_WAIT:    build_card_wait();    break;
        case UI_SCREEN_TOUCH_CAL:    build_touch_cal();    break;
    }

    /* The pointer's job ends with the dispatch — a later rebuild of the same
     * screen (a wrong code, say) must go back through the ordinary clearing
     * path rather than stacking a second set of widgets into this one. */
    s_sheet = NULL;
    if (sheet != NULL) { sheet_slide_in(sheet); }

    /* Guard the freshly built screen against a tap carried over from the
     * previous one (see indev_read). The "Tap card" screen gets a longer
     * window because its Cancel button is destructive. */
    uint32_t lockout = 450U;
    if (s_req_screen == UI_SCREEN_TX_STATUS &&
        s_tx_state == UI_TX_STATE_PLACE_CARD) {
        lockout = 900U;
    }
    s_input_block_until = lv_tick_get() + lockout;
    s_wait_release      = true;

    /* Now, not on the next poll, so the mark's visibility matches the new
     * screen without a three-second lag. */
    if (s_sig_box != NULL) { signal_refresh(NULL); }
}
