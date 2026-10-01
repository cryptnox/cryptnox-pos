/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file tron_rpc.cpp
 * @brief Tron HTTP API client implementation.
 */

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "tron_rpc.h"
#include "tron_tx.h"
#include "eth_json.h"    /* eth_json_hex_quantity — one saturating hex parser */
#include "https_post.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <inttypes.h>

/* CW_Utils.h pulls in Arduino.h; keep it ahead of any lwip-including header
 * (see the note in https_post.cpp). */
#include "CW_Utils.h"

#include "esp_log.h"
#include "mbedtls/sha256.h"
#include <sys/time.h>   /* gettimeofday — raw_data expiration bound */
#include "cJSON.h"

static const char *const TAG = "tron_rpc";

/* Responses echo the whole transaction, raw_data expanded as JSON; a TRX
 * transfer needs ~1 KB and a TriggerSmartContract roughly twice that. */
#define RESP_BUF_SIZE   3072U
#define RESP_LOG_MAX    120

/* raw_data is at most TRON_RAW_HEX_MAX/2 bytes. */
#define RAW_BYTES_MAX   (TRON_RAW_HEX_MAX / 2U)

static const char *s_base_url = NULL;
static const char *s_ca_cert  = NULL;   /* pinned cert; NULL = CA bundle */

/******************************************************************
 * 2. Local helpers
 ******************************************************************/

static char nibble_hex(unsigned n)
{
    return (n < 10U) ? static_cast<char>('0' + n)
                     : static_cast<char>('a' + n - 10U);
}

static int nibble_val(char c)
{
    if ((c >= '0') && (c <= '9')) { return c - '0'; }
    if ((c >= 'a') && (c <= 'f')) { return (c - 'a') + 10; }
    if ((c >= 'A') && (c <= 'F')) { return (c - 'A') + 10; }
    return -1;
}

/**
 * @brief Decode a hex string into bytes.
 *
 * @param[in]  hex      NUL-terminated hex, even length.
 * @param[out] out      Destination bytes.
 * @param[in]  out_size Capacity of @p out.
 * @return number of bytes written, 0 on odd length, non-hex input or overflow.
 */
