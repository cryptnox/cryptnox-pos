/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file tron_tx.cpp
 * @brief Implementation of the Tron transaction hex helpers.
 */

#include "tron_tx.h"

#include <stdio.h>
#include <string.h>

/* Protobuf wire tags used below (field number << 3 | wire type):
 *   TransferContract.owner_address : field 1, bytes  -> 0x0a, len 0x15 (21)
 *   TransferContract.to_address    : field 2, bytes  -> 0x12, len 0x15 (21)
 *   TransferContract.amount        : field 3, varint -> 0x18
 *   TriggerSmartContract.owner_address    : field 1, bytes -> 0x0a, len 0x15
 *   TriggerSmartContract.contract_address : field 2, bytes -> 0x12, len 0x15
 *   TriggerSmartContract.data             : field 4, bytes -> 0x22, len 0x44 (68)
 *   Transaction.raw_data           : field 1, bytes  -> 0x0a
 *   Transaction.raw_data.fee_limit : field 18, varint -> 0x90 0x01
 *   Transaction.signature          : field 2, bytes  -> 0x12, len 0x41 (65)
 */

static char nibble_hex(unsigned n)
{
    return (n < 10U) ? static_cast<char>('0' + n)
                     : static_cast<char>('a' + n - 10U);
}

static bool is_hex_digit(char c)
{
    return ((c >= '0') && (c <= '9')) ||
           ((c >= 'a') && (c <= 'f')) ||
           ((c >= 'A') && (c <= 'F'));
}

static char lower_ascii(char c)
{
    return ((c >= 'A') && (c <= 'Z')) ? static_cast<char>(c + ('a' - 'A')) : c;
}

/** @brief Length of @p s if every character is hex, 0 otherwise. */
static size_t hex_strlen(const char *s)
{
    size_t n = 0U;
    for (; s[n] != '\0'; n++) {
        if (!is_hex_digit(s[n])) { return 0U; }
    }
    return n;
}

/**
 * @brief true if @p raw is usable raw_data hex: all hex, non-empty, whole bytes.
 *
 * Odd-length hex is not a byte string, so it is refused here rather than read.
 */
static bool raw_hex_ok(const char *raw)
{
    if (raw == NULL) { return false; }
    const size_t n = hex_strlen(raw);
    return (n != 0U) && ((n % 2U) == 0U);
}

/** @brief true if @p addr is a "41"-prefixed 21-byte Tron address in hex. */
static bool addr_hex_ok(const char *addr)
{
    return (addr != NULL) &&
           (hex_strlen(addr) == TRON_ADDR_HEX_LEN) &&
           (addr[0] == '4') && (addr[1] == '1');
}

size_t tron_varint_hex(uint64_t v, char *out, size_t out_size)
{
    uint8_t b[10];            /* 64 bits / 7 bits per byte = 10 bytes max */
    size_t  n = 0U;

    do {
        uint8_t x = static_cast<uint8_t>(v & 0x7FU);
        v >>= 7U;
        if (v != 0U) { x |= 0x80U; }
        b[n] = x;
        n++;
    } while ((v != 0U) && (n < sizeof(b)));

    if ((out == NULL) || (out_size < ((n * 2U) + 1U))) { return 0U; }

    for (size_t i = 0U; i < n; i++) {
        out[i * 2U]      = nibble_hex((b[i] >> 4U) & 0x0FU);
        out[i * 2U + 1U] = nibble_hex(b[i] & 0x0FU);
    }
    out[n * 2U] = '\0';
    return n * 2U;
}

/* ── A strict reader for the one protobuf shape a node may hand back ──
 *
 * The node serialises raw_data and the card signs sha256 of it, so every byte
 * must be accounted for. Finding the expected bytes *somewhere* is not enough: a
 * node could put them in the memo (raw_data.data) while the contract that
 * executes pays somebody else. So raw_data is walked field by field and only
 * this shape is accepted:
 *
 *   raw      { 1 ref_block_bytes, 3 ref_block_num, 4 ref_block_hash,
 *              8 expiration, 11 contract (exactly one), 14 timestamp,
 *              18 fee_limit (TRC-20 only: exactly the cap we asked for) }
 *   contract { 1 type, 2 parameter }        no Permission_id, provider, name
 *   Any      { 1 type_url, 2 value }        value byte-exact
 *
 * No field may repeat. Anything else — data (10), auths (9), scripts (12), an
 * unknown field, a fixed-width wire type, a length past the end — is refused. */

