/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ui_internal.h
 * @ingroup ui
 * @brief Private to the ui*.cpp files: the state and helpers they share.
 */

#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "ui.h"

#include <Arduino.h>

#include <SPI.h>

#include <TFT_eSPI.h>

#include <XPT2046_Touchscreen.h>

#include <stdio.h>

#include <string.h>

#include <inttypes.h>

#include <time.h>        /* the status-band clock */

#include "lvgl.h"

#include "logo_img.h"

#include "logo_small.h"

#include "logo_mid.h"    /* 80px, the welcome card — tools/gen_logo_mid.py */

#include "chain_icons.h"

#include "tap_icon.h"    /* the "tap your card" mark — tools/gen_tap_icon.py */

#include "assets.h"      /* the per-asset table: ticker, standard, caption, network */

#include "settings.h"

#include "money.h"       /* keypad cents, the native cap, amount text — host-tested */

#include "touch_cal.h"   /* two-point calibration arithmetic, host-tested */

#include "civil_time.h"  /* civil_local_offset_min() — the clock's DST, host-tested */

#include "provision.h"   /* QR payload + the pending payout-address handshake */

#include "ota.h"         /* running version + the update window and its handshake */

#include "ota_version.h" /* ota_version_display() — the 'v' is added for the screen */

#include "freertos/FreeRTOS.h"

#include "freertos/task.h"

#include "freertos/semphr.h"   /* s_ui_mx */

#include "esp_timer.h"

#include "wdt.h"               /* the UI loop is on the task watchdog */

#include "esp_log.h"

#include "esp_system.h"   /* esp_restart() for factory reset */

#include "driver/ledc.h"

#include "CW_Utils.h"   /* hardened memory primitives (CODING_RULES §1.4) */

/* Plus Jakarta Sans for titles, buttons and figures, Inter for small text;
 * one weight per role — main/fonts/, tools/gen_fonts.py.
 * Spelled out rather than LV_FONT_DECLARE, which cppcheck cannot expand
 * without the LVGL headers (unknownMacro fails CI). */
extern const lv_font_t font_inter_14;         /* Inter Regular: body, captions */

extern const lv_font_t font_inter_14_medium;  /* Inter Medium: values, clock   */

extern const lv_font_t font_pjs_20_medium;    /* Medium: titles and messages  */

extern const lv_font_t font_pjs_20_semibold;  /* SemiBold: button labels      */

extern const lv_font_t font_pjs_28_semibold;  /* SemiBold: amounts            */

extern const lv_font_t font_pjs_28_light;     /* Light: keypad digits         */

extern const lv_font_t font_icons_48;         /* LV_SYMBOL_OK/CLOSE/WARNING   */

/******************************************************************
 * 2. Hardware — panel (TFT_eSPI) and touch (XPT2046, separate SPI bus)
 ******************************************************************/
#define T_CS    33

#define T_IRQ   36

#define T_CLK   25

#define T_MOSI  32

#define T_MISO  39

/* Portrait orientation (240x320) — natural for a hand-held POS terminal. */
#define SCR_W   240

#define SCR_H   320

/* Splash logo X trim, tuned by eye on the panel: the bitmap is geometrically
 * centred, but 0 reads as too far right. A calibration value — do not "correct"
 * it from the image geometry. */
#define LOGO_X_NUDGE (-4)

/******************************************************************
 * 3. Theme — light, minimal: white bg, black ink
 ******************************************************************/
#define COL_BG       lv_color_hex(0xFFFFFF)   /* white — page background       */

#define COL_SURFACE  lv_color_hex(0xF2F2F2)   /* light grey — cards/secondary  */

#define COL_TEXT     lv_color_hex(0x000000)   /* black — primary text          */

#define COL_DIM      lv_color_hex(0x9A9A9A)   /* grey — secondary labels       */

#define COL_TITLE    lv_color_hex(0x424242)   /* dark grey — screen titles     */

/* Slate, not black: the setup portal's hue, so panel and browser match. Tuned
 * on the panel: this is the portal's DARK-scheme accent (--accf, 13.5:1); its
 * light ink #2C3E50 (11:1) reads washed out on this TFT. Text on it is COL_BG. */
