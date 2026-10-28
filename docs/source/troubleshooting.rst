:description: Common Cryptnox POS symptoms and what to check, plus answers on which card pays, which chains are supported and how the dev kit differs from the C-WaaS terminal.

==============================
Troubleshooting and questions
==============================

Troubleshooting
===============

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Symptom
     - What to check
   * - ``No ACK from PN532`` at boot, or *Card not found*
     - The PN532 mode switches (``1 0`` for I²C, then unplug and replug USB), SDA on GPIO 27
       and SCL on GPIO 22, and the card centred on the antenna.
   * - Inverted colours or banding on gradients
     - Already handled in the firmware for the common panel. A blank or scrambled screen means
       the other ILI9341 variant: set ``CONFIG_TFT_ILI9341_DRIVER=y`` instead of ``_2`` and
       rebuild.
   * - Touch targets are offset
     - Calibrate: menu → admin code → **Screen** → **Touch**. An unconfirmed calibration
       reverts after 20 seconds, so a bad one cannot lock you out.
   * - Wi-Fi will not connect
     - Only WPA2 is supported. Check the network name and password.
   * - ``Signature check failed``
     - The card's signature did not verify under the public key it exported for the same
       path. Retry with the card held still; if it repeats, the card needs looking at.
   * - *Update* says there is no second firmware slot
     - The unit still has the old single-slot partition table. Reflash once over USB.
   * - Build says the app partition is full
     - The table has two 1.94 MB slots on 4 MB flash. The image assets and the panel fonts
       are the largest items; ``idf.py size-components`` shows where the space goes.

Frequently asked questions
==========================

**Can I use it to take payments in a shop?**
   Technically yes, and that is what it does. It is published as a reference dev kit with its
   limits stated plainly: no tamper resistance, secrets in RAM at run time, no payment record
   on the device. For a merchant programme with issued cards, terminals with a Secure Access
   Module and issuer controls, see `C-WaaS
   <https://cryptnox.com/card-wallet-as-a-service-cwaas/>`__.

**Which card pays?**
   Any Cryptnox Hardware Wallet card with firmware v1.6.0 or later that holds a seed. The
   cardholder's own self-custody card, not a card issued by the merchant.

**Does the terminal hold any keys?**
   No. The card signs inside its EAL6+ certified secure element after PIN verification. The
   terminal builds the transaction, asks the card for a signature and broadcasts the result.

**Does it need an internet connection?**
   Yes, over Wi-Fi (WPA2): to reach the JSON-RPC endpoint, broadcast the transaction and wait
   for the receipt. Without network time it refuses to sign.

**Which chains and assets does it accept?**
   ETH, USDC and USDT on Ethereum; POL, USDC and USDT on Polygon; TRX and USDT on Tron, each
   on the production network or its test network. The asset is picked on the device for each
   sale.

**Is this the same as the C-WaaS terminal?**
   No. The C-WaaS terminal is part of an issuer-run card scheme and uses a Secure Access Module
   and issuer co-signing. ``cryptnox-pos`` has no issuer: the cardholder's own card signs and
   the merchant's configured address receives the funds.

**Can I change the user interface or ship it in my own product?**
   Yes. The firmware is LGPL-3.0; a commercial licence is available for proprietary use
   without LGPL obligations. Ask through the `contact form <https://cryptnox.com/contact/>`__.
