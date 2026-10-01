# Testing the config portal

Test plan for `main/provision.cpp` — the SoftAP + captive portal, in both its
wizard and admin modes. Design notes are in
[config-portal.md](config-portal.md).

Four layers, cheapest first. Do them in order — a failure at layer 1 makes the
rest meaningless.

1. **Host** — the urlencoded parser and the address checks, no hardware.
2. **Bench, wizard** — a laptop joined to the setup AP, `curl` and `nslookup`.
   Proves the DNS and HTTP halves independently of any phone's opinion.
3. **Bench, admin** — a laptop joined to the same setup AP, admin mode.
4. **Handset** — real iOS and real Android. The only thing that proves the portal
   *opens by itself*, which is the whole feature.

---

## 0. Prerequisites

The CYD's CH340 moves COM ports between sessions. Find it first:

```bash
powershell -NoProfile -Command "[System.IO.Ports.SerialPort]::GetPortNames()"
```

Build, flash and read the log (substitute your port). Run the `.bat` directly —
`cmd //c` works only in Git Bash, and in PowerShell or cmd it opens a nested shell
and runs nothing at all, silently:

```bash
./scripts/idf-build-flash.bat COM7        # Git Bash
./scripts/idf-monitor.bat COM7 20
```

```powershell
& .\scripts\idf-build-flash.bat COM7      # PowerShell
& .\scripts\idf-monitor.bat COM7 20
```

The port defaults to COM3 in both scripts. `idf-monitor.bat` wraps
`serial_tail.py` because `idf.py monitor` needs a tty.

### Getting into first-run setup

The wizard runs on a terminal that is not configured, and there are two ways to be
unconfigured:

* **no admin code** (`!settings_has_admin_code()`) — a virgin or factory-reset
  unit. Welcome screen, admin code on the panel, then the browser flow.
* **no payout address** (neither `settings_has_payout(false)` nor `(true)`) — a
  half-configured unit, which is what a firmware update leaves behind if setup was
  never finished: NVS survives the update, so the admin code is still there. No
  welcome and no new code — it opens straight on the QR code and asks for the code
  the unit already has. If a network is already saved the wizard ends at the
  addresses rather than asking for a venue passphrase it already knows, so the path
  is `2 → 3 → 4 → 6`.

Two ways to get back to the first of those:

| Method | What it costs | When |
|---|---|---|
| Settings → About → Reset → confirm | admin code, Wi-Fi creds, brightness, fees, payout addresses, contracts, **and** the TLS identity in the `prov` namespace | normal test loop |
| `idf.py -p COM7 erase-flash` then flash | all of the above | testing a truly virgin unit |

Both clear the `prov` namespace, so both exercise TLS-key generation. The AP
passphrase is not stored at all — every portal open shows a different one, so check
the panel against the log each time rather than reusing yesterday's photo. Watch for
this once per wipe:

```
W (nnnn) prov: generating this terminal's TLS identity (once)
```

### The shortened wizard

To reach the `1 → 2 → 3 → 5 → 6` path (already configured, network gone): finish
setup, then take the venue AP down (or change its passphrase) and reboot. The
terminal should raise the setup AP and skip the address step.

---

## 1. Host tests

```bash
bash scripts/checks.sh          # everything below, in one command
```

Individually, if one of them fails and you want it on its own:

```bash
for t in test_eth_addr test_hardening test_tron_tx test_prov_form \
         test_addr_check test_json_out test_ota_version; do
  g++ -std=c++14 -Wall -Imain -Icryptnox-sdk-esp32/cryptnox-sdk-cpp \
      tests/units/$t.cpp -o $t && ./$t || exit 1
done
```

* `test_prov_form` covers `main/form_parse.h`: prefix anchoring (`wifipass` must
  not answer a lookup for `pass`), percent and plus decoding, malformed escapes,
  encoded `&` and `=` inside values, truncation at the buffer boundary.
* `test_addr_check` covers `main/addr_check.h`: the structural Tron check, and in
  particular that an empty string is **not** base58.
* `test_json_out` covers `main/json_out.h`: an SSID with a quote, a backslash, a
  trailing backslash or a control byte in it, and truncation at every buffer size
  from 1 to 40 — the output has to stay parseable JSON at all of them, because a
  response the page cannot parse is a blank config UI during setup.
* `test_eth_addr` now also covers `eth_addr_format` — the EIP-55 round trip that a
  card-derived payout address goes through.

