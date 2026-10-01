/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ui_theme.cpp
 * @ingroup ui
 * @brief Colours, the LVGL theme and the shared layout metrics.
 */

#include "ui_internal.h"

/******************************************************************
 * 3a. LVGL theme — extends the built-in default rather than replacing it
 *
 * Restyles the widgets not styled by hand (tab bar as a segmented control,
 * thin scrollbars, sliders, fields, keys) to match the hand-drawn pills. A
 * theme applies at creation, so every future tabview gets it too.
 *
 * Deliberately narrow: buttons, pills and cards are styled explicitly at each
 * call site, and a theme fighting an explicit style depends on which ran last.
 ******************************************************************/
#define TAB_SEG_INSET   5      /* pill inset inside the 42px bar, top+bottom */
#define TAB_SEG_GAP     4      /* gap between segments                       */
#define SCROLLBAR_W     4      /* hairline, not the default block            */

static lv_theme_t s_theme;
static lv_style_t s_st_tabbar;    /* the bar the segments sit on   */
static lv_style_t s_st_tab;       /* one segment, not selected     */
static lv_style_t s_st_tab_sel;   /* the selected segment          */
static lv_style_t s_st_tabview;   /* the tabview container itself  */
static lv_style_t s_st_scrollbar;
static lv_style_t s_st_track;     /* slider/arc/spinner background */
static lv_style_t s_st_ink;       /* their filled part and knob    */
static lv_style_t s_st_field;     /* text entry                    */
static lv_style_t s_st_key;       /* one key of the keyboard       */

static void theme_styles_init(void)
{
    /* The bar stays white: the selected pill is the only ink. */
    lv_style_init(&s_st_tabbar);
    lv_style_set_bg_color(&s_st_tabbar, COL_BG);
    lv_style_set_bg_opa(&s_st_tabbar, LV_OPA_COVER);
    lv_style_set_border_width(&s_st_tabbar, 0);
    lv_style_set_pad_ver(&s_st_tabbar, TAB_SEG_INSET);
    lv_style_set_pad_hor(&s_st_tabbar, TAB_SEG_GAP);
    lv_style_set_pad_column(&s_st_tabbar, TAB_SEG_GAP);

    lv_style_init(&s_st_tab);
    lv_style_set_bg_opa(&s_st_tab, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_st_tab, 0);
    lv_style_set_radius(&s_st_tab, BTN_RADIUS);
    lv_style_set_text_color(&s_st_tab, COL_DIM);
    lv_style_set_text_font(&s_st_tab, &font_inter_14_medium);

    /* Filled pill, same shape and colour (COL_ACTION) as the admin panel's
     * action buttons. */
    lv_style_init(&s_st_tab_sel);
    lv_style_set_bg_color(&s_st_tab_sel, COL_ACTION);
    lv_style_set_bg_opa(&s_st_tab_sel, LV_OPA_COVER);
    lv_style_set_radius(&s_st_tab_sel, BTN_RADIUS);
    lv_style_set_text_color(&s_st_tab_sel, COL_BG);
    lv_style_set_border_width(&s_st_tab_sel, 0);

    lv_style_init(&s_st_tabview);
    lv_style_set_bg_color(&s_st_tabview, COL_BG);
    lv_style_set_bg_opa(&s_st_tabview, LV_OPA_COVER);
    lv_style_set_border_width(&s_st_tabview, 0);

    lv_style_init(&s_st_scrollbar);
    lv_style_set_bg_color(&s_st_scrollbar, COL_BORDER);
    lv_style_set_bg_opa(&s_st_scrollbar, LV_OPA_COVER);
    lv_style_set_radius(&s_st_scrollbar, LV_RADIUS_CIRCLE);
    lv_style_set_width(&s_st_scrollbar, SCROLLBAR_W);
    lv_style_set_pad_right(&s_st_scrollbar, 2);

    /* Colour and radius only from here down — no size, pad or length: these
     * widgets sit in hand-positioned layouts that moved geometry would break. */
    lv_style_init(&s_st_track);
    lv_style_set_bg_color(&s_st_track, COL_BORDER);
    lv_style_set_bg_opa(&s_st_track, LV_OPA_COVER);
    lv_style_set_radius(&s_st_track, LV_RADIUS_CIRCLE);
    lv_style_set_arc_color(&s_st_track, COL_BORDER);

    lv_style_init(&s_st_ink);
    lv_style_set_bg_color(&s_st_ink, COL_ACCENT);
    lv_style_set_bg_opa(&s_st_ink, LV_OPA_COVER);
    lv_style_set_radius(&s_st_ink, LV_RADIUS_CIRCLE);
    lv_style_set_arc_color(&s_st_ink, COL_ACCENT);
    lv_style_set_border_width(&s_st_ink, 0);
    lv_style_set_shadow_width(&s_st_ink, 0);   /* software-rendered: not free */

    /* A field is a filled grey rounded box, the pills' and cards' surface. */
    lv_style_init(&s_st_field);
    lv_style_set_bg_color(&s_st_field, COL_SURFACE);
    lv_style_set_bg_opa(&s_st_field, LV_OPA_COVER);
    lv_style_set_radius(&s_st_field, 10);
    lv_style_set_border_width(&s_st_field, 0);
    lv_style_set_text_color(&s_st_field, COL_TEXT);

    /* White rounded keys on the grey field. */
    lv_style_init(&s_st_key);
    lv_style_set_bg_color(&s_st_key, COL_BG);
    lv_style_set_bg_opa(&s_st_key, LV_OPA_COVER);
    lv_style_set_radius(&s_st_key, 8);
    lv_style_set_border_width(&s_st_key, 0);
    lv_style_set_text_color(&s_st_key, COL_TEXT);
}

