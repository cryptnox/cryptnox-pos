#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (c) 2026 Cryptnox SA
"""One-shot factory provisioning of a FRESH board into the full secure state:
Flash Encryption + encrypted NVS + Secure Boot v2.

Per board, in one command:
  1. sanity-check the board is fresh (aborts otherwise),
  2. generate a flash-encryption key for THIS board (by MAC) and burn it into
     eFuse BLOCK1 — one key per unit, so a key that leaks decrypts one unit's
     flash rather than every unit provisioned from the same file,
  3. enable Flash Encryption (FLASH_CRYPT_CONFIG=0xF, FLASH_CRYPT_CNT),
  4. flash the signed + pre-encrypted images (bootloader, table, app) via
     tools/secure_flash.py.
The Secure Boot public-key digest + ABS_DONE_1 are then burned automatically by
the bootloader on the first boot (no risky manual digest burn here).

============================  IRREVERSIBLE  ============================
This burns eFuses. A misconfigured board is bricked. Validate on a
sacrificial unit first. Requires --yes to actually burn anything.
=======================================================================

Prerequisites (generate ONCE per product/batch, keep OFFLINE):
  espsecure.py generate_signing_key --version 2 secure_keys/secure_boot_signing_key.pem
The flash-encryption key is generated per board by this script, as
secure_keys/flash_encryption_key_<mac>.bin. BACK EACH ONE UP OFFLINE: it is the
only way to serial-flash that board again (tools/secure_flash.py finds it by the
board's MAC); without it the board still takes signed OTA updates, nothing else.
--key FILE burns a given key instead (the old one-key-per-batch scheme).
and a build produced with the secure overlay:
  idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.flash_encryption" build

Usage:
  python tools/secure_provision.py --port COM12 --baud 921600 --yes
"""

import argparse
import glob
import json
import os
import subprocess
import sys

from fe_keys import board_key_path, board_mac

FLASH_CRYPT_CONF = "0xF"          # classic-ESP32 default key-tweak config


def find_key():
    hits = sorted(glob.glob("secure_keys/flash_encryption_key.*"))
    return hits[0] if hits else "secure_keys/flash_encryption_key.bin"


def efuse(port, *args):
    return [sys.executable, "-m", "espefuse", "--port", port, *args]


def fresh_check(port):
    """Abort unless every secure eFuse is still in its virgin state."""
    out = subprocess.check_output(
        efuse(port, "summary", "--format", "json"), text=True)
    # espefuse can print a connection banner before (and log lines after) the
    # JSON object, so isolate it instead of parsing the whole stdout.
    start = out.find("{")
    end = out.rfind("}")
    if start < 0 or end < 0:
        sys.exit("ABORT: espefuse returned no JSON summary (is the port free? "
                 "close any idf.py monitor / serial monitor and retry).\n"
                 "--- espefuse output ---\n" + out[:400])
    fuses = json.loads(out[start:end + 1])

    def val(name):
        return fuses.get(name, {}).get("value")

    cnt = val("FLASH_CRYPT_CNT")
    abs1 = val("ABS_DONE_1")
    if cnt not in (0, "0", None) and cnt != 0:
        sys.exit("ABORT: FLASH_CRYPT_CNT=%r — board already has Flash Encryption." % cnt)
    if abs1 not in (False, 0, "0", None):
        sys.exit("ABORT: ABS_DONE_1=%r — board already has Secure Boot." % abs1)
    print("Board is fresh (FLASH_CRYPT_CNT=0, ABS_DONE_1=0).")


def main():
    ap = argparse.ArgumentParser(description="Factory-provision a fresh board (IRREVERSIBLE).")
    ap.add_argument("--port", required=True)
    ap.add_argument("--key", default=None,
                    help="Burn this key file instead of generating one for the board")
    ap.add_argument("--baud", type=int, default=460800)
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--yes", action="store_true",
                    help="Confirm the IRREVERSIBLE eFuse burns (required)")
    args = ap.parse_args()

    if (args.key is not None) and not os.path.isfile(args.key):
        sys.exit("Key not found: %s" % args.key)
    if not os.path.isfile(os.path.join(args.build_dir, "flasher_args.json")):
        sys.exit("No build/flasher_args.json — build with the secure overlay first.")

    print("== 0. Verify the board is fresh ==")
    fresh_check(args.port)

    if args.key is None:
        args.key = board_key_path(board_mac(args.port))

    if not args.yes:
        print("\nDry run (no --yes): would now %s the FE key %s, burn it, enable "
              "Flash Encryption, and flash the signed+encrypted image.\n"
              "Re-run with --yes to perform the IRREVERSIBLE provisioning."
              % ("use" if os.path.isfile(args.key) else "generate", args.key))
        return

    if not os.path.isfile(args.key):
        # A file for this MAC already there means an earlier run generated it and
        # stopped before the burn (the board is still fresh, checked above) — it
        # is reused rather than replaced, so a backup taken then stays valid.
        print("\n== 0b. Generate this board's flash-encryption key ==")
        os.makedirs(os.path.dirname(args.key), exist_ok=True)
        subprocess.check_call([sys.executable, "-m", "espsecure",
                               "generate_flash_encryption_key", "--keylen", "256",
                               args.key])
    print("Key for this board: %s  <-- BACK IT UP OFFLINE" % args.key)

    print("\n== 1. Burn flash-encryption key (BLOCK1) ==")
    subprocess.check_call(efuse(args.port, "--do-not-confirm",
                                "burn_key", "flash_encryption", args.key))

    print("\n== 2. Enable Flash Encryption (FLASH_CRYPT_CONFIG + FLASH_CRYPT_CNT) ==")
    subprocess.check_call(efuse(args.port, "--do-not-confirm",
                                "burn_efuse", "FLASH_CRYPT_CONFIG", FLASH_CRYPT_CONF))
    subprocess.check_call(efuse(args.port, "--do-not-confirm",
                                "burn_efuse", "FLASH_CRYPT_CNT", "1"))

    print("\n== 3. Flash signed + pre-encrypted images (bootloader, table, app) ==")
    subprocess.check_call([
        sys.executable, "tools/secure_flash.py",
        "--port", args.port, "--baud", str(args.baud),
        "--key", args.key, "--build-dir", args.build_dir,
    ])

    print("\nProvisioning written. RESET the board now: the bootloader finalizes "
          "Secure Boot on first boot (burns the key digest + ABS_DONE_1).\n"
          "Verify after:  espefuse.py --port %s summary | findstr ABS_DONE_1" % args.port)


if __name__ == "__main__":
    main()