#define COL_ACCENT   lv_color_hex(0x22303D)   /* slate — chrome, tabs, controls */

/* The sale's two buttons: the one that commits is filled (WHITE label, 16.4:1),
 * the one that backs out is an outline button in the card's white. Its fill is
 * the card's, so the hairline in make_ghost_button() is load-bearing: keep it. */
#define COL_ACTION      lv_color_hex(0x101F2E)  /* charcoal blue — Charge, Confirm */

/* The page behind the white card: a vertical ramp, pale sky deepening into
 * COL_PAGE_GRAD. The ramp is what separates the card (the top is barely off
 * white), so do not flatten it; the COL_DIM home indicator must stay far darker. */
#define COL_PAGE      lv_color_hex(0xE2F5FF)    /* pale sky — head of the ramp */

#define COL_PAGE_GRAD lv_color_hex(0xA8D8F0)    /* deeper sky — foot of it     */

/* The admin side has no page colour of its own: it is plain COL_BG. */
#define COL_HOME_BAR COL_DIM       /* grey — the swipe handle           */

#define COL_WARN     lv_color_hex(0xE39A2D)   /* amber — "not confirmed yet"   */

#define COL_SUCCESS  lv_color_hex(0x1E9E50)   /* green — "Sent"                */

#define COL_DANGER   lv_color_hex(0xD63A3A)   /* red — failures / reset        */

#define COL_BORDER   lv_color_hex(0xE0E0E0)   /* light grey — hairlines        */

/* Corner radius for every button on the panel, and the tab bar's segments, so
 * the two shapes match — see make_button. */
#define BTN_RADIUS      6

/******************************************************************
 * 3b. Layout metrics — shared so every screen's header lines up
 ******************************************************************/
#define HDR_TITLE_Y     11     /* title offset — optically centred in the

                                  42px band above the divider (font_pjs_20_medium) */
#define HDR_DIVIDER_Y   42     /* rule under the title                */

#define ACT_BTN_H       46     /* bottom action-button height         */

#define ACT_BTN_Y       (-8)   /* bottom action-button offset         */

/* Every text field's vertical inset (height = one line + twice this). Not in
 * the theme's s_st_field: that style is on the Wi-Fi keyboard too, and the
 * theme sets no geometry. */
#define FIELD_PAD_V     8

#define MENU_BTN_W      42

#define MENU_BTN_H      30

#define MENU_BTN_X      4

#define MENU_BTN_Y      6

/* Asset selector — on the amount's own row, hard right, so it reads as the
 * figure's currency. It carries the ticker; the figure is a bare number.
 *
 * No fixed width: the pill is content-sized flex (tickers vary) and
 * amount_row_place() measures it. 42 tall is 3px of air above and below the
 * 36px badge (BADGE_SZ, with the icon helpers). */
#define ASSET_BTN_H     42

/* Both in CARD coordinates — the amount screen builds into the white card. */
#define ASSET_BTN_X     (-8)   /* right edge inset                     */

#define ASSET_BTN_Y     10     /* the figure's row is centred on THIS  */

/* The figure's row shares the selector's line, clear of the keypad at 90; its
 * offsets depend on what has been typed — see amount_row_place(). */
#define ASSET_BTN_PAD   8      /* badge inset from the left edge       */

#define TAB_PAD         12     /* settings tab page padding           */

#define TAB_W           (SCR_W - (2 * TAB_PAD))   /* usable tab width */

/******************************************************************
 * 4. LVGL display + input plumbing
 ******************************************************************/
#define LV_TICK_PERIOD_MS  2

/* Touch calibration. Two targets: two points are the whole of the linear map
 * touch_to_screen() applies.
 * ponytail: two-point linear. If a panel turns out skewed rather than offset
 * and scaled, the upgrade is an affine map and four corners.
 *
 * Steps 0 and 1 capture a corner each; step 2 applies the result live and asks
 * the operator to confirm by tapping with it, so a calibration that makes Save
 * unreachable is never stored, and the deadline restores the old numbers. */
