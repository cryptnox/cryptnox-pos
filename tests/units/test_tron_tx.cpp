/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * test_tron_tx.cpp — host unit test for the Tron hex helpers: protobuf varints,
 * the TransferContract and TriggerSmartContract integrity checks (the things
 * standing between a hostile full node and a redirected payment — or, for
 * TRC-20, a payment in the wrong token) and the signed-Transaction envelope.
 *
 * Single translation unit: it #includes the production source directly, the
 * same pattern as the other tests here. Build & run from the repo root:
 *
 *   g++ -std=c++14 -Imain tests/units/test_tron_tx.cpp -o test_tron_tx && \
 *       ./test_tron_tx
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "tron_tx.cpp"

/* ── a tiny protobuf builder, so each vector below says what it contains ── */
static std::string vi(uint64_t v)
{
    char b[24];
    assert(tron_varint_hex(v, b, sizeof(b)) != 0U);
    return b;
}
/* length-delimited field: tag in hex, then the payload's length and bytes */
static std::string ld(const char *tag, const std::string &payload)
{
    return std::string(tag) + vi(payload.size() / 2U) + payload;
}
static std::string ascii_hex(const char *s)
{
    std::string h;
    char b[3];
    for (; *s != 0; s++) {
        snprintf(b, sizeof(b), "%02x", (unsigned)(unsigned char)*s);
        h += b;
    }
    return h;
}
/* Contract { 1 type, 2 Any { 1 type_url, 2 value } } */
static std::string contract(unsigned type, const char *url,
                            const std::string &value)
{
    return "08" + vi(type) +
           ld("12", ld("0a", ascii_hex(url)) + ld("12", value));
}

#define EXPIRES   1785922800000ULL          /* the Nile answer's expiration */
#define NOW       (EXPIRES - 60000ULL)      /* a node answers ~60 s ahead   */
#define REF       "0a021f5e2208c011d0fa8e1ebf4d"    /* ref_block_bytes/_hash */
#define EXP       "408093ad8afd33"                  /* 8: expiration         */
#define TS        "7091c4a98afd33"                  /* 14: timestamp         */

/* 1.5 TRX (1500000 sun): TransferContract { owner, to, amount e0c65b }. */
#define OWNER "41d4a5f19c1b9e0e2a1d5f0b3c4d5e6f708192a3b4"
#define DEST  "41cadddf4677544d0eb25e4f87cd978aa5de23ebc6"
#define THIEF "41deadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
static const std::string XFER_OK   = "0a15" OWNER "1215" DEST  "18e0c65b";
static const std::string XFER_EVIL = "0a15" OWNER "1215" THIEF "18e0c65b";
static const std::string RAW_OK_S  = std::string(REF EXP) +
    ld("5a", contract(1U, TRON_URL_TRANSFER, XFER_OK)) + TS;
static const char *const RAW_OK = RAW_OK_S.c_str();

static bool trx_ok(const std::string &raw, uint64_t now = NOW)
{
    return tron_tx_contract_ok(raw.c_str(), OWNER, DEST, 1500000U, now, NULL);
}

