/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file eth_rlp.cpp
 * @brief RLP encoder implementation for EIP-1559 (type 2) transactions.
 */

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "eth_rlp.h"
#include "CW_Utils.h"   /* bounds-checked memory primitives (CODING_RULES 1.4) */

/******************************************************************
 * 2. Constants
 ******************************************************************/

/* Maximum scratch buffer for the list content before prepending the header. */
#define ITEMS_BUF_MAX  320U

/*
 * Worst-case bytes consumed in items[] by every non-calldata field:
 *   5x uint64 (<=9 each = 45) + to (21) + eth_value (<=9) + calldata
 *   header (<=9) + empty access list (1) + v (<=9) + r (<=33) + s (<=33) = 160.
 * Any calldata longer than what remains would overflow the stack buffer
 * before the out_max check ever runs, so it is rejected up front.
 */
#define ITEMS_FIXED_MAX   160U
#define CALLDATA_MAX      (ITEMS_BUF_MAX - ITEMS_FIXED_MAX)

/******************************************************************
 * 3. Internal helpers
 *
 * NOTE (CODING_RULES 1.4): every byte copy goes through
 * CW_Utils::safe_memcpy, which bounds-checks against the destination
 * capacity threaded in as out_cap. Helpers return the number of bytes
 * written, or 0 on a capacity failure (every successful encode writes at
 * least one byte, so 0 is an unambiguous error code that callers propagate).
 ******************************************************************/

/**
 * @brief Write @p value as a minimal big-endian byte sequence.
 *
 * Value 0 produces one 0x00 byte; the caller is responsible for treating
 * the empty-integer case (value==0 -> RLP 0x80) before calling this function.
 *
 * @param[in]  value Integer to encode.
 * @param[out] out   Output buffer (exactly 8 bytes; up to 8 used).
 * @return Number of bytes written (1-8).
 */
static uint8_t be_minimal(uint64_t value, uint8_t out[8])
{
    uint8_t tmp[8];
    uint8_t first = 0U;
    uint8_t i;

    tmp[0] = static_cast<uint8_t>((value >> 56U) & 0xFFU);
    tmp[1] = static_cast<uint8_t>((value >> 48U) & 0xFFU);
    tmp[2] = static_cast<uint8_t>((value >> 40U) & 0xFFU);
    tmp[3] = static_cast<uint8_t>((value >> 32U) & 0xFFU);
    tmp[4] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
    tmp[5] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    tmp[6] = static_cast<uint8_t>((value >>  8U) & 0xFFU);
    tmp[7] = static_cast<uint8_t>(value & 0xFFU);

    while ((first < 7U) && (tmp[first] == 0U)) {
        first++;
    }
    uint8_t count = static_cast<uint8_t>(8U - first);
    for (i = 0U; i < count; i++) {
        out[i] = tmp[static_cast<size_t>(first) + i];
    }
    return count;
}

/**
 * @brief RLP-encode a byte string.
 *
 * @param[in]  data     Input bytes.
 * @param[in]  data_len Number of input bytes.
 * @param[out] out      Output buffer.
 * @param[in]  out_cap  Capacity of @p out.
 * @return Bytes written, or 0 if @p out_cap is too small.
 */
static size_t rlp_bytes(const uint8_t *data, size_t data_len,
                        uint8_t *out, size_t out_cap)
{
    size_t written = 0U;

    if (data_len == 0U) {
        if (out_cap < 1U) { return 0U; }
        out[0] = 0x80U;
        written = 1U;
    } else if ((data_len == 1U) && (data[0] < 0x80U)) {
        if (out_cap < 1U) { return 0U; }
        out[0] = data[0];
        written = 1U;
    } else if (data_len <= 55U) {
        if (out_cap < 1U) { return 0U; }
        out[0] = static_cast<uint8_t>(0x80U + data_len);
        if (!CW_Utils::safe_memcpy(out + 1U, out_cap - 1U, data, data_len)) {
            return 0U;
        }
        written = 1U + data_len;
    } else {
        /* Long string: 0xb7 + len(length) bytes, then length, then data. */
        uint8_t len_bytes[8];
        uint8_t num_lb = be_minimal(static_cast<uint64_t>(data_len), len_bytes);
        if (out_cap < (static_cast<size_t>(num_lb) + 1U)) { return 0U; }
        out[0] = static_cast<uint8_t>(0xB7U + num_lb);
        if (!CW_Utils::safe_memcpy(out + 1U, out_cap - 1U, len_bytes, num_lb)) {
            return 0U;
        }
        if (!CW_Utils::safe_memcpy(out + 1U + num_lb,
                                   out_cap - 1U - num_lb, data, data_len)) {
            return 0U;
        }
        written = 1U + num_lb + data_len;
    }
    return written;
}

