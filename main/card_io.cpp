/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file card_io.cpp
 * @ingroup app
 * @brief The card on the reader: is it usable, open a channel, sign, read the
 *        payout addresses off it.
 */

#include "pos_app.h"

/* Why the card just tapped was refused, or NULL. Set by card_connect(), read by
 * its callers in place of a generic "Card not found". Plain static: written and
 * read only on the main task. */
const char *s_card_fault = NULL;

/**
 * @brief Whether the card now on the reader is set up at all.
 *
 * Runs before the secure channel because the SELECT response says so in the
 * clear; later, an uninitialised card reads as "Wrong card PIN" and a seedless
 * one as a sign error. A second SELECT (establishSecureChannel) is harmless.
 *
 * @return NULL if the card is usable, or if this cannot tell; else the line to
 *         show. See card_status.h.
 */
static const char *card_fault(Pn532NfcTransport &transport)
{
    static const uint8_t SELECT[] = CARD_SELECT_APDU;
    uint8_t r[40];
    uint8_t n = static_cast<uint8_t>(sizeof(r));
    if (!transport.sendAPDU(SELECT, static_cast<uint8_t>(sizeof(SELECT)), r, n)) {
        return NULL;   /* card gone or not answering — the caller's own story */
    }
    const card_state_t st = card_state(r, n);
    if (st != CARD_READY) {
        ESP_LOGW(TAG, "card refused: state %d", static_cast<int>(st));
    }
    return card_state_text(st);
}

/**
 * @brief Why verifyPin said no: the PIN, or the card leaving the field.
 *
 * verifyPin returns a bare bool, and a card pulled away mid-APDU fails exactly
 * like a mistyped PIN. Asked before the session is dropped: if the card does not
 * answer a SELECT, it was not the PIN.
 */
const char *pin_fail_text(Pn532NfcTransport &transport, const char *wrong)
{
    static const uint8_t SELECT[] = CARD_SELECT_APDU;
    uint8_t r[40];
    uint8_t n = static_cast<uint8_t>(sizeof(r));
    if (!transport.sendAPDU(SELECT, static_cast<uint8_t>(sizeof(SELECT)), r, n)) {
        ESP_LOGW(TAG, "verifyPin failed and the card is gone - not a PIN error");
        return "Card moved - tap it again";
    }
    return wrong;
}

/**
 * @brief Wait for a card and open a secure channel, cancellable from the UI.
 *
 * Manual connect loop with cancel checks between PN532 polls, so a Cancel aborts
 * within one PN532 timeout. Drives the "Tap your card" / "Processing" screens.
 *
 * @param[in]  wallet    Initialised wallet instance.
 * @param[in]  transport PN532 transport, polled directly.
 * @param[out] session   Open secure session on success.
 * @param[in]  setup     true when reading payout addresses rather than paying;
 *                       shows the card-wait screen instead of the transaction one.
 * @return true with @p session open; false on user cancel, after 60 s, or on a
 *         card that is not set up — the three are told apart by
 *         @ref s_user_cancelled and @ref s_card_fault.
 */
