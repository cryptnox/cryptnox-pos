/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * fuzz_eth_rpc_json.cpp — libFuzzer harness for the JSON-RPC response parser.
 *
 * Targets (both fed the same fuzz input — they parse the same network bodies):
 *   eth_json_result_string()  extracts the "result" string (nonce, tx hash)
 *   eth_json_receipt_status() classifies an eth_getTransactionReceipt body
 *
 * These are the parsers fed straight off the network, so they are the
 * highest-value targets here.
 *
 * The parser lives in main/eth_json.cpp, a pure unit (cJSON + CW_Utils only),
 * so it is #included directly — no copy, no risk of drift from the firmware.
 *
 * Dependencies pulled in by the CMake target:
 *   - cJSON  (from $IDF_PATH/components/json/cJSON)
 *   - CW_Utils::safe_memcpy  (cryptnox-sdk-esp32/cryptnox-sdk-cpp/CW_Utils.cpp)
 *
 * Build (Linux / macOS, or WSL on Windows — clang + IDF_PATH required):
 *   cd fuzz && mkdir build && cd build
 *   cmake .. -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
 *   make fuzz_eth_rpc_json
 *
 * Run:
 *   ./fuzz_eth_rpc_json ../corpus/eth_rpc_json -max_len=4096 -jobs=4
 *
 * Input: raw bytes, treated as the JSON-RPC response body (NUL-terminated by
 * the harness — cJSON_Parse expects a C string).
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "CW_Utils.h"

/* CW_Utils::fill_secure_random is ESP32-specific (esp32_random.cpp) and is
 * never reached from safe_memcpy; stub it for the linker, like the SDK's own
 * fuzz harness does. */
bool CW_Utils::fill_secure_random(uint8_t *dest, size_t len)
{
    if ((dest != NULL) && (len > 0U)) {
        memset(dest, 0xA5U, len);
    }
    return true;
}

/* Real safe_memcpy + the production parser, compiled into the harness. */
#include "CW_Utils.cpp"
#include "../main/eth_json.cpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* cJSON_Parse needs a NUL-terminated string; copy onto the heap so ASan
     * catches any read past the terminator inside the parser. */
    char *json = static_cast<char *>(malloc(size + 1U));
    if (json == NULL) {
        return 0;
    }
    memcpy(json, data, size);
    json[size] = '\0';

    /* Mirror the production call site: a 128-byte result buffer (the largest
     * field eth_rpc reads is a 66-char "0x..." tx hash). */
    char result[128];
    (void)eth_json_result_string(json, result, sizeof result);

    /* Same body through the receipt classifier — the other network JSON path. */
    (void)eth_json_receipt_status(json);

    /* And through the error extractor, which is reached by exactly the bodies
     * result_string rejects — a node's refusal. It truncates rather than
     * refusing an oversized value, so it is the one of the three doing its own
     * length arithmetic, on attacker-supplied prose. Deliberately a smaller
     * buffer than result[]: the panel's is 64. */
    char err[64];
    (void)eth_json_error_message(json, err, sizeof err);

    free(json);
    return 0;
}
