/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ui_hal.cpp
 * @ingroup ui
 * @brief Panel and touch hardware: TFT_eSPI flush, XPT2046 reads, the LVGL display and input drivers, backlight PWM.
 */

#include "ui_internal.h"

TFT_eSPI            tft;
SPIClass            touchSPI(VSPI);
XPT2046_Touchscreen touch(T_CS, T_IRQ);
/* Partial draw buffer — 40 lines (no PSRAM on the CYD, keep it small). */
lv_color_t        s_buf[SCR_W * 40];
lv_disp_draw_buf_t s_draw_buf;
lv_disp_drv_t      s_disp_drv;
lv_indev_drv_t     s_indev_drv;
void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    /* swap=true converts LVGL's native 16-bit order to the panel's. If colours
     * come out byte-swapped/garbled, flip this bool or set CONFIG_LV_COLOR_16_SWAP. */
    tft.pushColors(reinterpret_cast<uint16_t *>(&px->full), w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(drv);
}

/* Raw XPT2046 counts at the panel's edges, cached from NVS (indev_read runs
 * every few ms, no place to open flash). The 200/3800 defaults are close enough
 * on most CYDs to reach the calibration screen on an uncalibrated panel. */
uint16_t s_cal_xmin = 200U;
uint16_t s_cal_xmax = 3800U;
uint16_t s_cal_ymin = 200U;
uint16_t s_cal_ymax = 3800U;
void touch_cal_load(void) {
    settings_get_touch_cal(&s_cal_xmin, &s_cal_xmax, &s_cal_ymin, &s_cal_ymax);
}

/* Uncalibrated sample, for the calibration screen itself — the only caller that
 * must not go through the mapping it is measuring. */
bool touch_raw(int16_t *rx, int16_t *ry) {
    if (!touch.tirqTouched() || !touch.touched()) { return false; }
    TS_Point p = touch.getPoint();
    *rx = p.x;
    *ry = p.y;
    return true;
}

/* @p sz is the raw pressure, for the second-contact guard — see
 * touch_jump_filter(). Higher means a harder press or one more finger. */
static bool touch_to_screen(int16_t *sx, int16_t *sy, int16_t *sz) {
    if (!touch.tirqTouched() || !touch.touched()) {
        return false;
    }
    TS_Point p = touch.getPoint();
    *sz = (int16_t)p.z;
    int16_t mx = map(p.x, s_cal_xmin, s_cal_xmax, 0, SCR_W);
    int16_t my = map(p.y, s_cal_ymin, s_cal_ymax, 0, SCR_H);
    if (mx < 0)      { mx = 0; }
    if (mx >= SCR_W) { mx = SCR_W - 1; }
    if (my < 0)      { my = 0; }
    if (my >= SCR_H) { my = SCR_H - 1; }
    *sx = mx;
    *sy = my;
    return true;
}

/* Swallow taps carried over from the screen we just left: a press is ignored
 * until this tick (ms) AND until the finger has been released at least once.
 * Set on every screen (re)build in render_requested_screen(). */
uint32_t s_input_block_until = 0;
bool     s_wait_release      = false;

/* The white card the sale-flow screens draw into — see build_page(). NULL on
 * every other screen, which is how the driver below tells the two apart. */
lv_obj_t *s_page_card = NULL;

/* Second-contact guard — see touch_jump_filter(). Sale screens only: every
 * control there is a tap, so nothing legitimately jumps, while the admin panel's
 * sliders and scrolling lists cross far more than 25px in a 30ms read period. */
static touch_jump_t s_jump = { 0, 0, 0, false };
static bool         s_jump_logged = false;   /* one log line per press */

/* Swipe up from the bottom edge — the admin panel's door. Detected in the
 * driver, not as an LVGL gesture: the screen is covered by a keypad and a
 * Charge button, and a gesture delivered to a button never reaches the screen.
 *
 * The travel is deliberately longer than a fat-finger slip on the Charge button
 * directly above the handle — that button commits a sale, so the two must not be
 * confusable in either direction. */
