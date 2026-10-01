/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file hardening.h
 * @ingroup app
 * @brief Decision-integrity primitives (see docs/HARDENING.md §3, §4).
 *
 * Anti-symmetric booleans, dual-stored business data (amount, recipient) and a
 * fail-closed payment-decision gate. On ESP32 these give bit-flip robustness,
 * NOT a fault-injection boundary — scope any FI claim to the STM32U585 secure
 * host, per the threat model.
 *
 * The types and the *pure* comparators are header-only (no ESP-IDF deps) so
 * they build and self-test on the host (see tests/units/test_hardening.cpp).
 * Anomaly persistence (NVS + log) lives in hardening.cpp, firmware-only.
 */

#ifndef HARDENING_H
#define HARDENING_H

#include <stdint.h>
#include <string.h>

#include "eth_addr.h"   /* ETH_ADDR_LEN — the recipient is an ETH address */

#ifdef __cplusplus
#include "CW_Utils.h"   /* SDK hardened primitives: constant-time secure_compare */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── §3.1 Anti-symmetric boolean ──────────────────────────────────── */
typedef uint32_t bool32;
/* Bitwise complements, Hamming distance 32: no small burst of flips can turn
 * FALSE32 into TRUE32. Only TRUE32 reads as true; ANY other pattern is false. */
#define TRUE32       ((bool32)0x5AA55AA5u)
#define FALSE32      ((bool32)0xA55AA55Au)
#define IS_TRUE32(x) ((x) == TRUE32)

/* ── §4 payment verdict token (anti-symmetric, never 0/1) ─────────── */
typedef uint32_t pos_verdict_t;
#define POS_VERDICT_APPROVED ((pos_verdict_t)0x33CC33CCu)
#define POS_VERDICT_DECLINED ((pos_verdict_t)0xCC33CC33u)

#ifdef __cplusplus
static_assert(TRUE32 == (bool32)~FALSE32,
              "bool32 sentinels must be bitwise complements");
static_assert(POS_VERDICT_APPROVED == (pos_verdict_t)~POS_VERDICT_DECLINED,
              "verdict sentinels must be bitwise complements");
#endif

/* ── §3.2 dual-stored business data ───────────────────────────────── */
typedef struct {
    uint64_t amount_minor;      /**< primary (USDC base units, 6 dp)            */
    uint64_t amount_minor_inv;  /**< bitwise complement of the primary, written
                                     independently — anti-symmetric like bool32 */
} pos_amount_t;

typedef struct {
    uint8_t addr[ETH_ADDR_LEN];       /**< primary recipient              */
    uint8_t addr_echo[ETH_ADDR_LEN];  /**< parsed independently, compared */
} pos_addr_t;

/** @brief Write both amount stores from one value (two independent writes). */
static inline void pos_amount_set(pos_amount_t *a, uint64_t v)
{
    a->amount_minor     = v;
    a->amount_minor_inv = ~v;   /* distinct pattern: the two stores can't be
                                   proven redundant, so neither is elided */
}

/**
 * @brief Report an anomaly: bump a self-checked persisted counter, log it, and
 *        keep a local ring buffer for on-site inspection.
 *
 * Fails the *current* transaction closed (the caller's gate returns FALSE32);
 * never bricks — a keyless POS holds no secret to protect (§3.3). Firmware
 * only (NVS + esp_log); defined in hardening.cpp.
 */
void pos_handle_anomaly(const char *where);

/** @brief Persisted total anomaly count (for the optional maintenance view). */
uint32_t pos_anomaly_count(void);

/* ── Pure comparators — no side effects, so the caller decides how to react
 * (and the host self-test needs no NVS/log stubs). ────────────────── */

static inline bool32 amount_consistent(const pos_amount_t *a)
{
    return (a->amount_minor == (uint64_t)~a->amount_minor_inv) ? TRUE32 : FALSE32;
}

static inline bool32 address_consistent(const pos_addr_t *a)
{
    /* ponytail: C++-only; every consumer is a .cpp. Restore a memcmp #else
     * branch if a C translation unit ever needs this header. */
    return CW_Utils::secure_compare(a->addr, a->addr_echo, ETH_ADDR_LEN) ? TRUE32
                                                                        : FALSE32;
}

/**
 * @brief §4 decision gate: return TRUE32 only if amount and recipient are
 *        self-consistent AND the verdict is the explicit APPROVED token.
 *
 * Single exit, default FALSE32 — every unexpected path (including a flip on
 * @p ok itself) leaves the gate closed. Pure: leaves logging to the caller.
 *
 * @param[in] amount  Dual-stored amount to reconcile.
 * @param[in] to      Dual-stored recipient to reconcile.
 * @param[in] verdict Verdict token from the operator's confirmation.
 * @return TRUE32 to proceed with the payment, FALSE32 otherwise.
 */
static inline bool32 run_payment_decision(const pos_amount_t *amount,
                                          const pos_addr_t   *to,
                                          pos_verdict_t       verdict)
{
    bool32 ok = FALSE32;

    if (IS_TRUE32(amount_consistent(amount)) &&
        IS_TRUE32(address_consistent(to))    &&
        (verdict == POS_VERDICT_APPROVED)) {
        ok = TRUE32;
    }

    return ok;
}

#ifdef __cplusplus
}
#endif

#endif /* HARDENING_H */
