/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file eth_json.cpp
 * @brief Implementation of the JSON-RPC response parsing helpers.
 */

#include "eth_json.h"

#include <string.h>

#include "CW_Utils.h"   /* hardened memory primitives (CODING_RULES §1.4) */
#include "cJSON.h"

bool eth_json_result_string(const char *resp, char *out, size_t out_size)
{
    bool ok = false;
    cJSON *root = cJSON_Parse(resp);
    if (root == NULL) {
        return false;
    }
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (cJSON_IsString(result) && (result->valuestring != NULL)) {
        size_t len = strlen(result->valuestring);
        if ((len + 1U) <= out_size) {
            ok = CW_Utils::safe_memcpy(
                reinterpret_cast<uint8_t *>(out), out_size,
                reinterpret_cast<const uint8_t *>(result->valuestring),
                len + 1U);
        }
    }
    cJSON_Delete(root);
    return ok;
}

bool eth_json_error_message(const char *resp, char *out, size_t out_size)
{
    if ((out == NULL) || (out_size == 0U)) { return false; }

    bool ok = false;
    cJSON *root = cJSON_Parse(resp);
    if (root == NULL) {
        return false;
    }
    const cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (cJSON_IsObject(err)) {
        const cJSON *msg = cJSON_GetObjectItemCaseSensitive(err, "message");
        if (cJSON_IsString(msg) && (msg->valuestring != NULL) &&
            (msg->valuestring[0] != '\0')) {
            /* Truncate, unlike result_string: that one carries a tx hash, where
             * a short copy would be a different hash and must be refused. This
             * one carries prose for a human, and a clipped sentence is still
             * worth more than the generic message it replaces. */
            size_t len = strlen(msg->valuestring);
            if (len > (out_size - 1U)) { len = out_size - 1U; }
            ok = CW_Utils::safe_memcpy(
                reinterpret_cast<uint8_t *>(out), out_size,
                reinterpret_cast<const uint8_t *>(msg->valuestring), len);
            if (ok) { out[len] = '\0'; }
        }
    }
    cJSON_Delete(root);
    return ok;
}

eth_json_receipt_t eth_json_receipt_status(const char *resp)
{
    eth_json_receipt_t verdict = ETH_JSON_RECEIPT_ERROR;
    cJSON *root = cJSON_Parse(resp);
    if (root == NULL) {
        return ETH_JSON_RECEIPT_ERROR;
    }
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (cJSON_IsNull(result)) {
        verdict = ETH_JSON_RECEIPT_PENDING;   /* not mined yet */
    } else if (cJSON_IsObject(result)) {
        const cJSON *status =
            cJSON_GetObjectItemCaseSensitive(result, "status");
        if (cJSON_IsString(status) && (status->valuestring != NULL)) {
            verdict = (strcmp(status->valuestring, "0x1") == 0)
                          ? ETH_JSON_RECEIPT_SUCCESS
                          : ETH_JSON_RECEIPT_REVERTED;
        }
    }
    cJSON_Delete(root);
    return verdict;
}

/* keccak256("Transfer(address,address,uint256)") */
static const char TRANSFER_TOPIC[] =
    "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef";

static char lc(char c)
{
    return ((c >= 'A') && (c <= 'Z')) ? static_cast<char>(c + ('a' - 'A')) : c;
}

/** @brief true if a JSON string equals @p want, ignoring hex case. */
static bool str_is(const cJSON *j, const char *want)
{
    if (!cJSON_IsString(j) || (j->valuestring == NULL)) { return false; }
    const char *v = j->valuestring;
    size_t i = 0U;
    for (; (v[i] != '\0') && (want[i] != '\0'); i++) {
        if (lc(v[i]) != lc(want[i])) { return false; }
    }
    return (v[i] == '\0') && (want[i] == '\0');
}

/** @brief "0x" + the 20 bytes as hex, or left-padded to a 32-byte word. */
static void addr_hex(const uint8_t a[20], bool word, char *out)
{
    static const char H[] = "0123456789abcdef";
    size_t p = 0U;
    out[p++] = '0';
    out[p++] = 'x';
    if (word) { for (size_t i = 0U; i < 24U; i++) { out[p++] = '0'; } }
    for (size_t i = 0U; i < 20U; i++) {
        out[p++] = H[a[i] >> 4];
        out[p++] = H[a[i] & 0x0FU];
    }
    out[p] = '\0';
}

/** @brief "0x" + @p v as a 32-byte big-endian word. */
static void word_hex(uint64_t v, char *out)
{
    static const char H[] = "0123456789abcdef";
    size_t p = 0U;
    out[p++] = '0';
    out[p++] = 'x';
    for (size_t i = 0U; i < 48U; i++) { out[p++] = '0'; }
    for (int sh = 60; sh >= 0; sh -= 4) {
        out[p++] = H[(v >> sh) & 0x0FULL];
    }
    out[p] = '\0';
}

/** @brief true if @p logs holds a Transfer(*, payee, amount) from @p token. */
static bool has_transfer(const cJSON *logs, const char *token,
                         const char *payee_word, const char *amount_word)
{
    const cJSON *log = NULL;
    cJSON_ArrayForEach(log, logs) {
        const cJSON *topics = cJSON_GetObjectItemCaseSensitive(log, "topics");
        if (!str_is(cJSON_GetObjectItemCaseSensitive(log, "address"), token) ||
            !cJSON_IsArray(topics) || (cJSON_GetArraySize(topics) != 3)) {
            continue;
        }
        if (str_is(cJSON_GetArrayItem(topics, 0), TRANSFER_TOPIC) &&
            str_is(cJSON_GetArrayItem(topics, 2), payee_word) &&
            str_is(cJSON_GetObjectItemCaseSensitive(log, "data"), amount_word)) {
            return true;
        }
    }
    return false;
}

eth_json_receipt_t eth_json_receipt_check(const char *resp,
                                          const eth_receipt_expect_t *want)
{
    if ((want == NULL) || (want->tx_hash == NULL) || (want->to == NULL)) {
        return ETH_JSON_RECEIPT_ERROR;
    }
    const eth_json_receipt_t status = eth_json_receipt_status(resp);
    if ((status != ETH_JSON_RECEIPT_SUCCESS) &&
        (status != ETH_JSON_RECEIPT_REVERTED)) {
        return status;              /* pending, or not a receipt at all */
    }

    cJSON *root = cJSON_Parse(resp);
    if (root == NULL) { return ETH_JSON_RECEIPT_ERROR; }
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "result");

    char to_hex[43];
    addr_hex(want->to, false, to_hex);
    bool match = str_is(cJSON_GetObjectItemCaseSensitive(r, "transactionHash"),
                        want->tx_hash) &&
                 str_is(cJSON_GetObjectItemCaseSensitive(r, "to"), to_hex);

    /* A reverted token call moved nothing, so there is no log to look for —
     * but it still has to be OUR transaction before its verdict is believed. */
    if (match && (status == ETH_JSON_RECEIPT_SUCCESS) && (want->payee != NULL)) {
        char payee_word[67];
        char amount_word[67];
        addr_hex(want->payee, true, payee_word);
        word_hex(want->amount, amount_word);
        match = has_transfer(cJSON_GetObjectItemCaseSensitive(r, "logs"),
                             to_hex, payee_word, amount_word);
    }
    cJSON_Delete(root);
    return match ? status : ETH_JSON_RECEIPT_MISMATCH;
}
