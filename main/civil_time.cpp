/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file civil_time.cpp
 * @brief TZ-independent calendar arithmetic and date-string parsing.
 */

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "civil_time.h"

#include <stdio.h>      /* sscanf */
#include <string.h>     /* memcmp, strcmp */

/******************************************************************
 * 2. Internal helpers
 ******************************************************************/

/**
 * @brief Days from 1970-01-01 to the given proleptic-Gregorian civil date.
 *
 * Howard Hinnant's days_from_civil (public domain, chrono-Compatible-Date
 * paper).  Shifts the year so that March starts the year, which makes the
 * leap day the last day and removes every special case from the arithmetic.
 *
 * @param[in] y Full year.
 * @param[in] m Month, 1–12.
 * @param[in] d Day of month, 1–31.
 * @return Signed day count relative to the Unix epoch.
 */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= (m <= 2) ? 1 : 0;

    const int64_t  era = ((y >= 0) ? y : (y - 399)) / 400;
    const int64_t  yoe = static_cast<int64_t>(y) - (era * 400);          /* [0, 399]    */
    const int64_t  doy = (((153 * (m + ((m > 2) ? -3 : 9))) + 2) / 5)
                         + d - 1;                                        /* [0, 365]    */
    const int64_t  doe = (yoe * 365) + (yoe / 4) - (yoe / 100) + doy;    /* [0, 146096] */

    return (era * 146097) + doe - 719468;
}

/**
 * @brief Range-check a parsed civil date/time.
 *
 * Month-length and leap-year rules are deliberately not enforced: a bogus
 * "Feb 31" simply rolls into March, which cannot help an attacker (the
 * back-dating this guards against needs an error of weeks, not one day).
 * ponytail: no per-month day table, add one if a caller ever needs to
 * reject impossible dates rather than normalise them.
 *
 * @param[in] mon  Month, 1–12.
 * @param[in] day  Day of month, 1–31.
 * @param[in] hour Hour, 0–23.
 * @param[in] min  Minute, 0–59.
 * @param[in] sec  Second, 0–60.
 * @param[in] year Full year; bounded to a sane span.
 * @return true when every field is in range.
 */
static bool fields_sane(int year, int mon, int day, int hour, int min, int sec)
{
    return ((year >= 1970) && (year <= 9999) &&
            (mon  >= 1)    && (mon  <= 12)   &&
            (day  >= 1)    && (day  <= 31)   &&
            (hour >= 0)    && (hour <= 23)   &&
            (min  >= 0)    && (min  <= 59)   &&
            (sec  >= 0)    && (sec  <= 60));   /* 60: leap second */
}

/******************************************************************
 * 3. Public API
 ******************************************************************/

int64_t civil_to_epoch(int year, int mon, int day, int hour, int min, int sec)
{
    const int64_t days = days_from_civil(year, mon, day);

    return (days * 86400) + (static_cast<int64_t>(hour) * 3600) +
           (static_cast<int64_t>(min) * 60) + sec;
}

int civil_month_from_abbrev(const char *mmm)
{
    static const char *const MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int i;

    if (mmm == NULL) {
        return 0;
    }

    for (i = 0; i < 12; i++) {
        if (memcmp(mmm, MONTHS + (i * 3), 3U) == 0) {
            return i + 1;
        }
    }
    return 0;
}

bool civil_parse_build_stamp(const char *date, const char *time_str,
                            int64_t *out)
{
    char mon_abbrev[4] = { 0 };
    int  day  = 0;
    int  year = 0;
    int  hour = 0;
    int  min  = 0;
    int  sec  = 0;
    int  mon;

    if ((date == NULL) || (time_str == NULL) || (out == NULL)) {
        return false;
    }

    /* "%3s %d %d" absorbs the space-padded single-digit day ("Jul  9 2026")
     * because a space in the format matches any run of whitespace. */
    if (sscanf(date, "%3s %d %d", mon_abbrev, &day, &year) != 3) {
        return false;
    }
    if (sscanf(time_str, "%d:%d:%d", &hour, &min, &sec) != 3) {
        return false;
    }

    mon = civil_month_from_abbrev(mon_abbrev);
    if ((mon == 0) || !fields_sane(year, mon, day, hour, min, sec)) {
        return false;
    }

    *out = civil_to_epoch(year, mon, day, hour, min, sec);
    return true;
}

