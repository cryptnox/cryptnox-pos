/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file eth_sig.cpp
 * @brief secp256k1 verification + recovery bit, on mbedTLS's ECP module.
 */

#include "eth_sig.h"

#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"

bool eth_sig_parity(const uint8_t hash[32], const uint8_t r[32],
                    const uint8_t s[32], const uint8_t pub64[64],
                    uint8_t *v_out)
{
    if ((hash == NULL) || (r == NULL) || (s == NULL) || (pub64 == NULL) ||
        (v_out == NULL)) {
        return false;
    }

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q, R;
    mbedtls_mpi       e, rr, ss, w, u1, u2, x;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_ecp_point_init(&R);
    mbedtls_mpi_init(&e);  mbedtls_mpi_init(&rr); mbedtls_mpi_init(&ss);
    mbedtls_mpi_init(&w);  mbedtls_mpi_init(&u1); mbedtls_mpi_init(&u2);
    mbedtls_mpi_init(&x);

    bool    ok = false;
    uint8_t q65[65];
    uint8_t r65[65];
    size_t  olen = 0U;

    q65[0] = 0x04U;
    for (size_t i = 0U; i < 64U; i++) { q65[i + 1U] = pub64[i]; }

    /* One pass, each step gated on the one before: any failure leaves ok false. */
    if ((mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1) == 0) &&
        (mbedtls_ecp_point_read_binary(&grp, &Q, q65, sizeof(q65)) == 0) &&
        (mbedtls_ecp_check_pubkey(&grp, &Q) == 0) &&
        (mbedtls_mpi_read_binary(&rr, r, 32U) == 0) &&
        (mbedtls_mpi_read_binary(&ss, s, 32U) == 0) &&
        /* 1 <= r, s < n */
        (mbedtls_mpi_cmp_int(&rr, 1) >= 0) && (mbedtls_mpi_cmp_mpi(&rr, &grp.N) < 0) &&
        (mbedtls_mpi_cmp_int(&ss, 1) >= 0) && (mbedtls_mpi_cmp_mpi(&ss, &grp.N) < 0) &&
        /* e = hash mod n; a 256-bit digest against a 256-bit order */
        (mbedtls_mpi_read_binary(&e, hash, 32U) == 0) &&
        (mbedtls_mpi_mod_mpi(&e, &e, &grp.N) == 0) &&
        /* w = s^-1, u1 = e*w, u2 = r*w (mod n) */
        (mbedtls_mpi_inv_mod(&w, &ss, &grp.N) == 0) &&
        (mbedtls_mpi_mul_mpi(&u1, &e, &w) == 0) &&
        (mbedtls_mpi_mod_mpi(&u1, &u1, &grp.N) == 0) &&
        (mbedtls_mpi_mul_mpi(&u2, &rr, &w) == 0) &&
        (mbedtls_mpi_mod_mpi(&u2, &u2, &grp.N) == 0) &&
        /* R = u1*G + u2*Q; fails on the point at infinity */
        (mbedtls_ecp_muladd(&grp, &R, &u1, &grp.G, &u2, &Q) == 0) &&
        (mbedtls_ecp_point_write_binary(&grp, &R, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                        &olen, r65, sizeof(r65)) == 0) &&
        (olen == sizeof(r65)) &&
        /* R.x itself must equal r: an R.x >= n would need recovery id 2 or 3,
         * which v cannot carry, so compare unreduced. */
        (mbedtls_mpi_read_binary(&x, &r65[1], 32U) == 0) &&
        (mbedtls_mpi_cmp_mpi(&x, &rr) == 0)) {
        *v_out = static_cast<uint8_t>(r65[64] & 1U);
        ok = true;
    }

    mbedtls_mpi_free(&e);  mbedtls_mpi_free(&rr); mbedtls_mpi_free(&ss);
    mbedtls_mpi_free(&w);  mbedtls_mpi_free(&u1); mbedtls_mpi_free(&u2);
    mbedtls_mpi_free(&x);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_point_free(&R);
    mbedtls_ecp_group_free(&grp);
    return ok;
}