All must print `OK`.

Then the page itself, which the C compiler cannot check:

```bash
python tools/check_portal_page.py --emit-js build/portal_page.js
node tools/test_portal_render.js build/portal_page.js
```

**Pass:** `portal page OK (… bytes, … ids, all wired)` then `portal render test OK`.
The first extracts the two string literals from `provision.cpp`, runs the
`<script>` body through `node --check`, and asserts every element id is referenced
from both sides. The second drives `render()` against each `(mode, step, authed,
pending)` the device can report and asserts which sections are visible — the wizard
showing two steps at once, or the admin page growing a Continue button, fails here.

Run both before flashing, not after: a typo in that page is a wizard nobody can
complete, on a device with no console to read the reason from.

---

## 2. Bench tests — laptop on the setup AP

Factory-reset, tap **Start**, set an admin code on the panel (say `1234`). The
screen should then show **Step 2 / Scan with your phone / Point your camera at the
code**, a QR code, `Cryptnox-XXXX` and a 10-character passphrase. The log carries
both:

```
I (nnnn) prov: setup portal up: SSID 'Cryptnox-A2E8', pass 'K7QMR3XPZW', http://192.168.4.1/
```

Join that network from a laptop, then:

### 2.1 DNS hijack

```bash
nslookup captive.apple.com 192.168.4.1
nslookup anything-at-all.invalid 192.168.4.1
```

**Pass:** both answer `192.168.4.1`. A made-up name answering is the point, not a
bug — every lookup resolves to the portal.

**Fail:** timeout means the DNS task never started. Check the log for
`DNS task failed - captive portal will NOT auto-open`.

### 2.2 Probe endpoints

```bash
for u in generate_204 gen_204 connecttest.txt ncsi.txt canonical.html \
         success.txt chat some/random/path; do
  printf '%-22s ' "$u"
  curl -s -o /dev/null -w '%{http_code} -> %{redirect_url}\n' \
       "http://192.168.4.1/$u"
done
curl -s http://192.168.4.1/hotspot-detect.html
```

| URL | Expected | Why it matters |
|---|---|---|
| `/generate_204`, `/gen_204` | `302 -> http://192.168.4.1/` | Android needs a **non**-204 |
| `/connecttest.txt`, `/ncsi.txt` | `302` | Windows NCSI |
| `/canonical.html`, `/success.txt` | `302` | Firefox |
| any unregistered path | `302` | the 404 catch-all |
| `/hotspot-detect.html` | `200`, body **without** the word `Success` | iOS follows redirects then compares the body, so a 302 here is wrong |

A `204` from `/generate_204` or a body containing `Success` from the Apple probe
means the portal will not auto-open on that platform. That is the failure this
whole feature exists to avoid — treat it as a blocker, not a cosmetic issue.

### 2.3 Nothing works unauthorised

This replaces the old per-step gating. Every mutating endpoint is behind the
session token, and there is no way to get one without the panel.

```bash
P=http://192.168.4.1
curl -s $P/api/state                                   # allowed, minimal
curl -si -X POST -d 'ssid=x&pass=y'         $P/api/wifi     | head -1
curl -si -X POST -d 'net=eth&addr=0x0'      $P/api/payout   | head -1
curl -si -X POST -d 'net=eth&addr=0x0'      $P/api/contract | head -1
curl -si -X POST                            $P/api/card     | head -1
curl -si -X POST                            $P/api/next     | head -1
curl -si -X POST --data-binary @/dev/null   $P/api/ota      | head -1
curl -si                                    $P/api/scan     | head -1
```

**Pass:** `/api/state` returns `{"mode":"wizard","step":"auth","authed":false,…}`
and carries **no** addresses or SSID. Every other line is `401`, body "This browser
is not authorised."

Then try a guessed token — 128 bits, so this is a formality, but it must fail:

```bash
curl -si -X POST -H 'X-Prov-Token: 00000000000000000000000000000000' \
     -d 'ssid=x&pass=y' $P/api/wifi | head -1
```

**Pass:** `401`.

### 2.4 Authorisation happens on the panel

```bash
curl -s -X POST $P/api/auth        # prints a 32-hex token
```

**Pass, in order:**

1. The log says
   `I (nnnn) prov: browser asked to be authorised - admin code needed on panel`.
