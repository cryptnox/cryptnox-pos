/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ota.cpp
 * @brief Firmware slot handling. See ota.h for the why.
 */

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "ota.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* Before esp_ota_ops.h, and it has to stay there — same reason provision.cpp
 * includes it first: CW_Utils.h drags in Arduino's IPAddress.h, whose
 * `extern const IPAddress INADDR_NONE;` stops parsing once lwIP's headers have
 * turned INADDR_NONE into a macro, and the esp_ota headers reach lwIP. */
#include "CW_Utils.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "ota_version.h"

static const char *const TAG = "ota";

/******************************************************************
 * 2. Constants
 ******************************************************************/

/* Below this an "image" is a truncated download or somebody poking at the
 * endpoint, and not worth erasing a 2 MB partition over. */
#define OTA_MIN_IMAGE  (256U * 1024U)

/******************************************************************
 * 3. Module state
 ******************************************************************/

static char s_running_ver[OTA_VERSION_MAX + 1] = "";

/* An image that has been received, verified and written to the idle slot but
 * NOT made bootable. Written by the HTTP task, read and resolved by the UI task
 * once the operator has accepted it on the panel — two tasks and a value that
 * decides which firmware signs the next transaction, so it takes a lock. */
static SemaphoreHandle_t s_lock          = NULL;
static bool              s_staged        = false;
static bool              s_staged_older  = false;
static char              s_staged_ver[OTA_VERSION_MAX + 1] = "";

/* The upload in flight. Only one at a time — a second POST is refused rather than
 * interleaved into the same partition. */
static volatile bool     s_receiving     = false;
static esp_ota_handle_t  s_handle        = 0;
static const esp_partition_t *s_dst      = NULL;

/** @brief Create the staging lock on first use. */
static bool lock_ready(void)
{
    if (s_lock == NULL) { s_lock = xSemaphoreCreateMutex(); }
    return s_lock != NULL;
}

/******************************************************************
 * 4. Receiving an image
 ******************************************************************/

/**
 * @brief Refuse an upload, saying so to the browser AND to the log.
 *
 * Five of these refusals used to be silent on the serial side, which left an
 * operator reporting "it says not installed" with nothing to match it against —
 * and the reasons are not interchangeable: a stale staging, a file of the wrong
 * size and a slot that would not erase want three different things done.
 */
static bool refuse(const char **err, const char *msg)
{
    *err = msg;
    ESP_LOGW(TAG, "upload refused: %s", msg);
    return false;
}

bool ota_begin(size_t len, const char **err)
{
    static const char *ignored = "";
    if (err == NULL) { err = &ignored; }

    if (!lock_ready()) {
        return refuse(err, "The terminal is out of memory.");
    }
    if (s_receiving) {
        return refuse(err, "Another upload is in progress.");
    }

    bool staged = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        staged = s_staged;
        (void)xSemaphoreGive(s_lock);
    }
    if (staged) {
        return refuse(err, "An update is already waiting to be accepted on the "
                           "terminal screen. Accept or discard it there first.");
    }

    s_dst = esp_ota_get_next_update_partition(NULL);
    if (s_dst == NULL) {
        /* Single-app partition table: this unit predates OTA support and cannot
         * be updated over the air at all. Say which, or it reads as a bug. */
        ESP_LOGE(TAG, "no OTA slot - unit needs a serial reflash first");
        return refuse(err, "This terminal has no second firmware slot. It has to "
                           "be reflashed over USB once before it can take "
                           "updates.");
    }
    if (len < OTA_MIN_IMAGE) {
        return refuse(err, "That file is too small to be firmware.");
    }
    if (len > s_dst->size) {
        return refuse(err, "That file is larger than the firmware slot.");
    }

    /* Passing the real length erases only the pages that will be written. */
    const esp_err_t rc = esp_ota_begin(s_dst, len, &s_handle);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(rc));
        return refuse(err, "The terminal could not prepare its firmware slot.");
    }

    s_receiving = true;
    ESP_LOGI(TAG, "receiving %u bytes into '%s'",
             static_cast<unsigned>(len), s_dst->label);
    return true;
}

bool ota_write(const void *buf, size_t n)
{
    if (!s_receiving) { return false; }
    const esp_err_t rc = esp_ota_write(s_handle, buf, n);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(rc));
        return false;
    }
    return true;
}

void ota_abort(void)
{
    if (!s_receiving) { return; }
    (void)esp_ota_abort(s_handle);
    s_handle    = 0;
    s_receiving = false;
    ESP_LOGW(TAG, "upload aborted - nothing installed");
}

bool ota_receiving(void) { return s_receiving; }

