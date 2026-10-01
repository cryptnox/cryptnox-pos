# Firmware updates over Wi-Fi

The terminal never connects to GitHub. A browser does, and carries the bytes:

```
 you:     download cryptnox_pos.bin anywhere with internet
 browser ──HTTP──>  http://192.168.4.1/api/ota                  streamed to flash
 panel:   operator accepts the version → reboot into the other slot
```

Why the browser and not `esp_https_ota`, which would be a tenth of the code: a
terminal that phones a third party for updates tells that third party how many
units exist, roughly where they are and what each one runs. Nobody needs a
register of deployed payment terminals, least of all one kept by somebody else.

Firmware slots and image verification are `main/ota.cpp`, and the reasoning lives
in `main/ota.h`. The page, the endpoint and the authorisation belong to the config
portal — see **[config-portal.md](config-portal.md)**, which is also where the
addresses and contracts are set. Firmware is one section of one page.

## Why the flow has this shape

The page is on the terminal's own SoftAP and nowhere else — that AP is the
radio's only interface while the config portal is up, which is what keeps the
payout forms off the venue LAN (see
[config-portal.md](config-portal.md)). So the browser reading the page is on a
network with no route to the internet, and there is exactly one way to feed it an
image: **Browse**, and pick a `.bin` that is already on the phone or laptop.
Download it — desk, phone, USB stick — *before* joining the terminal's AP.

There used to be a "Check for updates" button that fetched a `firmware.json`
release list from the browser. It is gone. It only ever worked when a phone
happened to keep cellular data alive alongside a Wi-Fi network with no route,
which is not something to build on, and the other 90% of the time it was a
button whose only output was a network error. Publishing a release is now
publishing a signed `.bin` somewhere people can download it.

### What the transport is worth, and what it is not

The **firmware** does not need the transport to be trustworthy — it is verified
against a signing key compiled into the running image (see below), so a modified
upload is rejected whatever the network did to it.

The **admin code** is not on the network at all. It is typed on the terminal's own
screen to authorise the browser, and the browser then carries a random session
token; there is no code field in the page and no `X-Admin-Code` header any more.
See the authorisation section of [config-portal.md](config-portal.md).

The page is served over plain HTTP on the terminal's own WPA2 SoftAP, which
admits one station at a time and is the radio's only interface while the portal is
up. That link, not a certificate, is what protects the session token and the
addresses on the page — and neither is what keeps a stranger's firmware off the
device. The signature is.

## Operating it

1. Settings → About → **Update** (or Wi-Fi → **Configure**; same page). The panel
   shows a QR code for the terminal's own Wi-Fi, the SSID and passphrase in text,
   and starts a **15-minute** window (`PROV_WINDOW_MIN`). Tapping **Done** closes
   it immediately — and puts the terminal back on its network, which it leaves for
   the duration.
2. Scan the code with a phone camera to join, and the page opens itself. The panel
   asks for the admin code; enter it there. Then **Browse** and pick the `.bin`
   (see above — download it *before* joining). Picking it starts the transfer;
   there is no second confirm button, because nothing the page does installs
   anything.
3. The terminal verifies the image and asks on its own screen. Nothing reboots
   until somebody accepts it there — the same rule payout addresses follow: a
   browser may propose, only the panel may accept. A version that goes
   *backwards* is called out in red.
4. First boot after an update is on probation. `ota_mark_valid()` runs only once
   the panel, the card reader and the wallet layer have all come up; a reset
   before that point returns the bootloader to the previous slot. The network is
   deliberately not part of it: the first boot after an update runs setup (the
   update wipes the settings), and a power cut or an RPC outage during setup must
   not roll back an image that works.

## Publishing a release

There is no manifest and no URL compiled into the firmware: a release is a
signed `cryptnox_pos.bin` put somewhere operators can download it (a GitHub
release asset is fine), fetched *before* joining the terminal's AP and then
picked with **Browse**.

The version is built from git, never from a file: `<latest tag>-<first-parent
commit count>-g<short hash>`, e.g. `v1.0.0-91-gc10d526` (project
`CMakeLists.txt`). A release is a tag — `git tag v1.1.0` on the release commit,
then push the tag. The commit count doubles as `BUILD_ID`, which decides whether
the terminal wipes its settings on first boot. The panel
compares the uploaded image's version with the running one and calls out a
*downgrade* in red before anything is installed. Ordering is dotted-numeric with
an optional leading `v`; anything after the numbers (`-rc1`, `+sha`) is ignored
rather than ordered (`main/ota_version.h`, `tests/units/test_ota_version.cpp`).

## Signing — do not ship without this

The terminal cannot validate a TLS certificate chain to GitHub, because it never
talks to GitHub. Its only way to know an image is genuine is a signature over
that image, checked against a public key compiled into the firmware already
running. The secure build turns that on — `sdkconfig.defaults.flash_encryption`
enables hardware Secure Boot v2, and `sdkconfig.defaults.release` stacks on top
of it — and `esp_ota_end()` refuses anything that fails it, before the staged
slot can ever become bootable.

Without it, `POST /api/ota` is one authorised browser session away from replacing
the firmware on a device that signs cryptocurrency transactions. The session token,
the 15-minute window and the on-screen accept are not a substitute — they make it
harder to reach, not impossible, and none of them can tell a genuine image from a
convincing one.

This is **hardware** Secure Boot v2: the first boot of a secure build burns the
signing key's digest into eFuse, and that is one-way — from then on the unit only
boots, and only installs, images signed with the same key. (An earlier version of
the release overlay tried to add the software-only variant on top; Kconfig drops
it whenever `CONFIG_SECURE_BOOT` is set, so both overlays now name the one model
and the one key.)

Generate the key once:

```sh
idf.py secure-generate-signing-key --version 2 secure_keys/secure_boot_signing_key.pem
```

Keep it out of this repository and off any build machine you do not control; it
is the whole of the trust anchor. Losing it means no unit already in the field
can ever be updated again. The RSA scheme needs ESP32 revision v3.0 (ECO3) —
if `esptool.py chip_id` reports less, switch to
`CONFIG_SECURE_SIGNED_APPS_ECDSA_SCHEME` and drop `CONFIG_ESP32_REV_MIN_3`.

## Flash budget

`partitions.csv` carries two 1.94 MB app slots (`ota_0`, `ota_1`) — the largest
64 KB-aligned pair that fits in 4 MB alongside the data partitions. The image is
~1.6 MB, so there is roughly 390 KB of headroom; check `idf.py size` before
adding fonts or images.

**A unit flashed with the old single-slot table cannot be updated over the air.**
The partition table is never rewritten by an update, so those units need one
serial reflash before any of this applies to them.

## Endpoints

Firmware uses two of the portal's; the full list is in
[config-portal.md](config-portal.md).

| | |
|---|---|
| `GET /api/state` | includes `version`, the running firmware's, for the page's header |
| `POST /api/ota` | image body, `X-Prov-Token` header, streamed to the idle slot |

Same-origin, so no CORS headers are needed on the device side and no preflight
happens. The page does not hash the file itself — `crypto.subtle` is not even
available to it, since `http://192.168.4.1/` is not a secure context, and there
would be no point if it were: the image carries its own SHA-256 and
`esp_ota_end()` is what checks it, along with the signature, on the device that
has to trust the result.
