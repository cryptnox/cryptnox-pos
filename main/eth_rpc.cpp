/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file eth_rpc.cpp
 * @brief Ethereum JSON-RPC client implementation (HTTPS).  Network bring-up
 *        (Wi-Fi, SNTP) lives in net.cpp.
 */

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "eth_rpc.h"
#include "eth_json.h"
#include "https_post.h"

#include <string.h>
#include <strings.h>    /* strncasecmp */
#include <stdlib.h>     /* strtoull, malloc, free */
#include <stdio.h>      /* snprintf */
#include <inttypes.h>   /* PRIu64 */

/* CW_Utils.h pulls in Arduino.h (via platform_compat.h); it must come before
 * any lwip-including IDF header so that IPAddress.h declares INADDR_NONE
 * before lwip defines it as a macro. */
#include "CW_Utils.h"   /* hardened memory primitives (CODING_RULES §1.4) */

#include "esp_log.h"

static const char *const TAG = "eth_rpc";

/* JSON-RPC response buffer — large enough for any expected response. */
#define RESP_BUF_SIZE  1024U

/* Hex chars per byte */
#define HEX_PER_BYTE  2U

/* never dump full RPC responses (they can echo credentials embedded
 * in the URL) — log at most this many bytes on parse failures. */
#define RESP_LOG_MAX  80

/* sanity bound for the account nonce — a real terminal never gets
 * anywhere near 2^32 transactions, so anything above is a bogus response. */
#define NONCE_MAX  0xFFFFFFFFULL

/* Largest expected "result" string: 0x + 64 hex chars + NUL, rounded up. */
#define RESULT_STR_MAX  80U

/******************************************************************
 * 2. Constants and module state
 ******************************************************************/

static const char *s_rpc_url     = NULL;
static const char *s_from_addr   = NULL;
static const char *s_project_id  = NULL;
static const char *s_api_secret  = NULL;
static const char *s_ca_cert     = NULL;   /* pinned cert; NULL = CA bundle */

/******************************************************************
 * 4. HTTP helper
 ******************************************************************/

/**
 * @brief POST a JSON-RPC body to the configured endpoint over HTTPS.
 *
 * Thin wrapper over @ref https_post_json that supplies this module's endpoint,
 * optional Infura credentials and optional pinned certificate.
 *
 * @param[in]  body          JSON request body (NUL-terminated).
 * @param[out] resp_buf      Response buffer, NUL-terminated on return.
 * @param[in]  resp_buf_size Capacity of @p resp_buf.
 * @return true on an HTTP 200 with a non-empty body, false otherwise.
 */
static bool do_post(const char *body, char *resp_buf, size_t resp_buf_size)
{
    return https_post_json(s_rpc_url, body, resp_buf, resp_buf_size,
                           s_project_id, s_api_secret, s_ca_cert);
}

/******************************************************************
 * 5. Hex utilities
 ******************************************************************/

/**
 * @brief Convert a nibble value to its lowercase ASCII hex digit.
 *
 * @param[in] n Nibble value; only the range 0–15 is meaningful.
 * @return '0'–'9' or 'a'–'f'.
 */
static char hex_nibble(uint8_t n)
{
    return (n < 10U) ? static_cast<char>('0' + n)
                     : static_cast<char>('a' + n - 10U);
}

/**
 * @brief Hex-encode a byte buffer (lowercase, no prefix, no NUL).
 *
 * @param[in]  data Input bytes.
 * @param[in]  len  Number of input bytes.
 * @param[out] out  Output buffer of at least 2*len chars; not NUL-terminated.
 */
static void bytes_to_hex(const uint8_t *data, size_t len, char *out)
{
    size_t i;
    for (i = 0U; i < len; i++) {
        out[i * HEX_PER_BYTE]       = hex_nibble((data[i] >> 4U) & 0x0FU);
        out[i * HEX_PER_BYTE + 1U]  = hex_nibble(data[i] & 0x0FU);
    }
}

/* The JSON-RPC "result" string extractor lives in eth_json.cpp (a pure,
 * host-fuzzable unit — see fuzz/fuzz_eth_rpc_json.cpp). */

