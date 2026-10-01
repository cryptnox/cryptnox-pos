/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file pay_evm.cpp
 * @ingroup app
 * @brief Ethereum and Polygon: endpoint, fees, pre-flight balance, sign and
 *        broadcast an EIP-1559 transfer.
 */

#include "pos_app.h"

/* ── Unsigned and signed tx buffers (EIP-1559 type 2) ─────────── */
#define TX_BUF_SIZE 300U

/**
 * @brief Point eth_rpc at the endpoint for the selected EVM network.
 *
 * Called at boot and at the top of every payment, since the network can change
 * between sales. URL, credentials and pinned cert are separate statics in
 * eth_rpc, so all three are re-applied each time (a stale cert or auth from the
 * other network shows up as an unexplained TLS failure).
 */
void eth_rpc_select_for(bool polygon) {
    if (polygon) {
        eth_rpc_init(settings_net_str(POLY_RPC_URL, POLY_RPC_URL_MAIN),
                     "0x" ADDR_FROM);
        eth_rpc_set_auth(NULL, NULL);
#ifdef POLY_CA_CERT_PEM
        eth_rpc_set_ca_cert(POLY_CA_CERT_PEM);
#else
        eth_rpc_set_ca_cert(NULL);
#endif
    } else {
        eth_rpc_init(settings_net_str(RPC_URL, RPC_URL_MAIN), "0x" ADDR_FROM);
#if defined(RPC_PROJECT_ID) && defined(RPC_API_SECRET)
        eth_rpc_set_auth(RPC_PROJECT_ID, RPC_API_SECRET);
#else
        eth_rpc_set_auth(NULL, NULL);
#endif
#ifdef RPC_CA_CERT_PEM
        eth_rpc_set_ca_cert(RPC_CA_CERT_PEM);
#else
        eth_rpc_set_ca_cert(NULL);
#endif
    }
}

void eth_rpc_select(void) { eth_rpc_select_for(chain_is_polygon()); }

/**
 * @brief The EIP-1559 fees one EVM sale will offer, in wei per gas.
 *
 * One function so the signed transaction and the pre-flight check agree: a
 * check against a cheaper fee than the tx carries would pass the very sale it
 * exists to catch.
 *
 * @param[in]  polygon  true on Polygon, which has a tip floor of its own.
 * @param[out] max_fee  Fee cap, wei per gas.
 * @param[out] prio_fee Tip, wei per gas; never above @p max_fee.
 */
void evm_fees_wei(bool polygon, uint64_t *max_fee, uint64_t *prio_fee)
{
    /* Fees from settings (config.h defaults); the Gwei-to-wei arithmetic, tip
     * clamp and Polygon floor are evm_fees_from_gwei in money.h. */
    evm_fees_from_gwei(settings_get_max_fee_gwei(),
                       settings_get_priority_fee_gwei(), polygon,
                       (uint32_t)POLY_MIN_PRIORITY_FEE_GWEI, max_fee, prio_fee);
}

/**
 * @brief Refuse an EVM sale the tapped card cannot fund, before it signs.
 *
 * Otherwise an underfunded token transfer is broadcast, mined, reverted and
 * charged gas, and a short coin balance is refused only after signing.
 *
 * **The caller must have selected the endpoint and set the payer first.** This
 * does not call @ref eth_rpc_select: that resets the from-address to config.h
 * and would check the integrator's account instead of the tapped card.
 *
 * A failed read is NOT a refusal: this improves a message, it does not gate
 * payments.
 *
 * @param[in]  amount  The reconciled sale amount, in keypad base units.
 * @param[out] err     Panel-facing reason on refusal; untouched otherwise.
 * @param[in]  err_max Capacity of @p err.
 * @return true to let the sale proceed (funded, or unknowable).
 */
