/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file hardening.cpp
 * @brief Anomaly handling (§3.3): local fail-closed + persisted counter and a
 *        RAM ring buffer for on-site inspection. Never bricks.
 */

#include "hardening.h"

#include <stdio.h>
#include "nvs.h"
#include "esp_log.h"

static const char *const TAG = "harden";

#define NS_HARDEN   "harden"
#define K_ANOMALY   "anomaly_ct"

/* In-RAM ring of the most recent anomalies; a technician reads these over the
 * log on-site. The total count is persisted so it survives reboots. */
#define ANOM_RING 8U
typedef struct {
    char     where[24];
    uint32_t at_count;
} anom_entry_t;
static anom_entry_t s_ring[ANOM_RING];
static uint32_t     s_ring_head = 0U;

/* Dual counter — a glitch on the counter itself is also an anomaly (§3.3). */
static volatile uint32_t s_ctr      = 0U;
static volatile uint32_t s_ctr_echo = 0U;
static bool              s_loaded   = false;

/**
 * @brief Read the persisted anomaly counter from NVS.
 *
 * @return The stored count, or 0 when nothing is stored yet or the entry
 *         could not be read (never fails — a lost history must not brick).
 */
static uint32_t nvs_load_ctr(void)
{
    uint32_t v = 0U;
    nvs_handle_t h;
    if (nvs_open(NS_HARDEN, NVS_READONLY, &h) == ESP_OK) {
        esp_err_t err = nvs_get_u32(h, K_ANOMALY, &v);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            /* First boot: no anomaly has ever been recorded. */
        } else if (err != ESP_OK) {
            /* The entry exists but failed its CRC — the history is lost, and
             * that loss is itself worth surfacing to the technician. */
            ESP_LOGE(TAG, "anomaly counter unreadable (%s), restarting from 0",
                     esp_err_to_name(err));
            v = 0U;
        }
        nvs_close(h);
    } else {
        ESP_LOGW(TAG, "anomaly counter: nvs_open failed");
    }
    return v;
}

/**
 * @brief Persist the anomaly counter to NVS.
 *
 * @param[in] v Counter value to store. Write failures are logged and ignored.
 */
static void nvs_store_ctr(uint32_t v)
{
    nvs_handle_t h;
    if (nvs_open(NS_HARDEN, NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_set_u32(h, K_ANOMALY, v);
        (void)nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGW(TAG, "anomaly counter: nvs_open failed");
    }
}

/**
 * @brief Lazily seed both counter copies from NVS on first use.
 *
 * Idempotent: later calls are no-ops, so every public entry point can call it
 * without caring whether NVS has been read yet.
 */
static void ensure_loaded(void)
{
    if (!s_loaded) {
        const uint32_t v = nvs_load_ctr();
        s_ctr      = v;
        s_ctr_echo = v;
        s_loaded   = true;
    }
}

void pos_handle_anomaly(const char *where)
{
    ensure_loaded();

    s_ctr      += 1U;
    s_ctr_echo += 1U;
    if (s_ctr != s_ctr_echo) {
        /* the counter got glitched too — take the higher of the two */
        const uint32_t hi = (s_ctr > s_ctr_echo) ? s_ctr : s_ctr_echo;
        s_ctr      = hi;
        s_ctr_echo = hi;
    }

    anom_entry_t *e = &s_ring[s_ring_head % ANOM_RING];
    s_ring_head++;
    (void)snprintf(e->where, sizeof(e->where), "%s", (where != NULL) ? where : "?");
    e->at_count = s_ctr;

    ESP_LOGE(TAG, "anomaly #%u: %s", (unsigned)s_ctr, (where != NULL) ? where : "?");
    nvs_store_ctr(s_ctr);

    /* Fail-closed for the CURRENT transaction only: the caller's decision
     * returns FALSE32 and the UI shows FAILED. No server, no brick — the
     * persisted counter lets a technician inspect anomalies on-site (§3.3). */
}

uint32_t pos_anomaly_count(void)
{
    ensure_loaded();
    return s_ctr;
}
