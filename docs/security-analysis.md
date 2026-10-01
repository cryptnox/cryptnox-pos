# Security analysis

What this terminal is worth attacking for, who can reach it, what stops them,
and what does not. Written against the code in this repo — every claim below
names the file that implements it, so a reader can disagree with the code rather
than with this document.

Scope: the firmware. Not the card applet, not the merchant's Wi-Fi, not the
chains or the RPC providers.

---

## 1. What an attacker wants

| Asset | Why it matters | Where it lives |
|---|---|---|
| **Payout addresses** | Change one and every future payment goes to the attacker. This is the whole game. | NVS, `settings.cpp` (`K_PAY_ETH`/`K_PAY_TRX`, written in duplicate) |
| **Token contract addresses** | A wrong contract moves a different asset than the one on screen | NVS + `config.h` fallbacks |
| **What the operator believes** | A terminal that displays PAID on a failed payment is a free-goods machine | `ui_sale.cpp`, gated in `pay.cpp` (`settle_inflight`) |
| **Firmware** | Replace it and everything above is moot | `ota_0`/`ota_1` |
| **Card PIN** | Signing authority for the tap in progress | RAM only, wiped after use |
| **Wi-Fi credentials** | Lateral movement into the venue network | NVS (encrypted) |

**The most important thing on this list is what is not on it: private keys.**
The card signs; the terminal never holds a key and never sees a seed. A fully
compromised terminal cannot sign a transaction the cardholder did not tap for.
That single architectural decision removes the class of attack that would
otherwise dominate this document.

---

## 2. Trust boundaries

```
   ┌────────────────┐   NFC / I²C     ┌──────────────────────┐
   │ Cryptnox card  │◄───────────────►│                      │
   └────────────────┘  PIN verified   │                      │
                       before signing │                      │
   ┌────────────────┐   touch         │                      │
   │ Operator       │◄───────────────►│   ESP32 terminal     │
   │ (at the panel) │  admin code,    │                      │
   └────────────────┘  on-screen      │   secure boot v2     │
                       confirmations  │   flash encryption   │
   ┌────────────────┐   HTTP over     │   NVS encryption     │
   │ Browser        │◄───────────────►│                      │
   │ (config page)  │  the device's   │                      │
   └────────────────┘  own WPA2 AP    │                      │
                                      │                      │
   ┌────────────────┐   HTTPS +       │                      │
   │ RPC nodes      │◄───────────────►│                      │
   └────────────────┘  cert bundle    │                      │
                                      │                      │
   ┌────────────────┐   UART          │                      │
   │ Host / flasher │◄───────────────►│                      │
   └────────────────┘  signed images  └──────────────────────┘
```

The boundary that carries the most weight is **operator ↔ panel**. Every change
that can redirect money is confirmed on the terminal's own screen, by someone
physically in front of it — never in the browser alone. That is a deliberate
design choice and it is what makes the config page's weaker transport tolerable.

---

## 3. What stops an attacker today

### Firmware integrity

- **Secure Boot v2, RSA-3072.** `CONFIG_SECURE_BOOT_V2_ENABLED`,
  `CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME` (`sdkconfig` 350–361). Verified at every
  boot — the boot log shows `secure_boot_v2: Signature verified successfully`.
- **Signed OTA.** The terminal never contacts GitHub, so it cannot judge a TLS
  chain to decide whether an image is genuine. It has exactly one test: an
  RSA-3072 signature checked by `esp_ota_end()` against a key compiled into the
  firmware already running (`ota.cpp:177`, rationale in
  `sdkconfig.defaults.release`). An unsigned image is refused, and the operator
  is told why (`ota.cpp:183`).
- **Downgrade is visible, not blocked.** `ota_version_cmp()`
  (`ota_version.h`) tells the operator "This is an OLDER version" in red
  (`ui_admin.cpp`), and they decide. Deliberate: blocking downgrades outright makes a
  bricked fleet unrecoverable.