#define CAL_INSET       20      /* target centre, in from each corner       */

#define CAL_VERIFY_MS   20000U  /* un-confirmed calibration reverts after this */

/* Wi-Fi picker */
#define WIFI_MAX_APS 16

/* Admin code (admin panel lock; it opens the payout addresses). Six digits for
 * a new code: under the escalating penalty (capped at an hour) that is a few
 * dozen guesses a night against 10^6. The merchant may choose up to
 * ADMIN_CODE_MAX.
 *
 * The unlock screen also accepts four-digit codes stored by older builds — they
 * must keep opening the menu (it gates the factory reset too) — so only the
 * creation screen enforces the six-digit floor. */
#define ADMIN_CODE_MIN        6

#define ADMIN_CODE_MIN_STORED 4   /* shortest code an older build could store */

#define ADMIN_CODE_MAX        9

/******************************************************************
 * 6. Button actions
 ******************************************************************/
enum BtnAction {
    ACT_CONFIRM, ACT_CANCEL, ACT_SEND, ACT_NEW, ACT_TX_RECHECK,
    ACT_CLOSE, ACT_PIN_CANCEL, ACT_PIN_REVEAL,
    ACT_WIFI, ACT_WIFI_CANCEL, ACT_WIFI_PASS_REVEAL,
    ACT_ADMIN_CANCEL, ACT_ADMIN_REVEAL, ACT_WELCOME_OK,
    /* Config portal: accept/reject a proposed value, finish the wizard. */
    ACT_PROV_OK, ACT_PROV_NO, ACT_PROV_FINISH,
    ACT_RESET, ACT_RESET_CONFIRM, ACT_MODAL_CLOSE,
    /* Asset selector (amount screen and the Tx tab): network, then the coin. */
    ACT_NET_PICK, ACT_NET_ETH, ACT_NET_POLY, ACT_NET_TRON,
    /* The admin web page: open it (QR to scan), close it, resolve an upload. */
    ACT_PORTAL, ACT_PORTAL_CLOSE, ACT_OTA_OK, ACT_OTA_NO,
    /* Touch calibration: start it, keep the result, put the old one back. */
    ACT_TOUCH_CAL, ACT_CAL_SAVE, ACT_CAL_CANCEL,
    /* One action per chain, contiguous, so an action's offset from here IS its
     * pos_chain_t (ACT_CHAIN_OF below, decoded in btn_event_cb). KEEP LAST: the
     * block runs to POS_CHAIN__COUNT and nothing may share its numbers. */
    ACT_CHAIN_BASE,
};

/** The action that selects @p chain. */
#define ACT_CHAIN_OF(chain) \
    static_cast<BtnAction>(ACT_CHAIN_BASE + static_cast<int>(chain))

#define SETTINGS_TAB_COUNT  4

#define TAB_ABOUT           3

/* From 100.00 up, the cents move to the small font so the figure fits beside
 * the asset button on the row. Below 100 there is room for all of it. */
#define AMOUNT_SMALL_CENTS_FROM  10000ULL   /* 100.00 in cents */

/* Air between the figure and the selector, so they read as two things. */
#define AMOUNT_ROW_GAP  8

/* Left gutter the figure may never cross — see amount_row_place(). */
#define AMOUNT_ROW_MIN_X  4

/******************************************************************
 * 6b. Sale-flow page chrome
 *
 * Sky page, white rounded card, and the home indicator (the admin panel's
 * handle). build_page() screens place children into the CARD, so (0,0) is the
 * card's top-left. Only the sale uses this; the admin panel stays plain white.
 ******************************************************************/
#define CARD_X    8

#define CARD_Y    28

#define CARD_W    (SCR_W - (2 * CARD_X))   /* 224 */

#define CARD_H    262                      /* 28..290; home bar sits below */

#define CARD_PAD  10

/* Bottom action button, in card coordinates. */
#define CARD_BTN_H  44

#define CARD_BTN_Y  (-10)

#define CARD_BTN_W  (CARD_W - (2 * CARD_PAD))

