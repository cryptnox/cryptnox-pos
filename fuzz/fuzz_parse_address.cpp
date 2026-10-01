/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * fuzz_parse_address.cpp — libFuzzer harness for the hex address parser.
 *
 * Target: eth_addr_parse() — decodes a "0x"-optional 40-char hex string into a
 * 20-byte Ethereum address (address validation before it is baked into the
 * USDC transfer calldata).
 *
 * The parser lives in main/eth_addr.cpp. It now verifies the EIP-55 checksum
 * via keccak256, whose squeeze routes through CW_Utils::safe_memcpy — so the
 * harness also #includes keccak256.cpp and the real CW_Utils (SDK include
 * path), the same single-TU pattern fuzz_eth_rlp uses. No copy, no drift.
 *
 * Build (Linux / macOS, or WSL on Windows — clang required):
 *   cd fuzz && mkdir build && cd build
 *   cmake .. -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
 *   make fuzz_parse_address
 *
 * Run:
 *   ./fuzz_parse_address ../corpus/parse_address -max_len=64 -jobs=4
 *
 * Input: raw bytes, treated as the candidate address string (NUL-terminated
 * by the harness before the call).
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* CW_Utils::fill_secure_random is ESP32-specific and never reached from
 * safe_memcpy; stub it for the linker, like the SDK's own fuzz harness. */
#include "CW_Utils.h"
bool CW_Utils::fill_secure_random(uint8_t *dest, size_t len)
{
    (void)dest;
    (void)len;
    return false;
}

/* Real safe_memcpy, keccak256, and the production parser, all compiled
 * straight into this harness. */
#include "CW_Utils.cpp"
#include "../main/keccak256.cpp"
#include "../main/eth_addr.cpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* eth_addr_parse() reads a C string. Cap at a generous bound and
     * NUL-terminate; a fully zeroed buffer keeps p[0]/p[1] in bounds even for
     * empty input. */
    char buf[256];
    memset(buf, 0, sizeof buf);
    size_t n = (size < (sizeof(buf) - 1U)) ? size : (sizeof(buf) - 1U);
    memcpy(buf, data, n);

    uint8_t out[20];
    (void)eth_addr_parse(buf, out);
    return 0;
}
