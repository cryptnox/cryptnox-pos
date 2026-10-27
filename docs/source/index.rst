:description: Cryptnox POS is open-source firmware that turns an ESP32 "Cheap Yellow Display" and a PN532 NFC module into a crypto payment terminal paid by tapping a Cryptnox Hardware Wallet card. Parts, wiring, flashing, first-time setup, the payment flow, supported assets, the secure build and what the dev kit is not.

=================================================================
Cryptnox POS: Open-Source Crypto Payment Terminal (ESP32 Dev Kit)
=================================================================

*Covers* ``cryptnox-pos`` *v1.0.0.*

``cryptnox-pos`` is open-source firmware that turns a low-cost ESP32 touchscreen board, the
ESP32-2432S028 "Cheap Yellow Display" (CYD), plus a PN532 NFC module into a tap-to-pay crypto
payment terminal. The customer taps a **Cryptnox Hardware Wallet** card, the card signs the
transfer inside its secure element, the terminal broadcasts it to the chain and shows
**Approved** only once the transaction is mined.

Start here
==========

Four illustrated walkthroughs, one per thing you will actually do:

* :doc:`guide-assembly` — the board, the NFC module and the printed snap-fit shell, in
  seven steps.
* :doc:`guide-setup` — first run, from the admin code to the venue Wi-Fi.
* :doc:`guide-payment` — a sale from the amount to *Approved*.
* :doc:`guide-settings` — the control panel and the browser settings page.

The pages below are the reference: what the parts are, how they are wired, how the firmware
is built and flashed, and what the design does and does not defend.

.. toctree::
   :maxdepth: 1
   :caption: Illustrated guides

   guide-assembly
   guide-setup
   guide-payment
   guide-settings

.. toctree::
   :maxdepth: 2
   :caption: Reference

   hardware
   flashing
   operating
   security
   troubleshooting

What it is, and what it is not
==============================

.. important::

   **Reference and educational dev kit, not a tamper-resistant payment terminal.**
   The ESP32 is not a secure microcontroller and has no protected RAM. The optional
   :doc:`secure build <security>` protects secrets at rest and locks the boot chain, but at
   run time the card PIN (typed on the screen, never stored) and the Wi-Fi password sit in
   plaintext RAM. What keeps the money safe is the card: **private keys never leave the
   Cryptnox Hardware Wallet**, so a compromised terminal still cannot sign a payment the
   cardholder did not tap for.

It is **not** the C-WaaS terminal. Cryptnox's `C-WaaS blockchain card scheme
<https://cryptnox.com/card-wallet-as-a-service-cwaas/>`__ runs issuer-managed cards through
terminals with a Secure Access Module and issuer co-signing, for banks and card programmes.
``cryptnox-pos`` is the self-custody counterpart: anyone with a Cryptnox Hardware Wallet card
can pay, there is no issuer in the loop, and the merchant receives the funds directly at an
address they configured. How the C-WaaS terminal differs is described in `How the C-WaaS POS
terminal works <https://cryptnox.com/cwaas-pos-terminal-registration-and-payment-flow/>`__.

Related
=======

* `cryptnox-pos on GitHub <https://github.com/cryptnox/cryptnox-pos>`__: source code,
  releases, wiring diagram, the config-portal, over-the-air update and security-analysis
  documents.
* `cryptnox-pos API reference <api/>`__: the generated documentation for the firmware.
* **Cryptnox SDK for ESP32** [`HTML <https://docs.cryptnox.com/cryptnox-sdk-esp32/>`__]: the
  secure channel, PN532 transport and signing component the terminal is built on.
* **Cryptnox CLI** [`HTML <https://docs.cryptnox.com/cryptnox-cli/>`__]: initialising a card
  and generating its seed.
* **Hardware Wallet Technical Reference v2.0**
  [`HTML <https://docs.cryptnox.com/cryptnox-hardware-wallet/v2.0/>`__]: key derivation and
  signature commands used by the terminal.
* `Cryptnox Hardware Wallet Dual Card Set
  <https://shop.cryptnox.com/product/hardware-wallet-smartcard-dual/>`__: the card that pays.
* **Licence:** LGPL-3.0, or a commercial licence for proprietary use without LGPL
  obligations. Ask through the `contact form <https://cryptnox.com/contact/>`__.