bool civil_parse_http_date(const char *hdr, int64_t *out)
{
    char mon_abbrev[4] = { 0 };
    char zone[8]       = { 0 };
    int  day  = 0;
    int  year = 0;
    int  hour = 0;
    int  min  = 0;
    int  sec  = 0;
    int  mon;

    if ((hdr == NULL) || (out == NULL)) {
        return false;
    }

    /* "Wed, 29 Jul 2026 15:14:09 GMT" — the day-of-week is skipped (%*3s),
     * the zone is captured so that a non-conforming numeric offset is
     * rejected instead of being silently treated as UTC. */
    if (sscanf(hdr, "%*3s, %2d %3s %4d %2d:%2d:%2d %7s",
               &day, mon_abbrev, &year, &hour, &min, &sec, zone) != 7) {
        return false;
    }
    if (strcmp(zone, "GMT") != 0) {
        return false;
    }

    mon = civil_month_from_abbrev(mon_abbrev);
    if ((mon == 0) || !fields_sane(year, mon, day, hour, min, sec)) {
        return false;
    }

    *out = civil_to_epoch(year, mon, day, hour, min, sec);
    return true;
}

/** @brief Day of week of a day count from days_from_civil(), 0 = Sunday. */
static int64_t weekday(int64_t days)
{
    const int64_t w = (days + 4) % 7;   /* 1970-01-01 was a Thursday */
    return (w < 0) ? (w + 7) : w;
}

/** @brief Day count of the @p n th Sunday of a month (n >= 1). */
static int64_t nth_sunday(int y, int m, int n)
{
    const int64_t d1 = days_from_civil(y, m, 1);
    return d1 + ((7 - weekday(d1)) % 7) + (7 * (n - 1));
}

/** @brief Day count of the last Sunday of a month (m <= 11). */
static int64_t last_sunday(int y, int m)
{
    const int64_t dl = days_from_civil(y, m + 1, 1) - 1;
    return dl - weekday(dl);
}

int civil_local_offset_min(int64_t utc, int std_off_min, int rule)
{
    /* The year the local clock is in. Days from 1970 / 365.2425 can be a year
     * out near New Year, so step it until Jan 1 of it brackets the day. */
    const int64_t std_s = utc + (static_cast<int64_t>(std_off_min) * 60);
    const int64_t day   = (std_s >= 0) ? (std_s / 86400) : (((std_s + 1) / 86400) - 1);
    int y = 1970 + static_cast<int>((day * 400) / 146097);
    while (days_from_civil(y, 1, 1) > day)      { y--; }
    while (days_from_civil(y + 1, 1, 1) <= day) { y++; }

    /* Transition instants in UTC seconds. The local ones are at 02:00 standard
     * time, which is also 03:00 daylight time for the way back. */
    const int64_t loc = (2 * 3600) - (static_cast<int64_t>(std_off_min) * 60);
    int64_t on  = 0;
    int64_t off = 0;
    switch (rule) {
        case CIVIL_DST_EU:
            on  = (last_sunday(y, 3)  * 86400) + 3600;
            off = (last_sunday(y, 10) * 86400) + 3600;
            break;
        case CIVIL_DST_US:
            on  = (nth_sunday(y, 3, 2)  * 86400) + loc;
            off = (nth_sunday(y, 11, 1) * 86400) + loc - 3600;
            break;
        case CIVIL_DST_AU:   /* southern: DST spans New Year, so on > off */
            on  = (nth_sunday(y, 10, 1) * 86400) + loc;
            off = (nth_sunday(y, 4, 1)  * 86400) + loc;
            break;
        case CIVIL_DST_NZ:
            on  = (last_sunday(y, 9)   * 86400) + loc;
            off = (nth_sunday(y, 4, 1) * 86400) + loc;
            break;
        default:
            return std_off_min;
    }
    const bool dst = (on < off) ? ((utc >= on) && (utc < off))
                                : ((utc >= on) || (utc < off));
    return std_off_min + (dst ? 60 : 0);
}
