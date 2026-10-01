/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file pay_tron.cpp
 * @ingroup app
 * @brief Tron: pre-flight balance, sign and broadcast a TRX or TRC-20 transfer.
 *
 * The sender is not configured: the card's m/44'/195'/0'/0/0 public key is read
 * over the secure channel at sign time and turned into an address by CW_Tron, so
 * the terminal follows whichever card is presented.
 */

#include "pos_app.h"

/**
 * @brief Format a raw 21-byte Tron address as the "41..." hex the API wants.
 *
 * @param[in]  addr21 21-byte address (0x41 prefix included).
 * @param[out] out    #TRON_ADDR_HEX_LEN chars + NUL.
 * @param[in]  n      Capacity of @p out.
 */
void tron_addr_to_hex(const uint8_t *addr21, char *out, size_t n)
{
    if (n < (TRON_ADDR_HEX_LEN + 1U)) { if (n > 0U) { out[0] = '\0'; } return; }
    for (size_t i = 0U; i < CW_TRON_ADDRESS_BYTES; i++) {
        (void)snprintf(&out[i * 2U], 3U, "%02x",
                       static_cast<unsigned>(addr21[i]));
    }
}

/**
 * @brief Refuse a Tron sale the tapped card cannot fund, before it signs.
 *
 * The Tron half of @ref evm_balance_ok, run once the card is read and its PIN
 * verified, before signing. Matters mostly for TRC-20: the node builds a
 * TriggerSmartContract regardless of balance, so an underfunded one would be
 * signed, broadcast, reverted and charged for.
 *
 * A read that fails is not a refusal, as on the EVM side.
 *
 * @param[in]  owner_hex  The tapped card's address, "41"-prefixed hex.
 * @param[in]  token_hex  Token contract, same form; NULL/empty for native TRX.
 * @param[in]  amount     Sale amount — sun for TRX, base units for a token.
 * @param[out] err        Panel-facing reason on refusal; untouched otherwise.
 * @param[in]  err_max    Capacity of @p err.
 * @return true to let the sale proceed (funded, or unknowable).
 */
static bool tron_balance_ok(const char *owner_hex, const char *token_hex,
                            uint64_t amount, char *err, size_t err_max)
{
    const bool is_token = (token_hex != NULL) && (token_hex[0] != '\0');

    uint64_t trx_sun = 0U;
    if (!tron_rpc_get_balance(owner_hex, &trx_sun)) {
        ESP_LOGW(TAG, "pre-flight: TRX balance read failed - letting it run");
        return true;
    }

    if (!is_token) {
        /* A TRX transfer pays its amount out of this balance, and its bandwidth
         * out of the free daily allowance — so the balance is the whole test. */
        if (trx_sun < amount) {
            (void)snprintf(err, err_max, "Not enough TRX for this amount");
            return false;
        }
        return true;
    }

    uint64_t have = 0U;
    if (!tron_rpc_get_trc20_balance(owner_hex, token_hex, &have)) {
        ESP_LOGW(TAG, "pre-flight: token balance read failed - letting it run");
    } else if (have < amount) {
        (void)snprintf(err, err_max, "Not enough %s on the card",
                       pos_asset_of(settings_get_chain())->ticker);
        return false;
    } else {
        /* enough tokens — the fee is the remaining question */
    }

    /* The fee: TRC-20 burns energy from a stake or TRX, so the only certain
     * refusal is an account with neither. Pricing the burn is left out: getting
     * it wrong would refuse sales that would have settled.
     *
     * ponytail: zero-or-not. Price the energy properly if a thin-but-nonzero
     * balance turns out to be a real support case. */
    if (trx_sun == 0U) {
        uint64_t energy = 0U;
        if (tron_rpc_get_energy(owner_hex, &energy) && (energy == 0U)) {
            (void)snprintf(err, err_max, "No TRX for the network fee");
            return false;
        }
    }
    return true;
}

/**
 * @brief Sign a Tron transfer on the card and broadcast it — TRX or TRC-20.
 *
 * Same shape as @ref sign_and_broadcast, but Tron has no RLP and no local
 * nonce: the full node serialises the transaction and we sign its txID. What
 * the node returns is therefore verified before the card ever sees the hash
 * (see tron_rpc.h), and the recipient handed to the node is derived from the
 * dual-stored @p to right after the reconcile, never from a config literal.
 *
 * TRX and TRC-20 differ only in which transaction the node builds, so they share
 * one function and the security checks cannot drift apart.
 *
 * @param[in]  wallet       Initialised wallet instance.
 * @param[in]  transport    PN532 transport (cancellable connect loop).
 * @param[in]  amount       Dual-stored amount, 6 decimals — sun for TRX, token
 *                          base units for TRC-20.
 * @param[in]  to           Dual-stored recipient (20-byte key hash).
 * @param[in]  token        Token to charge in, or NULL for native TRX.
 * @param[in]  pin          Operator-entered card PIN (scrubbed after signing).
 * @param[in]  pin_chars    Number of PIN characters in @p pin.
 * @param[out] fl           Filled once built: the txID and its expiration.
 * @param[out] err_out      Short UI-facing error message on failure.
 * @param[in]  err_max      Capacity of @p err_out.
 * @return BCAST_SENT, BCAST_UNKNOWN (broadcast not confirmed — poll @p fl
 *         until it expires), or BCAST_FAILED on refusal or user cancel.
 */