- **Rollback verdict surfaced.** An update that installed, booted and was then
  reverted says so on the About tab rather than silently reading as the old
  version (`ota_last_update_failed()`).

### Data at rest

- **Flash encryption** (`CONFIG_SECURE_FLASH_ENC_ENABLED`) and **NVS
  encryption** with a dedicated `nvs_keys` partition — visible at boot as
  `nvs_sec_provider: NVS Encryption`.
- **Payout addresses stored in duplicate** and cross-checked, so a single
  corrupted NVS entry cannot silently redirect funds (`settings.cpp`,
  `dual_get`/`dual_set`).
- **Dead secrets erased.** A self-signed TLS identity from an earlier build is
  actively deleted from NVS on boot rather than left to rot
  (`provision.cpp:225–238`), with an honest note that NVS deletes logically and
  this closes the NVS read, not a raw flash dump.
- **Signing keys are not in this repo.** `secure_keys/` is untracked —
  confirmed with `git ls-files`.

### The config page

- **The admin code is never typed into the browser.** It is entered on the
  panel; that is what proves the person is standing in front of the terminal
  (`provision.cpp`, the authorise section).
- **128-bit session token**, hex, from the hardware RNG, compared in constant
  time via `CW_Utils::secure_compare` (`provision.cpp:376–388`, token minted at
  `provision.cpp:1307`). The code notes the RNG is only a true one once the RF
  clock is running, and seeds accordingly (`provision.cpp:175`, `1815`).
- **Every value that matters is confirmed on the panel.** The browser can only
  *propose* a payout address or a contract; a human accepts it on the device's
  own screen (`provision.cpp`, `ui_setup.cpp` accept flow). This is what contains a
  compromised browser.
- **Session expiry and clear refusals**: `503` once the window closes, `401`
  when unauthorised, both with plain-language messages.
- **The page runs on the device's own AP**, not the venue LAN — which is why it
  no longer carries TLS at all (see finding **F3**).

### Payment correctness

- **PIN verified before signing**, not after (commit `82efc6d`).
- **Integrity gates** around every decision that ends in PAID: complement-encoded
  sentinels (`bool32`, `pos_verdict_t`) that a single flipped bit cannot forge,
  asserted at compile time (`hardening.h:50–55`), with `address_consistent()`
  re-checked before calldata, before signing and before display
  (`sign_and_broadcast` in `pay_evm.cpp`, `sign_and_broadcast_tron` in
  `pay_tron.cpp`, the confirm step in `main.cpp`, `settle_inflight` in `pay.cpp`).
- **Anomalies are counted in NVS**, not just logged, so a pattern survives a
  power cycle (`pos_handle_anomaly()`, `hardening.cpp`).
- **Address validation** on every entry path: base58 + checksum
  (`addr_check.h`), EIP-55 (`eth_addr.cpp`), Tron (`tron_tx.cpp`).
- **Clock hardening.** SNTP alone is spoofable by whoever runs the network, so
  the HTTPS response `Date` header is used to corroborate it and a skew beyond
  `CLOCK_SKEW_MAX_S` refuses the transaction (`https_post.cpp:111–131`). Without
  network time at all, the terminal declines rather than guessing
  (`UI_EVENT_AMOUNT_CONFIRMED` in `main.cpp`).
- **The node is not trusted for what gets signed.** Tron: the node-built
  transaction is decoded field by field (`tron_tx.cpp`) — exactly one contract of
  the expected type, byte-exact parameters, no memo / extra fields, bounded
  expiration, exact fee limit — before the card signs its `txID`. EVM: the
  transaction is built and hashed on the device, the recovery bit is computed
  locally (`eth_sig.cpp`), and a receipt only counts when it is for that hash, to
  the expected contract, with a `Transfer` log to the payee for exactly the
  amount (`eth_json_receipt_check`).
