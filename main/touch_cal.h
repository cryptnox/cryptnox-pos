/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file touch_cal.h
 * @brief Touch arithmetic: two-point calibration, and the second-contact guard.
 *
 * Header-only, and separate from ui.cpp for one reason: this is the part that
 * can be wrong without looking wrong. A resistive panel calibrated from two
 * corners is a pair of linear maps, and the sum of the work is extrapolating
 * each captured target out to its edge of the glass — arithmetic a host test
 * can check, in a file that does not need Arduino and LVGL to compile. The
 * jump filter at the bottom is here for the same reason.
 *
 * Two points, not four. The map ui.cpp applies is linear per axis, so a
 * four-corner routine would be averaging away a tilt the map cannot express
 * anyway.
 */

#ifndef TOUCH_CAL_H
#define TOUCH_CAL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Raw XPT2046 counts that map to the panel's four edges. */
typedef struct {
    uint16_t x_min;
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
} touch_cal_t;

/**
 * @brief Solve the edge-to-edge raw range from two corner taps.
 *
 * The targets sit @p inset pixels in from the top-left and bottom-right
 * corners of a @p w x @p h panel; @p r0 and @p r1 are the raw counts captured
 * at each. Both axes are extended past their target by the same @p inset, at
 * the counts-per-pixel the two taps imply.
 *
 * @param r0x,r0y  Raw counts at the top-left target.
 * @param r1x,r1y  Raw counts at the bottom-right target.
 * @param inset    Target centre, in pixels from each corner.
 * @param w,h      Panel size in pixels.
 * @param[out] out Filled only on success.
 * @return false — and @p out untouched — when an axis spans less than
 *         TOUCH_CAL_MIN_SPAN counts between the two taps. That is a stuck
 *         panel, a mirrored axis this map cannot express, or two taps on the
 *         same spot; storing any of them makes the panel untappable, including
 *         the screen that would fix it.
 */
#define TOUCH_CAL_MIN_SPAN  500
/** Full scale of the XPT2046's 12-bit converter. */
#define TOUCH_CAL_RAW_MAX   4095

/* Extrapolating past a target that was tapped near the edge of the converter's
 * range lands outside it. Clamped rather than rejected: the tap itself was
 * fine, and a raw count the hardware can never report is simply an edge the
 * finger can never reach. Unsigned underflow is the reason this exists at all —
 * a -261 stored as 65275 inverts the axis. */
static inline uint16_t touch_cal_clamp(int32_t v)
{
    if (v < 0)                  { return 0U; }
    if (v > TOUCH_CAL_RAW_MAX)  { return (uint16_t)TOUCH_CAL_RAW_MAX; }
    return (uint16_t)v;
}

static inline bool touch_cal_from_corners(int16_t r0x, int16_t r0y,
                                          int16_t r1x, int16_t r1y,
                                          int16_t inset, int16_t w, int16_t h,
                                          touch_cal_t *out)
{
    const int32_t span_x = (int32_t)(w - 1 - inset) - (int32_t)inset;
    const int32_t span_y = (int32_t)(h - 1 - inset) - (int32_t)inset;
    const int32_t dx     = (int32_t)r1x - (int32_t)r0x;
    const int32_t dy     = (int32_t)r1y - (int32_t)r0y;

    if ((out == NULL) || (span_x <= 0) || (span_y <= 0)) { return false; }
    if ((dx < TOUCH_CAL_MIN_SPAN) || (dy < TOUCH_CAL_MIN_SPAN)) { return false; }

    out->x_min = touch_cal_clamp((int32_t)r0x - ((dx * inset) / span_x));
    out->x_max = touch_cal_clamp((int32_t)r1x + ((dx * inset) / span_x));
    out->y_min = touch_cal_clamp((int32_t)r0y - ((dy * inset) / span_y));
    out->y_max = touch_cal_clamp((int32_t)r1y + ((dy * inset) / span_y));
    return true;
}