/* The status band, left to right: clock, the TEST chip when there is one, the
 * Wi-Fi mark. Fixed zones, not measured, so nothing moves as the minute ticks.
 *
 * Everything sits on one optical centre line at y≈12.5: font_inter_14_medium
 * draws an 18px box with its 11px cap 4 down (so text at 3 puts the cap at
 * 7..18), the Wi-Fi mark is 13 tall at 6. Move one and move the other.
 *
 * CLOCK_W and CHIP_W are reserves rather than the real widths — "00:00", the
 * widest time, measures ~40 against 42, "TEST" with its pads ~48 against 56. */
#define BAND_Y      3      /* text top: clock and chip                  */

#define CLOCK_X     10

#define CLOCK_Y     BAND_Y

#define CLOCK_W     42

#define CHIP_X      (CLOCK_X + CLOCK_W + 6)

#define CHIP_Y      3      /* 20px tall, so 3 centres it on the band    */

/* Steps of one sale, passed to build_page(). Nothing draws them. */
enum {
    PAY_STEP_NONE   = -1,
    PAY_STEP_AMOUNT = 0,
    PAY_STEP_REVIEW,
    PAY_STEP_AUTH,
    PAY_STEP_TAP,
    PAY_STEP__COUNT
};

/* Round icon disc with a glyph — the coin mark in a pill's left slot, and (at
 * CHIP_SZ) the network chip clipped to its corner. Never clickable: it sits
 * inside a pill button, and a clickable child would swallow that tap. */
#define COIN_SZ   30

#define TAP_MARK_H    65

/* Selector row in the style of button_style.png: full-radius grey pill, round
 * icon on the left, title over a dim subtitle, chevron on the right. The icon
 * is left to the caller (asset badge or glyph disc):
 *
 *   lv_obj_t *p = make_pill(...);
 *   lv_obj_align(make_asset_badge(p, chain), LV_ALIGN_LEFT_MID, PILL_ICON_X, 0);
 *
 * @p w is in pixels on purpose — lv_pct() cannot be measured for the subtitle. */
#define PILL_H       52

#define PILL_ICON_X  10

/* Card geometry for the two update modals. 228 wide leaves 212 inside the 8 px
 * pad; wrapped text gets 200 so it clears the rounded corners. The text is
 * runtime-length, so blocks stack with lv_obj_align_to() and ota_fit_card()
 * grows the card to fit. */
#define OTA_CARD_W   228

#define OTA_TEXT_W   200

/* Masked one-line code field, with no soft-keyboard popup — the on-screen keypad
 * is the only input. Shared by the card PIN and the admin code, both paired
 * with the reveal eye (a blind mistype on a resistive panel is otherwise opaque).
 *
 * The code screens place field and eye on the keypad's columns — see
 * make_code_row(); the Wi-Fi passphrase uses CODE_FIELD_W/CODE_FIELD_X. */
#define CODE_EYE_GAP    6

#define CODE_FIELD_W(kbd_w)  ((kbd_w) - MENU_BTN_W - CODE_EYE_GAP)

#define CODE_FIELD_X    (-(MENU_BTN_W + CODE_EYE_GAP) / 2)

/* Numeric keypad: no key boxes — black glyphs on white, grey flash on press.
 * The map is static because lv_btnmatrix keeps the pointer.
 *
 * ONE WIDTH for both code screens (card PIN and admin code), taken from the
 * sale card, so the two look identical. */
#define CODE_KBD_W   (CARD_W - (2 * CARD_PAD))

/* ── Shared between the ui*.cpp files ─────────────────────────── */

