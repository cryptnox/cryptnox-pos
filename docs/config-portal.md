# The config portal

One web app configures a terminal, in two modes. Implementation is
`main/provision.cpp`; the reasoning lives in `main/provision.h`.

| | **Wizard** (`PROV_MODE_WIZARD`) | **Admin** (`PROV_MODE_ADMIN`) |
|---|---|---|
| When | blank terminal, or saved Wi-Fi unreachable | any time, from the settings menu |
| Network | the terminal's own WPA2 SoftAP | the same SoftAP |
| Reached by | captive portal — the browser opens itself | the same captive portal |
| Transport | plain HTTP, port 80 | plain HTTP, port 80 |
| Closes | when setup ends | after `PROV_WINDOW_MIN` (15 min), or **Done** |
| Serves | one step at a time | everything at once |

The modes differ in what they serve and when they close. They do not differ in
how they are reached — that is the point of the next section.

## Authorisation: the panel, never the wire

There is no admin-code field in the page. The browser asks to be let in; the
terminal shows its admin-code screen; the operator types the code **on the panel**;
the session becomes authorised and carries a random 128-bit token
(`X-Prov-Token`) from then on.

So the code never crosses the network in either direction, on either mode's
transport, and a browser that can reach the page without the terminal in reach can
do nothing at all. (One exception, in the wizard only: the Wi-Fi-only re-join below
asks for no code at all.) Guessing at the panel earns the same escalating lockout the
settings menu imposes — it is the same screen and the same counter, so this is not
a cheaper door.

Everything that decides where money goes goes further than that. A browser may
**propose** a payout address or a token contract; the value is then displayed on
the panel and somebody has to accept it there. Nothing is stored until they do,
and the same is true of firmware: an upload is verified and staged, and only an
on-screen accept makes it bootable.

## The SoftAP is the only interface, and that is the security model

`esp_http_server` binds every interface and gives you no bind address. So a portal
running beside a station association is not "on the AP" — it is on the venue LAN
too, answering the payout forms to every device holding the venue PSK. The
perimeter this module relies on is *a passphrase shown on the panel to whoever is
standing there*, and on a venue LAN that perimeter is not merely weaker, it is
absent.

So `net_ap_start()` raises the AP in `WIFI_MODE_AP`, not APSTA, and drops the
station association on the way in. Both modes. The station comes back in
`net_ap_stop()` — the driver still holds the credentials, so it is a reconnect,
not a reconfigure, and closing the admin page puts a working terminal back online
rather than leaving it offline until somebody reboots it.

Two calls borrow the station back, for as long as they need it and no longer:
`net_wifi_scan()` flips to APSTA for the scan and hands the radio straight back,
and `net_wifi_connect()` holds APSTA for the join the operator asked for — the
phone that submitted the form is on the AP and has to be told what happened. The
caller then either keeps the join and stops the portal in the same breath, or
drops the association again (`net_wifi_disconnect()`, which also returns the radio
to AP-only).

**Consequences, stated plainly.** There is no remote administration: you are in
front of the terminal or you are nowhere. The terminal is off its network while
the admin page is open, so it is not taking payments during that window — which
is fine, because somebody is standing at it with the panel in one hand and a phone
in the other. And the browser-side update check needs the *phone's* internet, not
the terminal's, so on a phone with no cellular data the release-list check fails
and says so; the file picker still works.

**Both modes are plain HTTP**, and that is not laziness in the plaintext one:

* A captive-portal probe fetches a bare `http://` URL on port 80 and will not
  follow a redirect to 443. TLS means the phone's browser never opens by itself,
  which is the entire feature.
* The link is already encrypted — a WPA2 SoftAP whose random per-session
  passphrase is on the panel in front of the operator, admitting one station at a
  time, existing only while the portal does.
* The admin code is not on that link, and the values that are get confirmed on the
  panel.
* There is nothing else on the wire: the AP is the radio's only interface.

