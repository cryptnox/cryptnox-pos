:description: First-time setup of a Cryptnox POS terminal through its own Wi-Fi config portal, what happens during a sale, which assets and networks are supported, and how firmware updates arrive over Wi-Fi.

=====================
Setting up and using
=====================

First-time setup: the config portal
===================================

Setup is a short browser flow on the terminal's **own Wi-Fi access point**. Nothing is typed
on the 2.8" panel except the admin code, and nothing that decides where the money goes is
accepted anywhere except on the panel.

.. list-table::
   :header-rows: 1
   :widths: 6 20 14 60

   * - Step
     - What
     - Where
     - Detail
   * - 1
     - Admin code
     - panel
     - A six-digit code, typed on the terminal. It never crosses a network. Wrong attempts
       earn an escalating lockout that survives a power cycle.
   * - 2
     - QR code
     - panel
     - The panel shows a QR code for its own WPA2 access point. Scanning it with a phone
       camera joins the network, and the captive portal opens the setup page by itself.
   * - 3
     - Authorise
     - both
     - The page shows a four-digit browser number and the terminal shows the same one. If they
       match, the operator enters the admin code on the panel. The session then carries a random
       128-bit token. If they do not match, another device is trying to connect.
   * - 4
     - Addresses
     - browser
     - Payout addresses are typed, or **read off a Cryptnox card** with one tap after its PIN:
       whichever card is tapped, its own addresses are used, so it must be the merchant's card.
       Each address is displayed on the panel and must be accepted there. Ethereum and Polygon
       share one address; Tron has its own.
   * - 5
     - Wi-Fi
     - browser
     - The venue network is picked from the terminal's own scan and the password entered.
       WPA2 only.
   * - 6
     - Finish
     - panel
     - The terminal restarts, which is what applies the configuration.

Step by step with screenshots: :doc:`guide-setup`.

Consequences worth knowing before deploying one:

* **There is no remote administration.** The access point is the radio's only interface while
  the portal is open, so an operator is in front of the terminal or nowhere. The terminal is
  off the venue network, and not taking payments, while the page is open (15-minute window,
  or until **Done**).
* **A terminal whose saved network has gone** opens the portal by itself with only the Wi-Fi
  step, and asks for no admin code, because that step cannot move money.
* **An asset with no payout address of its own is not offered** on the amount screen, and the
  panel says in red when the recipient shown is the firmware's built-in fallback.
* The same page, reached later from the control panel (swipe the grey bar at the bottom of the
  sale screen upwards, enter the admin code; tabs **Screen**, **Wi-Fi**, **Tx**, **About**),
  changes everything else: gas fees, production or test networks, token contracts, the clock's
  time zone, firmware updates. The terminal's own tabs report and do not edit; brightness and
  touch calibration are the exceptions. Details: :doc:`guide-settings`.

Taking a payment
================

1. **Amount.** The operator picks the network and the coin, types the amount on the keypad (two
   decimals, up to 9999.99) and taps **Charge**.
2. **Review.** The screen shows the amount, the destination address, the maximum network fee and
   the token contract in full; **Confirm** or **Cancel**.
3. **PIN.** The cardholder enters the card PIN on the on-screen keypad. It is verified before
   signing and wiped from RAM afterwards.
4. **Tap.** The cardholder holds the card on the PN532. The terminal opens the secure channel
   (*Processing*), the card signs the transaction hash (*Signing*), the terminal works out the
   signature's recovery bit against the card's own public key, RLP-encodes the EIP-1559
   transaction and broadcasts it (*Authorizing*), then polls for the receipt (*Confirming*).
5. **Approved or Declined.** *Approved* appears only on a mined receipt with status ``0x1``,
   for that transaction hash, to the expected contract, with a transfer to the payee for
   exactly that amount; the screen shows the amount, the coin and a shortened transaction hash.
   **New sale** starts the next one. Screens: :doc:`guide-payment`.

How long step 4 takes is the chain's block time plus the RPC provider's latency, not the
terminal's. A terminal with no network time refuses to sign rather than guess: SNTP is
corroborated against the HTTPS ``Date`` header, and a clock skew beyond the limit declines the
sale.

Supported assets and networks
=============================

.. list-table::
   :header-rows: 1
   :widths: 22 30 24 24

   * - Chain
     - Assets
     - Production network
     - Test network
   * - Ethereum
     - ETH, USDC, USDT
     - Ethereum mainnet
     - Sepolia
   * - Polygon
     - POL, USDC, USDT
     - Polygon
     - Amoy
   * - Tron
     - TRX, USDT (USDC on test network only)
     - Tron mainnet
     - Nile

The **Network** setting switches all three chains together between production and test
deployments; a mixed configuration is deliberately impossible. A terminal that has never been
told otherwise is on **production**, and switching restarts it. Token contracts are stored per
network; payout addresses are shared, since a card's address is the same account on either
deployment. USDC on Tron is a test-network-only selection: Circle wound it down in 2024–25.

.. _updating-over-wifi:

Updating over Wi-Fi
===================

The terminal never contacts GitHub or any update server. A browser carries the bytes:

1. Download the release's ``-app.bin`` on a phone or laptop **before** joining the terminal's
   access point.
2. On the terminal: Settings → About → **Update** (or Wi-Fi → **Configure**). Scan the QR
   code, enter the admin code on the panel.
3. **Browse** and pick the file. The terminal verifies the image and asks on its own screen;
   nothing reboots until somebody accepts it there. A version older than the running one is
   called out in red.
4. The first boot after an update is on probation: a reset before the panel, the card reader
   and the wallet layer have all come up returns to the previous firmware.

Updates wipe the settings, so the terminal runs first-time setup again. A unit flashed with
the old single-slot partition table cannot update over the air and needs one serial reflash
first. With the :ref:`secure build <secure-build>`, an image that is not signed with the
project's key is refused before it can ever become bootable.