2. The **panel** switches to a keypad titled **Authorise browser**. There is no
   code field in the browser at all — confirm by loading `http://192.168.4.1/` and
   reading the page: it should say the code is entered on the terminal, with a
   *Waiting for the admin code on the terminal screen…* box.
3. `curl -s $P/api/state` still reports `"authed":false` and
   `"auth_pending":true`.
4. Type `9999` on the panel → "Wrong code", still `authed:false`. Do this four
   times: the escalating wait must appear ("Wrong code - wait 1s", then 2s, 4s…).
   It is the same counter the settings menu uses, so this must not be a cheaper
   door.
5. Type `1234` → the log says `W (nnnn) prov: browser authorised from the panel`,
   `/api/state` flips to `"authed":true` and now includes the addresses.
6. Tap the back arrow instead, on a fresh request → `browser authorisation
   refused`, and the token stops working.

Keep the token for the rest of §2:

```bash
T=$(curl -s -X POST $P/api/auth)   # returns the existing token once authorised
H="X-Prov-Token: $T"
```

### 2.5 Payout addresses

```bash
A=$P/api/payout
# Ethereum — bad EIP-55 checksum (one character case-flipped)
curl -s -X POST -H "$H" -d 'net=eth&addr=0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAeD' $A
# Ethereum — too short
curl -s -X POST -H "$H" -d 'net=eth&addr=0xdead' $A
# Tron — 33 characters
curl -s -X POST -H "$H" -d 'net=tron&addr=THQGuFzL87ZqhxkgqYEryRAd7gqFqL5rd' $A
# Tron — wrong prefix
curl -s -X POST -H "$H" -d 'net=tron&addr=AHQGuFzL87ZqhxkgqYEryRAd7gqFqL5rdc' $A
# Tron — non-base58 character ('0')
curl -s -X POST -H "$H" -d 'net=tron&addr=T0QGuFzL87ZqhxkgqYEryRAd7gqFqL5rdc' $A
# Valid, all-lowercase Ethereum (no checksum to verify — accepted by design)
curl -s -X POST -H "$H" -d 'net=eth&addr=0x5aaeb6053f3e94c9b9a09f33669435e7ef1beaed' $A
```

**Pass:** the five bad ones are refused with a reason; the lowercase one is
accepted, because an all-lowercase address carries no checksum to check (the same
lenient rule `eth_addr_parse` applies to `config.h`, see `eth_addr.cpp`).

### 2.6 Token contracts

Same shape, same checks, different store:

```bash
C=$P/api/contract
curl -s -X POST -H "$H" -d 'net=tron&addr=notanaddress' $C          # refused
curl -s -X POST -H "$H" -d 'net=tron&addr=TXYZopYRdj2D9XRtbG411XZZ3kM5VkAeBf' $C
```

**Pass:** the second appears on the panel as **Set token contract?** — different
title and a different warning from a payout address, because it is a different
question. Accept it and check the log for
`W (nnnn) settings: contract(tron) set to TXYZ…`.

### 2.7 The security boundary

This is the most important test in the document.

1. Submit a valid address from the laptop.
2. **The address must appear on the device screen**, wrapped in full, with Accept
   and Reject buttons. Nothing is stored yet. `/api/state` shows
   `"pending":"Ethereum payout address"`.
3. Tap **Reject**. Check the log: no `payout(...) set to ...` line.
4. Submit a *second* address while the first modal is still up → expect `409`,
   "Something is already waiting to be accepted".
5. Submit again, tap **Accept**. Expect `W (nnnn) settings: payout(eth) set to 0x…`.
6. Finish the wizard, and after the restart take a payment: the Confirm screen's
   **To** row must show the new address.

**Fail:** any path that stores an address without a tap on the panel. A browser is
allowed to propose; only the panel may commit.

### 2.8 Card-derived addresses

```bash
curl -s -X POST -H "$H" $P/api/card
```

**Pass, in order:**

1. The panel asks for the **Card PIN** (its own title, not "Enter PIN").
2. Then **Cryptnox card / Hold card to reader** — *not* the Transaction screen, and
   with no amount on it.
3. Tap the card. The log prints
   `I (nnnn) cryptnox_pos: card addresses: eth=0x… tron=T…`.
4. The Ethereum address appears for acceptance, in **mixed case** — check it
   against the same card in a wallet, character for character.
5. Accept or reject it; the **Tron** address is then offered. Both must be
   offered from one tap.