extern const char *TAG;
extern TFT_eSPI            tft;
extern SPIClass            touchSPI;
extern XPT2046_Touchscreen touch;
void theme_init(void);
extern lv_color_t        s_buf[SCR_W * 40];
extern lv_disp_draw_buf_t s_draw_buf;
extern lv_disp_drv_t      s_disp_drv;
extern lv_indev_drv_t     s_indev_drv;
void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px);
extern uint16_t s_cal_xmin;
extern uint16_t s_cal_xmax;
extern uint16_t s_cal_ymin;
extern uint16_t s_cal_ymax;
void touch_cal_load(void);
bool touch_raw(int16_t *rx, int16_t *ry);
extern uint32_t s_input_block_until;
extern bool     s_wait_release;
extern lv_obj_t *s_page_card;
extern volatile bool s_swipe_admin;
void indev_read(lv_indev_drv_t *drv, lv_indev_data_t *data);
void tick_cb(void *arg);
extern uint8_t s_brightness;
void backlight_set_pct(uint8_t pct);
void backlight_init(uint8_t pct);
extern ui_event_cb_t s_cb;
extern volatile ui_screen_t s_req_screen;
extern uint64_t s_amount_units;
extern uint64_t s_confirm_amount;
extern char     s_confirm_addr[64];
extern char     s_confirm_fee[40];
extern ui_tx_state_t s_tx_state;
extern char          s_tx_info[72];
extern lv_obj_t     *s_tx_info_lbl;
extern uint8_t  s_cal_step;
extern int16_t  s_cal_raw[2][2];
extern bool     s_cal_pressed;
extern int16_t  s_cal_last_x;
extern int16_t  s_cal_last_y;
extern uint32_t s_cal_deadline;
extern lv_obj_t *s_cal_countdown;
extern lv_obj_t     *s_sheet;
extern volatile bool s_sheet_pending;
extern char          s_card_note[64];
extern lv_obj_t *s_amount_label;
extern lv_obj_t *s_amount_cents_label;
extern lv_obj_t *s_amount_row;
extern uint64_t  s_amount_cents;
extern lv_obj_t *s_asset_btn;
extern lv_obj_t *s_asset_arrow;
extern lv_obj_t *s_charge_btn;
extern lv_obj_t *s_clock_lbl;
extern bool      s_charge_busy;
extern lv_obj_t *s_pin_ta;
extern char      s_pin[16];
extern uint8_t   s_pin_len;
extern net_wifi_ap_t s_aps[WIFI_MAX_APS];
extern uint16_t      s_ap_count;
extern char          s_wifi_ssid[33];
extern char          s_wifi_pass[65];
extern char          s_wifi_note[64];
extern lv_obj_t     *s_wifi_pass_ta;
extern lv_obj_t     *s_wifi_eye_lbl;
extern lv_obj_t     *s_pin_eye_lbl;
extern lv_obj_t     *s_admin_eye_lbl;
extern char          s_wifi_caption[24];
extern char          s_wifi_name[33];
extern char          s_boot_step[40];
extern lv_obj_t     *s_boot_step_lbl;
extern lv_obj_t     *s_fee_max_lbl;
extern lv_obj_t     *s_fee_prio_lbl;
extern volatile int  s_prov_step;
extern char          s_prov_msg[64];
extern ui_boot_err_t s_boot_err;
extern char          s_boot_detail[64];
extern lv_obj_t     *s_admin_ta;
extern lv_obj_t     *s_admin_note_lbl;
extern char          s_admin_first[ADMIN_CODE_MAX + 1];
extern char          s_admin_note[48];
extern bool          s_admin_confirming;
extern char          s_welcome_sub[96];
extern uint32_t      s_admin_lock_start;
extern uint32_t      s_admin_lock_ms;
extern bool          s_admin_for_portal;
extern bool          s_pin_for_card;
extern const char *s_addr_usdc;
extern const char *s_addr_dest;
extern lv_obj_t *s_reset_btn;
extern lv_obj_t *s_close_btn;
extern uint16_t s_settings_tab;
extern pos_chain_t s_view_chain;
const pos_asset_t *asset(pos_chain_t c = settings_get_chain());
const char *asset_name(pos_chain_t c = settings_get_chain());
const char *asset_network(pos_chain_t c = settings_get_chain());
const char *asset_caption(pos_chain_t c = settings_get_chain());
void request_screen(ui_screen_t s);
void set_wifi_progress(const char *caption, const char *name);
uint64_t amount_cents_max(void);
void charge_set_busy(void);
void code_field_reveal(lv_obj_t *ta, lv_obj_t *eye_lbl);
void btn_event_cb(lv_event_t *e);
lv_obj_t *make_button(lv_obj_t *parent, const char *label, lv_color_t bg,
                             lv_color_t fg, lv_coord_t w, lv_coord_t h,
                             lv_align_t align, lv_coord_t x, lv_coord_t y,
                             BtnAction act, const lv_font_t *font);