/**
 * @brief Second-contact guard: hold the first finger's point while two are down.
 *
 * The XPT2046 returns ONE point per read. Two fingers do not read as two
 * touches — they read as a single sample somewhere on the line between them,
 * pulled toward whichever presses harder. LVGL then does what it does for any
 * press that slides: it re-targets, and the widget under that midpoint takes the
 * click. An operator resting a thumb on the glass while tapping therefore lands
 * the tap halfway between thumb and finger — a wrong digit, or Charge.
 *
 * Two signals, because position alone is not enough. The reported point lands
 * midway between the contacts, so its jump is only HALF their separation: a
 * thumb one key away from the finger moves the sample a dozen pixels, which no
 * distance threshold can tell from a finger rolling under its own tap. Pressure
 * does not care how far apart they are — a second contact is a second resistive
 * path in parallel, so z steps up the moment it lands, and by the same amount
 * whether the fingers are 10px or 100px apart.
 *
 *   - z rises TOUCH_Z_STEP_PCT above the lightest press seen so far, or
 *   - the point jumps more than TOUCH_JUMP_MAX_PX, which no finger can cross in
 *     one read period.
 *
 * Either one holds @p x and @p y at the last accepted point — the first
 * finger's — until the panel is back to one touch.
 *
 * Over-triggering is close to free, which is what lets the pressure test be
 * blunt: one finger simply bearing down harder also steps z, and all that
 * happens is a point that was not moving stops being allowed to move.
 *
 * Only safe where nothing legitimately jumps — a screen of taps. A slider drag
 * or a flicked list crosses far more than this in one period and must not go
 * through here.
 *
 * @param st            Filter state; zero-initialise once, then leave alone.
 * @param pressed       Whether the panel reports a touch this read.
 * @param z             Raw XPT2046 pressure for this read; ignored when 0, so a
 *                      caller with no pressure to offer still gets the distance
 *                      test.
 * @param[in,out] x,y   Screen coordinates, held back on a second contact.
 */
#define TOUCH_JUMP_MAX_PX  25

/* How far z must rise above the lightest press of this touch to count as a
 * second contact. A knob, deliberately: z is contact area and force as much as
 * finger count, it differs between panels, and the only honest way to set it is
 * to watch the "second contact:" line ui.cpp logs under a real thumb. Too low
 * freezes hard taps, which costs nothing; too high lets the thumb through, which
 * is the bug. Err low.
 *
 * 30 because the bench said so: on the CYD this was written against, z idles
 * around 1900-2000 under one finger and a second contact stepped it 43% — which
 * the 50 this started at sat just above, and missed. */
#define TOUCH_Z_STEP_PCT   30

/** Last point accepted, and the lightest z seen. Zero-init = no press yet. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z_min;
    bool    have;
} touch_jump_t;

static inline bool touch_jump_second_contact(const touch_jump_t *st,
                                             int16_t z, int16_t x, int16_t y)
{
    const int32_t dx = (int32_t)x - (int32_t)st->x;
    const int32_t dy = (int32_t)y - (int32_t)st->y;

    if ((z > 0) && (st->z_min > 0)) {
        const int32_t step = (int32_t)st->z_min +
                             (((int32_t)st->z_min * TOUCH_Z_STEP_PCT) / 100);
        if ((int32_t)z > step) { return true; }
    }
    return ((dx * dx) + (dy * dy)) >
           ((int32_t)TOUCH_JUMP_MAX_PX * (int32_t)TOUCH_JUMP_MAX_PX);
}

static inline void touch_jump_filter(touch_jump_t *st, bool pressed,
                                     int16_t z, int16_t *x, int16_t *y)
{
    if (!pressed) { st->have = false; return; }

    if (st->have) {
        if (touch_jump_second_contact(st, z, *x, *y)) {
            *x = st->x;      /* hold the first finger's point */
            *y = st->y;
            return;
        }
        /* Track the lightest press, not the first: an operator who lands heavy
         * and eases off would otherwise set a bar their own thumb fits under. */
        if ((z > 0) && (z < st->z_min)) { st->z_min = z; }
    } else {
        st->z_min = z;
    }

    st->x    = *x;
    st->y    = *y;
    st->have = true;
}

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_CAL_H */