6. Wrong PIN → `Wrong card PIN` shown as the page's note, nothing proposed.
7. Cancel on the PIN keypad → back to the setup screen, nothing proposed.

### 2.9 Wi-Fi

```bash
curl -s -H "$H" $P/api/scan            # the device's own scan
curl -s -X POST -H "$H" $P/api/rescan  # ask for a fresh one
curl -s -X POST -H "$H" -d 'ssid=&pass=x' $P/api/wifi
curl -s -X POST -H "$H" --data-urlencode 'ssid=My Cafe' \
     --data-urlencode 'pass=p@ss&word' $P/api/wifi
```

**Pass:** `/api/scan` lists networks with `rssi` and `open`; an empty SSID is
refused; a valid submission returns the "about to drop" text *before* the radio
moves, and the device screen switches to "Connecting to My Cafe…".

The second submission deliberately puts `&` and a space in the credentials — the
path `test_prov_form` covers, verified here end to end. An SSID containing a
double quote is worth one manual check too: `/api/state` and `/api/scan` must
still parse as JSON (that is what `json_str()` is for).

**Expected and not a bug:** the laptop loses the AP a second or two later. One
radio, one channel. If the network was wrong, the device screen returns to the
Wi-Fi step with a reason in the page's note and the AP stays up.

**The associated-but-rejected case is worth its own run**, because it is the one
that used to leave a second door open: give it a network that joins but cannot
reach NTP (a hotspot with no uplink, or block UDP 123 on the router). The panel must
return to the Wi-Fi step with "no network time", and the log must carry
`I (nnnn) net: station association dropped`. Then, from a machine on *that* network,
`curl http://<the IP the terminal had> /api/state` — it must not answer. The setup
forms are reachable from the SoftAP only; `httpd` binds every interface, so an
association the terminal is not keeping has to be dropped, not left up.

### 2.10 Finishing

**Pass:** on a good network the panel shows a green tick, **All set**, and a
**Finish** button. Tapping it logs
`W (nnnn) cryptnox_pos: setup finished - restarting to apply it` and reboots into
the amount screen.

Then scan for Wi-Fi from the laptop: `Cryptnox-XXXX` must be gone and the log must
show `config portal down`. The AP must not outlive setup — it is the attack surface.

### 2.11 Refusing to finish half configured

At the address step, with no payout address stored:

```bash
curl -s -X POST -H "$H" $P/api/next
```

**Pass:** the step does not advance, and `/api/state` carries
`"note":"Set at least one payout address first."`

---

## 3. Bench tests — laptop on the setup AP (admin mode)

Settings → Wi-Fi → **Configure**, or Settings → About → **Update**. Both open the
same page.

**Pass on the panel:** a QR code, the SSID and passphrase in text underneath, a
line saying the terminal leaves its own network while this is open, and a
15-minute countdown. Same AP, same page, same port as the wizard:

```bash
T=http://192.168.4.1
curl -s $T/api/state
```

### 3.1 The terminal is AP-only while this is open

With the terminal on a venue network, note its LAN address, then open the admin
page and from a machine **on the venue LAN** (not on the AP):

```bash
ping -n 2 192.168.1.34                                   # substitute
curl -s  --max-time 3 http://192.168.1.34/api/state
curl -sk --max-time 3 https://192.168.1.34/api/state
```

**Pass:** all three fail. The terminal is not on that network at all while the
portal is up, and nothing on it answers on 80 or 443. This is the whole
architectural claim — a LAN that can reach `/api/state` can reach `/api/payout`.

Then tap **Done** and watch the log: `SoftAP down`, `re-joining '<ssid>'`, and the
venue address answers again. A terminal that stays offline after the page closes
is a regression in `net_ap_stop()`.

**Also pass:** no TLS identity anywhere. On a unit provisioned by an earlier
build, grep the whole boot-and-portal log for `TLS` — it must not appear, and the
`tls_crt`/`tls_key` NVS keys are erased the first time a portal opens.

### 3.2 Everything the wizard tested, again

§2.3 through §2.9 apply unchanged — same scheme, same host, same port. Two
differences worth checking explicitly:

* The page shows **all** sections at once — addresses, contracts, Wi-Fi, firmware —
  rather than one step, and there is no Continue button.
* Accepting a value on the panel **restarts** the terminal
  (`config changed from the admin page - restarting`). That is deliberate: the
  dual stores are built at boot.

### 3.3 The window closes itself

Leave it 15 minutes without touching the panel, or shorten `PROV_WINDOW_MIN` for
the test.

