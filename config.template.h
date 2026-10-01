/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file config.template.h
 * @brief Build-time configuration template — copy to main/config.h and fill
 *        in (main/config.h is gitignored, NEVER commit it).
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* =========================
 * WiFi
 * ========================= */
/* Wi-Fi is NOT set here. It is chosen on the device during setup — from a browser,
 * out of a list the terminal scans for itself — and stored in NVS. No SSID or
 * password is baked into the firmware. See docs/config-portal.md. */

/* =========================
 * Ethereum / RPC
 * ========================= */
/**
 * Choose ONE provider below and comment out the other.
 *
 * Option A — PublicNode (free, no account required)
 *   Keep the lines under "Option A" as-is.
 *
 * Option B — Infura (requires a free account at app.infura.io)
 *   1. Create an API key in the Infura dashboard.
 *   2. In the key's Settings tab, reveal (or generate) the API Secret.
 *   3. Comment out Option A, uncomment the lines under "Option B" and
 *      fill in the values (the API Secret must have NO leading or
 *      trailing spaces).
 */

/* Not here: the panel clock's UTC offset. It is an operator setting, picked
 * from a list on the config page under "Clock" and stored in NVS — a fixed
 * offset rather than a timezone, because the DST rules that would make it
 * automatic cost 64 KB of the app slot (measured) and moving it twice a year
 * from a browser does not. */

/* --- Option A: PublicNode ----------------------------------------- */
#define RPC_HOST       "ethereum-sepolia-rpc.publicnode.com"
#define RPC_URL        "https://" RPC_HOST
/* No authentication needed — leave RPC_PROJECT_ID / RPC_API_SECRET
 * undefined (or commented out) when using PublicNode.               */

/* --- Option B: Infura --------------------------------------------- */
/* #define RPC_HOST        "sepolia.infura.io"                        */
/* #define RPC_PORT        443                                        */
/* #define RPC_PROJECT_ID  "<YOUR_INFURA_PROJECT_ID>"                 */
/* #define RPC_URL         "https://sepolia.infura.io/v3/" RPC_PROJECT_ID */
/* #define RPC_API_SECRET  "<YOUR_INFURA_API_SECRET>"                 */

/* --- Optional: pin the RPC TLS certificate ----------- */
/* By default the HTTPS chain is validated against the full Mozilla CA bundle
 * (~150 CAs, any of which could MITM). Define RPC_CA_CERT_PEM with the
 * endpoint's certificate (leaf, or — more stable across renewals — its issuing
 * CA) to trust ONLY that. Fetch it with:
 *   openssl s_client -connect ethereum-sepolia-rpc.publicnode.com:443 -showcerts </dev/null
 * then paste the relevant PEM block as a C string literal, e.g.:
 *   #define RPC_CA_CERT_PEM "-----BEGIN CERTIFICATE-----\n" \
 *                           "MIIB...\n" \
 *                           "-----END CERTIFICATE-----\n"
 * Note: pinning ties the build to that endpoint — update it if you change RPC.
 * It is applied on the production endpoint (RPC_URL_MAIN) too, so a pin has to
 * cover whichever network the terminal is switched to, or the mainnet side fails
 * TLS with nothing on screen to say why. */

/* The card PIN is NOT set here — the operator types it on the touchscreen
 * keypad at sign time, and it is scrubbed from RAM right after signing.
 * Nothing PIN-related is baked into the firmware. */

/* =========================
 * Ethereum Addresses
 * ========================= */
/* Sender address — the card's m/44'/60'/0'/0/0 key, lowercase hex, no 0x */
#define ADDR_FROM         "<SENDER_ADDRESS>"

/* FALLBACK recipient address — where transfers go until an operator sets one on
 * the device (no 0x). Setup asks for the real one and stores it in NVS, either
 * typed in a browser or read off the operator's Cryptnox card; this value is what
 * a terminal pays to before that happens, so it is worth being a real address of
 * yours rather than a placeholder.
 *
 * MUST be in EIP-55 mixed-case checksum form: the firmware verifies the checksum
 * at boot and refuses to start on a mismatch, catching the most probable typo of
 * the recipient. An all-lowercase address is accepted but bypasses that typo
 * protection (and logs a warning at boot).
 *
 * Note that an asset whose payout address was never *stored* is not offered on the
 * amount screen at all — the fallback keeps a configured terminal working, it does
 * not make an unconfigured one sell things. See docs/config-portal.md. */