- **TLS with the ESP-IDF certificate bundle** for all RPC traffic
  (`https_post.cpp`). This is **not pinning**: any CA in the ~150-entry bundle is
  accepted. A deployment can pin an endpoint by defining `RPC_CA_CERT_PEM`,
  `POLY_CA_CERT_PEM` or `TRON_CA_CERT_PEM` in `config.h`; none are defined by
  default, and boot logs every endpoint that is left unpinned.

### Build and process

- **CodeQL** (`.github/workflows/codeql.yml`) and **cppcheck + clang-tidy**
  (`misra_check.yml`) are configured to run on every push — but both jobs are
  currently failing and have been for at least a month, so neither is actually
  protecting anything. See **F6**, which is why this bullet cannot be counted as
  a mitigation today.
- **Host tests** for the security-relevant pure logic — address checks,
  hardening sentinels, Tron transaction decoding, receipt checks, version
  comparison, RPC error mapping — runnable without hardware
  (`bash scripts/checks.sh`), and run by CI (`.github/workflows/tests.yml`).
- **Quiet boot in release**: `CONFIG_LOG_DEFAULT_LEVEL_NONE`, so nonces and tx
  hashes are not echoed over UART on a production unit.

---

## 4. Findings

Ordered by what I would fix first. None of these is a break of the payment path;
the first is a process gap that would undo much of section 3.

### F1 — A production unit built with the dev config is not a secure unit
**Severity: high (process, not code).**
This repo's checked-in `sdkconfig` has
`CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT=y` and
`CONFIG_SECURE_INSECURE_ALLOW_DL_MODE=y`. The board says so itself at every
boot: `flash_encrypt: Flash encryption mode is DEVELOPMENT (not secure)`. In
that mode the UART download path can still encrypt and decrypt, so an attacker
with the device in hand and a cable can read out flash — including NVS, and so
the payout addresses and Wi-Fi credentials.

This is *correct* for the bench and `sdkconfig.defaults.release` exists to fix
it, is well documented, and switches to `RELEASE` mode plus signed OTA. The risk
is entirely that someone ships a unit without layering it.

**Fix:** make it impossible to do by accident rather than by discipline — a
release build script that always passes the three overlays in order, and a
runtime refusal (or at minimum a permanent on-screen banner) when a build
reports `DEVELOPMENT` mode. A terminal should be able to tell the operator it is
not a secure unit.

### F2 — ROM UART download mode is left enabled
**Severity: medium, and deliberate.**
`CONFIG_SECURE_DISABLE_ROM_DL_MODE` is off, documented as a trade-off in
`sdkconfig.defaults.release`: enabling it makes recovery OTA-only. With RELEASE
flash encryption the download path can no longer decrypt, so this is much
narrower than F1 — but it keeps a physical foothold open.

**Fix:** none required today. Revisit once the signed-OTA path has enough field
history to be the only recovery route. The decision and its reason are already
written down, which is the important part.

### F3 — The config page is plain HTTP
**Severity: medium.**
`httpd_start()` with `HTTPD_DEFAULT_CONFIG()` (`provision.cpp:1833`, `1889`) —
no TLS. The mitigation is real: the page is served on the terminal's **own**
WPA2 AP with a randomly generated password, not on the venue LAN, and the old
self-signed identity is now actively erased. But everyone who has that AP
password shares the segment, and over it travel the session token and — during
setup — the venue Wi-Fi password.

**Fix:** the cheap and effective half is scope reduction, most of which is
already done. If this needs closing, a self-signed cert re-introduces the
browser warning that made it worse last time; the better direction is to stop
sending the Wi-Fi password over the page at all (enter it on the panel) and
accept the token exposure, which on its own cannot move money.

### F4 — No transaction record on the device
**Severity: low for security, real for disputes.**
Nothing is persisted about a completed payment: no log, no receipt, no counter.
I grepped for it — there is no tx history in NVS and the Tx tab shows
configuration, not past payments. There is therefore no forensic trail on the
device after an incident, and no way to reconcile the terminal against the chain
without knowing what to look for.

