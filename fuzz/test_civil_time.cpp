/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file test_civil_time.cpp
 * @brief Host self-check for the civil-date arithmetic behind the clock
 *        hardening.  Single-TU: #includes the production code under test,
 *        same pattern as the fuzz harnesses.
 *
 * Runnable: cmake --build . --target test_civil_time && ./test_civil_time
 * Exits non-zero (assert) on the first disagreement.
 */

#include "../main/civil_time.cpp"

#include <assert.h>
#include <stdio.h>

/** @brief Reference values cross-checked against `date -u -d ... +%s`. */
static void test_epoch_reference_points(void)
{
    assert(civil_to_epoch(1970, 1, 1, 0, 0, 0) == 0);
    assert(civil_to_epoch(1970, 1, 2, 0, 0, 0) == 86400);
    assert(civil_to_epoch(2000, 1, 1, 0, 0, 0) == 946684800);
    assert(civil_to_epoch(2026, 7, 29, 15, 14, 9) == 1785338049);

    /* The pinned GTS WE1 intermediate's notAfter — the re-pin deadline. */
    assert(civil_to_epoch(2029, 2, 20, 14, 0, 0) == 1866290400);
}

/** @brief Leap-year handling is where hand-rolled date maths usually dies. */
static void test_leap_years(void)
{
    /* 2024 is a leap year: Feb 29 exists and Mar 1 is the day after. */
    const int64_t feb29 = civil_to_epoch(2024, 2, 29, 0, 0, 0);
    assert(feb29 == 1709164800);
    assert(civil_to_epoch(2024, 3, 1, 0, 0, 0) - feb29 == 86400);

    /* 2100 is NOT a leap year (divisible by 100, not by 400). */
    assert(civil_to_epoch(2100, 3, 1, 0, 0, 0) -
           civil_to_epoch(2100, 2, 28, 0, 0, 0) == 86400);

    /* 2000 WAS a leap year (divisible by 400). */
    assert(civil_to_epoch(2000, 3, 1, 0, 0, 0) -
           civil_to_epoch(2000, 2, 28, 0, 0, 0) == 2 * 86400);

    /* Year length: 365 days normally, 366 across a leap year. */
    assert(civil_to_epoch(2027, 1, 1, 0, 0, 0) -
           civil_to_epoch(2026, 1, 1, 0, 0, 0) == 365 * 86400);
    assert(civil_to_epoch(2025, 1, 1, 0, 0, 0) -
           civil_to_epoch(2024, 1, 1, 0, 0, 0) == 366 * 86400);
}

/** @brief Monotonicity across every month boundary for a decade. */
static void test_monotonic(void)
{
    int64_t prev = civil_to_epoch(2020, 1, 1, 0, 0, 0);
    int     y;
    int     m;

    for (y = 2020; y <= 2030; y++) {
        for (m = 1; m <= 12; m++) {
            const int64_t cur = civil_to_epoch(y, m, 1, 0, 0, 0);
            if ((y == 2020) && (m == 1)) { continue; }
            assert(cur > prev);
            assert(cur - prev >= 28 * 86400);
            assert(cur - prev <= 31 * 86400);
            prev = cur;
        }
    }
}

static void test_month_abbrev(void)
{
    assert(civil_month_from_abbrev("Jan") == 1);
    assert(civil_month_from_abbrev("Jul") == 7);
    assert(civil_month_from_abbrev("Dec") == 12);
    assert(civil_month_from_abbrev("Foo") == 0);
    assert(civil_month_from_abbrev("jan") == 0);   /* case-sensitive by design */
    assert(civil_month_from_abbrev(NULL)  == 0);
}

/** @brief __DATE__/__TIME__ parsing, including the space-padded day. */
static void test_build_stamp(void)
{
    int64_t out = 0;

    assert(civil_parse_build_stamp("Jul 29 2026", "15:14:09", &out));
    assert(out == 1785338049);

    /* Single-digit days are space-padded by the standard: "Jul  9 2026". */
    assert(civil_parse_build_stamp("Jul  9 2026", "00:00:00", &out));
    assert(out == civil_to_epoch(2026, 7, 9, 0, 0, 0));

    /* This firmware's own stamp must parse — catches a toolchain that
     * deviates from the standard __DATE__ format. */
    assert(civil_parse_build_stamp(__DATE__, __TIME__, &out));
    assert(out > civil_to_epoch(2020, 1, 1, 0, 0, 0));

    /* Malformed input is rejected, not guessed. */
    assert(!civil_parse_build_stamp("Foo 29 2026", "15:14:09", &out));
    assert(!civil_parse_build_stamp("Jul 29 2026", "15:14",    &out));
    assert(!civil_parse_build_stamp("",            "15:14:09", &out));
    assert(!civil_parse_build_stamp("Jul 32 2026", "15:14:09", &out));
    assert(!civil_parse_build_stamp("Jul 29 1969", "15:14:09", &out));
    assert(!civil_parse_build_stamp(NULL,          "15:14:09", &out));
    assert(!civil_parse_build_stamp("Jul 29 2026", NULL,       &out));
}