bcast_t sign_and_broadcast_tron(CryptnoxWallet &wallet,
                                        Pn532NfcTransport &transport,
                                        CW_CryptoProvider &crypto,
                                        const pos_amount_t *amount,
                                        const pos_addr_t *to,
                                        const token_t *token,
                                        const char *pin, size_t pin_chars,
                                        inflight_t *fl,
                                        char *err_out, size_t err_max)
{
    if (!IS_TRUE32(amount_consistent(amount)) ||
        !IS_TRUE32(address_consistent(to)) ||
        ((token != NULL) && !IS_TRUE32(address_consistent(&token->addr)))) {
        pos_handle_anomaly("pre-create reconcile (tron)");
        (void)snprintf(err_out, err_max, "Integrity check failed");
        return BCAST_FAILED;
    }
    if ((token != NULL) && !token->ok) {
        (void)snprintf(err_out, err_max, "Token contract not configured");
        return BCAST_FAILED;
    }
    const uint64_t amount_sun = amount->amount_minor;   /* both 6 decimals */

    /* Recipient in Tron form, built from the reconciled copy. */
    uint8_t to21[CW_TRON_ADDRESS_BYTES];
    to21[0] = CW_TRON_ADDRESS_PREFIX;
    (void)CW_Utils::safe_memcpy(&to21[1], sizeof(to21) - 1U,
                                to->addr, ETH_ADDR_LEN);
    char to_hex[TRON_ADDR_HEX_LEN + 1U];
    tron_addr_to_hex(to21, to_hex, sizeof(to_hex));

    /* Same for the token contract — the node is told which contract to call,
     * so that address has to come from the reconciled store too. */
    char token_hex[TRON_ADDR_HEX_LEN + 1U] = { 0 };
    if (token != NULL) {
        uint8_t c21[CW_TRON_ADDRESS_BYTES];
        c21[0] = CW_TRON_ADDRESS_PREFIX;
        (void)CW_Utils::safe_memcpy(&c21[1], sizeof(c21) - 1U,
                                    token->addr.addr, ETH_ADDR_LEN);
        tron_addr_to_hex(c21, token_hex, sizeof(token_hex));
    }

    /* The card comes before the RPC on this path: Tron will not serialise a
     * transfer without the sender, and the sender is whoever tapped. */
    CW_SecureSession session;
    if (!card_connect(wallet, transport, session)) {
        if (!s_user_cancelled) {
            (void)snprintf(err_out, err_max, "%s",
                           (s_card_fault != NULL) ? s_card_fault
                                                  : "Card not found");
        }
        return BCAST_FAILED;
    }

    if (!s_user_cancelled) {
        ui_show_tx_status(UI_TX_STATE_SIGNING, NULL);
    }

    /* Read this card's Tron account. The export needs the PIN verified for the
     * session first; the same PIN then rides along with the SIGN APDU. */
    char    owner_hex[TRON_ADDR_HEX_LEN + 1] = { 0 };
    char    owner_b58[CW_TRON_ADDRESS_STR_SIZE] = { 0 };
    uint8_t pubkey[64];
    uint8_t owner21[CW_TRON_ADDRESS_BYTES];
    WipeGuard g_pub(pubkey, sizeof(pubkey));
    if (!wallet.verifyPin(session,
                          reinterpret_cast<const uint8_t *>(pin),
                          static_cast<uint8_t>(pin_chars))) {
        (void)snprintf(err_out, err_max, "%s", pin_fail_text(transport, "Wrong PIN"));
        wallet.disconnect(session);
        return BCAST_FAILED;
    }
    if (!wallet.getPublicKey(session, CW_TRON_DERIVE_PATH,
                             CW_TRON_PATH_LENGTH, pubkey) ||
        !CW_Tron::addressBytesFromPublicKey(pubkey, owner21) ||
        !CW_Tron::encodeAddress(owner21, crypto,
                                owner_b58, sizeof(owner_b58))) {
        wallet.disconnect(session);
        (void)snprintf(err_out, err_max, "Cannot read card address");
        return BCAST_FAILED;
    }
    tron_addr_to_hex(owner21, owner_hex, sizeof(owner_hex));
    ESP_LOGI(TAG, "Tron sender (this card): %s", owner_b58);

    /* Before create, signature and broadcast: nothing is spent on a no. */
    if (!tron_balance_ok(owner_hex, (token != NULL) ? token_hex : NULL,
                         amount_sun, err_out, err_max)) {
        wallet.disconnect(session);
        return BCAST_FAILED;
    }

    tron_tx_ctx_t tx;
    const bool built = (token == NULL)
        ? tron_rpc_create_transfer(owner_hex, to_hex, amount_sun, &tx)
        : tron_rpc_create_trc20_transfer(owner_hex, token_hex, to_hex,
                                         amount_sun, TRON_TRC20_FEE_LIMIT_SUN,
                                         &tx);
    if (!built) {
        wallet.disconnect(session);
        /* Most often an account the chain has never seen funded: Tron will not
         * build a transfer from it, and a TRC-20 call additionally needs TRX
         * for the energy the transfer burns. */
        (void)snprintf(err_out, err_max, "No %s on %.12s...",
                       (token == NULL) ? "TRX" : "funds/TRX", owner_b58);
        return BCAST_FAILED;
    }

    /* Last reconcile before the card produces an irreversible signature.
     * Immediately before card_sign(), which does nothing else first. */
    if (!IS_TRUE32(amount_consistent(amount)) ||
        !IS_TRUE32(address_consistent(to)) ||
        ((token != NULL) && !IS_TRUE32(address_consistent(&token->addr)))) {
        pos_handle_anomaly("pre-sign reconcile (tron)");
        (void)snprintf(err_out, err_max, "Integrity check failed");
        wallet.disconnect(session);
        return BCAST_FAILED;
    }

    uint8_t rs[64];
    WipeGuard g_rs(rs, sizeof(rs));
    if (!card_sign(wallet, session, tx.txid,
                   static_cast<uint8_t>(sizeof(tx.txid)),
                   CW_TRON_DERIVE_PATH, CW_TRON_PATH_LENGTH,
                   pin, pin_chars, rs, err_out, err_max)) {
        return BCAST_FAILED;
    }
    const uint8_t *sig_r = rs;
    const uint8_t *sig_s = rs + 32U;

    if (!s_user_cancelled) {
        ui_show_tx_status(UI_TX_STATE_SENDING, NULL);
    }

    /* Tron wants r || s || recovery id, and nothing on the card or in the API
     * tells us the id — so try 0 and fall back to 1, the same way the SDK's
     * TronSigning example does. A wrong id recovers to some other account, the
     * node answers SIGERROR and nothing is committed, so the retry is safe. The
     * two attempts carry identical raw_data, hence the identical txID: only one
     * transaction can ever exist. */
    uint8_t sig[65];
    WipeGuard g_sig65(sig, sizeof(sig));
    (void)CW_Utils::safe_memcpy(sig, sizeof(sig), sig_r, 32U);
    (void)CW_Utils::safe_memcpy(sig + 32U, sizeof(sig) - 32U, sig_s, 32U);

    /* last cancel check right before the irreversible broadcast. */
    if (s_user_cancelled) {
        return BCAST_FAILED;
    }

    /* Before the broadcast: the txID is fixed by the verified raw_data, and a
     * reset between the first attempt and its answer is what this record is for. */
    (void)snprintf(fl->hash, sizeof(fl->hash), "%s", tx.txid_hex);
    fl->tron          = true;
    fl->expiration_ms = tx.expiration_ms;
    inflight_persist(fl);

    bool sent = false;
    for (uint8_t v = 0U; (v < 2U) && !sent; v++) {
        sig[64] = v;
        sent = tron_rpc_broadcast(&tx, sig);
        if (!sent) {
            ESP_LOGW(TAG, "broadcast rejected with v=%u", static_cast<unsigned>(v));
        }
    }
    /* ~4 KB of locals on top of TLS in a 16 KB task: log the headroom. */
    ESP_LOGI(TAG, "main stack after Tron broadcast: %u bytes free",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(NULL)));

    /* "Refused" and "we never heard back" are not the same thing, and only the
     * chain can tell them apart. A v=0 broadcast that landed but whose response
     * was lost leaves this loop with sent == false, and the v=1 retry is then
     * refused as a duplicate. The txID is fixed by the verified raw_data, so
     * both attempts are the same transaction: the receipt poll settles it, and
     * waits past raw_data's expiration before calling it not sent — until then
     * the chain can still include it. */
    if (!sent) {
        ESP_LOGW(TAG, "broadcast not confirmed - polling %s until it expires",
                 tx.txid_hex);
        return BCAST_UNKNOWN;
    }
    return BCAST_SENT;
}

/** @brief Map a Tron receipt onto the Ethereum verdicts the UI flow uses. */
eth_rpc_receipt_result_t tron_receipt_as_eth(tron_receipt_t r)
{
    switch (r) {
        case TRON_RECEIPT_SUCCESS: return ETH_RPC_RECEIPT_SUCCESS;
        case TRON_RECEIPT_FAILED:  return ETH_RPC_RECEIPT_REVERTED;
        case TRON_RECEIPT_PENDING: return ETH_RPC_RECEIPT_PENDING;
        default:                   return ETH_RPC_RECEIPT_RPC_ERROR;
    }
}