/** @brief Cursor over a hex byte string; positions count BYTES, not nibbles. */
typedef struct {
    const char *hex;
    size_t      pos;
    size_t      end;
} pb_t;

static int hex_val(char c)
{
    c = lower_ascii(c);
    if ((c >= '0') && (c <= '9')) { return c - '0'; }
    if ((c >= 'a') && (c <= 'f')) { return (c - 'a') + 10; }
    return -1;
}

static bool pb_byte(pb_t *p, uint8_t *b)
{
    if (p->pos >= p->end) { return false; }
    const int hi = hex_val(p->hex[p->pos * 2U]);
    const int lo = hex_val(p->hex[(p->pos * 2U) + 1U]);
    if ((hi < 0) || (lo < 0)) { return false; }
    *b = static_cast<uint8_t>((hi << 4) | lo);
    p->pos++;
    return true;
}

static bool pb_varint(pb_t *p, uint64_t *v)
{
    uint64_t x = 0U;
    for (unsigned i = 0U; i < 10U; i++) {
        uint8_t b;
        if (!pb_byte(p, &b)) { return false; }
        x |= static_cast<uint64_t>(b & 0x7FU) << (7U * i);
        if ((b & 0x80U) == 0U) { *v = x; return true; }
    }
    return false;   /* longer than any 64-bit varint */
}

/** @brief One field: its number and either its varint or its payload range. */
typedef struct {
    uint32_t num;
    uint32_t wire;    /* 0 varint or 2 length-delimited; nothing else passes */
    uint64_t value;   /* wire 0 */
    pb_t     sub;     /* wire 2 */
} pb_field_t;

static bool pb_next(pb_t *p, pb_field_t *f)
{
    uint64_t key;
    if (!pb_varint(p, &key)) { return false; }
    f->num  = static_cast<uint32_t>(key >> 3U);
    f->wire = static_cast<uint32_t>(key & 7U);
    if ((f->num == 0U) || (f->num > 31U)) { return false; }
    if (f->wire == 0U) { return pb_varint(p, &f->value); }
    if (f->wire != 2U) { return false; }
    uint64_t len;
    if (!pb_varint(p, &len) || (len > (p->end - p->pos))) { return false; }
    f->sub.hex = p->hex;
    f->sub.pos = p->pos;
    f->sub.end = p->pos + static_cast<size_t>(len);
    p->pos     = f->sub.end;
    return true;
}

/** @brief true if field @p f is wire @p wire and was not seen before. */
static bool pb_once(const pb_field_t *f, uint32_t wire, uint32_t *seen)
{
    const uint32_t bit = 1UL << f->num;
    if ((f->wire != wire) || ((*seen & bit) != 0U)) { return false; }
    *seen |= bit;
    return true;
}

/** @brief true if a payload is exactly the hex string @p want (any case). */
static bool pb_is_hex(const pb_t *sub, const char *want)
{
    const size_t n = sub->end - sub->pos;
    if (strlen(want) != (n * 2U)) { return false; }
    const char *h = sub->hex + (sub->pos * 2U);
    for (size_t i = 0U; i < (n * 2U); i++) {
        if (lower_ascii(h[i]) != lower_ascii(want[i])) { return false; }
    }
    return true;
}

/** @brief true if a payload is exactly the ASCII string @p want. */
static bool pb_is_str(const pb_t *sub, const char *want)
{
    const size_t n = sub->end - sub->pos;
    if (strlen(want) != n) { return false; }
    pb_t c = *sub;
    for (size_t i = 0U; i < n; i++) {
        uint8_t b;
        if (!pb_byte(&c, &b) || (b != static_cast<uint8_t>(want[i]))) {
            return false;
        }
    }
    return true;
}

#define TRON_TYPE_TRANSFER  1U    /* ContractType.TransferContract     */
#define TRON_TYPE_TRIGGER   31U   /* ContractType.TriggerSmartContract */
#define TRON_URL_TRANSFER   "type.googleapis.com/protocol.TransferContract"
#define TRON_URL_TRIGGER    "type.googleapis.com/protocol.TriggerSmartContract"

/** @brief Any { 1 type_url, 2 value }: exactly @p url and one of the values. */
static bool any_ok(pb_t any, const char *url, const char *v1, const char *v2)
{
    uint32_t   seen = 0U;
    bool       url_ok = false;
    bool       val_ok = false;
    pb_field_t f;
    while (any.pos < any.end) {
        if (!pb_next(&any, &f) || !pb_once(&f, 2U, &seen)) { return false; }
        if (f.num == 1U) {
            url_ok = pb_is_str(&f.sub, url);
        } else if (f.num == 2U) {
            val_ok = pb_is_hex(&f.sub, v1) ||
                     ((v2 != NULL) && pb_is_hex(&f.sub, v2));
        } else {
            return false;
        }
    }
    return url_ok && val_ok;
}

