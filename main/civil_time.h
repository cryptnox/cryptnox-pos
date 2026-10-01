/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file civil_time.h
 * @ingroup device
 * @brief TZ-independent calendar arithmetic and date-string parsing.
 *
 * Pure unit — no IDF, no lwip, no globals — so it builds and self-checks on
 * the host (see fuzz/test_civil_time.cpp), same pattern as eth_json.cpp.
 *
 * Deliberately does NOT use mktime()/timegm(): mktime() interprets its input
 * in the current TZ, which on ESP-IDF depends on whether setenv("TZ") ran,
 * and timegm() is not portable.  Everything here is UTC by construction.
 */

#ifndef CIVIL_TIME_H
#define CIVIL_TIME_H

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************
 * 2. Public API
 ******************************************************************/

/**
 * @brief Convert a UTC civil date/time to seconds since the Unix epoch.
 *
 * Proleptic Gregorian, valid far beyond any range this firmware cares about.
 * No range validation — callers that parse untrusted text must validate
 * first (both parsers below do).
 *
 * @param[in] year Full year (e.g. 2026).
 * @param[in] mon  Month, 1–12.
 * @param[in] day  Day of month, 1–31.
 * @param[in] hour Hour, 0–23.
 * @param[in] min  Minute, 0–59.
 * @param[in] sec  Second, 0–60 (60 tolerated for leap seconds).
 * @return Seconds since 1970-01-01T00:00:00Z (negative before the epoch).
 */
int64_t civil_to_epoch(int year, int mon, int day, int hour, int min, int sec);

/**
 * @brief Map a three-letter English month abbreviation to 1–12.
 *
 * @param[in] mmm Three characters, case-sensitive ("Jan" … "Dec"); need not
 *                be NUL-terminated beyond the third character.
 * @return 1–12, or 0 if @p mmm is not a valid abbreviation.
 */
int civil_month_from_abbrev(const char *mmm);

/**
 * @brief Parse the compiler's __DATE__ / __TIME__ pair into an epoch value.
 *
 * Accepts the exact forms the C standard mandates: "Mmm dd yyyy" where dd is
 * space-padded for single-digit days ("Jul  9 2026"), and "hh:mm:ss".
 *
 * @param[in]  date     __DATE__-style string, e.g. "Jul 29 2026".
 * @param[in]  time_str __TIME__-style string, e.g. "15:14:09".
 * @param[out] out      Epoch seconds on success; untouched on failure.
 * @return true on success, false if either string is malformed.
 */
bool civil_parse_build_stamp(const char *date, const char *time_str,
                             int64_t *out);

/**
 * @brief Parse an HTTP-date in IMF-fixdate form into an epoch value.
 *
 * Example: "Wed, 29 Jul 2026 15:14:09 GMT".  RFC 9110 §5.6.7 requires senders
 * to emit exactly this form; the two obsolete forms (RFC 850, asctime) are
 * rejected rather than guessed at.  The day-of-week field is not cross-checked
 * against the date — it carries no information the date does not.
 *
 * @param[in]  hdr HTTP Date header value (NUL-terminated).
 * @param[out] out Epoch seconds on success; untouched on failure.
 * @return true on success, false if @p hdr is NULL or malformed.
 */
bool civil_parse_http_date(const char *hdr, int64_t *out);

/**
 * @brief Daylight-saving rules the panel clock knows, persisted in NVS as a
 *        number, so never renumber them.
 *
 * ponytail: the four rule sets that cover most places with DST; a region with
 * other rules (Chile, Israel, Egypt...) picks a fixed offset and moves it by
 * hand. Add a rule here and to the page's region list when one is needed.
 */
typedef enum {
    CIVIL_DST_NONE = 0,  /**< Fixed offset, no DST.                           */
    CIVIL_DST_EU   = 1,  /**< Last Sun Mar 01:00 UTC to last Sun Oct 01:00 UTC. */
    CIVIL_DST_US   = 2,  /**< 2nd Sun Mar 02:00 to 1st Sun Nov 02:00, local.  */
    CIVIL_DST_AU   = 3,  /**< 1st Sun Oct 02:00 to 1st Sun Apr 03:00, local.  */
    CIVIL_DST_NZ   = 4,  /**< Last Sun Sep 02:00 to 1st Sun Apr 03:00, local. */
    CIVIL_DST__COUNT
} civil_dst_t;

/**
 * @brief Offset from UTC in force at @p utc, DST included.
 *
 * @param[in] utc         Seconds since the Unix epoch.
 * @param[in] std_off_min Standard (winter) offset, minutes east of UTC.
 * @param[in] rule        A @ref civil_dst_t; anything else is treated as none.
 * @return @p std_off_min, plus 60 while DST is in force.
 */
int civil_local_offset_min(int64_t utc, int std_off_min, int rule);

#ifdef __cplusplus
}
#endif

#endif // CIVIL_TIME_H