/******************************************************************
 * 7. Public API
 ******************************************************************/

void eth_rpc_init(const char *rpc_url, const char *from_addr)
{
    s_rpc_url   = rpc_url;
    s_from_addr = from_addr;
}

/* The payer, copied. eth_rpc_init keeps a pointer to its caller's storage,
 * which suits a config.h literal and not an address derived into a stack buffer
 * during one sale — so the per-tap override owns its bytes. */
static char s_from_buf[43];   /* "0x" + 40 hex + NUL */

bool eth_rpc_set_from(const char *addr)
{
    if (addr == NULL) { return false; }
    if ((addr[0] != '0') || ((addr[1] != 'x') && (addr[1] != 'X'))) {
        return false;
    }
    if (strlen(addr) != 42U) { return false; }

    (void)snprintf(s_from_buf, sizeof(s_from_buf), "%s", addr);
    s_from_addr = s_from_buf;
    return true;
}

void eth_rpc_set_auth(const char *project_id, const char *api_secret)
{
    s_project_id = project_id;
    s_api_secret = api_secret;
}

void eth_rpc_set_ca_cert(const char *ca_pem)
{
    s_ca_cert = ca_pem;
}

bool eth_rpc_get_nonce(uint64_t *nonce_out)
{
    char body[256];
    (void)snprintf(body, sizeof(body),
                   "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getTransactionCount\","
                   "\"params\":[\"%s\",\"latest\"],\"id\":1}",
                   s_from_addr);

    char resp[RESP_BUF_SIZE];
    if (!do_post(body, resp, sizeof(resp))) {
        return false;
    }

    char result[RESULT_STR_MAX];
    if (!eth_json_result_string(resp, result, sizeof(result))) {
        ESP_LOGE(TAG, "nonce: no result in: %.*s", RESP_LOG_MAX, resp);
        return false;
    }
    if (strncmp(result, "0x", 2U) != 0) {
        ESP_LOGE(TAG, "nonce: result not hex: %.*s", RESP_LOG_MAX, result);
        return false;
    }

    char *end = NULL;
    uint64_t nonce = strtoull(result + 2, &end, 16);
    if ((end == (result + 2)) || (*end != '\0')) {
        ESP_LOGE(TAG, "nonce: malformed hex: %.*s", RESP_LOG_MAX, result);
        return false;
    }
    /* strtoull saturates silently — reject absurd values outright. */
    if (nonce > NONCE_MAX) {
        ESP_LOGE(TAG, "nonce: out of range: %" PRIu64, nonce);
        return false;
    }

    *nonce_out = nonce;
    ESP_LOGI(TAG, "Nonce: %" PRIu64, nonce);
    return true;
}

/**
 * @brief The configured from-address with any "0x" prefix removed.
 *
 * Both callers below want the bare 40 characters — one to compare against what
 * ecrecover returned, the other to pad into an ABI argument.
 */