```bash
curl -s -o /dev/null -w '%{http_code}\n' -X POST -H "$H" \
     -d 'net=eth&addr=0x5aaeb6053f3e94c9b9a09f33669435e7ef1beaed' $T/api/payout
```

**Pass:** `503` while the server is still up, then the log shows
`config portal down`, the modal clears itself, and the port stops answering.

Also check it closes when the operator has **left the modal** — enter the admin
code so the panel moves to the settings page, then wait out the window. A config
server that outlives its window because nobody was looking at a card is the thing
the window exists to prevent.

---

## 4. Handset tests

Repeat on **both** a real iPhone and a real Android phone. Simulators do not run
the connectivity probes.

1. Factory-reset, tap Start, set the admin code on the panel.
2. Open the camera, point it at the QR code.
3. **Pass:** the phone offers to join `Cryptnox-XXXX`, joins, and the setup page
   opens by itself within a few seconds. No typing, no URL.
4. **Pass:** the page immediately says it is waiting for the admin code, and the
   panel is *already* asking for it — nothing to press in the browser first.
5. **Pass:** once the code is accepted, the panel drops the QR code and the AP
   passphrase and says it is connected. A code still on screen at the payout
   step reads as "scan me again", which is the one thing that cannot help.
6. Walk the addresses and Wi-Fi steps from the phone. The address step is one
   **Payout addresses** card: the card-read button first, then **Or type an
   address** with the `0x…` / `T…` fields
   open on arrival — and those must not disappear while you are typing (the page
   re-renders every 1.5 s).
7. On the Wi-Fi card, tap the eye beside the password: it must reveal and re-mask.
8. **Pass:** each address appears on the terminal for acceptance.

### 4.1 The Wi-Fi-only re-join

With a configured terminal, change the venue password (or move the terminal out of
range) and power-cycle it.

**Pass:** the panel raises the AP and shows **Wi-Fi / Wi-Fi network / Scan with your
phone** — no admin-code screen, no Step numbers, no payout step. The phone joins,
the page opens on the Wi-Fi card alone, and the log says
`W (nnnn) prov: browser let in without a code (Wi-Fi-only re-join)`. The panel then
replaces the QR code with "Connected - the setup page is open in the browser".

**Fail:** being asked for the admin code, or being shown the payout addresses.

Failure modes worth telling apart:

| Symptom | Cause |
|---|---|
| Joins, no page | probe answered correctly — check §2.2 for that platform |
| Joins, then drops back to cellular | phone decided there is no internet; usually the Apple probe body |
| "Cannot join" | passphrase mismatch (it changes on every portal open — re-read the screen), or the AP's single slot is still held by the last device; it frees itself within 2 min |
| Page opens but is blank | `httpd` out of memory; check the log for allocation failures |
| Page opens, panel never asks for the code | the auto `/api/auth` did not fire — check the browser console |

Also worth doing once on each platform: put the phone into airplane mode and back,
and re-join from Settings rather than the QR code. The portal should open both
times.

**Admin mode on a phone** is the same journey as the wizard's — the same QR code
joins the same AP and the same captive portal opens the page — so §4's steps 1-3
apply to it unchanged. Two things only admin mode has: the panel says the terminal
is off its network for the duration, and the firmware section is on the page —
**Browse**, pick a `.bin` that is already on the phone (see
[ota-testing.md](ota-testing.md) §3). Neither is a certificate warning any more —
there is no certificate.

---

## 5. Regression tests

The portal touched the money path. These must pass on a terminal that is **not**
in first-run:

| Check | Expected |
|---|---|
| Boot with no stored payout address | `no payout address configured - running setup`, and the wizard opens on the QR code instead of the amount screen — a unit that was never told where the money goes must not come up as a till (§5.3) |
| Boot after storing an Ethereum address | Confirm screen **To** row shows the stored one |
| Boot with a stored ERC-20 contract | Confirm screen contract row shows it; a corrupt one logs `stored ERC-20 contract rejected - using config.h` |
| Factory reset, then boot | back to the `config.h` recipient and contract |
| All-lowercase `ADDR_TO` in `config.h` | `recipient is all-lowercase: no EIP-55 checksum verified` |
| Existing terminal, unchanged NVS | boots to `Ready` with no portal and no new warnings |
| Saved network unplugged, addresses set | the **shortened wizard** appears (AP + QR), not the panel picker |