bool evm_balance_ok(const pos_amount_t *amount, char *err, size_t err_max)
{
    const bool  native  = chain_is_native_evm();
    const bool  polygon = chain_is_polygon();
    const char *coin    = polygon ? "POL" : "ETH";

    /* The sale's snapshot, not a fresh read — see s_sale_fee. */
    const uint64_t max_fee = s_sale_fee.max_fee;
    /* A gas limit is ~1e5 at the most and max_fee is a uint32 of Gwei scaled by
     * 1e9, so this product has ~25 bits of headroom. */
    const uint64_t gas_cost =
        (uint64_t)(native ? GAS_LIMIT_NATIVE : GAS_LIMIT_ERC20) * max_fee;

    uint64_t have_wei = 0U;
    if (!eth_rpc_get_balance(&have_wei)) {
        ESP_LOGW(TAG, "pre-flight: balance read failed - letting the sale run");
        return true;
    }

    /* The arithmetic, overflow guard included, is evm_funds_check in money.h. */
    const evm_funds_t funds =
        evm_funds_check(native, have_wei, gas_cost, amount->amount_minor);
    if (funds == EVM_FUNDS_SHORT_GAS) {
        (void)snprintf(err, err_max, "Not enough %s for the network fee", coin);
        return false;
    }

    if (native) {
        /* EVM_FUNDS_UNKNOWN is an amount past the keypad's cap; left to
         * sign_and_broadcast, which refuses it by name. */
        if (funds == EVM_FUNDS_SHORT_VALUE) {
            (void)snprintf(err, err_max, "Not enough %s for this amount", coin);
            return false;
        }
        return true;
    }

    const char *contract = active_token()->str;   /* not native: returned above */
    uint64_t    have_units = 0U;
    if (!eth_rpc_get_token_balance(contract, &have_units)) {
        ESP_LOGW(TAG, "pre-flight: token balance read failed - letting it run");
        return true;
    }
    if (have_units < amount->amount_minor) {
        (void)snprintf(err, err_max, "Not enough %s on the card",
                       pos_asset_of(settings_get_chain())->ticker);
        return false;
    }
    return true;
}

/**
 * @brief Sign an EVM token or coin transfer on the card and broadcast it.
 *
 * Pipeline: reconcile → calldata → card connect + PIN → payer address →
 * balance → nonce → RLP + keccak256 → reconcile → sign (cancellable) → local
 * parity check → broadcast. The PIN is scrubbed with @c CW_Utils::secure_wipe
 * right after signing; hash, signature and encoded txs via @c WipeGuard.
 *
 * @param[in]  wallet       Initialised wallet instance.
 * @param[in]  transport    PN532 transport, used for the cancellable
 *                          connect loop.
 * @param[in]  amount       Reconciled amount, keypad base units (6 decimals).
 * @param[in]  to           Reconciled recipient.
 * @param[in]  pin          Operator-entered card PIN (scrubbed after signing).
 * @param[in]  pin_chars    Number of PIN characters in @p pin.
 * @param[out] fl           Filled once signed: the locally computed hash and
 *                          what its receipt must show.
 * @param[out] err_out      Short UI-facing error message on failure.
 * @param[in]  err_max      Capacity of @p err_out.
 * @return BCAST_SENT, BCAST_UNKNOWN (no answer — poll @p fl), or BCAST_FAILED
 *         on refusal or user cancel (err_out is only meaningful when
 *         @ref s_user_cancelled is clear).
 */