/** @brief Contract { 1 type, 2 parameter } and nothing else. */
static bool contract_ok(pb_t c, uint32_t type, const char *url,
                        const char *v1, const char *v2)
{
    uint32_t   seen = 0U;
    bool       type_ok = false;
    bool       param_ok = false;
    pb_field_t f;
    while (c.pos < c.end) {
        if (!pb_next(&c, &f)) { return false; }
        if (f.num == 1U) {
            if (!pb_once(&f, 0U, &seen)) { return false; }
            type_ok = (f.value == type);
        } else if (f.num == 2U) {
            if (!pb_once(&f, 2U, &seen)) { return false; }
            param_ok = any_ok(f.sub, url, v1, v2);
        } else {
            return false;
        }
    }
    return type_ok && param_ok;
}

/**
 * @brief Walk raw_data. @p fee_limit 0 means the field must be absent.
 *
 * The expiration is bounded as well as returned. A node that set it hours out
 * could hold the signed transaction back and broadcast it after the terminal
 * has told the operator it never went out and a second payment was taken. The
 * chain allows up to 24 h; a real answer is about 60 s.
 */
static bool raw_ok(const char *raw_hex, uint32_t type, const char *url,
                   const char *v1, const char *v2, uint64_t fee_limit,
                   uint64_t now_ms, uint64_t *expiration_ms)
{
    pb_t       p = { raw_hex, 0U, strlen(raw_hex) / 2U };
    uint32_t   seen = 0U;
    bool       have_contract = false;
    bool       fee_ok = (fee_limit == 0U);
    uint64_t   expiration = 0U;
    pb_field_t f;
    while (p.pos < p.end) {
        if (!pb_next(&p, &f)) { return false; }
        switch (f.num) {
        case 1U: case 4U:                        /* ref_block_bytes / _hash */
            if (!pb_once(&f, 2U, &seen)) { return false; }
            break;
        case 3U: case 14U:                       /* ref_block_num, timestamp */
            if (!pb_once(&f, 0U, &seen)) { return false; }
            break;
        case 8U:
            if (!pb_once(&f, 0U, &seen)) { return false; }
            expiration = f.value;
            break;
        case 11U:
            if (!pb_once(&f, 2U, &seen)) { return false; }
            have_contract = contract_ok(f.sub, type, url, v1, v2);
            break;
        case 18U:
            if (!pb_once(&f, 0U, &seen)) { return false; }
            fee_ok = (fee_limit != 0U) && (f.value == fee_limit);
            break;
        default:
            return false;      /* data, auths, scripts, or anything unknown */
        }
    }
    if (!have_contract || !fee_ok || (expiration == 0U)) { return false; }
    if ((now_ms == 0U) || (expiration > (now_ms + TRON_TX_MAX_EXPIRY_MS))) {
        return false;
    }
    if (expiration_ms != NULL) { *expiration_ms = expiration; }
    return true;
}

bool tron_tx_contract_ok(const char *raw_data_hex, const char *owner_hex,
                         const char *to_hex, uint64_t amount_sun,
                         uint64_t now_ms, uint64_t *expiration_ms)
{
    if (!raw_hex_ok(raw_data_hex)) { return false; }
    if (!addr_hex_ok(owner_hex) || !addr_hex_ok(to_hex)) { return false; }
    /* A zero-amount transfer is not a payment; reject rather than sign it. */
    if (amount_sun == 0U) { return false; }

    char amount[24];
    if (tron_varint_hex(amount_sun, amount, sizeof(amount)) == 0U) {
        return false;
    }

    /* TransferContract { 1 owner, 2 to, 3 amount } — the whole value. */
    char want[4U + TRON_ADDR_HEX_LEN + 4U + TRON_ADDR_HEX_LEN + 2U +
              sizeof(amount)];
    int k = snprintf(want, sizeof(want), "0a15%s1215%s18%s",
                     owner_hex, to_hex, amount);
    if ((k <= 0) || (static_cast<size_t>(k) >= sizeof(want))) { return false; }

    return raw_ok(raw_data_hex, TRON_TYPE_TRANSFER, TRON_URL_TRANSFER,
                  want, NULL, 0U, now_ms, expiration_ms);
}