#define ADDR_TO           "<RECIPIENT_ADDRESS_EIP55>"

/* FALLBACK USDC ERC-20 contract address (Sepolia testnet, no 0x). Also settable
 * from the config page, with the same dual-store and on-screen accept as the
 * recipient — the contract decides which asset moves.
 * Same rule as ADDR_TO — use the EIP-55 mixed-case form for boot-time
 * checksum verification. */
#define ADDR_USDC         "<USDC_CONTRACT_ADDRESS_EIP55>"

/* USDT ERC-20 contract on Sepolia (no 0x, EIP-55). Not settable on the device —
 * the one NVS contract slot is USDC's — and not fatal if unset or malformed:
 * USDT on Ethereum is then refused at the confirm step and the rest of the
 * terminal boots.
 *
 * CHECK ITS decimals() IS 6 before using it. Mainnet USDT is 6, but Sepolia has
 * no canonical deployment — you are picking somebody's faucet token — and the
 * keypad and the calldata encoder assume 6 throughout, so an 18-decimal token
 * would move a millionth of a millionth of what the screen says. Read it with:
 *   cast call <contract> "decimals()(uint8)" --rpc-url <RPC_URL>
 * Same rule as ADDR_TO for the checksum form, and as TRON_ADDR_USDT below for
 * the decimals. */
#define ADDR_USDT         "<USDT_CONTRACT_ADDRESS_EIP55>"

/* =========================
 * Polygon (Amoy testnet)
 * ========================= */
/* Polygon is EVM, so the whole Ethereum path is reused — same card key, same
 * derivation path, same payout address, same RLP — and only these things change:
 * the endpoint, the chain id and the token contracts.
 *
 * PublicNode's Amoy endpoint needs no API key. It answered eth_chainId 0x13882
 * (80002) on 2026-08-28. */
#define POLY_RPC_URL      "https://polygon-amoy-bor-rpc.publicnode.com"
#define CHAIN_ID_AMOY     80002

/* Optional, same form and same reasoning as RPC_CA_CERT_PEM: pin the Polygon
 * endpoint's certificate instead of trusting the whole CA bundle. Kept separate
 * from the Ethereum pin because it is a different host — the firmware installs
 * whichever belongs to the network being charged on, and the CA bundle for the
 * one that has no pin.
 *
 * #define POLY_CA_CERT_PEM  "-----BEGIN CERTIFICATE-----\n...\n" \
 *                           "-----END CERTIFICATE-----\n"
 */

/* The two ERC-20s on Amoy (no 0x, EIP-55). One entry per token per network, never
 * one per token: the same USDT on Sepolia and on Amoy is two different
 * deployments, and pointing this at Sepolia's calls an address that holds
 * nothing. decimals() MUST be 6, as everywhere else. Non-fatal if unset: that
 * asset is refused, the terminal boots. */
#define POLY_ADDR_USDC    "<USDC_CONTRACT_ADDRESS_EIP55>"
#define POLY_ADDR_USDT    "<USDT_CONTRACT_ADDRESS_EIP55>"

/* Floor, in Gwei, under the priority fee on Polygon. Amoy's validators drop a
 * transaction whose tip is below ~25 Gwei with "transaction underpriced", and the
 * fee knobs on the settings Tx tab are shared with Ethereum, where 20 is a
 * sensible tip and 25 is wasteful. So Ethereum keeps the operator's number and
 * Polygon raises it to this when it is lower — one clamp instead of a second pair
 * of NVS settings and a second pair of steppers to explain. Raise it if Amoy
 * starts dropping transactions again. */
#define POLY_MIN_PRIORITY_FEE_GWEI  30U

/* =========================
 * Tron (Nile testnet)
 * ========================= */
/* Native TRX and TRC-20 (USDT / USDC) transfers, selectable at runtime from the
 * amount screen. TronGrid's public Nile endpoint needs no API key for these. */
