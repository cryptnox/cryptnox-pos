/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * test_eth_receipt.cpp — host unit test for eth_json_receipt_check: a receipt
 * only counts as a payment when it is OUR transaction (hash computed on the
 * device), sent to the expected contract, carrying a Transfer log to the payee
 * for exactly the amount. A status of 0x1 alone is not enough — a node that
 * never broadcast our transaction could answer with any successful one.
 *
 * Needs cJSON, which ships with ESP-IDF. From the repo root:
 *
 *   g++ -std=c++14 -Imain -Icryptnox-sdk-esp32/cryptnox-sdk-cpp \
 *       -I$IDF_PATH/components/json/cJSON -x c $IDF_PATH/components/json/cJSON/cJSON.c \
 *       -x c++ tests/units/test_eth_receipt.cpp -o test_eth_receipt && ./test_eth_receipt
 */

#include <assert.h>
#include <stdio.h>
#include <string>

#include "eth_json.cpp"
#include "CW_Utils.cpp"

#define HASH  "0x5c504ed432cb51138bcf09aa5e8a410dd4a1e204ef84bfed1be16dfba1b22060"
#define USDC  "0x1c7d4b196cb0c7b01d743fbc6116a902379c7238"
#define PAYEE "000000000000000000000000cadddf4677544d0eb25e4f87cd978aa5de23ebc6"
#define FROM  "000000000000000000000000d4a5f19c1b9e0e2a1d5f0b3c4d5e6f708192a3b4"
#define TOPIC "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef"
#define AMT   "0x000000000000000000000000000000000000000000000000000000000016e360"

static const uint8_t USDC_B[20] = {
    0x1c, 0x7d, 0x4b, 0x19, 0x6c, 0xb0, 0xc7, 0xb0, 0x1d, 0x74,
    0x3f, 0xbc, 0x61, 0x16, 0xa9, 0x02, 0x37, 0x9c, 0x72, 0x38 };
static const uint8_t PAYEE_B[20] = {
    0xca, 0xdd, 0xdf, 0x46, 0x77, 0x54, 0x4d, 0x0e, 0xb2, 0x5e,
    0x4f, 0x87, 0xcd, 0x97, 0x8a, 0xa5, 0xde, 0x23, 0xeb, 0xc6 };

static std::string receipt(const char *hash, const char *to, const char *status,
                           const std::string &logs)
{
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{"
                       "\"transactionHash\":\"") + hash + "\",\"to\":\"" + to +
           "\",\"status\":\"" + status + "\",\"logs\":[" + logs + "]}}";
}

static std::string log(const char *addr, const char *payee, const char *amt)
{
    return std::string("{\"address\":\"") + addr +
           "\",\"topics\":[\"" TOPIC "\",\"0x" FROM "\",\"0x" + payee +
           "\"],\"data\":\"" + amt + "\"}";
}

static eth_json_receipt_t check(const std::string &r, uint64_t amount = 1500000U)
{
    const eth_receipt_expect_t want = { HASH, USDC_B, PAYEE_B, amount };
    return eth_json_receipt_check(r.c_str(), &want);
}

int main(void)
{
    const std::string good_log = log(USDC, PAYEE, AMT);

    /* The payment asked for. Hex case is not significant anywhere. */
    assert(check(receipt(HASH, USDC, "0x1", good_log)) == ETH_JSON_RECEIPT_SUCCESS);
    assert(check(receipt(HASH, "0x1C7D4B196CB0C7B01D743FBC6116A902379C7238", "0x1",
                         log("0x1C7D4B196CB0C7B01D743FBC6116A902379C7238", PAYEE, AMT)))
           == ETH_JSON_RECEIPT_SUCCESS);
    /* Other logs beside ours (an approval, another token) do not matter. */
    assert(check(receipt(HASH, USDC, "0x1",
                         log("0x0000000000000000000000000000000000000001", PAYEE, AMT)
                         + "," + good_log)) == ETH_JSON_RECEIPT_SUCCESS);

    /* Somebody else's successful transaction. */
    assert(check(receipt("0x" "1111111111111111111111111111111111111111111111111111111111111111",
                         USDC, "0x1", good_log)) == ETH_JSON_RECEIPT_MISMATCH);
    /* Right hash, but it called another contract. */
    assert(check(receipt(HASH, "0x0000000000000000000000000000000000000001", "0x1",
                         good_log)) == ETH_JSON_RECEIPT_MISMATCH);
    /* No Transfer log, a Transfer to someone else, of another amount, or
     * emitted by another contract. */
    assert(check(receipt(HASH, USDC, "0x1", "")) == ETH_JSON_RECEIPT_MISMATCH);
    assert(check(receipt(HASH, USDC, "0x1",
                         log(USDC, "000000000000000000000000deadbeefdeadbeefdeadbeefdeadbeefdeadbeef", AMT)))
           == ETH_JSON_RECEIPT_MISMATCH);
    assert(check(receipt(HASH, USDC, "0x1", good_log), 1500001U) == ETH_JSON_RECEIPT_MISMATCH);
    assert(check(receipt(HASH, USDC, "0x1",
                         log("0x0000000000000000000000000000000000000001", PAYEE, AMT)))
           == ETH_JSON_RECEIPT_MISMATCH);

    /* Reverted is believed only for our own transaction. */
    assert(check(receipt(HASH, USDC, "0x0", "")) == ETH_JSON_RECEIPT_REVERTED);
    assert(check(receipt("0x" "2222222222222222222222222222222222222222222222222222222222222222",
                         USDC, "0x0", "")) == ETH_JSON_RECEIPT_MISMATCH);

    /* Pending and junk pass straight through. */
    assert(check("{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":null}") == ETH_JSON_RECEIPT_PENDING);
    assert(check("not json") == ETH_JSON_RECEIPT_ERROR);
    assert(eth_json_receipt_check("{\"result\":null}", NULL) == ETH_JSON_RECEIPT_ERROR);

    /* A native transfer has no log: hash and recipient are the whole check. */
    const eth_receipt_expect_t native = { HASH, PAYEE_B, NULL, 0U };
    assert(eth_json_receipt_check(receipt(HASH, "0xcadddf4677544d0eb25e4f87cd978aa5de23ebc6",
                                          "0x1", "").c_str(), &native)
           == ETH_JSON_RECEIPT_SUCCESS);
    assert(eth_json_receipt_check(receipt(HASH, USDC, "0x1", "").c_str(), &native)
           == ETH_JSON_RECEIPT_MISMATCH);

    printf("test_eth_receipt: all OK\n");
    return 0;
}