TLS on top of that would buy a certificate warning to explain on a 2.8" panel, a
self-signed key to generate and keep in NVS, a second transport through the same
forms, and no security anybody can point at. It used to be there; it is gone
(`CONFIG_ESP_HTTPS_SERVER_ENABLE` is deliberately off in `sdkconfig.defaults`),
and the leftover `tls_crt`/`tls_key` NVS entries on an already-provisioned unit
are erased the next time a portal opens (`ap_pass_load()`).

The AP passphrase is drawn once — on the first portal a unit ever opens — and kept
in NVS until `settings_factory_reset()` erases the `prov` namespace. It used to be
redrawn per session, on the grounds that the screen showing it is a screen a
customer can photograph. That defended one thing: the Wi-Fi-only re-join portal
asks for no admin code, so an old photograph can be used to join the setup AP and
change which network the terminal joins — but only while the terminal has that
portal open (it opens it by itself when the venue network fails), only from inside
the SoftAP's range, and only in front of a panel that is displaying the current
passphrase anyway. One association is all the AP allows, so a stranger using it
locks the operator out rather than watching them. Everything that moves money is
behind the admin code, which is typed on the panel and never on the page. What the
redraw cost was real: ten characters retyped every single time the page is opened.

Entropy comes from `esp_random()`, called after `net_wifi_init()` — the RNG is only
a true one with the RF subsystem running. `bootloader_random_enable()` (the SAR ADC
source, for entropy with the radio off) is deliberately *not* used: it must not be
called with Wi-Fi started, and by the time a portal opens it is.

## The wizard flow

```
 1. admin code       panel     the one secret that never touches a network
 2. QR code          panel     camera joins the SoftAP; portal opens the page
 3. authorise        both      browser asks, panel takes the code
 4. addresses        browser   typed, or read off a Cryptnox card
 5. Wi-Fi            browser   scanned list from the device's own radio
 6. Finish           panel     restarts, which is what applies everything
```

Steps 1 and 6 are on the panel, and the four in between are in the browser. There
is deliberately **no "use this screen instead"** escape: typing a payout address or
a venue passphrase on a resistive 240x320 panel is the thing this module exists to
avoid, and a second, worse path meant maintaining two of everything. The panel
picker still exists (`wifi_picker()` in `main/boot.cpp`) but it is now the
settings-menu route and the fallback for when the SoftAP itself will not come up.

**A configured terminal whose network has gone** gets `2 → 5 → 6`, and no admin code:
the addresses are already set and there is no code to create, so the page opens
straight on the Wi-Fi card and the first browser to ask is let in
(`prov_set_wifi_only()`). Three screens guarding a form whose only unconfirmed power
is "try this network" is three screens between an operator and a working till.

What still holds in that flow: the AP passphrase — per device, per session, on the
panel in front of whoever is asking — is the perimeter, and every value that decides where money
goes, or which firmware boots, is still accepted on the panel. The panel drops the
step numbers there, since there are no steps 1, 3 or 4 to count.

For that perimeter to mean anything the SoftAP has to be the *only* way in — which
is why the radio is AP-only while a portal is up (above). The Wi-Fi step is the one
place that has to open the station again, so a join that works is either kept and
the portal stopped in the same breath, or dropped again (`net_wifi_disconnect()`): a
network that associated but had no clock is rejected, and leaving that association
up would put the setup forms in front of everyone holding the venue PSK.

### The last screen the phone gets

Joining the venue network is where the page runs out of device to talk to: the
radio serves both networks for a few seconds and then follows the venue AP to its
channel, which drops the phone. So handing over the network does not leave the
operator on a Wi-Fi form with dead buttons — the page replaces itself with
**Configuration complete / follow the instructions on the terminal's screen**,
which is the truth: from here the panel is the only thing that knows anything.

That screen is not final, though. If the join fails the terminal comes back on the
same setup AP, the phone rejoins it by itself, and the poll that never stopped
running sees the device answer *with a note* — which is the only thing that brings
the page back to the Wi-Fi step, carrying the reason. An answer with nothing to
report is the join still being attempted, and must not undo the screen
(`tools/test_portal_render.js` asserts both).