#define TRON_URL          "https://nile.trongrid.io"

/* Optional, and worth setting in production: pin the Tron endpoint's certificate
 * (leaf or its issuing CA, PEM) so the connection is validated against that alone
 * instead of the ~150-CA Mozilla bundle. Same form as RPC_CA_CERT_PEM above.
 *
 * Leaving it unset is not a hole — a Tron terminal signs a txID the node builds,
 * so the node is treated as hostile regardless and every transaction it returns
 * is re-derived and compared before the card sees it. Pinning just means an
 * attacker has to defeat that check AND TLS.
 *
 * #define TRON_CA_CERT_PEM  "-----BEGIN CERTIFICATE-----\n...\n" \
 *                           "-----END CERTIFICATE-----\n"
 */

/* --- Tron addresses (Nile testnet) ---------------------------------
 * FALLBACK recipient, in base58 "T..." form, exactly as a Tron wallet shows it —
 * same story as ADDR_TO above: setup stores the real one in NVS and this is what a
 * terminal pays to until it does. The device decodes whichever is in use at boot
 * (checksum verified, so a typo is refused rather than paid) and derives the
 * "41"-prefixed hex the HTTP API wants.
 *
 * Only the recipient is configured. The sender is whatever card is presented:
 * its m/44'/195'/0'/0/0 public key is read over the secure channel and turned
 * into an address by CW_Tron, so swapping cards needs no rebuild. */
#define TRON_ADDR_TO      "THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdc"

/* --- TRC-20 token contract (Nile testnet) ---------------------------
 * FALLBACK again — settable from the config page. Base58 "T..." form. Confirm the
 * current Nile deployment on
 * https://nile.tronscan.org before trusting this — testnet token contracts get
 * redeployed, and a stale address decodes fine while paying the wrong asset.
 * The contract below answered symbol "USDT" / decimals 6 on 2026-08-07; the
 * amount keypad assumes 6 decimals, so re-check that too if you change it.
 *
 * Checksum-decoded at boot. Unlike TRON_ADDR_TO this is NOT fatal: a bad value
 * only disables the asset in the picker, so a terminal that charges in TRX or
 * on Sepolia still boots. */
#define TRON_ADDR_USDT    "TXYZopYRdj2D9XRtbG411XZZ3kM5VkAeBf"

/* USDC on Nile, base58 "T...". Unlike TRON_ADDR_USDT this one has no NVS slot —
 * the config page's single TRC-20 field is USDT's — so config.h is the only place
 * it can be set. Same non-fatal handling, same mandatory decimals() == 6 check
 * against a Nile node (see the curl in main/config.h, or nile.tronscan.org).
 *
 * Testnet only, and it will stay that way: Circle stopped minting USDC on Tron in
 * February 2024 and closed redemptions in February 2025, so there is no mainnet
 * asset for this selection to graduate to. It exists because the TRC-20 path is
 * generic and somebody asked to charge in it on Nile. */
#define TRON_ADDR_USDC    "<USDC_TRC20_CONTRACT_BASE58>"

/* Ceiling, in sun, on the TRX a TRC-20 call may burn when the card's account
 * has no energy staked. A transfer to an address that has never held the token
 * is the expensive case (~130k energy). This is signed into the transaction and
 * re-checked against the node's serialisation, so it is a real cap, not a hint.
 * 100 TRX is the usual wallet default. */
#define TRON_TRC20_FEE_LIMIT_SUN  100000000ULL


/* =========================
 * Production networks
 * =========================
 * The mainnet half of every pair above. Which half is used is a runtime setting
 * (Network, on the config page) and it defaults to PRODUCTION — a terminal
 * somebody bought to take money with must not come up settling nothing.
 *
 * Every constant here has a testnet twin above and is read through exactly the
 * same code path, so nothing below changes how a payment is built. What it
 * changes is where it settles, which is why the addresses want checking against a
 * block explorer rather than trusting this file: they are the canonical
 * deployments as of 2026-09, and a wrong one moves real money into the wrong
 * asset. All must have decimals() == 6, as everywhere else.
 *
 * An address whose EIP-55 case is wrong fails its parse at boot and disables that
 * one asset with a log line — so a typo here is a refusal, never a wrong charge.
 *
 * A network switch takes effect on a restart: the endpoints and contracts are
 * resolved once at boot into the dual stores, and the config page restarts the
 * terminal after writing the setting. The stored token contracts are kept in
 * separate NVS slots per deployment, so the two never bleed into each other. */