static size_t hex_to_bytes(const char *hex, uint8_t *out, size_t out_size)
{
    const size_t len = strlen(hex);
    if ((len == 0U) || ((len % 2U) != 0U) || ((len / 2U) > out_size)) {
        return 0U;
    }
    for (size_t i = 0U; i < (len / 2U); i++) {
        int hi = nibble_val(hex[i * 2U]);
        int lo = nibble_val(hex[i * 2U + 1U]);
        if ((hi < 0) || (lo < 0)) { return 0U; }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return len / 2U;
}

/** @brief Hex-encode @p len bytes into @p out (NUL-terminated). */
static void bytes_to_hex(const uint8_t *data, size_t len, char *out)
{
    for (size_t i = 0U; i < len; i++) {
        out[i * 2U]      = nibble_hex((data[i] >> 4U) & 0x0FU);
        out[i * 2U + 1U] = nibble_hex(data[i] & 0x0FU);
    }
    out[len * 2U] = '\0';
}

/** @brief POST @p body to @p path under the configured base URL. */
static bool tron_post(const char *path, const char *body,
                      char *resp, size_t resp_size)
{
    if (s_base_url == NULL) {
        ESP_LOGE(TAG, "tron_rpc_init not called");
        return false;
    }
    char url[128];
    int k = snprintf(url, sizeof(url), "%s%s", s_base_url, path);
    if ((k <= 0) || (static_cast<size_t>(k) >= sizeof(url))) { return false; }

    /* No auth — TronGrid's public endpoint needs none for these calls. The cert
     * is pinned only if the integrator supplied one; unpinned falls back to the
     * CA bundle, and tron_tx_contract_ok is what makes that survivable. */
    return https_post_json(url, body, resp, resp_size, NULL, NULL, s_ca_cert);
}

/** @brief Unix time in ms, 0 if the clock is unset (the checks then refuse). */
static uint64_t now_ms(void)
{
    struct timeval tv;
    if ((gettimeofday(&tv, NULL) != 0) || (tv.tv_sec < 1600000000)) { return 0U; }
    return (static_cast<uint64_t>(tv.tv_sec) * 1000U) +
           (static_cast<uint64_t>(tv.tv_usec) / 1000U);
}

/**
 * @brief Take a node-built transaction object apart into a @ref tron_tx_ctx_t.
 *
 * Shared by both create paths. Proves txID == sha256(raw_data): without it the
 * node could hand us the hash of an unrelated transaction and get it
 * blind-signed by the card, and no amount of checking raw_data would notice,
 * since the signature covers the hash and not the bytes.
 *
 * The caller still has to check that raw_data pays what it asked for — that
 * check differs per contract type, and it runs on @c out->raw_hex once this
 * has established that raw_data is the thing being signed.
 *
 * @param[in]  txobj cJSON object carrying @c txID and @c raw_data_hex.
 * @param[out] out   Filled on success.
 * @return true if the object is well-formed and internally consistent.
 */
static bool tx_ctx_from_json(const cJSON *txobj, tron_tx_ctx_t *out)
{
    const cJSON *txid = cJSON_GetObjectItemCaseSensitive(txobj, "txID");
    const cJSON *raw  = cJSON_GetObjectItemCaseSensitive(txobj, "raw_data_hex");
    if (!cJSON_IsString(txid) || (txid->valuestring == NULL) ||
        !cJSON_IsString(raw)  || (raw->valuestring == NULL)) {
        return false;
    }
    if (strlen(txid->valuestring) != 64U) {
        ESP_LOGE(TAG, "create: txID length %u",
                 (unsigned)strlen(txid->valuestring));
        return false;
    }
    if (strlen(raw->valuestring) >= TRON_RAW_HEX_MAX) {
        ESP_LOGE(TAG, "create: raw_data too long (%u)",
                 (unsigned)strlen(raw->valuestring));
        return false;
    }

    uint8_t raw_bytes[RAW_BYTES_MAX];
    size_t  raw_len = hex_to_bytes(raw->valuestring, raw_bytes,
                                   sizeof(raw_bytes));
    if (raw_len == 0U) {
        ESP_LOGE(TAG, "create: raw_data not hex");
        return false;
    }

    uint8_t digest[32];
    if (mbedtls_sha256(raw_bytes, raw_len, digest, 0) != 0) {
        ESP_LOGE(TAG, "create: sha256 failed");
        return false;
    }
    char digest_hex[65];
    bytes_to_hex(digest, sizeof(digest), digest_hex);
    if (strcasecmp(digest_hex, txid->valuestring) != 0) {
        ESP_LOGE(TAG, "create: txID is not sha256(raw_data) - refusing");
        return false;
    }

    (void)memcpy(out->txid, digest, sizeof(out->txid));
    (void)snprintf(out->txid_hex, sizeof(out->txid_hex), "%s", digest_hex);
    (void)snprintf(out->raw_hex, sizeof(out->raw_hex), "%s", raw->valuestring);
    return true;
}

/******************************************************************
 * 3. Public API
 ******************************************************************/

void tron_rpc_init(const char *base_url)
{
    s_base_url = base_url;
}

void tron_rpc_set_ca_cert(const char *ca_pem)
{
    s_ca_cert = ca_pem;
}

/**
 * @brief A cJSON number as a uint64, clamped at both ends.
 *
 * Tron reports sun and energy as JSON integers and cJSON hands them back as
 * doubles, so past 2^53 the low digits are lost. For a balance that is a
 * rounding error in the last drop of TRX; it is never the difference between
 * funded and empty, which is the only question asked of these numbers.
 *
 * A missing or non-numeric field reads as zero — for an account the chain has
 * never seen, that is the answer rather than an error.
 */
static uint64_t json_u64(const cJSON *n)
{
    if (!cJSON_IsNumber(n) || (n->valuedouble <= 0.0)) { return 0U; }
    if (n->valuedouble >= 18446744073709551615.0) { return UINT64_MAX; }
    return (uint64_t)n->valuedouble;
}

bool tron_rpc_get_balance(const char *owner_hex, uint64_t *sun_out)
{
    if ((owner_hex == NULL) || (sun_out == NULL)) { return false; }

    char body[96];
    (void)snprintf(body, sizeof(body),
                   "{\"address\":\"%s\",\"visible\":false}", owner_hex);

    char resp[RESP_BUF_SIZE];
    if (!tron_post("/wallet/getaccount", body, resp, sizeof(resp))) {
        return false;
    }

    cJSON *root = cJSON_Parse(resp);
    if (root == NULL) {
        ESP_LOGE(TAG, "getaccount: not JSON: %.*s", RESP_LOG_MAX, resp);
        return false;
    }
    *sun_out = json_u64(cJSON_GetObjectItemCaseSensitive(root, "balance"));
    cJSON_Delete(root);
    return true;
}

bool tron_rpc_get_energy(const char *owner_hex, uint64_t *energy_out)
{
    if ((owner_hex == NULL) || (energy_out == NULL)) { return false; }

    char body[96];
    (void)snprintf(body, sizeof(body),
                   "{\"address\":\"%s\",\"visible\":false}", owner_hex);

    char resp[RESP_BUF_SIZE];
    if (!tron_post("/wallet/getaccountresource", body, resp, sizeof(resp))) {
        return false;
    }

    cJSON *root = cJSON_Parse(resp);
    if (root == NULL) {
        ESP_LOGE(TAG, "getaccountresource: not JSON: %.*s", RESP_LOG_MAX, resp);
        return false;
    }
    const uint64_t limit =
        json_u64(cJSON_GetObjectItemCaseSensitive(root, "EnergyLimit"));
    const uint64_t used =
        json_u64(cJSON_GetObjectItemCaseSensitive(root, "EnergyUsed"));
    cJSON_Delete(root);

    *energy_out = (limit > used) ? (limit - used) : 0U;
    return true;
}

bool tron_rpc_get_trc20_balance(const char *owner_hex, const char *contract_hex,
                                uint64_t *units_out)
{
    if ((owner_hex == NULL) || (contract_hex == NULL) || (units_out == NULL)) {
        return false;
    }
    /* The ABI argument is the 20-byte address left-padded to 32, so Tron's "41"
     * prefix comes off first. Leaving it on would shift the padding by a byte
     * and ask the contract about a different account — which it would answer,
     * and the answer would be about somebody else. */
    if (strlen(owner_hex) != TRON_ADDR_HEX_LEN) {
        ESP_LOGE(TAG, "balanceOf: owner is not %u hex chars",
                 (unsigned)TRON_ADDR_HEX_LEN);
        return false;
    }
    const char *owner20 = owner_hex + 2;

    char body[320];
    (void)snprintf(body, sizeof(body),
                   "{\"owner_address\":\"%s\",\"contract_address\":\"%s\","
                   "\"function_selector\":\"balanceOf(address)\","
                   "\"parameter\":\"000000000000000000000000%s\","
                   "\"visible\":false}",
                   owner_hex, contract_hex, owner20);

    char resp[RESP_BUF_SIZE];
    if (!tron_post("/wallet/triggerconstantcontract", body,
                   resp, sizeof(resp))) {
        return false;
    }

    cJSON *root = cJSON_Parse(resp);
    if (root == NULL) {
        ESP_LOGE(TAG, "balanceOf: not JSON: %.*s", RESP_LOG_MAX, resp);
        return false;
    }
    const cJSON *arr =
        cJSON_GetObjectItemCaseSensitive(root, "constant_result");
    const cJSON *first = cJSON_IsArray(arr) ? cJSON_GetArrayItem(arr, 0) : NULL;

    bool ok = false;
    if (cJSON_IsString(first) && (first->valuestring != NULL)) {
        const size_t n = strlen(first->valuestring);
        /* A uint256 comes back as 64 characters. Anything longer is not a
         * number this can read — and must not be truncated into one, because a
         * padded value truncated from the right loses its significant digits
         * and would report a funded account as nearly empty. */
        if ((n > 0U) && (n <= 64U)) {
            char hex[80];
            (void)snprintf(hex, sizeof(hex), "0x%s", first->valuestring);
            ok = eth_json_hex_quantity(hex, units_out);
        }
    }
    if (!ok) {
        ESP_LOGE(TAG, "balanceOf: no usable constant_result: %.*s",
                 RESP_LOG_MAX, resp);
    }
    cJSON_Delete(root);
    return ok;
}

bool tron_rpc_get_trc20_decimals(const char *contract_hex, uint64_t *dec_out)
{
    if ((contract_hex == NULL) || (dec_out == NULL) ||
        (strlen(contract_hex) != TRON_ADDR_HEX_LEN)) {
        return false;
    }
    /* A constant call needs some owner; the contract itself will do. */
    char body[256];
    int k = snprintf(body, sizeof(body),
                     "{\"owner_address\":\"%s\",\"contract_address\":\"%s\","
                     "\"function_selector\":\"decimals()\",\"visible\":false}",
                     contract_hex, contract_hex);
    if ((k <= 0) || (static_cast<size_t>(k) >= sizeof(body))) { return false; }

    char resp[RESP_BUF_SIZE];
    if (!tron_post("/wallet/triggerconstantcontract", body, resp, sizeof(resp))) {
        return false;
    }
    cJSON *root = cJSON_Parse(resp);
    if (root == NULL) { return false; }
    const cJSON *arr   = cJSON_GetObjectItemCaseSensitive(root, "constant_result");
    const cJSON *first = cJSON_IsArray(arr) ? cJSON_GetArrayItem(arr, 0) : NULL;
    bool ok = false;
    if (cJSON_IsString(first) && (first->valuestring != NULL) &&
        (strlen(first->valuestring) > 0U) && (strlen(first->valuestring) <= 64U)) {
        char hex[80];
        (void)snprintf(hex, sizeof(hex), "0x%s", first->valuestring);
        ok = eth_json_hex_quantity(hex, dec_out);
    }
    if (!ok) {
        ESP_LOGE(TAG, "decimals: no usable answer: %.*s", RESP_LOG_MAX, resp);
    }
    cJSON_Delete(root);
    return ok;
}

bool tron_rpc_create_transfer(const char *owner_hex, const char *to_hex,
                              uint64_t amount_sun, tron_tx_ctx_t *out)
{
    if ((owner_hex == NULL) || (to_hex == NULL) || (out == NULL) ||
        (amount_sun == 0U)) {
        return false;
    }

    char body[192];
    int k = snprintf(body, sizeof(body),
                     "{\"owner_address\":\"%s\",\"to_address\":\"%s\","
                     "\"amount\":%" PRIu64 "}",
                     owner_hex, to_hex, amount_sun);
    if ((k <= 0) || (static_cast<size_t>(k) >= sizeof(body))) { return false; }

    char resp[RESP_BUF_SIZE];
    if (!tron_post("/wallet/createtransaction", body, resp, sizeof(resp))) {
        return false;
    }

    cJSON *root = cJSON_Parse(resp);
    if (root == NULL) {
        ESP_LOGE(TAG, "create: not JSON: %.*s", RESP_LOG_MAX, resp);
        return false;
    }

    /* Tron reports bad parameters as {"Error":"..."} with HTTP 200. */
    bool ok = tx_ctx_from_json(root, out);
    if (!ok) {
        ESP_LOGE(TAG, "create: unusable answer: %.*s", RESP_LOG_MAX, resp);
    } else if (!tron_tx_contract_ok(out->raw_hex, owner_hex, to_hex,
                                    amount_sun, now_ms(),
                                    &out->expiration_ms)) {
        /* The node is a trust boundary — see tron_tx.h. */
        ESP_LOGE(TAG, "create: raw_data does not match the requested transfer");
        ok = false;
    } else {
        ESP_LOGI(TAG, "Tron tx %s (%" PRIu64 " sun)", out->txid_hex,
                 amount_sun);
    }

    cJSON_Delete(root);
    return ok;
}

bool tron_rpc_create_trc20_transfer(const char *owner_hex,
                                    const char *contract_hex,
                                    const char *to_hex, uint64_t amount,
                                    uint64_t fee_limit_sun,
                                    tron_tx_ctx_t *out)
{
    if ((owner_hex == NULL) || (contract_hex == NULL) || (to_hex == NULL) ||
        (out == NULL) || (amount == 0U) || (fee_limit_sun == 0U)) {
        return false;
    }

    /* Build the calldata arguments ourselves rather than let the node infer
     * them: these same bytes are what tron_tx_trc20_ok looks for afterwards. */
    char param[TRON_TRC20_PARAM_HEX_LEN + 1U];
    if (tron_trc20_param_hex(to_hex, amount, param, sizeof(param)) == 0U) {
        ESP_LOGE(TAG, "trc20: bad recipient");
        return false;
    }

    char body[512];
    int k = snprintf(body, sizeof(body),
                     "{\"owner_address\":\"%s\",\"contract_address\":\"%s\","
                     "\"function_selector\":\"transfer(address,uint256)\","
                     "\"parameter\":\"%s\",\"call_value\":0,"
                     "\"fee_limit\":%" PRIu64 "}",
                     owner_hex, contract_hex, param, fee_limit_sun);
    if ((k <= 0) || (static_cast<size_t>(k) >= sizeof(body))) { return false; }

    char resp[RESP_BUF_SIZE];
    if (!tron_post("/wallet/triggersmartcontract", body, resp, sizeof(resp))) {
        return false;
    }

    cJSON *root = cJSON_Parse(resp);
    if (root == NULL) {
        ESP_LOGE(TAG, "trc20: not JSON: %.*s", RESP_LOG_MAX, resp);
        return false;
    }

    /* Unlike createtransaction, this one nests the transaction one level down;
     * a rejected call ({"result":{"code":"CONTRACT_VALIDATE_ERROR",...}})
     * simply has no "transaction" member. */
    const cJSON *txobj = cJSON_GetObjectItemCaseSensitive(root, "transaction");
    bool ok = cJSON_IsObject(txobj) && tx_ctx_from_json(txobj, out);
    if (!ok) {
        ESP_LOGE(TAG, "trc20: unusable answer: %.*s", RESP_LOG_MAX, resp);
    } else if (!tron_tx_trc20_ok(out->raw_hex, owner_hex, contract_hex, to_hex,
                                 amount, fee_limit_sun, now_ms(),
                                 &out->expiration_ms)) {
        ESP_LOGE(TAG, "trc20: raw_data does not match the requested transfer");
        ok = false;
    } else {
        ESP_LOGI(TAG, "Tron TRC-20 tx %s (%" PRIu64 " units of %s)",
                 out->txid_hex, amount, contract_hex);
    }

    cJSON_Delete(root);
    return ok;
}

bool tron_rpc_broadcast(const tron_tx_ctx_t *tx, const uint8_t sig[65])
{
    if ((tx == NULL) || (sig == NULL)) { return false; }

    char sig_hex[TRON_SIG_HEX_LEN + 1U];
    bytes_to_hex(sig, 65U, sig_hex);

    /* "0a" + varint len + raw_data + "1241" + signature */
    char envelope[TRON_RAW_HEX_MAX + TRON_SIG_HEX_LEN + 16U];
    if (tron_tx_envelope_hex(tx->raw_hex, sig_hex,
                             envelope, sizeof(envelope)) == 0U) {
        ESP_LOGE(TAG, "broadcast: envelope build failed");
        return false;
    }

    char body[sizeof(envelope) + 32U];
    int k = snprintf(body, sizeof(body), "{\"transaction\":\"%s\"}", envelope);
    if ((k <= 0) || (static_cast<size_t>(k) >= sizeof(body))) { return false; }

    char resp[RESP_BUF_SIZE];
    if (!tron_post("/wallet/broadcasthex", body, resp, sizeof(resp))) {
        return false;
    }

    bool   ok   = false;
    cJSON *root = cJSON_Parse(resp);
    if (root == NULL) {
        ESP_LOGE(TAG, "broadcast: not JSON: %.*s", RESP_LOG_MAX, resp);
        return false;
    }
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    ok = cJSON_IsTrue(result);
    if (!ok) {
        /* code/message carry the reason (SIGERROR, BANDWIDTH_ERROR, ...). */
        ESP_LOGE(TAG, "broadcast rejected: %.*s", RESP_LOG_MAX, resp);
    }
    cJSON_Delete(root);
    return ok;
}

tron_receipt_t tron_rpc_get_receipt(const char *txid_hex)
{
    if (txid_hex == NULL) { return TRON_RECEIPT_RPC_ERROR; }

    char body[96];
    int k = snprintf(body, sizeof(body), "{\"value\":\"%s\"}", txid_hex);
    if ((k <= 0) || (static_cast<size_t>(k) >= sizeof(body))) {
        return TRON_RECEIPT_RPC_ERROR;
    }

    char resp[RESP_BUF_SIZE];
    if (!tron_post("/wallet/gettransactioninfobyid", body,
                   resp, sizeof(resp))) {
        return TRON_RECEIPT_RPC_ERROR;
    }

    cJSON *root = cJSON_Parse(resp);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return TRON_RECEIPT_RPC_ERROR;
    }

    /* Not in a block yet: the node answers with an empty object. */
    tron_receipt_t verdict = TRON_RECEIPT_PENDING;
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    const cJSON *block  = cJSON_GetObjectItemCaseSensitive(root, "blockNumber");
    /* A TRC-20 call that ran out of energy or reverted is mined but did not
     * move any tokens; receipt.result carries that verdict (OUT_OF_ENERGY,
     * REVERT, ...) and only "SUCCESS" means the operator got paid. */
    const cJSON *rcpt   = cJSON_GetObjectItemCaseSensitive(root, "receipt");
    const cJSON *rres   = cJSON_IsObject(rcpt)
                          ? cJSON_GetObjectItemCaseSensitive(rcpt, "result")
                          : NULL;
    if (cJSON_IsString(result) && (result->valuestring != NULL) &&
        (strcmp(result->valuestring, "FAILED") == 0)) {
        verdict = TRON_RECEIPT_FAILED;
    } else if (cJSON_IsString(rres) && (rres->valuestring != NULL) &&
               (strcmp(rres->valuestring, "SUCCESS") != 0)) {
        ESP_LOGE(TAG, "receipt: contract result %s", rres->valuestring);
        verdict = TRON_RECEIPT_FAILED;
    } else if (cJSON_IsNumber(block)) {
        verdict = TRON_RECEIPT_SUCCESS;
    }
    cJSON_Delete(root);
    return verdict;
}