#define SWIPE_BAND_H   44    /* press must START in this bottom band */
#define SWIPE_MIN_DY   58    /* ...and travel at least this far up   */
static bool          s_swipe_armed  = false;
static int16_t       s_swipe_y0     = 0;
volatile bool s_swipe_admin  = false;   /* handed to the UI task loop */
void indev_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    int16_t x, y, z = 0;
    bool pressed = touch_to_screen(&x, &y, &z);

    /* Two fingers read as one point between them — hold the first one's. Ahead
     * of the swipe so an artifact cannot arm it, and kept on for an armed press.
     * The swipe's travel uses the raw point: that drag is the one big move on a
     * sale screen, and a false swipe only opens a PIN-locked screen. */
    const int16_t raw_x = x, raw_y = y;
    if (!pressed || (s_page_card != NULL)) {
        touch_jump_filter(&s_jump, pressed, z, &x, &y);
    }

    /* The numbers TOUCH_Z_STEP_PCT is tuned from. Once per press, at INFO
     * (CONFIG_LOG_MAXIMUM_LEVEL=3 compiles ESP_LOGD/V away). Never fires with a
     * thumb down: lower the percentage. Fires on one-finger taps: raise it. */
    if (!pressed) {
        s_jump_logged = false;
    } else if (!s_jump_logged && ((x != raw_x) || (y != raw_y))) {
        s_jump_logged = true;
        ESP_LOGI(TAG, "second contact: %d,%d held at %d,%d (z=%d, z_min=%d)",
                 (int)raw_x, (int)raw_y, (int)x, (int)y, (int)z,
                 (int)s_jump.z_min);
    }

    /* Bottom-edge swipe up. Armed on a press that starts in the band, fired
     * once it has travelled far enough; the rest of the drag is swallowed via
     * s_wait_release so the button under the finger never sees a click. The UI
     * task decides which screen it is allowed on. */
    if (pressed) {
        if (!s_swipe_armed && (y >= (SCR_H - SWIPE_BAND_H))) {
            s_swipe_armed = true;
            s_swipe_y0    = y;
        } else if (s_swipe_armed && ((s_swipe_y0 - raw_y) >= SWIPE_MIN_DY)) {
            s_swipe_armed  = false;
            s_swipe_admin  = true;
            s_wait_release = true;
            data->state    = LV_INDEV_STATE_REL;
            return;
        }
    } else {
        s_swipe_armed = false;
    }

    /* After a screen change, ignore a lingering or reflexive tap from the old
     * screen (e.g. cancelling the tx right after validating the PIN). */
    if (lv_tick_get() < s_input_block_until || s_wait_release) {
        if (!pressed) { s_wait_release = false; }   /* finger lifted — re-arm */
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    if (pressed) {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}
void tick_cb(void *arg) {
    (void)arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}

/******************************************************************
 * 4b. Backlight — LEDC PWM dimming on the CYD's GPIO 21
 ******************************************************************/
#define BL_GPIO        21
#define BL_LEDC_MODE   LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER  LEDC_TIMER_0
#define BL_LEDC_CH     LEDC_CHANNEL_0
#define BL_LEDC_RES    LEDC_TIMER_10_BIT   /* duty 0..1023            */
#define BL_PWM_HZ      5000                /* above the audible range */
uint8_t s_brightness = 80;   /* backlight %, restored from NVS */
void backlight_set_pct(uint8_t pct) {
    if (pct > 100U) { pct = 100U; }
    uint32_t duty = (1023U * pct) / 100U;
    (void)ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CH, duty);
    (void)ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CH);
}
void backlight_init(uint8_t pct) {
    ledc_timer_config_t t = {};
    t.speed_mode      = BL_LEDC_MODE;
    t.duty_resolution = BL_LEDC_RES;
    t.timer_num       = BL_LEDC_TIMER;
    t.freq_hz         = BL_PWM_HZ;
    t.clk_cfg         = LEDC_AUTO_CLK;
    (void)ledc_timer_config(&t);

    ledc_channel_config_t c = {};
    c.gpio_num   = BL_GPIO;
    c.speed_mode = BL_LEDC_MODE;
    c.channel    = BL_LEDC_CH;
    c.timer_sel  = BL_LEDC_TIMER;
    c.duty       = 0;
    c.hpoint     = 0;
    (void)ledc_channel_config(&c);

    backlight_set_pct(pct);
}