bool ota_end(char *ver_out, size_t ver_n, const char **err)
{
    static const char *ignored = "";
    if (err == NULL) { err = &ignored; }

    if (!s_receiving) {
        *err = "No upload was in progress.";
        return false;
    }

    const esp_err_t rc = esp_ota_end(s_handle);
    s_handle    = 0;
    s_receiving = false;
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "image rejected: %s", esp_err_to_name(rc));
        *err = (rc == ESP_ERR_OTA_VALIDATE_FAILED)
             ? "The terminal rejected that image: it is not valid firmware, or "
               "it is not signed with the key this terminal trusts."
             : "The terminal could not store that image.";
        return false;
    }

    /* Read the version out of the image that was just verified, not out of
     * anything the browser said about it. */
    esp_app_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    char ver[OTA_VERSION_MAX + 1] = "?";
    if ((s_dst != NULL) &&
        (esp_ota_get_partition_description(s_dst, &desc) == ESP_OK)) {
        (void)snprintf(ver, sizeof(ver), "%.*s",
                       static_cast<int>(sizeof(desc.version)), desc.version);
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        *err = "The terminal is busy.";
        return false;
    }
    s_staged       = true;
    s_staged_older = (ota_version_cmp(ver, ota_running_version()) < 0);
    (void)snprintf(s_staged_ver, sizeof(s_staged_ver), "%s", ver);
    (void)xSemaphoreGive(s_lock);

    ESP_LOGW(TAG, "staged %s in '%s' - awaiting on-screen accept", ver,
             (s_dst != NULL) ? s_dst->label : "?");
    if ((ver_out != NULL) && (ver_n > 0U)) {
        (void)snprintf(ver_out, ver_n, "%s", ver);
    }
    return true;
}

/******************************************************************
 * 5. Slot handling
 ******************************************************************/

bool ota_last_update_failed(void)
{
    const esp_partition_t *idle = esp_ota_get_next_update_partition(NULL);
    if (idle == NULL) { return false; }

    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(idle, &st) != ESP_OK) { return false; }
    return (st == ESP_OTA_IMG_ABORTED) || (st == ESP_OTA_IMG_INVALID);
}

bool ota_mark_valid(void)
{
    /* Before the early returns below, because this is the one call that happens
     * once per boot with bring-up behind it — and a terminal that reverted to its
     * old firmware overnight is exactly the thing whose log line nobody has. */
    if (ota_last_update_failed()) {
        ESP_LOGE(TAG, "the last update did NOT stick: the new image booted and "
                      "never confirmed itself, so this terminal rolled back to %s. "
                      "Install it again and leave it alone until the reader is up.",
                 ota_running_version());
    }

    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run == NULL) { return false; }

    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(run, &st) != ESP_OK) { return false; }
    if (st != ESP_OTA_IMG_PENDING_VERIFY) { return false; }  /* not a fresh update */

    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGW(TAG, "update to %s confirmed - rollback cancelled",
                 ota_running_version());
    } else {
        /* The next reset goes back to the old slot. Loud, because the terminal
         * works right now and will silently be a different version tomorrow. */
        ESP_LOGE(TAG, "could not confirm this image - it WILL roll back");
    }
    /* Fresh either way: the image did boot. Whether it also managed to cancel its
     * rollback changes nothing for the operator standing in front of it. */
    return true;
}

const char *ota_running_version(void)
{
    if (s_running_ver[0] == '\0') {
        const esp_app_desc_t *d = esp_app_get_description();
        /* esp_app_desc_t::version is a fixed 32-byte field with no promise of a
         * terminator. Bound the copy. */
        (void)snprintf(s_running_ver, sizeof(s_running_ver), "%.*s",
                       static_cast<int>(sizeof(d->version)), d->version);
    }
    return s_running_ver;
}

bool ota_staged(char *version, size_t version_n, bool *older)
{
    if (s_lock == NULL) { return false; }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) { return false; }

    const bool staged = s_staged;
    if (staged) {
        if ((version != NULL) && (version_n > 0U)) {
            (void)snprintf(version, version_n, "%s", s_staged_ver);
        }
        if (older != NULL) { *older = s_staged_older; }
    }
    (void)xSemaphoreGive(s_lock);
    return staged;
}

bool ota_commit(bool install)
{
    if (s_lock == NULL) { return false; }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) { return false; }

    const bool staged = s_staged;
    s_staged = false;
    char ver[OTA_VERSION_MAX + 1];
    (void)snprintf(ver, sizeof(ver), "%s", s_staged_ver);
    s_staged_ver[0] = '\0';
    (void)xSemaphoreGive(s_lock);

    if (!staged || !install) { return false; }

    /* The point of no return, and the only line in this file that changes what
     * the terminal boots. Everything before it was reversible. */
    const esp_partition_t *dst = esp_ota_get_next_update_partition(NULL);
    const esp_err_t rc = esp_ota_set_boot_partition(dst);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(rc));
        return false;
    }

    ESP_LOGW(TAG, "installing %s from '%s' - rebooting", ver,
             (dst != NULL) ? dst->label : "?");
    /* Let the log drain and the panel finish its last frame. */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return true;   /* not reached */
}
