/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file https_post.h
 * @ingroup eth
 * @brief One HTTPS JSON POST, shared by every RPC client in the firmware.
 *
 * Moved out of eth_rpc.cpp when the Tron client arrived: the response-Date
 * clock cross-check that defends against a spoofed SNTP clock must have a
 * single implementation, or one endpoint ends up without it.
 */

#ifndef HTTPS_POST_H
#define HTTPS_POST_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief POST a JSON body over HTTPS and read the response.
 *
 * The response is read until EOF or buffer-full and is always NUL-terminated.
 * The server's @c Date header is cross-checked against the system clock; a
 * parseable header that disagrees by more than five minutes fails the call
 * (see https_post.cpp for why that is the right verdict).
 *
 * @param[in]  url           Full HTTPS endpoint URL.
 * @param[in]  body          JSON request body (NUL-terminated).
 * @param[out] resp_buf      Response buffer, NUL-terminated on return.
 * @param[in]  resp_buf_size Capacity of @p resp_buf.
 * @param[in]  user          HTTP Basic Auth username, or NULL/"" for none.
 * @param[in]  pass          HTTP Basic Auth password, or NULL/"" for none.
 * @param[in]  ca_pem        PEM certificate to validate against instead of the
 *                           Mozilla CA bundle, or NULL for the bundle.
 * @return true only if at least one byte was read AND the server answered
 *         HTTP 200; false on transport error, non-200 status or clock refusal.
 */
bool https_post_json(const char *url, const char *body,
                     char *resp_buf, size_t resp_buf_size,
                     const char *user, const char *pass, const char *ca_pem);

#ifdef __cplusplus
}
#endif

#endif /* HTTPS_POST_H */