/* Ethereum mainnet. PublicNode, no API key — the same provider as Option A
 * above; swap in Infura by defining the credentials the same way. */
#define RPC_URL_MAIN      "https://ethereum-rpc.publicnode.com"
#define CHAIN_ID_MAINNET  1

/* Circle's USDC and Tether's USDT on Ethereum mainnet (no 0x, EIP-55). Both 6
 * decimals. Verify on etherscan.io before taking money with them. */
#define ADDR_USDC_MAIN    "A0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48"
#define ADDR_USDT_MAIN    "dAC17F958D2ee523a2206206994597C13D831ec7"

/* Polygon PoS mainnet. Same PublicNode arrangement; POLY_MIN_PRIORITY_FEE_GWEI
 * above applies here too — mainnet Polygon drops an underpriced tip exactly as
 * Amoy does. */
#define POLY_RPC_URL_MAIN "https://polygon-bor-rpc.publicnode.com"
#define CHAIN_ID_POLYGON  137

/* USDC and USDT on Polygon PoS (no 0x, EIP-55), both 6 decimals. The USDC here is
 * Circle's *native* issuance, NOT the bridged USDC.e at 0x2791Bca1… — they are
 * two different tokens with the same name on the same network, which is the one
 * mistake on this list a customer would notice. Verify on polygonscan.com. */
#define POLY_ADDR_USDC_MAIN  "3c499c542cEF5E3811e1192ce70d8cC03d5c3359"
#define POLY_ADDR_USDT_MAIN  "c2132D05D31c914a87C6611C10748AEb04B58e8F"

/* Tron mainnet. TronGrid's public endpoint needs no API key for these calls,
 * though it rate-limits harder than Nile does — set TRON_CA_CERT_PEM above and
 * consider a keyed endpoint for a busy terminal. */
#define TRON_URL_MAIN     "https://api.trongrid.io"

/* USDT (TRC-20) on Tron mainnet, base58 — 6 decimals. Verify on tronscan.org. */
#define TRON_ADDR_USDT_MAIN  "TR7NHqjeKQxGTCi8q8ZY4pL8otSzgjLj6t"

/* USDC on Tron mainnet — NOT SET, and it stays that way: Circle stopped minting
 * USDC on Tron in February 2024 and closed redemptions a year later. Unset means
 * that one selection is refused on mainnet, exactly as an unconfigured asset is
 * anywhere else; USDC on Nile still works. */
#define TRON_ADDR_USDC_MAIN  "<USDC_TRC20_CONTRACT_BASE58>"

/* =========================
 * Transaction Parameters
 * ========================= */
#define CHAIN_ID_SEPOLIA  11155111

/* Gas parameters (in wei). MAX_FEE / MAX_PRIORITY_FEE are only the first-boot
 * defaults — they're editable at runtime from the settings "Tx" tab (in Gwei)
 * and persisted to NVS. GAS_LIMIT_ERC20 stays compile-time. */
#define MAX_PRIORITY_FEE  20000000000ULL   /* 20 Gwei — tip to the validator   */
#define MAX_FEE           80000000000ULL   /* 80 Gwei — absolute cap           */
#define GAS_LIMIT_ERC20   100000ULL

/* Gas for a plain ETH / POL transfer to an account: exactly 21000, because no
 * contract runs. Optional — the firmware defaults to this if it is not set.
 *
 * Note the ceiling on those two assets: they are 18-decimal and the signed value
 * is a uint64 of wei, so a single sale stops at 18.446744 ETH or POL. The keypad
 * enforces it on the way in; the payment path re-checks it. The 6-decimal
 * stablecoins are unaffected and keep the full 99999.99 range. */
#define GAS_LIMIT_NATIVE  21000ULL

#endif // CONFIG_H