lv_obj_t *make_label(lv_obj_t *parent, const char *txt, lv_color_t color,
                            const lv_font_t *font, lv_align_t align,
                            lv_coord_t x, lv_coord_t y);
lv_obj_t *make_ghost_button(lv_obj_t *parent, const char *label,
                                   lv_coord_t w, lv_align_t align,
                                   lv_coord_t x, lv_coord_t y, BtnAction act);
void make_divider(lv_obj_t *parent, lv_coord_t y);
lv_obj_t *make_glyph_disc(lv_obj_t *parent, const char *sym,
                                 lv_color_t bg, lv_coord_t sz);
lv_obj_t *make_asset_badge(lv_obj_t *parent, pos_chain_t chain);
lv_obj_t *make_asset_button(lv_obj_t *parent);
void asset_btn_set_compact(bool compact);
lv_obj_t *make_net_badge(lv_obj_t *parent, pos_net_t net);
void make_tap_mark(lv_obj_t *parent, lv_coord_t y);
lv_obj_t *make_pill(lv_obj_t *parent, const char *title, const char *sub,
                           lv_coord_t w, lv_coord_t y, BtnAction act,
                           bool leaf = false);
lv_obj_t *make_field(lv_obj_t *parent, const char *caption,
                            const char *value, lv_color_t col = COL_TEXT);
void clear_screen(void);
extern volatile bool s_tz_dirty;
void paint_page(lv_obj_t *obj);
lv_obj_t *make_card(lv_obj_t *parent);
lv_obj_t *build_page(int step, bool band = true);
void settings_persist(void);
void build_settings(void);
void build_touch_cal(void);
void touch_cal_poll(void);
extern bool s_portal_modal;
void close_modal(void);
lv_obj_t *open_modal(lv_coord_t w, lv_coord_t h);
void open_reset_confirm(void);
lv_obj_t *ota_text(lv_obj_t *card, lv_obj_t *above, const char *txt,
                          lv_color_t col, const lv_font_t *font);
void ota_fit_card(lv_obj_t *card, lv_obj_t *last, lv_coord_t buttons_h);
void open_portal_window(void);   /* the config page: QR code + passphrase card */
void open_ota_gone(void);
void build_ota_confirm(void);
void open_network_picker(void);
void open_coin_picker(pos_net_t net);
lv_obj_t *make_icon_button(const char *sym, BtnAction act,
                                  lv_obj_t *parent = NULL);
void add_test_chip(void);
lv_obj_t *make_title(const char *txt, bool has_icon,
                            lv_obj_t *parent = NULL);
void build_header(const char *title);
void build_welcome(void);
void build_prov(void);
void build_prov_confirm(void);
void build_card_wait(void);
void build_splash(void);
void build_amount(void);
void build_confirm(void);
lv_obj_t *make_numeric_keypad(lv_event_cb_t cb, lv_obj_t *parent = NULL,
                                     lv_coord_t w = CODE_KBD_W,
                                     lv_coord_t h = 210);
lv_obj_t *make_code_row(lv_obj_t *host, lv_obj_t *kb, uint32_t max_len,
                               const char *hint, BtnAction eye_act,
                               lv_obj_t **eye_lbl);
void build_pin(void);
uint32_t admin_penalty_ms(uint8_t fails);
void build_admin_set(void);
void build_admin_unlock(void);
void build_wifi_list(void);
void build_wifi_pass(void);
void build_wifi_connecting(void);
void pop_in(lv_obj_t *obj);
void build_tx_status(void);
void build_boot_error(void);
void signal_init(void);
void render_requested_screen(void);

#endif /* UI_INTERNAL_H */