/**
 * @brief RLP-encode a non-negative integer.
 *
 * 0 is encoded as the empty byte string (0x80).
 *
 * @param[in]  value   Integer to encode.
 * @param[out] out     Output buffer.
 * @param[in]  out_cap Capacity of @p out.
 * @return Bytes written, or 0 if @p out_cap is too small.
 */
static size_t rlp_uint64(uint64_t value, uint8_t *out, size_t out_cap)
{
    if (value == 0U) {
        if (out_cap < 1U) { return 0U; }
        out[0] = 0x80U;
        return 1U;
    }
    uint8_t be[8];
    uint8_t be_len = be_minimal(value, be);
    return rlp_bytes(be, be_len, out, out_cap);
}

/**
 * @brief RLP-encode a 32-byte big-endian integer (e.g. ECDSA r or s).
 *
 * Leading zero bytes are stripped so the encoding is minimal.
 *
 * @param[in]  data    32-byte big-endian integer.
 * @param[out] out     Output buffer.
 * @param[in]  out_cap Capacity of @p out.
 * @return Bytes written, or 0 if @p out_cap is too small.
 */
static size_t rlp_int256(const uint8_t data[32], uint8_t *out, size_t out_cap)
{
    size_t first = 0U;
    while ((first < 31U) && (data[first] == 0U)) {
        first++;
    }
    if ((first == 31U) && (data[31] == 0U)) {
        /* Value is zero */
        if (out_cap < 1U) { return 0U; }
        out[0] = 0x80U;
        return 1U;
    }
    return rlp_bytes(data + first, 32U - first, out, out_cap);
}

/**
 * @brief Write the RLP list header for a list of @p content_len bytes.
 *
 * @param[in]  content_len Length of the list content (not written here).
 * @param[out] out         Output buffer.
 * @param[in]  out_cap     Capacity of @p out.
 * @return Bytes written (header only), or 0 if @p out_cap is too small.
 */
static size_t rlp_list_header(size_t content_len, uint8_t *out, size_t out_cap)
{
    if (content_len <= 55U) {
        if (out_cap < 1U) { return 0U; }
        out[0] = static_cast<uint8_t>(0xC0U + content_len);
        return 1U;
    }
    uint8_t len_bytes[8];
    uint8_t num_lb = be_minimal(static_cast<uint64_t>(content_len), len_bytes);
    if (out_cap < (static_cast<size_t>(num_lb) + 1U)) { return 0U; }
    out[0] = static_cast<uint8_t>(0xF7U + num_lb);
    if (!CW_Utils::safe_memcpy(out + 1U, out_cap - 1U, len_bytes, num_lb)) {
        return 0U;
    }
    return 1U + num_lb;
}

/******************************************************************
 * 4. Shared field encoder
 ******************************************************************/

/**
 * @brief Encode the nine common EIP-1559 fields into the items buffer.
 *
 * The calldata length is bounds-checked up front against @ref CALLDATA_MAX
 * so the fixed-size items buffer can never overflow.
 *
 * @param[in]  tx        Transaction to encode.
 * @param[out] items     Scratch buffer.
 * @param[in]  items_cap Capacity of @p items (>= @ref ITEMS_BUF_MAX).
 * @return Bytes written, or 0 if tx->calldata_len exceeds @ref CALLDATA_MAX
 *         or the buffer is too small.
 */
