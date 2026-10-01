/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * fuzz_eth_rlp.cpp — libFuzzer harness for the EIP-1559 RLP encoder.
 *
 * Target:
 *   eth_rlp_encode_unsigned()   0x02 || RLP([chainId, nonce, ...])
 *   eth_rlp_encode_signed()     0x02 || RLP([..., v, r, s])
 *
 * Why fuzz it: in production the calldata is a fixed 68-byte USDC transfer,
 * but the length-prefixing and the internal scratch bound are
 * exactly the kind of arithmetic that breaks under an attacker-chosen calldata
 * length — so we drive the full eth_tx_t from fuzz input and let ASan watch
 * the buffer math. eth_rlp.cpp routes every copy through CW_Utils::safe_memcpy
 * (CODING_RULES 1.4), so the harness links the real CW_Utils (needs the SDK
 * include path) the same way fuzz_eth_rpc_json does.
 *
 * Build (Linux / macOS, or WSL on Windows — clang required):
 *   cd fuzz && mkdir build && cd build
 *   cmake .. -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
 *   make fuzz_eth_rlp
 *
 * Run:
 *   ./fuzz_eth_rlp ../corpus/eth_rlp -max_len=4096 -jobs=4
 *
 * Input layout (bytes are consumed left to right; the harness tolerates a
 * short input by leaving the remaining fields zero):
 *   [0..47]   six big-endian u64 scalars: chain_id, nonce, max_priority_fee,
 *             max_fee, gas_limit, eth_value
 *   [48..67]  20-byte recipient address
 *   [68]      signature parity bit (low bit used)
 *   [69..]    remainder → calldata (pointer + length handed to the encoder)
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "CW_Utils.h"

/* CW_Utils::fill_secure_random is ESP32-specific and never reached from
 * safe_memcpy; stub it for the linker, like the SDK's own fuzz harness. */
bool CW_Utils::fill_secure_random(uint8_t *dest, size_t len)
{
    if ((dest != NULL) && (len > 0U)) {
        memset(dest, 0xA5U, len);
    }
    return true;
}

/* Real safe_memcpy + the production encoder, compiled into the harness. */
#include "CW_Utils.cpp"
#include "../main/eth_rlp.cpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const uint8_t *p   = data;
    size_t         rem = size;

    /* Consume up to 8 bytes into a big-endian u64; stops early at EOF. */
    auto take_u64 = [&](uint64_t &dst) {
        uint64_t v = 0U;
        for (int i = 0; (i < 8) && (rem > 0U); i++) {
            v = (v << 8) | static_cast<uint64_t>(*p++);
            rem--;
        }
        dst = v;
    };

    eth_tx_t tx;
    memset(&tx, 0, sizeof tx);

    take_u64(tx.chain_id);
    take_u64(tx.nonce);
    take_u64(tx.max_priority_fee);
    take_u64(tx.max_fee);
    take_u64(tx.gas_limit);
    take_u64(tx.eth_value);

    for (size_t i = 0U; (i < 20U) && (rem > 0U); i++) {
        tx.to[i] = *p++;
        rem--;
    }

    uint8_t v = 0U;
    if (rem > 0U) {
        v = static_cast<uint8_t>(*p++ & 0x01U);
        rem--;
    }

    /* Whatever is left becomes the calldata — the only attacker-influenced
     * length, which is what stresses the internal scratch bound. */
    tx.calldata     = (rem > 0U) ? p : NULL;
    tx.calldata_len = rem;

    uint8_t out[4096];

    (void)eth_rlp_encode_unsigned(&tx, out, sizeof out);

    /* r/s derived from the input so they vary across cases (their content is
     * opaque to the encoder — only their fixed 32-byte length matters). */
    uint8_t r[32];
    uint8_t s[32];
    for (size_t i = 0U; i < 32U; i++) {
        r[i] = static_cast<uint8_t>(i ^ (size & 0xFFU));
        s[i] = static_cast<uint8_t>((i * 7U) ^ ((size >> 3) & 0xFFU));
    }
    (void)eth_rlp_encode_signed(&tx, v, r, s, out, sizeof out);

    /* Also exercise the overflow path with a deliberately tiny output buffer
     * (must return 0, never write past the end). */
    uint8_t tiny[8];
    (void)eth_rlp_encode_unsigned(&tx, tiny, sizeof tiny);

    return 0;
}