A join that *works* is silence: the terminal stops the portal and the AP goes with
it, so nothing ever answers that poll again. Which is correct — there is nothing
left for the phone to do.

**Finishing restarts the terminal.** The recipient and contract dual stores are
built at boot from validated strings, so applying a change in place would mean a
second path into the money code that has to re-validate everything the boot path
already validates. Three seconds during setup buys reusing those checks verbatim.

### Payout addresses from a Cryptnox card

The alternative to typing 42 characters. One tap reads both networks —
`m/44'/60'/0'/0/0` for Ethereum, `m/44'/195'/0'/0/0` for Tron — and each address
goes through the same accept-on-the-panel handshake a typed one does, because "the
card said so" is not the same claim as "the operator checked it". A customer's card
presented at the wrong moment would otherwise redirect the takings.

The card will not export a public key without a verified PIN, so this needs the
card PIN on the keypad first, exactly as signing does.

**A card that was never set up is named as such.** A card out of its envelope has
no PIN and no key, and the applet says so in the clear in its SELECT response —
so that is read before the secure channel, on the payment path as well as this one
(`main/card_status.h`, `card_fault()` in `main/card_io.cpp`). Without it the holder of
a blank card is told *"Wrong card PIN"*, which is the one message that sends them
off to type it again. A response this cannot judge — a card type it has not been
told about, a SELECT that did not answer `90 00` — is not a refusal: the ordinary
paths report whatever happens next, and a parser that guessed here would turn a
working card away at a till.

The Ethereum address is rendered in EIP-55 mixed case (`eth_addr_format`). That is
not cosmetic: it is the form the operator's own wallet shows, which is what they are
being asked to compare against, and it means the stored string carries a checksum
so the boot-time parse is a real check on it.

## An asset with no payout address is not offered

A terminal set up for Ethereum and interrupted before Tron used to keep offering
Tron payments — to the `config.h` recipient, which is somebody else's address, while
looking entirely normal. Three things changed:

* the network picker greys out a network with no stored payout address, saying so
  rather than hiding the row;
* the Tx tab says in red when the recipient shown is the built-in default;
* boot corrects a stored chain selection whose payout address was never set,
  switching to one that was (`main/boot.cpp`, after the address resolution).

`settings_has_payout()` is the question being asked, and it means *stored*, not
*resolvable* — the `config.h` fallback still supplies the value, it just no longer
counts as configured.

There are two payout addresses for three networks. Polygon is EVM — same card key,
same `m/44'/60'/0'/0/0` derivation, same `0x` address — so it spends the Ethereum
one, and `settings_has_payout(false)` is what the Polygon row in the picker asks.
The page and the panel both say so where the address is entered and where it is
accepted; without that, an operator hunts for a Polygon field that will never exist
and concludes the terminal cannot take Polygon payments.

## Every setting is changed here, not on the panel

The panel is a 2.8" resistive screen behind an admin code; this page is a keyboard.
So the terminal's own settings screen **reports** and does not edit — brightness is
the single exception, because it is a property of the screen you are looking at and
nothing else can judge it. The Tx tab shows the asset, the token contract, the
payout address and the two gas caps as read-only rows; the gas caps were `+`/`-`
steppers there until they moved to this page.

Three panel controls are deliberately *not* settings, and stay where they are:

* the asset selector — on the amount row and at the top of the Tx tab. Which asset
  a sale is charged in is a shift-time choice made with a customer waiting, not
  something an administrator sets once;
* the factory reset on the About tab — the recovery path, which has to work when
  this page cannot be reached at all;
* the Wi-Fi picker the terminal raises by itself when it cannot re-join, for the
  same reason.

A gas cap is stored straight from this page rather than proposed on the panel like
an address: an address decides *who* gets the money and has to be read back by a
human, while a cap only decides how much gas the terminal pays for its own
transaction — and a wrong one announces itself by pricing the next sale out of a
block.