bcast_t sign_and_broadcast(CryptnoxWallet &wallet,
                                   Pn532NfcTransport &transport,
                                   const pos_amount_t *amount,
                                   const pos_addr_t *to,
                                   const char *pin, size_t pin_chars,
                                   inflight_t *fl,
                                   char *err_out, size_t err_max)
{
    /* The asset, read once so what is signed is what the confirm screen showed.
     * `native`: recipient in `to`, amount in `value`, nothing is called. */
    const bool native = chain_is_native_evm();
    const token_t *const tk = native ? NULL : active_token();
    if ((tk != NULL) && !tk->ok) {
        (void)snprintf(err_out, err_max, "Token contract not configured");
        return BCAST_FAILED;
    }
    const pos_addr_t *const token = (tk != NULL) ? &tk->addr : NULL;
    const bool polygon = chain_is_polygon();

    /* Endpoint first: the nonce below has to come from the network this will be
     * broadcast to, and the chain id signed into the transaction is what stops it
     * being replayed on the other one. */
    eth_rpc_select();

    /* Reconcile amount + recipient + contract right before they enter the calldata
     * (§3.2/§7.1); a mismatch means the working copy was corrupted. A native
     * transfer has no contract to reconcile — the recipient is the whole of it,
     * which is why `to` is checked on both paths. */
    if (!IS_TRUE32(amount_consistent(amount)) ||
        !IS_TRUE32(address_consistent(to)) ||
        ((token != NULL) && !IS_TRUE32(address_consistent(token)))) {
        pos_handle_anomaly("pre-calldata reconcile");
        (void)snprintf(err_out, err_max, "Integrity check failed");
        return BCAST_FAILED;
    }
    const uint64_t amount_units = amount->amount_minor;

    /* 6-decimal keypad units -> wei, for the 18-decimal coins only. Re-checked
     * here rather than trusted from the keypad's cap: a wrapped multiply signs a
     * value nobody entered (see POS_AMOUNT_UNITS_MAX_NATIVE). */
    uint64_t native_wei = 0U;
    if (native && !evm_units_to_wei(amount_units, &native_wei)) {
        (void)snprintf(err_out, err_max, "Amount too large for this asset");
        return BCAST_FAILED;
    }

    uint8_t calldata[USDC_CALLDATA_LEN];
    if (!native) {
        build_usdc_calldata(calldata, to->addr, amount_units);
    }

    /* The card comes before the RPC: the payer is whoever taps, so the nonce,
     * balance and parity check all depend on the card's account. */
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

    /* PIN first: the key export needs a verified session, and only verifyPin
     * reports a mistyped PIN as such (the sign APDU returns a generic error). */
    if (!wallet.verifyPin(session, reinterpret_cast<const uint8_t *>(pin),
                          static_cast<uint8_t>(pin_chars))) {
        (void)snprintf(err_out, err_max, "%s", pin_fail_text(transport, "Wrong PIN"));
        wallet.disconnect(session);
        return BCAST_FAILED;
    }

    /* The payer, derived from the card on the reader (ADDR_FROM is only the
     * boot-time probe's address): keccak256 of the uncompressed public key,
     * low 20 bytes. */
    char from_addr[SETTINGS_PAYOUT_MAX] = "";
    /* Kept past the address derivation: the recovery bit below is worked out
     * against this key. */
    uint8_t pubkey[64];
    WipeGuard g_pub(pubkey, sizeof(pubkey));
    {
        uint8_t key_hash[32];
        WipeGuard g_kh(key_hash, sizeof(key_hash));
        if (!wallet.getPublicKey(session, ETH_DERIVE_PATH,
                                 static_cast<uint8_t>(sizeof(ETH_DERIVE_PATH)),
                                 pubkey)) {
            wallet.disconnect(session);
            (void)snprintf(err_out, err_max, "Cannot read card address");
            return BCAST_FAILED;
        }
        keccak256(pubkey, sizeof(pubkey), key_hash);
        (void)eth_addr_format(&key_hash[12], from_addr, sizeof(from_addr));
    }
    /* Refused rather than ignored: leaving the previous payer in force would
     * fetch somebody else's nonce and sign a transaction against it. */
    if (!eth_rpc_set_from(from_addr)) {
        wallet.disconnect(session);
        (void)snprintf(err_out, err_max, "Cannot read card address");
        return BCAST_FAILED;
    }
    ESP_LOGI(TAG, "EVM sender (this card): %s", from_addr);

    /* Before the nonce, signature and broadcast: nothing is spent on a no. */
    ui_set_tx_info("Checking balance");
    if (!evm_balance_ok(amount, err_out, err_max)) {
        wallet.disconnect(session);
        return BCAST_FAILED;
    }
    ui_set_tx_info(NULL);

    uint64_t nonce = 0U;
    if (!eth_rpc_get_nonce(&nonce)) {
        wallet.disconnect(session);
        (void)snprintf(err_out, err_max, "RPC: get nonce failed");
        return BCAST_FAILED;
    }

    eth_tx_t tx;
    CW_Utils::secure_wipe(reinterpret_cast<uint8_t *>(&tx), sizeof(tx));
    /* Family from the selected chain, deployment from the settings flag. The
     * chain id is the replay protection between mainnet and testnet, so it is
     * read from the same setting the endpoint above was. */
    tx.chain_id          = settings_get_mainnet()
                             ? (polygon ? CHAIN_ID_POLYGON : CHAIN_ID_MAINNET)
                             : (polygon ? CHAIN_ID_AMOY    : CHAIN_ID_SEPOLIA);
    tx.nonce             = nonce;
    /* The same snapshot the balance check and the confirm screen used. */
    tx.max_priority_fee  = s_sale_fee.prio_fee;
    tx.max_fee           = s_sale_fee.max_fee;
    /* Token: amount in calldata, `to` is the contract. Coin: amount in `value`,
     * `to` is the payee, flat 21000 gas. `to` decides who is paid, so it comes
     * from the reconciled store on both paths, never a literal or fresh NVS
     * read. Selected on `token` (not `native`) so the dereference is provably
     * guarded. */
    tx.gas_limit         = native ? GAS_LIMIT_NATIVE : GAS_LIMIT_ERC20;
    tx.eth_value         = native ? native_wei : 0U;
    tx.calldata          = native ? NULL : calldata;
    tx.calldata_len      = native ? 0U   : sizeof(calldata);
    (void)CW_Utils::safe_memcpy(tx.to, sizeof(tx.to),
                                (token != NULL) ? token->addr : to->addr,
                                ETH_ADDR_LEN);

    uint8_t unsigned_tx[TX_BUF_SIZE];
    size_t  unsigned_len = eth_rlp_encode_unsigned(&tx, unsigned_tx, sizeof(unsigned_tx));
    if (unsigned_len == 0U) {
        wallet.disconnect(session);
        (void)snprintf(err_out, err_max, "RLP encode overflow");
        return BCAST_FAILED;
    }

    uint8_t hash[CW_HASH_SIZE];
    keccak256(unsigned_tx, unsigned_len, hash);

    /* From here the message hash and (soon) the signature live on the stack.
     * Scrub them and the encoded transactions on every exit path below. */
    WipeGuard g_unsigned(unsigned_tx, sizeof(unsigned_tx));
    WipeGuard g_hash(hash, sizeof(hash));

    /* Re-reconcile amount + recipient + contract immediately before card_sign():
     * the last point before the card produces an irreversible signature over
     * the calldata (§3.2/§7.1). */
    if (!IS_TRUE32(amount_consistent(amount)) ||
        !IS_TRUE32(address_consistent(to)) ||
        ((token != NULL) && !IS_TRUE32(address_consistent(token)))) {
        pos_handle_anomaly("pre-sign reconcile");
        (void)snprintf(err_out, err_max, "Integrity check failed");
        wallet.disconnect(session);
        return BCAST_FAILED;
    }

    uint8_t rs[64];
    WipeGuard g_rs(rs, sizeof(rs));
    if (!card_sign(wallet, session, hash, static_cast<uint8_t>(CW_HASH_SIZE),
                   ETH_DERIVE_PATH,
                   static_cast<uint8_t>(sizeof(ETH_DERIVE_PATH)),
                   pin, pin_chars, rs, err_out, err_max)) {
        return BCAST_FAILED;
    }
    const uint8_t *sig_r = rs;
    const uint8_t *sig_s = rs + 32U;

    if (!s_user_cancelled) {
        ui_show_tx_status(UI_TX_STATE_SENDING, NULL);
    }

    /* The recovery bit, worked out locally against the key the card just
     * exported rather than trusting the node. Doubles as a signature check: a
     * failure is an internal inconsistency, not an operator setup error. */
    uint8_t v = 0U;
    if (!eth_sig_parity(hash, sig_r, sig_s, pubkey, &v)) {
        ESP_LOGE(TAG, "signature does not verify under the card's own key");
        (void)snprintf(err_out, err_max, "Signature check failed");
        return BCAST_FAILED;
    }

    uint8_t signed_tx[TX_BUF_SIZE];
    WipeGuard g_signed(signed_tx, sizeof(signed_tx));
    size_t  signed_len = eth_rlp_encode_signed(&tx, v, sig_r, sig_s,
                                               signed_tx, sizeof(signed_tx));
    if (signed_len == 0U) {
        (void)snprintf(err_out, err_max, "RLP signed overflow");
        return BCAST_FAILED;
    }

    /* last cancel check right before the irreversible broadcast. */
    if (s_user_cancelled) {
        return BCAST_FAILED;
    }

    /* The hash is computed here from the signed bytes, before the broadcast, so
     * it exists even if the node never answers, and is never taken from the
     * node, which could name any other transaction. The receipt poll asks
     * about this hash and the receipt must match it. */
    {
        uint8_t h[32];
        keccak256(signed_tx, signed_len, h);
        fl->hash[0] = '0';
        fl->hash[1] = 'x';
        for (size_t i = 0U; i < sizeof(h); i++) {
            (void)snprintf(&fl->hash[2U + (2U * i)], 3U, "%02x", h[i]);
        }
    }
    fl->tron  = false;
    fl->token = (token != NULL);
    (void)CW_Utils::safe_memcpy(fl->to, sizeof(fl->to),
                                (token != NULL) ? token->addr : to->addr,
                                ETH_ADDR_LEN);
    (void)CW_Utils::safe_memcpy(fl->payee, sizeof(fl->payee), to->addr,
                                ETH_ADDR_LEN);
    fl->amount  = amount_units;
    fl->polygon = polygon;
    inflight_persist(fl);

    /* Roomier than err_out so the node's sentence arrives whole and is clipped
     * once, at the point that knows the panel's width. */
    char node_err[128] = "";
    char node_hash[72] = "";
    if (!eth_rpc_send_raw_tx(signed_tx, signed_len, node_hash, sizeof(node_hash),
                             node_err, sizeof(node_err))) {
        if (eth_rpc_err_already_known(node_err)) {
            /* An earlier attempt at these exact bytes got through. */
            ESP_LOGW(TAG, "node already has %s", fl->hash);
            return BCAST_SENT;
        }
        if (node_err[0] == '\0') {
            /* No refusal, just no answer: a timeout, a dropped TLS session, a
             * body that was not JSON. The node may well have taken it. */
            ESP_LOGW(TAG, "no answer to the broadcast - polling %s", fl->hash);
            return BCAST_UNKNOWN;
        }
        ESP_LOGE(TAG, "broadcast refused: %s", node_err);
        rpc_error_text(node_err, native, polygon, err_out, err_max);
        return BCAST_FAILED;
    }
    if (strcasecmp(node_hash, fl->hash) != 0) {
        /* Not fatal (ours is the one polled), but it is why the receipt is
         * checked against the transfer itself. */
        ESP_LOGE(TAG, "node answered hash %s for %s", node_hash, fl->hash);
    }
    return BCAST_SENT;
}
