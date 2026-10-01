/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file eth_addr.h
 * @ingroup eth
 * @brief Hex Ethereum-address parsing — a pure, dependency-free unit so it can
 *        be unit-tested and fuzzed on the host (see fuzz/fuzz_parse_address.cpp).
 */

#ifndef ETH_ADDR_H
#define ETH_ADDR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>   /* eth_addr_parse returns bool — keep this header self-contained */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Length of a raw (binary) Ethereum address, in bytes. */
#define ETH_ADDR_LEN     20U
/**
 * @brief Length of the same address in hex characters (no @c 0x prefix).
 *
 * Two hex chars per byte. The multiply is performed in @c size_t because every
 * use site is a @c size_t context (@c strlen result, array bound, loop bound):
 * multiplying in @c unsigned @c int and widening at the use site trips
 * bugprone-implicit-widening-of-multiplication-result on a 64-bit host.
 */
#define ETH_ADDR_HEX_LEN ((size_t)ETH_ADDR_LEN * 2U)

/**
 * @brief Parse a 20-byte Ethereum address from a hex string.
 *
 * Accepts an optional @c 0x / @c 0X prefix, then requires exactly 40 hex
 * characters and rejects any non-hex input instead of silently decoding
 * garbage. A mixed-case input is treated as EIP-55 checksummed and its
 * checksum is verified (keccak256 recomputed, rejected on case mismatch); an
 * all-lower/all-upper input is accepted as un-checksummed.
 *
 * @param[in]  hex Address string (with or without @c 0x prefix).
 * @param[out] out 20-byte decoded address; zeroed then left partially written
 *                 on failure — must not be used unless true is returned.
 * @return true on success, false on wrong length, non-hex character, the
 *         zero address, or a failed EIP-55 checksum.
 */
bool eth_addr_parse(const char *hex, uint8_t out[ETH_ADDR_LEN]);

/**
 * @brief Render a 20-byte address as an EIP-55 mixed-case "0x..." string.
 *
 * The inverse of @ref eth_addr_parse, and it produces the checksummed form on
 * purpose: an address derived on the device (from a card's public key, say) is then
 * displayed exactly as the operator's own wallet displays it, which is the whole
 * point of showing it to them. It also means the value that goes into storage
 * carries a checksum, so the boot-time parse is a real check on it rather than the
 * no-op it is for an all-lowercase string.
 *
 * @param[in]  addr 20-byte address.
 * @param[out] out  Buffer for "0x" + 40 hex + NUL.
 * @param[in]  n    Capacity of @p out; needs at least 43 bytes.
 * @return true on success, false if @p out is too small (and then left empty).
 */
bool eth_addr_format(const uint8_t addr[ETH_ADDR_LEN], char *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* ETH_ADDR_H */
