/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ui_sale.cpp
 * @ingroup ui
 * @brief The sale flow: amount, confirm, PIN, transaction status, and their page chrome.
 */

#include "ui_internal.h"

#define CHIP_W      56

/**
 * Place the figure's row: on the screen's centre line, and on the coin's.
 *
 * Vertically: the row's measured height against the selector's, so the digits'
 * ink middle (amount_ink_shift(), read off the font's '0') lands on the coin's.
 *
 * Horizontally: the screen's centre, which the keypad and Charge button share.
 * The row slides left only as far as it must to clear the selector, so a typical
 * amount stays centred. Both widths are measured, never predicted. Nothing on
 * this row is dropped to make it fit: the figure is money.
 */
/* How far below the line box's middle the middle of a digit's ink sits. */
static lv_coord_t amount_ink_shift(const lv_font_t *f) {
    lv_font_glyph_dsc_t g;
    if (!lv_font_get_glyph_dsc(f, &g, '0', 0)) { return 0; }
    const lv_coord_t ink_mid = f->line_height - f->base_line - g.ofs_y - (g.box_h / 2);
    return (f->line_height / 2) - ink_mid;
}

static void amount_row_place(void) {
    if (s_amount_row == NULL) { return; }

    /* Centred, then measured there: the pill and the figure are both
     * content-sized (chevron on/off, 3- or 4-letter ticker). */
    lv_obj_align(s_amount_row, LV_ALIGN_TOP_MID, 0, ASSET_BTN_Y);
    lv_obj_update_layout(s_amount_row);

    /* Measured off the pill's own left edge, not SCR_W: the row lives on the
     * card. All in absolute coordinates, as lv_obj_get_coords reports. */
    lv_area_t   r, pr;
    lv_obj_get_coords(s_amount_row, &r);
    lv_coord_t  stop = r.x2;
    if (s_asset_btn != NULL) {
        lv_obj_get_coords(s_asset_btn, &pr);
        stop = pr.x1 - AMOUNT_ROW_GAP;
    }
    lv_coord_t  x = (r.x2 > stop) ? (stop - r.x2) : 0;

    /* ...but never off the left edge: the leading digits decide what the
     * customer is charged. Overlapping the pill is survivable; silently losing
     * the left-hand digits is not, so the clamp wins. Measured against the
     * row's parent (the inset card), not the screen. */
    lv_area_t cr;
    lv_obj_get_coords(lv_obj_get_parent(s_amount_row), &cr);
    if ((r.x1 + x) < (cr.x1 + AMOUNT_ROW_MIN_X)) {
        x = (cr.x1 + AMOUNT_ROW_MIN_X) - r.x1;
    }

    lv_obj_align(s_amount_row, LV_ALIGN_TOP_MID, x,
                 ASSET_BTN_Y + ((ASSET_BTN_H - lv_area_get_height(&r)) / 2)
                     + amount_ink_shift(&font_pjs_28_semibold));
}

/**
 * Charge is available only once there is something to charge.
 *
 * 0.00 is not a sale; a lit button that ignores the tap reads as a missed touch
 * on a resistive panel. btn_event_cb drops the event; this makes that visible.
 *
 * LV_STATE_DISABLED rather than hiding: LVGL's hit test drops taps on a disabled
 * object (lv_obj_pos.c), so the state is the whole gate. The label is recoloured
 * by hand because make_button() styles the label itself, out of the state's reach.
 */
static void charge_set_enabled(bool on) {
    if (s_charge_btn == NULL) { return; }   /* not the amount screen */

    if (on) { lv_obj_clear_state(s_charge_btn, LV_STATE_DISABLED); }
    else    { lv_obj_add_state(s_charge_btn, LV_STATE_DISABLED); }

    lv_obj_set_style_bg_color(s_charge_btn, on ? COL_ACTION : COL_SURFACE,
                              LV_PART_MAIN);
    lv_obj_t *lbl = lv_obj_get_child(s_charge_btn, 0);
    if (lbl != NULL) {
        lv_obj_set_style_text_color(lbl, on ? COL_BG : COL_DIM, LV_PART_MAIN);
    }
}

/**
 * Charge has been tapped: inert until this screen is next built.
 *
 * A gate, not a look: it does not repaint the button. It stops a second
 * UI_EVENT_AMOUNT_CONFIRMED queued behind the first, whose echo would otherwise
 * pull an operator who double-tapped Charge back out of the PIN keypad.
 */
void charge_set_busy(void) {
    if (s_charge_btn == NULL) { return; }
    s_charge_busy = true;
    /* LVGL's hit test drops taps on a disabled object (lv_obj_pos.c). */
    lv_obj_add_state(s_charge_btn, LV_STATE_DISABLED);
}

static void amount_update_display(void) {
    char buf[24];
    const bool split = (s_amount_cents >= AMOUNT_SMALL_CENTS_FROM);

    if (s_amount_label != NULL) {
        if (split) {
            snprintf(buf, sizeof(buf), "%" PRIu64, s_amount_cents / 100ULL);
        } else {
            snprintf(buf, sizeof(buf), "%" PRIu64 ".%02" PRIu64,
                     s_amount_cents / 100ULL, s_amount_cents % 100ULL);
        }
        lv_label_set_text(s_amount_label, buf);
    }
    if (s_amount_cents_label != NULL) {
        snprintf(buf, sizeof(buf), ".%02" PRIu64, s_amount_cents % 100ULL);
        /* "" rather than the hidden flag: an empty label measures zero and the
         * flex row re-centres on its own. */
        lv_label_set_text(s_amount_cents_label, split ? buf : "");
    }

    /* The selector keeps its chevron until there is an amount, then gives the
     * digits the room back. The ticker stays either way. */
    asset_btn_set_compact(s_amount_cents != 0ULL);
    /* Left alone while a tap is in flight: the keypad stays live, so a digit
     * typed in that window would re-arm a button that has already asked. */
    if (!s_charge_busy) {
        charge_set_enabled(s_amount_cents != 0ULL);
    }

    /* Last: the pill has just changed width and the labels have just changed text,
     * and the row is placed against both. */
    amount_row_place();

    s_amount_units = amount_cents_to_units(s_amount_cents);   /* -> 6-decimal base units */
}