int main(void)
{
    /* ── varints (protobuf base-128, little-endian groups) ── */
    char v[24];
    assert(tron_varint_hex(0U, v, sizeof(v)) == 2U);
    assert(strcmp(v, "00") == 0);
    assert(tron_varint_hex(1U, v, sizeof(v)) == 2U);
    assert(strcmp(v, "01") == 0);
    assert(tron_varint_hex(127U, v, sizeof(v)) == 2U);
    assert(strcmp(v, "7f") == 0);
    assert(tron_varint_hex(128U, v, sizeof(v)) == 4U);
    assert(strcmp(v, "8001") == 0);
    assert(tron_varint_hex(1500000U, v, sizeof(v)) == 6U);   /* 1.5 TRX */
    assert(strcmp(v, "e0c65b") == 0);
    assert(tron_varint_hex(1000000U, v, sizeof(v)) == 6U);   /* 1.0 TRX */
    assert(strcmp(v, "c0843d") == 0);
    /* Too small an output buffer must fail, not truncate. */
    char tiny[3];
    assert(tron_varint_hex(128U, tiny, sizeof(tiny)) == 0U);
    printf("varint hex ... OK\n");

    /* ── the contract check accepts exactly what was asked for ── */
    uint64_t exp = 0U;
    assert(tron_tx_contract_ok(RAW_OK, OWNER, DEST, 1500000U, NOW, &exp));
    assert(exp == EXPIRES);
    assert(trx_ok(RAW_OK_S));
    /* ...and refuses everything else. Each of these is a payment the terminal
     * must decline rather than sign. */
    assert(!tron_tx_contract_ok(RAW_OK, OWNER, DEST, 1500001U, NOW, NULL));
    assert(!tron_tx_contract_ok(RAW_OK, DEST, OWNER, 1500000U, NOW, NULL));
    assert(!tron_tx_contract_ok(RAW_OK, OWNER,
                                "41cadddf4677544d0eb25e4f87cd978aa5de23ebc7",
                                1500000U, NOW, NULL));           /* recipient */
    assert(!tron_tx_contract_ok(RAW_OK, OWNER, DEST, 0U, NOW, NULL));
    assert(!tron_tx_contract_ok("", OWNER, DEST, 1500000U, NOW, NULL));
    assert(!tron_tx_contract_ok("not hex!", OWNER, DEST, 1500000U, NOW, NULL));
    assert(!tron_tx_contract_ok(RAW_OK, "41short", DEST, 1500000U, NOW, NULL));
    assert(!tron_tx_contract_ok(RAW_OK, OWNER,
                               /* right length, wrong address prefix */
                               "42cadddf4677544d0eb25e4f87cd978aa5de23ebc6",
                               1500000U, NOW, NULL));
    assert(!tron_tx_contract_ok(NULL, OWNER, DEST, 1500000U, NOW, NULL));
    /* Hex case must not matter: config addresses carry an EIP-55 checksum. */
    assert(tron_tx_contract_ok(RAW_OK, OWNER,
                               "41CADDDF4677544D0EB25E4F87CD978AA5DE23EBC6",
                               1500000U, NOW, NULL));

    /* ── what the old substring check let through ── */
    /* Our exact contract bytes in the memo (raw_data.data, field 10), while the
     * contract that executes pays THIEF — txID is still sha256(raw). */
    assert(!trx_ok(std::string(REF EXP) +
                   ld("5a", contract(1U, TRON_URL_TRANSFER, XFER_EVIL)) +
                   ld("52", XFER_OK) + TS));
    /* Any memo at all, even beside the right contract. */
    assert(!trx_ok(RAW_OK_S + ld("52", "deadbeef")));
    /* A second contract entry. */
    assert(!trx_ok(RAW_OK_S + ld("5a", contract(1U, TRON_URL_TRANSFER, XFER_EVIL))));
    /* The right bytes with an extra field inside the contract value. */
    assert(!trx_ok(std::string(REF EXP) +
                   ld("5a", contract(1U, TRON_URL_TRANSFER, XFER_OK + "2001")) + TS));
    /* Permission_id (contract field 5): a signature for another permission. */
    assert(!trx_ok(std::string(REF EXP) +
                   ld("5a", contract(1U, TRON_URL_TRANSFER, XFER_OK) + "2802") + TS));
    /* Right value under the wrong contract type or type_url. */
    assert(!trx_ok(std::string(REF EXP) +
                   ld("5a", contract(2U, TRON_URL_TRANSFER, XFER_OK)) + TS));
    assert(!trx_ok(std::string(REF EXP) +
                   ld("5a", contract(1U, TRON_URL_TRIGGER, XFER_OK)) + TS));
    /* Authorities (9), scripts (12), a repeated field, a fee_limit on TRX. */
    assert(!trx_ok(RAW_OK_S + ld("4a", "00")));
    assert(!trx_ok(RAW_OK_S + ld("62", "00")));
    assert(!trx_ok(RAW_OK_S + TS));
    assert(!trx_ok(RAW_OK_S + "900180c2d72f"));
    /* A length running past the end, and a fixed64 wire type. */
    assert(!trx_ok(RAW_OK_S + "5a7f00"));
    assert(!trx_ok(RAW_OK_S + "790000000000000000"));
    /* Expiry: set far out to hold the tx back, missing, or no clock. */
    assert(!trx_ok(RAW_OK_S, EXPIRES - TRON_TX_MAX_EXPIRY_MS - 1U));
    assert(trx_ok(RAW_OK_S, EXPIRES - TRON_TX_MAX_EXPIRY_MS));
    assert(!trx_ok(std::string(REF) +
                   ld("5a", contract(1U, TRON_URL_TRANSFER, XFER_OK)) + TS));
    assert(!trx_ok(RAW_OK_S, 0U));

    /* The same check against an actual /wallet/createtransaction answer from a
     * Nile full node (1.5 TRX), so the expected byte run is pinned to what the
     * chain really serialises and not just to our reading of the protobuf. */
    static const char *const NILE_RAW =
        "0a021f5e2208c011d0fa8e1ebf4d408093ad8afd335a67080112630a2d747970652e"
        "676f6f676c65617069732e636f6d2f70726f746f636f6c2e5472616e73666572436f"
        "6e747261637412320a1541d1e7a6bc354106cb410e65ff8b181c600ff142921215"
        "41e552f6487585c2b58bc2c9bb4492bc1f17132cd018e0c65b7091c4a98afd33";
    static const char *const NILE_OWNER =
        "41d1e7a6bc354106cb410e65ff8b181c600ff14292";
    static const char *const NILE_TO =
        "41e552f6487585c2b58bc2c9bb4492bc1f17132cd0";
    assert(tron_tx_contract_ok(NILE_RAW, NILE_OWNER, NILE_TO, 1500000U, NOW, &exp));
    assert(exp == EXPIRES);
    assert(!tron_tx_contract_ok(NILE_RAW, NILE_OWNER, NILE_TO, 1500000U + 1U,
                                NOW, NULL));

    /* The expected byte run must be found on a BYTE boundary and nowhere else.
     * raw_data below really pays THIEF, and carries the run we look for shifted
     * one nibble along — inside a field whose contents a node chooses freely. It
     * is even-length overall, so it hex-decodes fine and its txID is a consistent
     * sha256 of those bytes: this check is the only thing standing between a
     * hostile node and a signature over somebody else's payment. */
    static const char *const RAW_SHIFTED =
        "0a15" OWNER "1215" THIEF "18e0c65b"     /* what actually executes */
        "5a"                                     /* filler tag + odd padding */
        "f0a15" OWNER "1215" DEST "18e0c65b" "0";/* our run, one nibble out */
    assert(strlen(RAW_SHIFTED) % 2U == 0U);      /* decodes as bytes */
    assert(!tron_tx_contract_ok(RAW_SHIFTED, OWNER, DEST, 1500000U, NOW, NULL));
    /* Odd-length hex is not a byte string and cannot be checked at all. */
    assert(!tron_tx_contract_ok("0a15" OWNER "1215" DEST "18e0c65b" "0",
                               OWNER, DEST, 1500000U, NOW, NULL));
    printf("contract check ... OK\n");

    /* ── TRC-20: the ABI parameter block ── */
#define TOKEN "41eca9bc828a3005b9a3b909f2cc5c2a54794de05f"
    /* transfer(DEST, 1.5 tokens): address word (0x41 prefix dropped, left-
     * padded) then the amount word. 1500000 = 0x16e360. */
    static const char *const PARAM_OK =
        "000000000000000000000000" "cadddf4677544d0eb25e4f87cd978aa5de23ebc6"
        "000000000000000000000000000000000000000000000000" "000000000016e360";
    char param[TRON_TRC20_PARAM_HEX_LEN + 1];
    assert(tron_trc20_param_hex(DEST, 1500000U, param, sizeof(param)) ==
           TRON_TRC20_PARAM_HEX_LEN);
    assert(strcmp(param, PARAM_OK) == 0);
    /* Upper-case in, lower-case out: the node echoes what we send, and the
     * integrity check compares against this same string. */
    assert(tron_trc20_param_hex("41CADDDF4677544D0EB25E4F87CD978AA5DE23EBC6",
                                1500000U, param, sizeof(param)) ==
           TRON_TRC20_PARAM_HEX_LEN);
    assert(strcmp(param, PARAM_OK) == 0);
    assert(tron_trc20_param_hex("41short", 1U, param, sizeof(param)) == 0U);
    assert(tron_trc20_param_hex(NULL, 1U, param, sizeof(param)) == 0U);
    char param_small[TRON_TRC20_PARAM_HEX_LEN];   /* one short of the NUL */
    assert(tron_trc20_param_hex(DEST, 1U, param_small, sizeof(param_small)) == 0U);
    printf("trc20 param hex ... OK\n");

    /* ── TRC-20: the TriggerSmartContract integrity check ──
     * fee_limit 100 TRX = 100000000 sun -> varint 80 c2 d7 2f, under tag 9001. */
#define FEE_LIMIT 100000000ULL
    static const char *const TRC20_RAW =
        "0a021f5e2208c011d0fa8e1ebf4d408093ad8afd335aae01081f12a9010a31747970"
        "652e676f6f676c65617069732e636f6d2f70726f746f636f6c2e5472696767657253"
        "6d617274436f6e747261637412740a15" OWNER "1215" TOKEN "2244a9059cbb"
        "000000000000000000000000" "cadddf4677544d0eb25e4f87cd978aa5de23ebc6"
        "000000000000000000000000000000000000000000000000" "000000000016e360"
        "7091c4a98afd33900180c2d72f";

    assert(tron_tx_trc20_ok(TRC20_RAW, OWNER, TOKEN, DEST, 1500000U, FEE_LIMIT, NOW, NULL));
    /* Every one of these is a payment the terminal must decline. */
    assert(!tron_tx_trc20_ok(TRC20_RAW, OWNER, TOKEN, DEST, 1500001U, FEE_LIMIT, NOW, NULL));
    assert(!tron_tx_trc20_ok(TRC20_RAW, OWNER, TOKEN, OWNER, 1500000U, FEE_LIMIT, NOW, NULL));
    assert(!tron_tx_trc20_ok(TRC20_RAW, DEST, TOKEN, DEST, 1500000U, FEE_LIMIT, NOW, NULL));
    /* Wrong contract = wrong asset: the node swapped in another token. */
    assert(!tron_tx_trc20_ok(TRC20_RAW, OWNER,
                             "41eca9bc828a3005b9a3b909f2cc5c2a54794de05e",
                             DEST, 1500000U, FEE_LIMIT, NOW, NULL));
    /* A fee cap the operator never agreed to. */
    assert(!tron_tx_trc20_ok(TRC20_RAW, OWNER, TOKEN, DEST, 1500000U,
                             FEE_LIMIT * 2U, NOW, NULL));
    assert(!tron_tx_trc20_ok(TRC20_RAW, OWNER, TOKEN, DEST, 1500000U, 0U, NOW, NULL));
    assert(!tron_tx_trc20_ok(TRC20_RAW, OWNER, TOKEN, DEST, 0U, FEE_LIMIT, NOW, NULL));
    /* A TRX TransferContract must not pass as a token transfer, or vice versa. */
    assert(!tron_tx_trc20_ok(RAW_OK, OWNER, TOKEN, DEST, 1500000U, FEE_LIMIT, NOW, NULL));
    assert(!tron_tx_contract_ok(TRC20_RAW, OWNER, DEST, 1500000U, NOW, NULL));
    assert(!tron_tx_trc20_ok(NULL, OWNER, TOKEN, DEST, 1500000U, FEE_LIMIT, NOW, NULL));
    assert(!tron_tx_trc20_ok("not hex!", OWNER, TOKEN, DEST, 1500000U, FEE_LIMIT, NOW, NULL));
    assert(tron_tx_trc20_ok(TRC20_RAW, OWNER,
                            "41ECA9BC828A3005B9A3B909F2CC5C2A54794DE05F",
                            DEST, 1500000U, FEE_LIMIT, NOW, NULL));

    /* Some nodes serialise the proto3-default call_value explicitly ("1800");
     * that is the same contract and must still be accepted. */
    const std::string TRC20_CV = std::string(REF EXP) +
        ld("5a", contract(31U, TRON_URL_TRIGGER,
                          "0a15" OWNER "1215" TOKEN "18002244a9059cbb"
                          "000000000000000000000000"
                          "cadddf4677544d0eb25e4f87cd978aa5de23ebc6"
                          "000000000000000000000000000000000000000000000000"
                          "000000000016e360")) + TS "900180c2d72f";
    const char *const TRC20_RAW_CV = TRC20_CV.c_str();
    assert(tron_tx_trc20_ok(TRC20_RAW_CV, OWNER, TOKEN, DEST, 1500000U, FEE_LIMIT, NOW, NULL));
    /* The strict parse applies here too: a memo, a missing or different
     * fee_limit, a call_token_value (moves a TRC-10 token as well). */
    assert(!tron_tx_trc20_ok((std::string(TRC20_RAW) + ld("52", "00")).c_str(),
                             OWNER, TOKEN, DEST, 1500000U, FEE_LIMIT, NOW, NULL));
    const std::string no_fee(TRC20_RAW, strlen(TRC20_RAW) - 12U);
    assert(!tron_tx_trc20_ok(no_fee.c_str(), OWNER, TOKEN, DEST, 1500000U,
                             FEE_LIMIT, NOW, NULL));
    assert(!tron_tx_trc20_ok((std::string(REF EXP) +
        ld("5a", contract(31U, TRON_URL_TRIGGER,
                          "0a15" OWNER "1215" TOKEN "2244a9059cbb"
                          "000000000000000000000000"
                          "cadddf4677544d0eb25e4f87cd978aa5de23ebc6"
                          "000000000000000000000000000000000000000000000000"
                          "000000000016e360" "2801")) + TS "900180c2d72f").c_str(),
        OWNER, TOKEN, DEST, 1500000U, FEE_LIMIT, NOW, NULL));
    printf("trc20 contract check ... OK\n");

    /* ── envelope: Transaction { 1: raw_data, 2: signature } ── */
    static const char sig[TRON_SIG_HEX_LEN + 1] =
        "1111111111111111111111111111111111111111111111111111111111111111"
        "2222222222222222222222222222222222222222222222222222222222222222"
        "01";
    char env[1024];
    size_t n = tron_tx_envelope_hex(RAW_OK, sig, env, sizeof(env));
    assert(n == strlen(env));
    /* raw_data is over 127 bytes here -> a two-byte length varint */
    const std::string head = "0a" + vi(strlen(RAW_OK) / 2U);
    assert(head.size() == 6U);
    assert(strncmp(env, head.c_str(), 6) == 0);
    assert(strncmp(env + 6, RAW_OK, strlen(RAW_OK)) == 0);
    char tail[8 + TRON_SIG_HEX_LEN];
    (void)snprintf(tail, sizeof(tail), "1241%s", sig);
    assert(strcmp(env + 6 + strlen(RAW_OK), tail) == 0);

    /* Bad inputs produce nothing, never a half-built transaction. */
    assert(tron_tx_envelope_hex(RAW_OK, "0011", env, sizeof(env)) == 0U);
    assert(tron_tx_envelope_hex("abc", sig, env, sizeof(env)) == 0U);
    assert(tron_tx_envelope_hex("zz", sig, env, sizeof(env)) == 0U);
    char small[16];
    assert(tron_tx_envelope_hex(RAW_OK, sig, small, sizeof(small)) == 0U);
    assert(small[0] == '\0');
    printf("envelope hex ... OK\n");

    printf("test_tron_tx: all OK\n");
    return 0;
}