static const char *from_no_prefix(void)
{
    const char *p = s_from_addr;
    if ((p != NULL) && (p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X'))) {
        p += 2;
    }
    return p;
}

bool eth_rpc_get_balance(uint64_t *wei_out)
{
    if ((wei_out == NULL) || (s_from_addr == NULL)) { return false; }

    char body[256];
    (void)snprintf(body, sizeof(body),
                   "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBalance\","
                   "\"params\":[\"%s\",\"latest\"],\"id\":4}",
                   s_from_addr);

    char resp[RESP_BUF_SIZE];
    if (!do_post(body, resp, sizeof(resp))) {
        return false;
    }

    char result[RESULT_STR_MAX];
    if (!eth_json_result_string(resp, result, sizeof(result))) {
        ESP_LOGE(TAG, "balance: no result in: %.*s", RESP_LOG_MAX, resp);
        return false;
    }
    if (!eth_json_hex_quantity(result, wei_out)) {
        ESP_LOGE(TAG, "balance: malformed quantity: %.*s", RESP_LOG_MAX, result);
        return false;
    }
    return true;
}

bool eth_rpc_get_token_balance(const char *token_addr, uint64_t *units_out)
{
    if ((token_addr == NULL) || (units_out == NULL) || (s_from_addr == NULL)) {
        return false;
    }

    /* The ABI argument is the address left-padded to 32 bytes, so the bare 40
     * characters have to be exactly that. A short one would shift the padding
     * and ask the contract about a different account — which would answer, and
     * the answer would be about somebody else. */
    const char *from_hex = from_no_prefix();
    if (strlen(from_hex) != 40U) {
        ESP_LOGE(TAG, "token balance: from_addr is not 20 bytes");
        return false;
    }

    /* balanceOf(address): selector 70a08231, then 12 zero bytes + the address. */
    char body[320];
    (void)snprintf(body, sizeof(body),
                   "{\"jsonrpc\":\"2.0\",\"method\":\"eth_call\","
                   "\"params\":[{\"to\":\"%s\",\"data\":\"0x70a08231"
                   "000000000000000000000000%s\"},\"latest\"],\"id\":5}",
                   token_addr, from_hex);

    char resp[RESP_BUF_SIZE];
    if (!do_post(body, resp, sizeof(resp))) {
        return false;
    }

    char result[RESULT_STR_MAX];
    if (!eth_json_result_string(resp, result, sizeof(result))) {
        ESP_LOGE(TAG, "token balance: no result in: %.*s", RESP_LOG_MAX, resp);
        return false;
    }
    /* A call to an address with no code returns "0x" — an answer this must not
     * read as a zero balance, or a mistyped contract would look like an empty
     * account instead of like a misconfiguration. eth_json_hex_quantity rejects
     * it, and the caller treats a failed read as "cannot tell" rather than as
     * grounds to refuse. */
    if (!eth_json_hex_quantity(result, units_out)) {
        ESP_LOGE(TAG, "token balance: malformed quantity: %.*s",
                 RESP_LOG_MAX, result);
        return false;
    }
    return true;
}

bool eth_rpc_send_raw_tx(const uint8_t *tx, size_t tx_len,
                          char *tx_hash_out, size_t tx_hash_max,
                          char *err_out, size_t err_max)
{
    /* Cleared up front so every failure path below leaves a defined value: the
     * caller distinguishes "the node said why" from "we never got that far" by
     * whether this is empty, and a stale message from a previous sale would be
     * worse than none at all. */
    if ((err_out != NULL) && (err_max > 0U)) { err_out[0] = '\0'; }

    /* "0x" + 2 hex chars per byte + NUL */
    size_t hex_str_size = 2U + tx_len * HEX_PER_BYTE + 1U;
    char *tx_hex = static_cast<char *>(malloc(hex_str_size));
    if (tx_hex == NULL) { return false; }

    tx_hex[0] = '0';
    tx_hex[1] = 'x';
    bytes_to_hex(tx, tx_len, tx_hex + 2U);
    tx_hex[hex_str_size - 1U] = '\0';

    /* JSON body */
    size_t body_size = hex_str_size + 128U;
    char *body = static_cast<char *>(malloc(body_size));
    if (body == NULL) { free(tx_hex); return false; }

    (void)snprintf(body, body_size,
                   "{\"jsonrpc\":\"2.0\",\"method\":\"eth_sendRawTransaction\","
                   "\"params\":[\"%s\"],\"id\":2}",
                   tx_hex);
    free(tx_hex);

    char resp[RESP_BUF_SIZE];
    bool ok = do_post(body, resp, sizeof(resp));
    free(body);

    if (!ok) { return false; }

    /* Extract the "result" string (the tx hash) with a real JSON parse, so
     * a JSON-RPC error object is reported as a failure. */
    char result[RESULT_STR_MAX];
    if (!eth_json_result_string(resp, result, sizeof(result))) {
        ESP_LOGE(TAG, "send_raw_tx: no result in: %.*s", RESP_LOG_MAX, resp);
        /* A refusal, as opposed to a malformed body, carries the node's reason —
         * the one thing that tells an operator whether to top up gas, lower the
         * amount or just try again. Hand it back rather than log it and forget. */
        if ((err_out != NULL) && (err_max > 0U)) {
            (void)eth_json_error_message(resp, err_out, err_max);
        }
        return false;
    }
    if (strncmp(result, "0x", 2U) != 0) {
        ESP_LOGE(TAG, "send_raw_tx: result not a hash: %.*s",
                 RESP_LOG_MAX, result);
        return false;
    }

    size_t hash_len = strlen(result);
    if ((hash_len + 1U) > tx_hash_max) { return false; }

    (void)CW_Utils::safe_memcpy(reinterpret_cast<uint8_t *>(tx_hash_out),
                                tx_hash_max,
                                reinterpret_cast<const uint8_t *>(result),
                                hash_len);
    tx_hash_out[hash_len] = '\0';
    ESP_LOGI(TAG, "Tx hash: %s", tx_hash_out);
    return true;
}

bool eth_rpc_get_token_decimals(const char *token_addr, uint64_t *dec_out)
{
    if ((token_addr == NULL) || (dec_out == NULL)) { return false; }
    char body[224];
    int k = snprintf(body, sizeof(body),
                     "{\"jsonrpc\":\"2.0\",\"method\":\"eth_call\","
                     "\"params\":[{\"to\":\"%s\",\"data\":\"0x313ce567\"},"
                     "\"latest\"],\"id\":6}",
                     token_addr);
    if ((k <= 0) || (static_cast<size_t>(k) >= sizeof(body))) { return false; }

    char resp[RESP_BUF_SIZE];
    if (!do_post(body, resp, sizeof(resp))) { return false; }
    char result[RESULT_STR_MAX];
    /* "0x" from an address with no code is refused by eth_json_hex_quantity. */
    if (!eth_json_result_string(resp, result, sizeof(result)) ||
        !eth_json_hex_quantity(result, dec_out)) {
        ESP_LOGE(TAG, "decimals: no usable answer: %.*s", RESP_LOG_MAX, resp);
        return false;
    }
    return true;
}

bool eth_rpc_err_already_known(const char *node_err)
{
    return (node_err != NULL) &&
           ((strstr(node_err, "already known") != NULL) ||
            (strstr(node_err, "known transaction") != NULL) ||
            (strstr(node_err, "ALREADY_EXISTS") != NULL));
}

eth_rpc_receipt_result_t eth_rpc_get_tx_receipt(const eth_receipt_expect_t *want)
{
    if ((want == NULL) || (want->tx_hash == NULL)) { return ETH_RPC_RECEIPT_RPC_ERROR; }
    char body[160];
    (void)snprintf(body, sizeof(body),
                   "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getTransactionReceipt\","
                   "\"params\":[\"%s\"],\"id\":3}",
                   want->tx_hash);

    /* Receipts are large (the logsBloom field alone is 512 hex chars, plus
     * the ERC-20 Transfer log) — use a dedicated heap buffer, a truncated
     * body would fail the JSON parse. */
    const size_t resp_size = 4096U;
    char *resp = static_cast<char *>(malloc(resp_size));
    if (resp == NULL) { return ETH_RPC_RECEIPT_RPC_ERROR; }

    eth_rpc_receipt_result_t verdict = ETH_RPC_RECEIPT_RPC_ERROR;
    if (do_post(body, resp, resp_size)) {
        switch (eth_json_receipt_check(resp, want)) {
            case ETH_JSON_RECEIPT_PENDING:  verdict = ETH_RPC_RECEIPT_PENDING;  break;
            case ETH_JSON_RECEIPT_SUCCESS:  verdict = ETH_RPC_RECEIPT_SUCCESS;  break;
            case ETH_JSON_RECEIPT_REVERTED: verdict = ETH_RPC_RECEIPT_REVERTED; break;
            case ETH_JSON_RECEIPT_MISMATCH:
                ESP_LOGE(TAG, "receipt is not our payment: %.*s", RESP_LOG_MAX, resp);
                verdict = ETH_RPC_RECEIPT_MISMATCH;
                break;
            case ETH_JSON_RECEIPT_ERROR:    /* fall through */
            default:                        verdict = ETH_RPC_RECEIPT_RPC_ERROR; break;
        }
    }
    free(resp);
    return verdict;
}