/* The drawn backspace, in units of half its height — see kbd_backspace_draw_cb().
 * The tag is 2*BSP_H tall and BSP_W wide with a BSP_NOSE-deep point on the left,
 * which is the outline the LVGL symbol draws solid. */
#define BSP_W     16   /* half-length; 32 wide against 18 tall reads as the
                        * conventional backspace proportion (tuned by eye) */
#define BSP_H      9
#define BSP_NOSE   6
#define BSP_CROSS  4
#define BSP_LINE   2
/* The corner radius. The right-hand corners are rounded over two segments each
 * (a right-angle chamfer reads as a flat cut); the nose's 124-degree corners are
 * chamfered, since a fillet there would eat most of the 6 px nose.
 *
 * Lines, not lv_draw_arc: a tiny arc antialiases differently from the straight
 * runs and top/bottom differently from each other, so the corners look jittery.
 * Two segments through 45 degrees are within 0.4 px of a true quarter-circle.
 *
 * Integers everywhere, so the cuts above and below the centre line mirror
 * exactly. The nose's (2,3) is one third of its 6:9 slope, near enough BSP_R. */
#define BSP_R        3
#define BSP_R45      2   /* round(BSP_R * sin 45) — the fillet's middle point */
#define BSP_NOSE_DX  2   /* the nose's cut, one third of its run... */
#define BSP_NOSE_DY  3   /* ...and one third of its rise */

/* Keys tile the pad top to bottom: the default theme's btnmatrix padding and row
 * gap are dead zones where a touch lands on no key. */
static void kbd_fill_rows(lv_obj_t *kb) {
    lv_obj_set_style_pad_ver(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(kb, 0, LV_PART_MAIN);
}

/**
 * Draw the keypad's backspace as an outline rather than a solid glyph.
 *
 * LV_SYMBOL_BACKSPACE is a filled slab at 28px, the heaviest mark beside thin
 * numerals. It keeps the digits' colour: grey would read as disabled.
 *
 * Drawn because a btnmatrix key holds text and the symbol font has no outline
 * backspace: DRAW_PART_BEGIN hides the glyph, END draws in the key's own area.
 * Used on the amount, PIN and admin pads; matched on the key's text, not index.
 */
static void kbd_backspace_draw_cb(lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if ((dsc == NULL) || (dsc->class_p != &lv_btnmatrix_class) ||
        (dsc->type != LV_BTNMATRIX_DRAW_PART_BTN)) {
        return;
    }
    const char *key = lv_btnmatrix_get_btn_text(lv_event_get_target(e), dsc->id);
    if ((key == NULL) || (strcmp(key, LV_SYMBOL_BACKSPACE) != 0)) { return; }

    /* The glyph is still in the map — amount_kbd_cb() matches on that string —
     * so it is hidden at draw time instead of removed. */
    if (lv_event_get_code(e) == LV_EVENT_DRAW_PART_BEGIN) {
        if (dsc->label_dsc != NULL) { dsc->label_dsc->opa = LV_OPA_TRANSP; }
        return;
    }

    const lv_coord_t cx = (dsc->draw_area->x1 + dsc->draw_area->x2) / 2;
    const lv_coord_t cy = (dsc->draw_area->y1 + dsc->draw_area->y2) / 2;

    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = COL_TEXT;          /* the digits' own ink, as asked */
    ld.width = BSP_LINE;
    ld.round_start = 1;
    ld.round_end   = 1;

    /* Every coordinate named once, as lv_coord_t: int arithmetic inside the
     * braced point list would be a narrowing conversion. */
    const lv_coord_t xr = (lv_coord_t)(cx + BSP_W);                 /* right edge */
    const lv_coord_t xt = (lv_coord_t)(cx - BSP_W);                 /* the point  */
    const lv_coord_t nx = (lv_coord_t)(cx - BSP_W + BSP_NOSE);      /* nose's base*/
    const lv_coord_t xn = (lv_coord_t)(nx - BSP_NOSE_DX);           /* its cut    */
    const lv_coord_t l  = (lv_coord_t)(nx + BSP_R);   /* straight runs start/end */
    const lv_coord_t r  = (lv_coord_t)(xr - BSP_R);
    const lv_coord_t t  = (lv_coord_t)(cy - BSP_H);
    const lv_coord_t b  = (lv_coord_t)(cy + BSP_H);
    const lv_coord_t tr = (lv_coord_t)(t + BSP_R);    /* where the arcs take over */
    const lv_coord_t br = (lv_coord_t)(b - BSP_R);
    const lv_coord_t tn = (lv_coord_t)(t + BSP_NOSE_DY);
    const lv_coord_t bn = (lv_coord_t)(b - BSP_NOSE_DY);
    /* The 45-degree point of each right-hand fillet: its centre is (r, tr) at the
     * top and (r, br) at the bottom. Written from t and b so the two mirror by
     * construction. */
    const lv_coord_t xd = (lv_coord_t)(r + BSP_R45);
    const lv_coord_t td = (lv_coord_t)(t + BSP_R - BSP_R45);
    const lv_coord_t bd = (lv_coord_t)(b - BSP_R + BSP_R45);

    /* The whole outline in one primitive, clockwise from the top: straight run,
     * fillet, right edge, fillet, straight run, then the nose. */
    const lv_point_t seg[11][2] = {
        { { l,  t  }, { r,  t  } },
        { { r,  t  }, { xd, td } },
        { { xd, td }, { xr, tr } },
        { { xr, tr }, { xr, br } },
        { { xr, br }, { xd, bd } },
        { { xd, bd }, { r,  b  } },
        { { r,  b  }, { l,  b  } },
        { { l,  b  }, { xn, bn } },
        { { xn, bn }, { xt, cy } },
        { { xt, cy }, { xn, tn } },
        { { xn, tn }, { l,  t  } },
    };
    for (int i = 0; i < 11; i++) {
        lv_draw_line(dsc->draw_ctx, &ld, &seg[i][0], &seg[i][1]);
    }

    /* The cross, centred in the square end: cx + BSP_NOSE/2 whatever BSP_W is. */
    const lv_coord_t kx = cx + (BSP_NOSE / 2);
    const lv_point_t a1 = { (lv_coord_t)(kx - BSP_CROSS), (lv_coord_t)(cy - BSP_CROSS) };
    const lv_point_t a2 = { (lv_coord_t)(kx + BSP_CROSS), (lv_coord_t)(cy + BSP_CROSS) };
    const lv_point_t b1 = { (lv_coord_t)(kx + BSP_CROSS), (lv_coord_t)(cy - BSP_CROSS) };
    const lv_point_t b2 = { (lv_coord_t)(kx - BSP_CROSS), (lv_coord_t)(cy + BSP_CROSS) };
    lv_draw_line(dsc->draw_ctx, &ld, &a1, &a2);
    lv_draw_line(dsc->draw_ctx, &ld, &b1, &b2);
}

/* Keypad on the amount screen — cents entry: each digit shifts in
 * from the right (1 -> 0.01, 12 -> 0.12, 1250 -> 12.50). No decimal point. */
static void amount_kbd_cb(lv_event_t *e) {
    lv_obj_t *bm = lv_event_get_target(e);
    const char *txt = lv_btnmatrix_get_btn_text(bm, lv_btnmatrix_get_selected_btn(bm));
    if (txt == NULL) { return; }

    const uint64_t cap = amount_cents_max();
    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        s_amount_cents = amount_key_back(s_amount_cents);
    } else if (strcmp(txt, "00") == 0) {
        s_amount_cents = amount_key_00(s_amount_cents, cap);
    } else if ((txt[0] >= '0') && (txt[0] <= '9') && (txt[1] == '\0')) {
        s_amount_cents = amount_key_digit(s_amount_cents,
                                          static_cast<unsigned>(txt[0] - '0'), cap);
    }
    amount_update_display();
}