### 5.1 An asset with no payout address

This is the bug the change set out to fix — a terminal configured for Ethereum only
was still offering Tron, paying the compile-time recipient.

**A. The picker refuses an unconfigured network.**

1. Factory-reset, set **only** the Ethereum payout address, finish setup.
2. **Pass:** the amount screen's asset selector opens on Ethereum. Tapping it shows
   Tron greyed out, subtitled *No payout address*, and it does not respond.
3. Settings → Tx: with Ethereum selected the rows are normal and there is no red
   warning. Nothing in the log about switching chains — the stored chain (Ethereum,
   the default) is configured, so there is nothing to correct.
4. Now add the Tron address from the admin page. **Pass:** after the restart Tron is
   selectable.

**B. Boot corrects a stored chain whose address was never set.**

Do this the other way round, because it is the only way to reach the state from the
UI: a payout address can be added but never removed, so you cannot un-configure the
chain you are on.

1. Factory-reset, and in the wizard set **only the Tron** payout address. Finish.
2. The stored chain is still the default (Ethereum) and Ethereum has no address, so
   the correction fires on this boot. **Pass:**
   ```
   W (nnnn) cryptnox_pos: selected chain has no payout address - switching to 1
   ```
   and the terminal comes up charging in TRX, with **Ethereum** greyed out in the
   picker — rather than quietly selling USDC to the compile-time recipient.
3. **Neither configured** (a fresh unit, nothing set): no switch is possible, so
   expect the warning instead and no chain change:
   ```
   W (nnnn) cryptnox_pos: no payout address configured - payments will be refused; set one from the config page
   ```
   Then enter an amount and confirm it. **Pass:** the sale is refused with
   **Payout address not configured**, before any card is asked for — see §5.3.

### 5.3 A terminal with no payout address refuses to sell

`settings_has_payout()` gates two things now: which assets the picker *offers*, and
whether a sale may be confirmed at all. `UI_EVENT_AMOUNT_CONFIRMED` refuses when the
active chain has no stored address, alongside the "Token contract not configured"
guard it sits next to.

```
enter 1.00 -> Confirm  =>  "Payout address not configured"
```

**Pass:** refused at the confirm step, *before* the PIN keypad or any card prompt —
a customer must never tap a card into a sale that cannot complete. The amount is
cleared, and the terminal returns to amount entry rather than locking up.

**Fail:** reaching the Confirm screen, or the card reader, on a unit with no stored
address. That is the original bug in its most expensive form: `settings_get_payout()`
still hands out the `config.h` recipient (it has to — every caller needs a parseable
address), so a sale that gets past this guard pays a stranger and looks normal.

The cost of this rule: a `config.h`-only unit must be configured once, by card or by
the **Payout addresses** fields, before it can charge. That is intended.

There is no longer a cold-boot route to this screen, and that is the point: a unit
with no stored address runs setup instead of the main loop (see "Getting into
first-run setup"), and one with a single address is moved off the chain it cannot
pay (`selected chain has no payout address - switching to N`) and cannot be moved
back by the picker. So this guard is now the third layer. Test it by making it fire
deliberately — flip the condition to `true` in a scratch build, or store an address
for one chain only and select the other from a build with the picker's refusal
removed — because it is what holds if either layer above it is ever changed.

### 5.2 Corruption fallback

`settings_get_payout()` / `settings_get_contract()` store each value twice and
compare. To exercise the mismatch path you need to write one copy and not the
other, which nothing in the firmware does — covered by inspection only.

<!-- ponytail: no test hook for the echo-mismatch fallback. Add a debug-only
     command that writes pay_eth without pay_eth_e if this path ever changes. -->

---

## 6. What this plan does not cover

- **Two browsers at once.** One session token exists at a time, and a second
  browser's `/api/auth` mints a new one — which silently unseats the first. That is
  the intended behaviour (the panel authorises exactly one), but it is untested.
- **Portal RAM headroom.** An SSL socket costs ~40 KB and `max_open_sockets` is 2,
  on a chip also carrying LVGL and the Wi-Fi stack. Nothing here measures free heap
  with the admin page up and a 1.9 MB upload in flight — do that before shipping.
- **`esp_restart()` under a payment.** Config changes and firmware installs both
  reboot, and both are routed through the main task's queue so they wait behind a
  payment in progress. Covered in [`docs/ota-testing.md`](ota-testing.md) §6.1.