bool card_connect(CryptnoxWallet &wallet, Pn532NfcTransport &transport,
                         CW_SecureSession &session, bool setup)
{
    s_card_fault = NULL;
    if (!s_user_cancelled) {
        if (setup) { ui_show_card_wait("Reading your payout addresses"); }
        else { ui_show_tx_status(UI_TX_STATE_PLACE_CARD, "Hold card to reader"); }
    }

    /* Start from a clean reader state: a previous attempt that ended with the
     * card ripped away mid-exchange can leave a stale target selected, which
     * would make every InListPassiveTarget below time out. */
    transport.resetReader();

    /* Give the user up to 60 s to present the card. */
    const int64_t card_wait_us = 60LL * 1000000LL;
    const int64_t start_us     = esp_timer_get_time();
    while (true) {
        wdt_feed();   /* a minute of waiting for a card is on purpose */
        if (s_user_cancelled) {
            return false;
        }
        if ((esp_timer_get_time() - start_us) > card_wait_us) {
            return false;   /* timed out waiting for the card */
        }
        if (transport.inListPassiveTarget()) {
            /* Card tapped — show immediate feedback while the secure channel
             * comes up. */
            if (setup) { ui_show_card_wait("Reading..."); }
            else { ui_show_tx_status(UI_TX_STATE_PROCESSING, NULL); }
            vTaskDelay(pdMS_TO_TICKS(200));
            /* Before the channel: an uninitialised card would otherwise fail at
             * the PIN and send the holder off to retype it. */
            s_card_fault = card_fault(transport);
            if (s_card_fault != NULL) {
                transport.resetReader();
                return false;
            }
            if (wallet.establishSecureChannel(session)) {
                return true;
            }
            /* Card pulled away or channel failed: release the dead target, or
             * every following InListPassiveTarget times out. */
            transport.resetReader();
            if (!s_user_cancelled) {
                if (setup) { ui_show_card_wait("Hold it still"); }
                else { ui_show_tx_status(UI_TX_STATE_PLACE_CARD, "Hold card to reader"); }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/**
 * @brief Have the card sign @p hash, then close the session.
 *
 * Shared tail of both payment paths: build the request, copy the PIN in as late
 * as possible, sign, scrub the PIN, close the session, map the status byte to an
 * operator-readable message. One copy of the PIN handling and wipe.
 *
 * The caller reconciles its amount and addresses immediately before calling this,
 * which makes that check the last thing before an irreversible signature. It
 * stays with the caller because the anomaly label and values differ per path.
 *
 * The session is disconnected on every exit — including the failures — so no
 * caller can leave a card held open.
 *
 * @param[in]  wallet     Initialised wallet instance.
 * @param[in]  session    Open secure session; disconnected before returning.
 * @param[in]  hash       Digest to sign (the keccak of the RLP, or a Tron txID).
 * @param[in]  hash_len   Length of @p hash.
 * @param[in]  path       BIP-32 derivation path blob.
 * @param[in]  path_len   Length of @p path.
 * @param[in]  pin        Operator-entered card PIN; the caller still owns and
 *                        scrubs its own copy.
 * @param[in]  pin_chars  Number of PIN characters in @p pin.
 * @param[out] rs_out     64 bytes: r || s. Wiped by the caller (WipeGuard).
 * @param[out] err_out    Short UI-facing error message on failure.
 * @param[in]  err_max    Capacity of @p err_out.
 * @return true when @p rs_out holds a signature.
 */
bool card_sign(CryptnoxWallet &wallet, CW_SecureSession &session,
                      const uint8_t *hash, uint8_t hash_len,
                      const uint8_t *path, uint8_t path_len,
                      const char *pin, size_t pin_chars,
                      uint8_t rs_out[64], char *err_out, size_t err_max)
{
    CW_SignRequest req(session,
                       CW_SIGN_DERIVE_K1,
                       CW_SIGN_SIG_ECDSA_LOW_S,
                       CW_SIGN_WITH_PIN);
    req.hash             = hash;
    req.hashLength       = hash_len;
    req.derivePath       = path;
    req.derivePathLength = path_len;

    /* Copy the operator-entered PIN into the request as late as possible;
     * req.pin is zero-initialised by the CW_SignRequest constructor. */
    const size_t copy_len = (pin_chars < CW_MAX_PIN_LENGTH) ? pin_chars
                                                            : CW_MAX_PIN_LENGTH;
    (void)CW_Utils::safe_memcpy(req.pin, sizeof(req.pin),
                                reinterpret_cast<const uint8_t *>(pin),
                                copy_len);

    CW_SignResult result = wallet.sign(req);
    WipeGuard g_sig(result.signature, sizeof(result.signature));
    wallet.disconnect(session);

    /* scrub the PIN immediately after use (secure_wipe is not
     * dead-store-eliminated); ~CW_SignRequest wipes it again as a backstop. */
    CW_Utils::secure_wipe(req.pin, sizeof(req.pin));

    if (result.errorCode != CW_OK) {
        /* SIGN carries the PIN (CW_SIGN_WITH_PIN), so a mistyped one arrives
         * here as a status byte; name it rather than show "Sign error 0x82". */
        if (result.errorCode == CW_SIGN_PIN_INCORRECT) {
            (void)snprintf(err_out, err_max, "Wrong PIN");
        } else {
            (void)snprintf(err_out, err_max, "Sign error 0x%02X",
                           static_cast<unsigned int>(result.errorCode));
        }
        return false;
    }

    (void)CW_Utils::safe_memcpy(rs_out, 32U,
                                result.signature + CW_SIG_R_OFFSET, 32U);
    (void)CW_Utils::safe_memcpy(rs_out + 32U, 32U,
                                result.signature + CW_SIG_S_OFFSET, 32U);
    return true;
}

/**
 * @brief Read the card's payout addresses and put them through the panel.
 *
 * The card will not export a public key without a verified PIN, so this needs the
 * card PIN exactly as signing does — the caller collects it on the keypad first.
 *
 * One tap yields both addresses (Ethereum m/44'/60'/0'/0/0, Tron
 * m/44'/195'/0'/0/0), so a terminal is not left half configured, offering one
 * network's payments to the compiled-in address.
 *
 * Neither address is stored here. Both are *proposed*, through the same
 * accept-on-the-panel handshake a browser submission goes through, because "the
 * card said so" is not the same claim as "the operator checked it" — a card
 * presented by a customer would otherwise redirect the takings.
 *
 * @param[out] eth_out  EIP-55 "0x..." address, or "" if it could not be read.
 * @param[in]  eth_n    Capacity of @p eth_out.
 * @param[out] tron_out base58 "T..." address, or "".
 * @param[in]  tron_n   Capacity of @p tron_out.
 * @param[out] err      Short reason for the panel when nothing could be read.
 * @param[in]  err_n    Capacity of @p err.
 * @return true if at least one address was read.
 */
bool card_read_payouts(CryptnoxWallet &wallet,
                              Pn532NfcTransport &transport,
                              CW_CryptoProvider &crypto,
                              const char *pin, size_t pin_chars,
                              char *eth_out, size_t eth_n,
                              char *tron_out, size_t tron_n,
                              char *err, size_t err_n)
{
    eth_out[0]  = '\0';
    tron_out[0] = '\0';

    CW_SecureSession session;
    /* setup = true: same 60-second cancellable poll, but the card-wait screen
     * rather than the transaction one. Nothing is being paid here. */
    if (!card_connect(wallet, transport, session, true)) {
        (void)snprintf(err, err_n, "%s",
                       s_user_cancelled           ? "Cancelled" :
                       (s_card_fault != NULL)     ? s_card_fault
                                                  : "Card not found");
        return false;
    }

    if (!wallet.verifyPin(session, reinterpret_cast<const uint8_t *>(pin),
                          static_cast<uint8_t>(pin_chars))) {
        (void)snprintf(err, err_n, "%s", pin_fail_text(transport, "Wrong card PIN"));
        wallet.disconnect(session);
        return false;
    }

    uint8_t pubkey[64];
    WipeGuard g_pub(pubkey, sizeof(pubkey));

    /* Ethereum: keccak256 of the uncompressed public key, low 20 bytes, rendered
     * checksummed so it matches what the operator's own wallet shows. */
    if (wallet.getPublicKey(session, ETH_DERIVE_PATH,
                            static_cast<uint8_t>(sizeof(ETH_DERIVE_PATH)),
                            pubkey)) {
        uint8_t h[32];
        keccak256(pubkey, sizeof(pubkey), h);
        (void)eth_addr_format(&h[12], eth_out, eth_n);
        CW_Utils::secure_wipe(h, sizeof(h));
    } else {
        ESP_LOGW(TAG, "card would not export its Ethereum key");
    }

    uint8_t addr21[CW_TRON_ADDRESS_BYTES];
    if (wallet.getPublicKey(session, CW_TRON_DERIVE_PATH,
                            CW_TRON_PATH_LENGTH, pubkey) &&
        CW_Tron::addressBytesFromPublicKey(pubkey, addr21)) {
        (void)CW_Tron::encodeAddress(addr21, crypto, tron_out, tron_n);
    } else {
        ESP_LOGW(TAG, "card would not export its Tron key");
    }

    wallet.disconnect(session);

    if ((eth_out[0] == '\0') && (tron_out[0] == '\0')) {
        (void)snprintf(err, err_n, "Card gave no usable address");
        return false;
    }
    ESP_LOGI(TAG, "card addresses: eth=%s tron=%s", eth_out, tron_out);
    return true;
}