static void theme_apply(lv_theme_t *th, lv_obj_t *obj)
{
    LV_UNUSED(th);

    lv_obj_add_style(obj, &s_st_scrollbar, LV_PART_SCROLLBAR);

    if (lv_obj_check_type(obj, &lv_tabview_class)) {
        lv_obj_add_style(obj, &s_st_tabview, LV_PART_MAIN);
        return;
    }

    /* The tab bar is a button matrix, and so is the PIN pad; the parent tells
     * them apart, so the keypad does not become a segmented control. */
    if (lv_obj_check_type(obj, &lv_btnmatrix_class)) {
        lv_obj_t *parent = lv_obj_get_parent(obj);
        if ((parent != NULL) && lv_obj_check_type(parent, &lv_tabview_class)) {
            lv_obj_add_style(obj, &s_st_tabbar, LV_PART_MAIN);
            lv_obj_add_style(obj, &s_st_tab, LV_PART_ITEMS);
            lv_obj_add_style(obj, &s_st_tab_sel,
                             LV_PART_ITEMS | LV_STATE_CHECKED);
        }
        return;
    }

    if (lv_obj_check_type(obj, &lv_slider_class)) {
        lv_obj_add_style(obj, &s_st_track, LV_PART_MAIN);
        lv_obj_add_style(obj, &s_st_ink, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &s_st_ink, LV_PART_KNOB);
    } else if (lv_obj_check_type(obj, &lv_arc_class)
               || lv_obj_check_type(obj, &lv_spinner_class)) {
        lv_obj_add_style(obj, &s_st_track, LV_PART_MAIN);
        lv_obj_add_style(obj, &s_st_ink, LV_PART_INDICATOR);
    } else if (lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_add_style(obj, &s_st_field, LV_PART_MAIN);
    } else if (lv_obj_check_type(obj, &lv_keyboard_class)) {
        lv_obj_add_style(obj, &s_st_field, LV_PART_MAIN);
        lv_obj_add_style(obj, &s_st_key, LV_PART_ITEMS);
    }
}

/**
 * @brief Install the theme. After lv_disp_drv_register(), before any object.
 *
 * Copied from the active theme and parented to it, so the default styling
 * still runs first and this only adds on top.
 */
void theme_init(void)
{
    theme_styles_init();

    /* Re-initialised with the UI font: the one LVGL built it with is a
     * Kconfig built-in, and every widget not styled by hand inherits it. */
    lv_theme_t *base = lv_disp_get_theme(NULL);
    base = lv_theme_default_init(NULL, base->color_primary, base->color_secondary,
                                 LV_THEME_DEFAULT_DARK, &font_inter_14);
    s_theme = *base;
    lv_theme_set_parent(&s_theme, base);
    lv_theme_set_apply_cb(&s_theme, theme_apply);
    lv_disp_set_theme(NULL, &s_theme);
}