size_t tron_trc20_param_hex(const char *to_hex, uint64_t amount,
                            char *out, size_t out_size)
{
    if (!addr_hex_ok(to_hex) || (out == NULL) ||
        (out_size < (TRON_TRC20_PARAM_HEX_LEN + 1U))) {
        return 0U;
    }

    size_t p = 0U;
    /* word 1 — address: 12 zero bytes, then the key hash without its "41". */
    for (size_t i = 0U; i < 24U; i++, p++) { out[p] = '0'; }
    for (size_t i = 2U; i < TRON_ADDR_HEX_LEN; i++, p++) {
        out[p] = lower_ascii(to_hex[i]);
    }
    /* word 2 — amount: 24 zero bytes, then the 64-bit value big-endian. */
    for (size_t i = 0U; i < 48U; i++, p++) { out[p] = '0'; }
    for (int sh = 60; sh >= 0; sh -= 4, p++) {
        out[p] = nibble_hex(static_cast<unsigned>((amount >> sh) & 0x0FULL));
    }

    out[p] = '\0';
    return p;
}

bool tron_tx_trc20_ok(const char *raw_data_hex, const char *owner_hex,
                      const char *contract_hex, const char *to_hex,
                      uint64_t amount, uint64_t fee_limit_sun,
                      uint64_t now_ms, uint64_t *expiration_ms)
{
    if (!raw_hex_ok(raw_data_hex)) { return false; }
    if (!addr_hex_ok(owner_hex) || !addr_hex_ok(contract_hex) ||
        !addr_hex_ok(to_hex)) {
        return false;
    }
    /* A zero-token transfer is not a payment, and an uncapped call is not one
     * the operator agreed to. Neither gets signed. */
    if ((amount == 0U) || (fee_limit_sun == 0U)) { return false; }

    char param[TRON_TRC20_PARAM_HEX_LEN + 1U];
    if (tron_trc20_param_hex(to_hex, amount, param, sizeof(param)) == 0U) {
        return false;
    }

    /* TriggerSmartContract { 1 owner, 2 contract, 4 data } — the whole value.
     * call_value (3) is proto3-default 0 and normally omitted, but a node that
     * emits it explicitly ("1800") is serialising the same contract. Nothing
     * else: call_token_value / token_id would move a TRC-10 token as well. */
    char want[4U + TRON_ADDR_HEX_LEN + 4U + TRON_ADDR_HEX_LEN + 4U + 4U +
              sizeof(TRON_TRC20_SELECTOR) + sizeof(param) + 8U];
    int k = snprintf(want, sizeof(want),
                     "0a15%s1215%s2244" TRON_TRC20_SELECTOR "%s",
                     owner_hex, contract_hex, param);
    if ((k <= 0) || (static_cast<size_t>(k) >= sizeof(want))) { return false; }
    char want_cv[sizeof(want)];
    k = snprintf(want_cv, sizeof(want_cv),
                 "0a15%s1215%s18002244" TRON_TRC20_SELECTOR "%s",
                 owner_hex, contract_hex, param);
    if ((k <= 0) || (static_cast<size_t>(k) >= sizeof(want_cv))) {
        return false;
    }

    return raw_ok(raw_data_hex, TRON_TYPE_TRIGGER, TRON_URL_TRIGGER,
                  want, want_cv, fee_limit_sun, now_ms, expiration_ms);
}

size_t tron_tx_envelope_hex(const char *raw_data_hex, const char *sig_hex,
                            char *out, size_t out_size)
{
    if ((raw_data_hex == NULL) || (sig_hex == NULL) || (out == NULL) ||
        (out_size == 0U)) {
        return 0U;
    }

    const size_t raw_len = hex_strlen(raw_data_hex);
    if ((raw_len == 0U) || ((raw_len % 2U) != 0U)) { return 0U; }
    if (hex_strlen(sig_hex) != TRON_SIG_HEX_LEN) { return 0U; }

    char len_hex[24];
    if (tron_varint_hex(raw_len / 2U, len_hex, sizeof(len_hex)) == 0U) {
        return 0U;
    }

    int k = snprintf(out, out_size, "0a%s%s1241%s",
                     len_hex, raw_data_hex, sig_hex);
    if ((k <= 0) || (static_cast<size_t>(k) >= out_size)) {
        out[0] = '\0';
        return 0U;
    }
    return static_cast<size_t>(k);
}