**Fix:** a bounded NVS ring of the last N payments (timestamp, asset, amount, tx
hash). Bounded because NVS write endurance is finite. Note this is
privacy-relevant if the payer's address is included — that is a product
decision.

### F5 — Admin code strength is the merchant's choice
**Severity: low, mitigated.**
`ADMIN_CODE_MIN` was 4 digits (`ui_internal.h`). Against that: attempts are counted
**in NVS**, so the escalating lockout survives a power cycle and cannot be reset
by pulling the plug (`admin_penalty_ms`, `s_admin_lock_*`), and input is blocked
for the penalty window (`admin_lock_remaining_s`, `ui_admin.cpp`). Shoulder-surfing at a counter remains
the more realistic attack, and no PIN policy fixes that.

**Fix (done):** new codes need 6 digits and the penalty keeps doubling up to an
hour per attempt; a code stored under the old 4-digit minimum still unlocks, so
existing units are not forced mid-life.

---

### F6 — The automated scanning has never actually run
**Severity: high (process).**
Both security workflows fail on every push, and have since at least
2026-09-04 — checked with `gh run list`. The repo looks scanned and is not.

- **CodeQL** dies in its build step, so `Analyse` never executes and no results
  are ever uploaded (`CodeQL job status was configuration error`). Cause:
  `tests/units/test_networks.cpp` includes `config.h`, which is gitignored
  (`.gitignore:2`) because it holds real contract and payout addresses — so the
  CI checkout does not have it. `fatal error: config.h: No such file or
  directory`.
- **cppcheck** fails on two real diagnostics with `--error-exitcode=1`, and
  because it is the first step the clang-tidy step below it never runs either:
  - `main/main.cpp:784` — `nullPointerRedundantCheck` on
    `native ? to->addr : token->addr`. Almost certainly a false positive:
    `native` comes from `chain_is_native_evm()` and the dereference is on the
    non-native branch, which cppcheck cannot prove. Needs an
    `// cppcheck-suppress` with a reason, not a code change.
  - `main/settings.cpp:509` — `duplicateExpressionTernary` on
    `m ? "0x" ADDR_USDC_MAIN : "0x" ADDR_USDC`. The two macros are genuinely
    different addresses in `config.h`, so this is likely an artefact of
    `--force` exploring a configuration where neither is defined — but it sits
    on the line that decides which contract the terminal calls, so it deserves
    a real look rather than a blanket suppression.

**Fix:** generate `main/config.h` from `config.template.h` as a CI step before
either job builds — the template's placeholder values compile fine and no real
address enters CI. Then triage the two cppcheck diagnostics so the job goes
green and stays meaningful. A red pipeline that everyone has learned to ignore
is worse than no pipeline, because it is quoted as assurance.

---

## 5. What this analysis does not cover

Stated plainly so nobody reads more assurance into it than it carries:

- **No penetration test was performed.** Nothing here was attacked; this is code
  and configuration review.
- **The card applet is out of scope** — the terminal's security depends on it
  refusing to sign without a verified PIN, which is asserted, not verified here.
- **No side-channel or fault-injection analysis** (glitching the integrity
  gates, EM analysis of the NFC exchange).
- **The RPC providers are trusted for chain state, not for what is signed.** A
  lying node (or anyone holding a certificate from a bundled CA, since endpoints
  are unpinned by default) cannot get the card to sign a different transfer, but
  it can still fabricate a receipt for a transaction that never landed, or report
  a balance that is not there. The on-chain record is the truth, and the
  terminal's own display is not authoritative for settlement.
- **Supply chain** — the ESP-IDF, LVGL and managed component versions are pinned
  (`dependencies.lock`) but not audited.

An external review should start at F1 and F6. F1 decides whether everything in
section 3 is actually switched on in the field, and F6 is why nobody would be
told if it stopped being true.