/**
 * Flip a masked field between hidden and shown, and swap its eye glyph to match.
 *
 * Toggled in place, never via request_screen(): rebuilding the screen would
 * discard what has been typed, which on the PIN keypad costs the operator an
 * attempt the card is counting. The glyph shows what the next tap does.
 * Either argument may be NULL; nothing but convention ties the action to its screen.
 */
void code_field_reveal(lv_obj_t *ta, lv_obj_t *eye_lbl) {
    if (ta == NULL) { return; }
    const bool masked = lv_textarea_get_password_mode(ta);
    lv_textarea_set_password_mode(ta, !masked);
    if (eye_lbl != NULL) {
        lv_label_set_text(eye_lbl, masked ? LV_SYMBOL_EYE_CLOSE
                                          : LV_SYMBOL_EYE_OPEN);
    }
}
void build_splash(void) {
    clear_screen();
    /* The logo is black-on-white; put the whole splash on white so it blends. */
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), LV_PART_MAIN);

    lv_obj_t *logo = lv_img_create(lv_scr_act());
    lv_img_set_src(logo, &logo_img);
    lv_obj_align(logo, LV_ALIGN_CENTER, LOGO_X_NUDGE, -36);
    /* No pop_in() here: at power-on the backlight has only just come up, so a
     * scale-in reads as the logo blinking. Kept for the tx check/cross. */

    /* Tight under the logo (the image carries its own breathing margin). */
    make_label(lv_scr_act(), "cryptnox-pos", lv_color_black(),
               &font_pjs_20_medium, LV_ALIGN_CENTER, 0, 40);

    /* Boot feedback — the splash stays up while Wi-Fi/SNTP/RPC come up, so
     * show a discreet spinner instead of looking frozen. */
    lv_obj_t *sp = lv_spinner_create(lv_scr_act(), 1000, 60);
    lv_obj_set_size(sp, 28, 28);
    lv_obj_align(sp, LV_ALIGN_BOTTOM_MID, 0, -48);
    lv_obj_set_style_arc_width(sp, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sp, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, COL_ACCENT, LV_PART_INDICATOR);

    /* Which boot step is running, so a slow start is legible rather than
     * looking frozen. Discreet, and elided by width to never overflow. */
    s_boot_step_lbl = make_label(lv_scr_act(), s_boot_step, COL_DIM,
                                 &font_inter_14,
                                 LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_width(s_boot_step_lbl, SCR_W - 24);
    lv_label_set_long_mode(s_boot_step_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_boot_step_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_boot_step_lbl, LV_ALIGN_BOTTOM_MID, 0, -20);
}
void build_amount(void) {
    /* Step one of four. The admin panel is behind the swipe up from the home
     * indicator, so the only chrome on the teal is the step dashes and test chip. */
    lv_obj_t *card = build_page(PAY_STEP_AMOUNT);
    add_test_chip();

    /* One row: the amount with the asset selector at its right-hand end, so the
     * figure and currency read together (placed by amount_row_place()). The
     * card's 262px is fully spent: row 10..52, keypad 58..206, Charge 210..254. */
    make_asset_button(card);

    /* Figure and small cents in one content-sized flex row, so the group centres
     * itself whatever the fonts measure. Bottom-aligned, which puts the cents on
     * the big font's baseline near enough (LVGL 8 has no baseline align). */
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    s_amount_row = row;   /* placed by amount_update_display below */

    s_amount_label = make_label(row, "0.00", COL_TEXT,
                                &font_pjs_28_semibold, LV_ALIGN_DEFAULT, 0, 0);
    s_amount_cents_label = make_label(row, "", COL_TEXT,
                                      &font_pjs_20_semibold, LV_ALIGN_DEFAULT, 0, 0);

    /* Numeric keypad: digits, double-zero, backspace (cents entry). */
    static const char *amap[] = {
        "1", "2", "3", "\n",
        "4", "5", "6", "\n",
        "7", "8", "9", "\n",
        "00", "0", LV_SYMBOL_BACKSPACE, ""
    };
    lv_obj_t *kb = lv_btnmatrix_create(card);
    lv_btnmatrix_set_map(kb, amap);
    lv_obj_set_size(kb, CARD_W - (2 * CARD_PAD), 148);
    lv_obj_align(kb, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    /* Same columns as the PIN and admin pads (see make_numeric_keypad), so the
     * digits stand in one place on every keypad screen. */
    lv_obj_set_style_pad_hor(kb, 0, LV_PART_MAIN);
    kbd_fill_rows(kb);
    /* Minimal keypad: no key boxes — black glyphs on white, grey flash on press. */
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, COL_SURFACE, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &font_pjs_28_light, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, amount_kbd_cb, LV_EVENT_VALUE_CHANGED, NULL);
    /* Redraws the backspace as an outline — see kbd_backspace_draw_cb(). Both
     * halves: BEGIN hides the solid glyph, END draws over the key. */
    lv_obj_add_event_cb(kb, kbd_backspace_draw_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    lv_obj_add_event_cb(kb, kbd_backspace_draw_cb, LV_EVENT_DRAW_PART_END, NULL);

    s_charge_btn = make_button(card, "Charge", COL_ACTION, COL_BG,
                               CARD_BTN_W, CARD_BTN_H,
                               LV_ALIGN_BOTTOM_MID, 0, CARD_BTN_Y, ACT_CONFIRM,
                               &font_pjs_20_semibold);

    /* Also settles the Charge button: the screen is rebuilt on an asset change
     * with whatever was typed still standing, so it must not come back lit on an
     * empty figure or dead on a full one. */
    amount_update_display();   /* keep s_amount_units in sync with the string */
}
void build_confirm(void) {
    /* Step two of four. */
    lv_obj_t *card = build_page(PAY_STEP_REVIEW);

    /* Ledger-style transaction review: dim caption / value rows, everything
     * the operator should verify — amount, beneficiary AND the USDC contract
     * the terminal is about to call. All coordinates are the card's. */
    const lv_coord_t VW = CARD_W - (2 * CARD_PAD);
    char buf[24];
    amount_format(s_confirm_amount, buf, sizeof(buf));

    /* The vertical budget is fully spent: Total to 58, the two address rows to
     * 180 (both always wrap to two lines), the test warning to 203, buttons from
     * 208. Move anything down and the warning runs under the buttons. */
    make_label(card, "Total", COL_DIM, &font_inter_14,
               LV_ALIGN_TOP_LEFT, CARD_PAD, 8);

    /* Opposite "Total", costing no height: on testnet, which deployment, in red.
     * Mainnet stays silent (as settings_net_str does), which is what makes the
     * testnet form stand out on the last screen before the card is tapped. */
    const bool mainnet = settings_get_mainnet();
    if (!mainnet) {
        make_label(card, pos_net_info(asset()->net)->sub_test, COL_DANGER,
                   &font_inter_14, LV_ALIGN_TOP_RIGHT, -CARD_PAD, 8);
    }

    lv_obj_t *amt = make_label(card, buf, COL_TEXT, &font_pjs_28_semibold,
                               LV_ALIGN_TOP_LEFT, CARD_PAD, 24);
    lv_obj_t *cusdc = make_asset_badge(card, settings_get_chain());
    lv_obj_align_to(cusdc, amt, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
    /* Ticker beside the mark: the mark alone only names the asset to someone
     * who knows the logos. Gaps of 6 and 4 because "9999.99", the badge and a
     * four-letter ticker reach x=212 against the card's 214. */
    lv_obj_t *tick = make_label(card, asset_name(), COL_TEXT,
                                &font_inter_14_medium, LV_ALIGN_DEFAULT, 0, 0);
    lv_obj_align_to(tick, cusdc, LV_ALIGN_OUT_RIGHT_MID, 4, 0);

    make_label(card, "To", COL_DIM, &font_inter_14,
               LV_ALIGN_TOP_LEFT, CARD_PAD, 64);
    /* The most the card can be charged in network fees on top of the total —
     * the customer is agreeing to that too. Opposite "To" on its caption row, the
     * way the test-network name sits opposite "Total": the card has no height
     * left to give it a row of its own. ~150px at its longest against 204. */
    if (s_confirm_fee[0] != '\0') {
        make_label(card, s_confirm_fee, COL_DIM, &font_inter_14,
                   LV_ALIGN_TOP_RIGHT, -CARD_PAD, 64);
    }
    lv_obj_t *addr = make_label(card,
                                s_confirm_addr[0] ? s_confirm_addr : "-",
                                COL_TEXT, &font_inter_14_medium,
                                LV_ALIGN_TOP_LEFT, CARD_PAD, 82);
    lv_label_set_long_mode(addr, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(addr, VW);

    make_label(card, asset_caption(),
               COL_DIM, &font_inter_14, LV_ALIGN_TOP_LEFT, CARD_PAD, 126);
    lv_obj_t *ctr = make_label(card,
                               (s_addr_usdc != NULL) ? s_addr_usdc : "-",
                               COL_TEXT, &font_inter_14_medium,
                               LV_ALIGN_TOP_LEFT, CARD_PAD, 144);
    lv_label_set_long_mode(ctr, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ctr, VW);

    /* The warning in words, aligned under the contract row (which wraps to one
     * or two lines). Deliberately NOT given a width: a wrapped second line would
     * run under the buttons, and the sentence is ~190px against the card's 204. */
    if (!mainnet) {
        lv_obj_t *tn = make_label(card, "Test network - no real funds",
                                  COL_DANGER, &font_inter_14,
                                  LV_ALIGN_DEFAULT, 0, 0);
        lv_obj_update_layout(ctr);
        lv_obj_align_to(tn, ctr, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    }

    /* Cancel is a ghost button: available, without competing with the button
     * that commits. */
    const lv_coord_t half = (CARD_W - (2 * CARD_PAD) - 8) / 2;
    make_ghost_button(card, "Cancel", half,
                      LV_ALIGN_BOTTOM_LEFT, CARD_PAD, CARD_BTN_Y, ACT_CANCEL);
    make_button(card, "Confirm", COL_ACTION, COL_BG, half, CARD_BTN_H,
                LV_ALIGN_BOTTOM_RIGHT, -CARD_PAD, CARD_BTN_Y, ACT_SEND,
                &font_pjs_20_semibold);
}

/* OK pressed on the keypad: stash the PIN for main and move to the tx screen. */
static void pin_submit(void) {
    if (s_pin_ta == NULL) { return; }
    const char *p = lv_textarea_get_text(s_pin_ta);
    size_t len = (p != NULL) ? strlen(p) : 0U;
    if (len < 4U) { return; }   /* require at least 4 digits */

    strncpy(s_pin, p, sizeof(s_pin) - 1);
    s_pin[sizeof(s_pin) - 1] = '\0';
    s_pin_len = static_cast<uint8_t>(strlen(s_pin));

    /* Same keypad, two errands. A card read is not a payment: it gets the card
     * screen and its own event, so main cannot mistake it for a sale and the tx
     * screen's "Declined" wording never appears over a setup step. */
    if (s_pin_for_card) {
        s_pin_for_card = false;
        strncpy(s_card_note, "Reading your payout addresses",
                sizeof(s_card_note) - 1);
        s_card_note[sizeof(s_card_note) - 1] = '\0';
        request_screen(UI_SCREEN_CARD_WAIT);
        if (s_cb != NULL) { s_cb(UI_EVENT_CARD_PIN, 0); }
        return;
    }

    s_tx_state = UI_TX_STATE_PLACE_CARD;
    strncpy(s_tx_info, "Preparing...", sizeof(s_tx_info) - 1);
    s_tx_info[sizeof(s_tx_info) - 1] = '\0';
    request_screen(UI_SCREEN_TX_STATUS);
    if (s_cb != NULL) { s_cb(UI_EVENT_PIN_ENTERED, 0); }
}

static void pin_kbd_cb(lv_event_t *e) {
    lv_obj_t *bm = lv_event_get_target(e);
    uint32_t id = lv_btnmatrix_get_selected_btn(bm);
    const char *txt = lv_btnmatrix_get_btn_text(bm, id);
    if ((txt == NULL) || (s_pin_ta == NULL)) { return; }

    if (strcmp(txt, LV_SYMBOL_OK) == 0) {
        pin_submit();
    } else if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_del_char(s_pin_ta);
    } else if ((txt[0] >= '0') && (txt[0] <= '9') && (txt[1] == '\0')) {
        lv_textarea_add_char(s_pin_ta, static_cast<uint32_t>(txt[0]));
    }
}
/* Show the hint while the field is empty, hide it once anything is typed.
 * Driven by the textarea's VALUE_CHANGED, which LVGL sends from every mutating
 * call, so backspacing to empty or a reset brings it back without the caller. */
static void code_hint_cb(lv_event_t *e) {
    lv_obj_t *ta   = lv_event_get_target(e);
    lv_obj_t *hint = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    const char *txt = lv_textarea_get_text(ta);
    if (hint == NULL) { return; }
    if ((txt != NULL) && (txt[0] != '\0')) {
        lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(hint, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *make_code_field(uint32_t max_len, lv_coord_t x1, lv_coord_t y,
                                 const char *hint, lv_coord_t w,
                                 lv_obj_t *parent = NULL) {
    lv_obj_t *host = (parent != NULL) ? parent : lv_scr_act();
    lv_obj_t *ta = lv_textarea_create(host);
    lv_textarea_set_password_mode(ta, true);
    /* Echo each digit briefly so the operator can confirm the keypress, then
     * mask — a third of LVGL's 1500 ms default, which leaks the whole code. */
    lv_textarea_set_password_show_time(ta, 500);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    lv_textarea_set_text(ta, "");
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(ta, w);
    /* Trimmed off the theme's field padding, which made a one-line box as tall
     * as two keypad rows. Fill, radius and text colour come from s_st_field. */
    lv_obj_set_style_pad_ver(ta, FIELD_PAD_V, LV_PART_MAIN);
    lv_obj_align(ta, LV_ALIGN_TOP_LEFT, x1, y);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    /* What to type, in the box it is typed into. A label of our own, NOT
     * lv_textarea's placeholder: draw_placeholder() sets LV_TEXT_FLAG_EXPAND on
     * one-line fields, so it cannot be centred. Aligned to the field itself. */
    if (hint != NULL) {
        lv_obj_t *l = make_label(host, hint, COL_DIM,
                                 &font_inter_14, LV_ALIGN_DEFAULT, 0, 0);
        lv_obj_update_layout(ta);
        lv_obj_align_to(l, ta, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_event_cb(ta, code_hint_cb, LV_EVENT_VALUE_CHANGED, l);
    }
    return ta;
}
lv_obj_t *make_numeric_keypad(lv_event_cb_t cb, lv_obj_t *parent,
                                     lv_coord_t w,
                                     lv_coord_t h) {
    static const char *kbd_map[] = {
        "1", "2", "3", "\n",
        "4", "5", "6", "\n",
        "7", "8", "9", "\n",
        LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""
    };
    lv_obj_t *kb = lv_btnmatrix_create((parent != NULL) ? parent : lv_scr_act());
    lv_btnmatrix_set_map(kb, kbd_map);
    lv_obj_set_size(kb, w, h);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    /* No side inset: the outer keys' press slabs run to the keypad's edges, the
     * code field's left edge and the eye's right edge. */
    lv_obj_set_style_pad_hor(kb, 0, LV_PART_MAIN);
    kbd_fill_rows(kb);
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, COL_SURFACE, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &font_pjs_28_light, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, cb, LV_EVENT_VALUE_CHANGED, NULL);
    /* The amount screen's outline backspace, on the PIN and admin pads too. */
    lv_obj_add_event_cb(kb, kbd_backspace_draw_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    lv_obj_add_event_cb(kb, kbd_backspace_draw_cb, LV_EVENT_DRAW_PART_END, NULL);
    return kb;
}

/* The field and its reveal eye, laid on the keypad's columns below them: the
 * field spans the 1 and 2 keys, the eye is the 3 key's column with its glyph
 * centred over the 3/6/9 glyphs. Read off the keypad's own button areas rather
 * than recomputed, so the row cannot drift from the keys whatever gap the theme
 * puts between them. Build the keypad first. */
lv_obj_t *make_code_row(lv_obj_t *host, lv_obj_t *kb, uint32_t max_len,
                               const char *hint, BtnAction eye_act,
                               lv_obj_t **eye_lbl) {
    lv_obj_update_layout(kb);
    const lv_area_t *k = reinterpret_cast<lv_btnmatrix_t *>(kb)->button_areas;
    const lv_coord_t kx = lv_obj_get_x(kb);   /* k[] is relative to the keypad */
    /* The left edge comes in from the 1 key's slab (the eye reads the pad's edge
     * off the digits); the right runs @c grow past the 2 key's slab into the eye
     * button's empty side. Tune both by eye. */
    const lv_coord_t inset = 8;
    const lv_coord_t grow  = 12;
    const lv_coord_t w = ((k[1].x2 - k[0].x1) + 1) - inset + grow;
    /* No reveal (see build_pin()): nothing in the 3 key's column, so the field
     * centres over the pad rather than leaving that column empty beside it. */
    const lv_coord_t x1 = (eye_lbl == NULL)
                            ? kx + ((lv_obj_get_width(kb) - w) / 2)
                            : kx + k[0].x1 + inset;
    lv_obj_t *ta = make_code_field(max_len, x1, 44, hint, w, host);
    if (eye_lbl == NULL) { return ta; }
    /* make_icon_button() places itself top-left for the burger; move it, and
     * keep the label handle so the glyph can be swapped in place. */
    lv_obj_t *eye = make_icon_button(LV_SYMBOL_EYE_OPEN, eye_act, host);
    lv_obj_set_width(eye, (k[2].x2 - k[2].x1) + 1);
    lv_obj_align_to(eye, ta, LV_ALIGN_OUT_RIGHT_MID,
                    (k[2].x1 - k[1].x2) - 1 - grow, 0);   /* still on the 3 key */
    *eye_lbl = lv_obj_get_child(eye, 0);
    return ta;
}
void build_pin(void) {
    /* Step three of four — authorising the card. A card read during first-run
     * setup borrows this screen too, and that is not a sale, so it gets the
     * chrome without a lit dash. */
    lv_obj_t *card = build_page(s_pin_for_card ? PAY_STEP_NONE : PAY_STEP_AUTH,
                                !s_pin_for_card);   /* setup read: no band */
    /* Wipe any stale PIN from a previous attempt. */
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(s_pin), sizeof(s_pin));
    s_pin_len = 0;

    (void)make_title(s_pin_for_card ? "Card PIN" : "Enter PIN", true, card);
    /* Back (cancel) icon, top-left of the card. */
    (void)make_icon_button(LV_SYMBOL_LEFT, ACT_PIN_CANCEL, card);

    /* The reveal eye exists because a PIN typed blind on a resistive panel and
     * refused costs a card attempt. Only on the setup read, though: during a sale
     * this screen faces the customer and the PIN is theirs, so there is no eye
     * and no per-digit echo for a bystander to read. */
    lv_obj_t *kb = make_numeric_keypad(pin_kbd_cb, card, CODE_KBD_W, 170);
    s_pin_ta = make_code_row(card, kb, 9U, "Card PIN", ACT_PIN_REVEAL,
                             s_pin_for_card ? &s_pin_eye_lbl : NULL);
    if (!s_pin_for_card) { lv_textarea_set_password_show_time(s_pin_ta, 0); }
}

/* Animate an object's zoom (256 = 100%) — used for the success-check pop. */
static void zoom_anim_cb(void *obj, int32_t v) {
    lv_obj_set_style_transform_zoom(static_cast<lv_obj_t *>(obj), v, LV_PART_MAIN);
}

/* Pop-in: scale from small to full with a slight overshoot/bounce. */
void pop_in(lv_obj_t *obj) {
    lv_obj_update_layout(obj);
    lv_obj_set_style_transform_pivot_x(obj, lv_obj_get_width(obj) / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(obj, lv_obj_get_height(obj) / 2, LV_PART_MAIN);
    /* Apply the start scale before the first render: lv_anim_start() only calls
     * the exec cb on its first tick, so the object would flash at full size. */
    zoom_anim_cb(obj, 10);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, zoom_anim_cb);
    lv_anim_set_values(&a, 10, 256);          /* ~4% -> 100% (pronounced pop) */
    lv_anim_set_time(&a, 420);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);
}

/* "0x1234...abcd" — head and tail of a transaction hash. 66 characters do not
 * fit on a 240px panel at a readable size, and the ends are what somebody
 * matches against a block explorer. Tron hashes carry no 0x, which is why this
 * takes six characters from the front rather than skipping a prefix. */
static void hash_short(const char *h, char *out, size_t n) {
    size_t len = strlen(h);
    if ((len <= 14U) || (n < 16U)) {
        snprintf(out, n, "%s", h);
        return;
    }
    snprintf(out, n, "%.6s...%s", h, h + len - 4U);
}

/* "<amount> [coin mark] <TICKER>" centred at offset y from the top.
 *
 * @p ticker names the asset beside its mark, the way the amount and confirm
 * screens do. It is on for the tap screen and the "Approved" receipt: the mark
 * alone only says which asset to somebody who already knows the logos.
 *
 * A content-sized flex row, so the group centres itself whatever its widths.
 * @p y is where the figure's line box starts; the taller badge lifts the row by
 * half the difference. */
static void tx_amount_row(lv_obj_t *parent, const char *amt,
                          const lv_font_t *font, lv_coord_t y, bool ticker) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);

    lv_obj_t *al = make_label(row, amt, COL_TEXT, font, LV_ALIGN_DEFAULT, 0, 0);
    (void)make_asset_badge(row, settings_get_chain());
    if (ticker) {
        (void)make_label(row, asset_name(), COL_TEXT, &font_inter_14_medium,
                         LV_ALIGN_DEFAULT, 0, 0);
    }

    lv_obj_update_layout(row);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0,
                 y - ((lv_obj_get_height(row) - lv_obj_get_height(al)) / 2));
}
void build_tx_status(void) {
    /* The last step of the sale: "Total", the figure, the prompt, the
     * contactless mark, one way out. */
    lv_obj_t *card = build_page(PAY_STEP_TAP);
    const lv_coord_t VW = CARD_W - (2 * CARD_PAD);

    char amt[24];
    amount_format(s_confirm_amount, amt, sizeof(amt));

    if (s_tx_state == UI_TX_STATE_PLACE_CARD) {
        /* Total at 8 and the figure at 38: the badge is taller than the 28px
         * line it is centred on and needs the clearance from "Total". */
        make_label(card, "Total", COL_DIM, &font_inter_14,
                   LV_ALIGN_TOP_MID, 0, 8);
        tx_amount_row(card, amt, &font_pjs_28_semibold, 38, true);

        make_label(card, "Tap your card", COL_DIM, &font_pjs_20_medium,
                   LV_ALIGN_TOP_MID, 0, 82);

        /* Centred in the band between the prompt's 20px line (ends ~104) and
         * the Cancel button (starts 208). */
        make_tap_mark(card, 106 + ((102 - TAP_MARK_H) / 2));

        (void)make_ghost_button(card, "Cancel", CARD_BTN_W,
                                LV_ALIGN_BOTTOM_MID, 0, CARD_BTN_Y, ACT_CANCEL);
        return;
    }

    if (s_tx_state == UI_TX_STATE_DONE) {
        lv_obj_t *chk = make_label(card, LV_SYMBOL_OK, COL_SUCCESS,
                                   &font_icons_48, LV_ALIGN_TOP_MID, 0, 30);
        pop_in(chk);
        make_label(card, "Approved", COL_TEXT, &font_pjs_20_medium,
                   LV_ALIGN_TOP_MID, 0, 92);
        tx_amount_row(card, amt, &font_pjs_28_semibold, 122, true);

        /* The hash main hands over with DONE, so the merchant can look the
         * settled sale up. */
        if (s_tx_info[0] != '\0') {
            char shrt[24];
            hash_short(s_tx_info, shrt, sizeof(shrt));
            make_label(card, shrt, COL_DIM, &font_inter_14,
                       LV_ALIGN_TOP_MID, 0, 168);
        }

        make_button(card, "New sale", COL_ACCENT, COL_BG, CARD_BTN_W, CARD_BTN_H,
                    LV_ALIGN_BOTTOM_MID, 0, CARD_BTN_Y, ACT_NEW,
                    &font_pjs_20_semibold);
        return;
    }

    if (s_tx_state == UI_TX_STATE_UNCONFIRMED) {
        /* The sale the terminal cannot call either way: signed and possibly on
         * the chain, with no verdict in 120 s. Amber and a warning mark, not the
         * red cross — "Declined" is what made a merchant take the money twice.
         * Recheck polls the same hash; Clear is the operator saying they have
         * looked, and is deliberately the quieter of the two. */
        lv_obj_t *warn = make_label(card, LV_SYMBOL_WARNING, COL_WARN,
                                    &font_icons_48, LV_ALIGN_TOP_MID, 0, 26);
        pop_in(warn);
        make_label(card, "Not confirmed yet", COL_TEXT,
                   &font_pjs_20_medium, LV_ALIGN_TOP_MID, 0, 86);
        char shrt[24];
        hash_short(s_tx_info, shrt, sizeof(shrt));
        make_label(card, shrt, COL_TEXT, &font_inter_14_medium,
                   LV_ALIGN_TOP_MID, 0, 114);
        lv_obj_t *note = make_label(card,
                                    "It may still arrive. Check the explorer "
                                    "before charging again.",
                                    COL_DIM, &font_inter_14, LV_ALIGN_TOP_MID, 0, 138);
        lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(note, VW);
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(note, LV_ALIGN_TOP_MID, 0, 138);

        const lv_coord_t half = (CARD_W - (2 * CARD_PAD) - 8) / 2;
        make_ghost_button(card, "Clear", half,
                          LV_ALIGN_BOTTOM_LEFT, CARD_PAD, CARD_BTN_Y, ACT_NEW);
        make_button(card, "Recheck", COL_ACTION, COL_BG, half, CARD_BTN_H,
                    LV_ALIGN_BOTTOM_RIGHT, -CARD_PAD, CARD_BTN_Y, ACT_TX_RECHECK,
                    &font_pjs_20_semibold);
        return;
    }

    if (s_tx_state == UI_TX_STATE_FAILED) {
        lv_obj_t *cross = make_label(card, LV_SYMBOL_CLOSE, lv_color_hex(0xEC5B5B),
                                     &font_icons_48, LV_ALIGN_TOP_MID, 0, 30);
        pop_in(cross);
        make_label(card, "Declined", COL_TEXT,
                   &font_pjs_20_medium, LV_ALIGN_TOP_MID, 0, 92);
        lv_obj_t *info = make_label(card, s_tx_info, COL_DIM,
                                    &font_inter_14, LV_ALIGN_TOP_MID, 0, 124);
        lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(info, VW);
        lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 124);

        make_button(card, "New sale", COL_ACCENT, COL_BG, CARD_BTN_W, CARD_BTN_H,
                    LV_ALIGN_BOTTOM_MID, 0, CARD_BTN_Y, ACT_NEW,
                    &font_pjs_20_semibold);
        return;
    }

    /* PROCESSING / SIGNING / SENDING — animated spinner + status text. The
     * spinner keeps turning because lv_timer_handler runs on the UI task while
     * the main task is busy connecting/signing/broadcasting. */
    const char *state_str =
        (s_tx_state == UI_TX_STATE_PROCESSING) ? "Processing" :
        (s_tx_state == UI_TX_STATE_SIGNING)    ? "Signing"    :
        (s_tx_state == UI_TX_STATE_CONFIRMING) ? "Confirming" : "Authorizing";

    lv_obj_t *sp = lv_spinner_create(card, 1000, 60);
    lv_obj_set_size(sp, 60, 60);
    lv_obj_align(sp, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_arc_width(sp, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sp, COL_SURFACE, LV_PART_MAIN);      /* track */
    lv_obj_set_style_arc_color(sp, COL_ACCENT, LV_PART_INDICATOR);  /* moving arc */

    make_label(card, state_str, COL_TEXT, &font_pjs_20_medium,
               LV_ALIGN_TOP_MID, 0, 112);

    /* Held: "Confirming" can stand for two minutes, and ui_set_tx_info() writes
     * the countdown straight into this label — rebuilding the screen per tick
     * would restart the spinner above it. */
    s_tx_info_lbl = make_label(card, s_tx_info, COL_DIM,
                               &font_inter_14, LV_ALIGN_TOP_MID, 0, 146);
    lv_label_set_long_mode(s_tx_info_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_tx_info_lbl, VW);
    lv_obj_set_style_text_align(s_tx_info_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_tx_info_lbl, LV_ALIGN_TOP_MID, 0, 146);
}

/* Startup fault. Not the transaction screen: its red cross and "Declined" would
 * make a wiring problem read as a refused sale. No action button — nothing here is
 * recoverable from the touchscreen, and main restarts the terminal by itself
 * after BOOT_FAULT_RESTART_S (30 s), so the body text says that. */
void build_boot_error(void) {
    clear_screen();
    build_header("Startup");

    lv_obj_t *warn = make_label(lv_scr_act(), LV_SYMBOL_WARNING, COL_DANGER,
                                &font_icons_48, LV_ALIGN_TOP_MID, 0, 62);
    pop_in(warn);

    const char *title;
    const char *body;
    switch (s_boot_err) {
        case UI_BOOT_ERR_WALLET:
            title = "Wallet not ready";
            body  = "The card reader answered but the Cryptnox wallet could not "
                    "be initialised.\n\n"
                    "The terminal restarts by itself in 30 s. If this keeps "
                    "happening, the reader or its firmware is at fault.";
            break;
        case UI_BOOT_ERR_CONFIG:
            title = "Invalid configuration";
            body  = "An address built into this firmware (config.h) does not "
                    "parse.\n\n"
                    "The terminal restarts in 30 s. Install a build with a "
                    "corrected config.h.";
            break;
        case UI_BOOT_ERR_NFC:
        default:
            title = "NFC reader not found";
            body  = "The PN532 module did not answer on the I2C bus.\n\n"
                    "Check the SDA/SCL wiring, the RST pin and the 3V3 supply. "
                    "The terminal restarts by itself in 30 s.";
            break;
    }

    make_label(lv_scr_act(), title, COL_TEXT, &font_pjs_20_medium,
               LV_ALIGN_TOP_MID, 0, 124);

    lv_obj_t *b = make_label(lv_scr_act(), body, COL_DIM,
                             &font_inter_14, LV_ALIGN_TOP_MID, 0, 158);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, 216);
    lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    /* Technician detail, kept off the main message so the operator reads the
     * instruction and not the error code. At the foot, but never above the
     * body, so long text clips at the bottom rather than overprinting it. */
    if (s_boot_detail[0] != '\0') {
        lv_obj_t *d = make_label(lv_scr_act(), s_boot_detail, COL_DIM,
                                 &font_inter_14,
                                 LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(d, 216);
        lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

        lv_obj_update_layout(b);
        lv_obj_update_layout(d);
        const lv_coord_t body_end = lv_obj_get_y(b) + lv_obj_get_height(b) + 8;
        if (lv_obj_get_y(d) < body_end) {
            lv_obj_align(d, LV_ALIGN_TOP_MID, 0, body_end);
        }
    }
}
