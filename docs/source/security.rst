:description: The optional Cryptnox POS secure build (flash encryption, encrypted NVS, Secure Boot v2) and what the design does and does not defend against.

========
Security
========

.. _secure-build:

Secure build
============

By default the firmware and its settings storage are **unencrypted and unsigned**, which is
right for a bench. The optional secure build adds:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Feature
     - Effect
   * - Flash Encryption
     - A flash dump is ciphertext without the key.
   * - Encrypted NVS (``nvs_keys`` partition)
     - The Wi-Fi password, payout addresses and gas settings are encrypted at rest.
   * - Secure Boot v2 (RSA-3072)
     - Only firmware signed with your key boots, and only such firmware installs over Wi-Fi.

.. warning::

   These burn **eFuses, which is irreversible**. Validate on a sacrificial board first; a
   misconfigured burn bricks the unit. On the classic ESP32, Secure Boot v2 supports **a single
   key with no revocation or rotation**: a leaked key cannot be revoked, and a lost key means
   no unit in the field can ever be updated again. Keep both keys offline, in several copies,
   and out of every repository.

The repository provides two configuration overlays (``sdkconfig.defaults.flash_encryption`` for
development, which stays reflashable, and ``sdkconfig.defaults.release`` for a locked
production unit), a one-shot provisioning script (``tools/secure_provision.py``) and a routine
reflash script (``tools/secure_flash.py``) that pre-encrypts images on the host. A unit built
with the development overlay still reports *Flash encryption mode is DEVELOPMENT (not secure)*
at boot, and its flash can be read out over the cable: the repository's own security analysis
lists shipping such a unit as the first thing to prevent. The full procedure is in the
repository README.

Security notes
==============

What the design defends, in the repository's own words (``docs/security-analysis.md``):

* **No private key on the terminal, ever.** The card signs; the terminal never holds a key
  and never sees a seed.
* **Every change that can redirect money is confirmed on the panel**, by a person in front
  of the terminal, never in the browser alone.
* **Payout addresses are stored in duplicate** and cross-checked, so one corrupted settings
  entry cannot silently redirect funds.
* **The node is not trusted for what gets signed.** On Ethereum and Polygon the transaction
  is built and hashed on the device, and a receipt counts only when it matches that hash,
  contract, payee and amount. On Tron the node builds the transaction, and the device checks
  it against the sale before the card signs its ID.
* **Firmware updates are signed** and verified against a key compiled into the running
  firmware.

What it does **not** defend:

* The ESP32 has no protected RAM: the card PIN and the Wi-Fi password are readable by anyone
  who can reach live memory.
* The setup page is plain HTTP, on the terminal's own WPA2 access point rather than the venue
  network. The session token and, during setup, the venue Wi-Fi password travel over that
  link.
* Nothing about a completed payment is recorded on the device: no receipt, no log. The chain
  is the record.
* The RPC provider is trusted for chain state. It cannot make the card sign a different
  transfer, but it could report a receipt that never landed; the chain, not the terminal's
  screen, is authoritative for settlement.
* No penetration test, side-channel or fault-injection analysis has been performed. The card
  applet is out of scope of that analysis.
