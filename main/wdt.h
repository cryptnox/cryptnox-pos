/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file wdt.h
 * @ingroup device
 * @brief Task-watchdog feed that is safe to call from any task.
 *
 * The main loop and the UI loop subscribe to the task watchdog, which resets the
 * terminal (CONFIG_ESP_TASK_WDT_PANIC) when either stops feeding it for
 * CONFIG_ESP_TASK_WDT_TIMEOUT_S — a hung PN532 I2C transaction or a wedged HTTP
 * read, which used to freeze the panel until somebody pulled the plug.
 *
 * The long waits both loops make on purpose (the card wait, the receipt poll, an
 * HTTPS request) feed it from inside. Those helpers are also reached from tasks
 * that are not subscribed, where a bare esp_task_wdt_reset() logs "task not
 * found" on every call — hence the status check.
 */

#ifndef WDT_H
#define WDT_H

#include "esp_task_wdt.h"

/** @brief Feed the task watchdog if, and only if, the calling task is subscribed. */
static inline void wdt_feed(void)
{
    if (esp_task_wdt_status(NULL) == ESP_OK) { (void)esp_task_wdt_reset(); }
}

#endif /* WDT_H */