static size_t encode_common_fields(const eth_tx_t *tx,
                                   uint8_t *items, size_t items_cap)
{
    if (tx->calldata_len > CALLDATA_MAX) {
        return 0U;
    }
    size_t n = 0U;
    size_t w;

    w = rlp_uint64(tx->chain_id,         items + n, items_cap - n); if (w == 0U) { return 0U; } n += w;
    w = rlp_uint64(tx->nonce,            items + n, items_cap - n); if (w == 0U) { return 0U; } n += w;
    w = rlp_uint64(tx->max_priority_fee, items + n, items_cap - n); if (w == 0U) { return 0U; } n += w;
    w = rlp_uint64(tx->max_fee,          items + n, items_cap - n); if (w == 0U) { return 0U; } n += w;
    w = rlp_uint64(tx->gas_limit,        items + n, items_cap - n); if (w == 0U) { return 0U; } n += w;
    w = rlp_bytes(tx->to, 20U,           items + n, items_cap - n); if (w == 0U) { return 0U; } n += w;
    w = rlp_uint64(tx->eth_value,        items + n, items_cap - n); if (w == 0U) { return 0U; } n += w;
    w = rlp_bytes(tx->calldata, tx->calldata_len,
                                         items + n, items_cap - n); if (w == 0U) { return 0U; } n += w;

    if ((items_cap - n) < 1U) { return 0U; }
    items[n] = 0xC0U;   /* empty access list */
    n += 1U;
    return n;
}

/******************************************************************
 * 5. Public API
 ******************************************************************/

size_t eth_rlp_encode_unsigned(const eth_tx_t *tx, uint8_t *out, size_t out_max)
{
    uint8_t items[ITEMS_BUF_MAX];
    size_t  items_len = encode_common_fields(tx, items, sizeof(items));
    if (items_len == 0U) { return 0U; }   /* calldata too large */

    uint8_t hdr[10];
    size_t  hdr_len = rlp_list_header(items_len, hdr, sizeof(hdr));
    if (hdr_len == 0U) { return 0U; }

    size_t total = 1U + hdr_len + items_len;   /* 0x02 prefix + list */
    if (total > out_max) { return 0U; }

    out[0] = 0x02U;                             /* EIP-1559 type prefix */
    if (!CW_Utils::safe_memcpy(out + 1U, out_max - 1U, hdr, hdr_len)) {
        return 0U;
    }
    if (!CW_Utils::safe_memcpy(out + 1U + hdr_len,
                               out_max - 1U - hdr_len, items, items_len)) {
        return 0U;
    }
    return total;
}

size_t eth_rlp_encode_signed(const eth_tx_t *tx, uint8_t v,
                              const uint8_t r[32], const uint8_t s[32],
                              uint8_t *out, size_t out_max)
{
    uint8_t items[ITEMS_BUF_MAX];
    size_t  items_len = encode_common_fields(tx, items, sizeof(items));
    if (items_len == 0U) { return 0U; }   /* calldata too large */

    size_t w;
    w = rlp_uint64(static_cast<uint64_t>(v),
                   items + items_len, sizeof(items) - items_len);
    if (w == 0U) { return 0U; } items_len += w;
    w = rlp_int256(r, items + items_len, sizeof(items) - items_len);
    if (w == 0U) { return 0U; } items_len += w;
    w = rlp_int256(s, items + items_len, sizeof(items) - items_len);
    if (w == 0U) { return 0U; } items_len += w;

    uint8_t hdr[10];
    size_t  hdr_len = rlp_list_header(items_len, hdr, sizeof(hdr));
    if (hdr_len == 0U) { return 0U; }

    size_t total = 1U + hdr_len + items_len;
    if (total > out_max) { return 0U; }

    out[0] = 0x02U;
    if (!CW_Utils::safe_memcpy(out + 1U, out_max - 1U, hdr, hdr_len)) {
        return 0U;
    }
    if (!CW_Utils::safe_memcpy(out + 1U + hdr_len,
                               out_max - 1U - hdr_len, items, items_len)) {
        return 0U;
    }
    return total;
}
