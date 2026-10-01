/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * test_eth_sig.cpp — host unit test for eth_sig_parity: the recovery bit the
 * signed transaction carries, computed locally rather than asked of a node. A
 * wrong bit recovers the transaction to somebody else's account and the node
 * refuses it, so every EVM sale depends on this.
 *
 * Vector: the EIP-155 example (private key 0x4646...46, chain id 1, v = 37,
 * i.e. recovery id 0). Its low-s twin (s' = n - s) must come out as 1.
 *
 * Needs mbedTLS, which ships with ESP-IDF — scripts/checks.sh builds it.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "eth_sig.cpp"
#include "keccak256.cpp"
#include "CW_Utils.cpp"

#include "mbedtls/ecp.h"

static void hex(const char *h, uint8_t *out, size_t n)
{
    for (size_t i = 0U; i < n; i++) {
        unsigned b = 0U;
        (void)sscanf(h + (2U * i), "%2x", &b);
        out[i] = static_cast<uint8_t>(b);
    }
}

/* mbedtls_ecp_mul wants an RNG for its blinding; any bytes will do in a test. */
static int fake_rng(void *ctx, unsigned char *out, size_t n)
{
    (void)ctx;
    for (size_t i = 0U; i < n; i++) { out[i] = static_cast<unsigned char>(i * 37U + 11U); }
    return 0;
}

int main(void)
{
    uint8_t hash[32], r[32], s[32], priv[32], pub[64];
    hex("daf5a779ae972f972197303d7b574746c7ef83eadac0f2791ad23db92e4c8e53", hash, 32);
    hex("28ef61340bd939bc2195fe537567866003e1a15d3c71ff63e1590620aa636276", r, 32);
    hex("67cbe9d8997f761aecb703304b3800ccf555c9f3dc64214b297fb1966a3b6d83", s, 32);
    memset(priv, 0x46, sizeof(priv));

    /* Public key from the private key, checked against the vector's address. */
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi       d, sn;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&sn);
    assert(mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1) == 0);
    assert(mbedtls_mpi_read_binary(&d, priv, 32) == 0);
    assert(mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, fake_rng, NULL) == 0);
    uint8_t q65[65];
    size_t  olen = 0U;
    assert(mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                          &olen, q65, sizeof(q65)) == 0);
    memcpy(pub, &q65[1], 64);
    uint8_t kh[32];
    keccak256(pub, 64, kh);
    uint8_t want_addr[20];
    hex("9d8a62f656a8d1615c1294fd71e9cfb3e4855a4f", want_addr, 20);
    assert(memcmp(&kh[12], want_addr, 20) == 0);

    /* The vector: recovery id 0. */
    uint8_t v = 0xFFU;
    assert(eth_sig_parity(hash, r, s, pub, &v));
    assert(v == 0U);

    /* Its twin, s' = n - s: equally valid, the other point — parity 1. */
    uint8_t s2[32];
    assert(mbedtls_mpi_read_binary(&sn, s, 32) == 0);
    assert(mbedtls_mpi_sub_mpi(&sn, &grp.N, &sn) == 0);
    assert(mbedtls_mpi_write_binary(&sn, s2, 32) == 0);
    v = 0xFFU;
    assert(eth_sig_parity(hash, r, s2, pub, &v));
    assert(v == 1U);

    /* Not this key's signature: refused, v untouched. */
    uint8_t bad[32];
    memcpy(bad, hash, 32);
    bad[31] ^= 0x01U;
    v = 0xFFU;
    assert(!eth_sig_parity(bad, r, s, pub, &v));
    assert(v == 0xFFU);

    /* Another key (the generator itself, private key 1): refused. */
    uint8_t g65[65];
    assert(mbedtls_ecp_point_write_binary(&grp, &grp.G, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                          &olen, g65, sizeof(g65)) == 0);
    assert(!eth_sig_parity(hash, r, s, &g65[1], &v));

    /* Not on the curve: refused. */
    uint8_t junk[64];
    memcpy(junk, pub, 64);
    junk[63] ^= 0x01U;
    assert(!eth_sig_parity(hash, r, s, junk, &v));

    /* r = 0 and s = 0 are out of range. */
    uint8_t zero[32] = { 0 };
    assert(!eth_sig_parity(hash, zero, s, pub, &v));
    assert(!eth_sig_parity(hash, r, zero, pub, &v));

    mbedtls_mpi_free(&d);
    mbedtls_mpi_free(&sn);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
    printf("test_eth_sig OK\n");
    return 0;
}