### Production or test networks

The Network section switches all three networks at once between their production
and their test deployments — Ethereum/Sepolia, Polygon/Amoy, Tron/Nile. Not one
per network: "Ethereum mainnet with Tron on Nile" is not a configuration anybody
wants, it is a terminal half of whose sales settle in nothing. A terminal that has
never been told otherwise is on **production**; a unit somebody bought to take
money with must not come up settling nothing.

Stored straight through like the gas caps and for the same reason — it cannot send
money anywhere, it only decides where the operator's own payout address is paid —
and then **the terminal restarts**. That restart is the mechanism, not an
inconvenience around one: the RPC endpoints, the chain ids and the token contracts
are resolved once at boot into the dual stores that every signature is reconciled
against, and there is no honest way to move those under a running payment.

Two things are stored per network and one is not:

* **token contracts** are per network. The same USDC on mainnet and on Sepolia are
  different deployments, so one slot would carry a testnet address onto mainnet —
  an address that holds nothing, under a name that says it holds money. Switching
  therefore falls back to the firmware's own contract for the network you arrive
  on until somebody sets one there;
* **payout addresses** are shared. A card's address is the same account on either
  deployment, so switching does not send the operator back through setup.

The names on the panel follow: the Tx tab and the asset picker read "Ethereum
Sepolia" on a test terminal and plain "Ethereum" on a production one. The mainnet
names carry no suffix on purpose — a production terminal should not be shouting a
word nobody needs, and the testnet ones then stand out.

The addresses themselves live in `config.h`, one pair per asset, and
`tests/units/test_networks.cpp` checks every one of them parses with a correct
EIP-55 checksum. What no test can check is that an address is the *right*
contract; that is a block explorer's job before a terminal takes real money.

## Endpoints

Everything except `GET /` and `GET /api/state` requires the session token.

| | |
|---|---|
| `GET /` | the page — HTML then JS, two strings in `provision.cpp` |
| `GET /api/state` | mode, step, `authed`, version; and once authorised the addresses, contracts, SSID, pending value, note, `mainnet`, gas caps, scan generation |
| `GET /api/scan` | the device's last Wi-Fi scan |
| `POST /api/auth` | ask to be authorised → returns the token; the panel decides (except the Wi-Fi-only wizard, which grants it) |
| `POST /api/payout` | `net=eth\|tron`, `addr=…` → proposed, panel confirms |
| `POST /api/contract` | same shape, for the ERC-20 / TRC-20 contract |
| `POST /api/fees` | `max=…`, `prio=…` in Gwei → stored directly, 1&ndash;500 each and the tip no higher than the max |
| `POST /api/network` | `net=main\|test` → stored directly, then the terminal restarts to apply it |
| `POST /api/card` | read the payout addresses off a Cryptnox card |
| `POST /api/wifi` | `ssid=…`, `pass=…` |
| `POST /api/rescan` | ask the device to scan again |
| `POST /api/next` | wizard: leave this step |
| `POST /api/ota` | firmware image body (see [ota.md](ota.md)) |

Bodies are `application/x-www-form-urlencoded` and parsed by `form_field()`
(`main/form_parse.h`), which is the one piece of this that reads input a stranger
controls and is therefore host-tested — `tests/units/test_prov_form.cpp`, and
`test_addr_check.cpp` for the address checks it feeds.

### Why the Wi-Fi scan is not in the handler

Scanning makes the radio hop channels, which briefly drops whoever is joined to the
SoftAP — including the browser making the request. So `net_wifi_scan()` runs on the
main task at known moments (entering the Wi-Fi step, or a rescan the browser asked
for) and hands the result to `prov_set_scan()`; the handler serves the cached list.
The page refetches when `scan_gen` moves.

In admin mode a scan interrupts the page rather than the AP, which is why the
rescan there is an explicit button and the page says what it will do.