/** @brief IMF-fixdate parsing — the corroboration path's only input. */
static void test_http_date(void)
{
    int64_t out = 0;

    assert(civil_parse_http_date("Wed, 29 Jul 2026 15:14:09 GMT", &out));
    assert(out == 1785338049);

    /* Any day-of-week is accepted: it is redundant with the date, and a
     * server that computes it wrong must not take the terminal offline. */
    assert(civil_parse_http_date("Mon, 29 Jul 2026 15:14:09 GMT", &out));
    assert(out == 1785338049);

    /* Midnight and the leap-second second both round-trip. */
    assert(civil_parse_http_date("Thu, 01 Jan 2026 00:00:00 GMT", &out));
    assert(out == civil_to_epoch(2026, 1, 1, 0, 0, 0));
    assert(civil_parse_http_date("Tue, 30 Jun 2026 23:59:60 GMT", &out));

    /* Rejected: obsolete formats, non-UTC offsets, truncation, junk.
     * Every one of these returns false, which the caller treats as
     * "no usable corroboration" (skip) — never as agreement. */
    assert(!civil_parse_http_date("Wednesday, 29-Jul-26 15:14:09 GMT", &out));
    assert(!civil_parse_http_date("Wed Jul 29 15:14:09 2026",          &out));
    assert(!civil_parse_http_date("Wed, 29 Jul 2026 15:14:09 +0200",   &out));
    assert(!civil_parse_http_date("Wed, 29 Jul 2026 15:14:09",         &out));
    assert(!civil_parse_http_date("Wed, 29 Jul 2026 25:14:09 GMT",     &out));
    assert(!civil_parse_http_date("Wed, 29 Foo 2026 15:14:09 GMT",     &out));
    assert(!civil_parse_http_date("garbage",                           &out));
    assert(!civil_parse_http_date("",                                  &out));
    assert(!civil_parse_http_date(NULL,                                &out));
}

/**
 * @brief The property the hardening actually relies on: reviving an expired
 *        certificate needs a clock error of weeks, and both the build-date
 *        floor and the ±5 min window are far tighter than that.
 */
static void test_attack_window(void)
{
    const int64_t build  = civil_to_epoch(2026, 7, 29, 12, 0, 0);
    const int64_t spoof  = civil_to_epoch(2026, 1, 15, 12, 0, 0); /* back-dated */
    const int64_t honest = civil_to_epoch(2026, 7, 29, 12, 0, 30);

    assert(spoof  <  build);    /* refused by the build-date floor   */
    assert(honest >= build);    /* a normal boot is unaffected       */

    /* Latency-scale disagreement passes the ±5 min window; a back-date
     * of weeks does not. */
    assert((honest - build) < 300);
    assert((build - spoof)  > 300);
}

/** @brief DST switches at the published 2026 instants, a second each side. */
static void check_switch(int off, int rule, int64_t at, int before, int after)
{
    assert(civil_local_offset_min(at - 1, off, rule) == before);
    assert(civil_local_offset_min(at,     off, rule) == after);
}

static void test_dst(void)
{
    /* Zurich: Mar 29 and Oct 25, 01:00 UTC. */
    check_switch(60, CIVIL_DST_EU, civil_to_epoch(2026, 3, 29, 1, 0, 0), 60, 120);
    check_switch(60, CIVIL_DST_EU, civil_to_epoch(2026, 10, 25, 1, 0, 0), 120, 60);
    /* New York: Mar 8 02:00 EST, Nov 1 02:00 EDT. */
    check_switch(-300, CIVIL_DST_US, civil_to_epoch(2026, 3, 8, 7, 0, 0), -300, -240);
    check_switch(-300, CIVIL_DST_US, civil_to_epoch(2026, 11, 1, 6, 0, 0), -240, -300);
    /* Sydney: Apr 5 03:00 AEDT, Oct 4 02:00 AEST. */
    check_switch(600, CIVIL_DST_AU, civil_to_epoch(2026, 4, 4, 16, 0, 0), 660, 600);
    check_switch(600, CIVIL_DST_AU, civil_to_epoch(2026, 10, 3, 16, 0, 0), 600, 660);
    /* Auckland: Apr 5 03:00 NZDT, Sep 27 02:00 NZST. */
    check_switch(720, CIVIL_DST_NZ, civil_to_epoch(2026, 4, 4, 14, 0, 0), 780, 720);
    check_switch(720, CIVIL_DST_NZ, civil_to_epoch(2026, 9, 26, 14, 0, 0), 720, 780);
    /* Southern summer across New Year, and no rule means no change. */
    assert(civil_local_offset_min(civil_to_epoch(2026, 12, 31, 23, 0, 0), 600,
                                  CIVIL_DST_AU) == 660);
    assert(civil_local_offset_min(civil_to_epoch(2026, 7, 1, 0, 0, 0), 330,
                                  CIVIL_DST_NONE) == 330);
    assert(civil_local_offset_min(civil_to_epoch(2026, 7, 1, 0, 0, 0), 60, 99) == 60);
}

int main(void)
{
    test_epoch_reference_points();
    test_leap_years();
    test_monotonic();
    test_month_abbrev();
    test_build_stamp();
    test_http_date();
    test_attack_window();
    test_dst();

    printf("civil_time: all checks passed\n");
    return 0;
}
