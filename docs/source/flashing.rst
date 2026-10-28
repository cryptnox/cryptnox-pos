:description: Flashing Cryptnox POS firmware onto an ESP32 board from a prebuilt release image or from source with ESP-IDF, what config.h holds, and how to prepare the Cryptnox card that pays.

=======================
Firmware and the card
=======================

Flashing the firmware
=====================

Prebuilt image (no toolchain)
-----------------------------

Each `release <https://github.com/cryptnox/cryptnox-pos/releases>`__ ships two files:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - File
     - Use
   * - ``cryptnox-pos-<version>-full.bin``
     - Fresh install over USB, written at flash offset ``0x0``
   * - ``cryptnox-pos-<version>-app.bin``
     - Update of an already installed terminal, over Wi-Fi (see :ref:`updating-over-wifi`)

Over USB, with ``esptool`` (``pip install esptool``), on the port the board enumerates as
(``COM3``, ``/dev/ttyUSB0``, …):

.. code-block:: text

   python -m esptool --chip esp32 --port COM3 -b 460800 write_flash 0x0 cryptnox-pos-v1.0.0-full.bin

Without installing anything, Espressif's `web flasher <https://espressif.github.io/esptool-js/>`__
in Chrome or Edge does the same with flash address ``0x0``. The board restarts into first-time
setup.

Building from source
--------------------

1. Install `ESP-IDF v5.5
   <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/get-started/index.html>`__ and
   open its environment (``export.sh`` on macOS and Linux, the ESP-IDF PowerShell on
   Windows).
2. Clone the repository **with submodules**; the Cryptnox SDK, TFT_eSPI and
   XPT2046_Touchscreen are submodules, and LVGL and arduino-esp32 are fetched from the IDF
   component registry on the first build:

   .. code-block:: text

      git clone --recursive https://github.com/cryptnox/cryptnox-pos.git
      cd cryptnox-pos

3. Copy the configuration template and fill it in (``main/config.h`` is gitignored):

   .. code-block:: text

      cp config.template.h main/config.h

4. Set the target, build, flash and watch the log:

   .. code-block:: text

      idf.py set-target esp32
      idf.py -p PORT build flash monitor

The first build is long: it downloads and compiles LVGL and the Arduino runtime.

.. warning::

   Never use plain ``idf.py flash`` on a board that has already been provisioned with the
   :ref:`secure build <secure-build>`: it would write plaintext that the chip then
   mis-decrypts. Use the repository's ``tools/secure_flash.py`` instead.

What goes in ``config.h`` and what does not
===========================================

``config.h`` holds what is fixed per build: the JSON-RPC endpoints for each network (PublicNode
by default), chain IDs, the token contract addresses for the production and test deployments,
EIP-1559 gas defaults, optional TLS certificate pins, and **fallback** payout addresses. Every
address must be in EIP-55 mixed-case form; the firmware checks the checksum at boot and refuses
to start when a ``config.h`` address fails it. A payout address set on the device that fails it
does not stop the boot, but that payout then refuses every sale. An all-lowercase address
carries no checksum at all and only earns a warning in the log. ``tests/units/test_networks.cpp`` checks that each configured address
parses, but only a block explorer can confirm that a contract is the right one before the
terminal takes real money.

Four things are deliberately **not** in the firmware and are set on the device instead:

* **Wi-Fi** network and password, chosen during setup from the terminal's own scan;
* **payout addresses** and token contracts, typed in a browser or read off the operator's
  own Cryptnox card, and accepted on the panel;
* the **card PIN**, entered on the touchscreen for each sale and wiped from RAM after
  signing;
* the **amount**, entered on the keypad per sale.

.. _preparing-the-card:

Preparing the card
==================

The card that pays must hold a seed, with the Ethereum derivation path
``m/44'/60'/0'/0/0`` (and ``m/44'/195'/0'/0/0`` for Tron) available. A new card is initialised
once with the `Cryptnox CLI <https://docs.cryptnox.com/cryptnox-cli/>`__:

.. code-block:: text

   cryptnox initialize
   cryptnox seed generate

The same card can also be set up in the Cryptnox Wallet app on a phone. A card that was never
set up is reported as such by the terminal, rather than as a wrong PIN.
