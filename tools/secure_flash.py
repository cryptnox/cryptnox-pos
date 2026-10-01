#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (c) 2026 Cryptnox SA
"""Pre-encrypt the build images with the host flash-encryption key and either
flash them or package a distributable image.

Because the images are encrypted on the host (with the same key burned into the
board's eFuse), the chip does NOT re-encrypt on boot, so FLASH_CRYPT_CNT is
never consumed — reflash as often as you like. Plain `idf.py flash` would
re-encrypt on boot and burn a counter cycle each time.

Targets the classic ESP32 scheme (FLASH_CRYPT_CONFIG-based, NOT AES-XTS, which
is for the S2/S3/C-series). Offsets and flash parameters are read from
build/flasher_args.json, so nothing is hard-coded.

Prerequisites:
  - The flash-encryption key burned into the board's eFuse, and the SAME key
    file kept here. Found by the board's MAC
    (secure_keys/flash_encryption_key_<mac>.bin, written by secure_provision.py),
    else the older shared secure_keys/flash_encryption_key.*; --key overrides.
  - A build produced with the overlay:
      idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.flash_encryption" build

Run from the repo root, inside the IDF environment:
  python tools/secure_flash.py --port COM5             # encrypt all images + flash
  python tools/secure_flash.py --port COM5 --app-only  # only the app @0x10000 (daily)
  python tools/secure_flash.py --package               # dist/ encrypted image, no flash
"""

import argparse
import glob
import json
import os
import subprocess
import sys

from fe_keys import board_key_path, board_mac

# ESP32 classic default flash-encryption config (the IDF bootloader burns
# FLASH_CRYPT_CONFIG to 0xF on first encrypted boot — host must match it).
FLASH_CRYPT_CONF = "0xf"


def find_key():
    hits = sorted(glob.glob("secure_keys/flash_encryption_key.*"))
    return hits[0] if hits else "secure_keys/flash_encryption_key.bin"


def run(cmd):
    print("  $", " ".join(cmd))
    subprocess.check_call(cmd)


def main():
    ap = argparse.ArgumentParser(
        description="Pre-encrypt + flash/package a secure build (classic ESP32).")
    ap.add_argument("--port", help="Serial port (required unless --package)")
    ap.add_argument("--key", default=None,
                    help="Flash-encryption key file (default: this board's, by MAC)")
    ap.add_argument("--build-dir", default="build", help="IDF build directory")
    ap.add_argument("--app-only", action="store_true",
                    help="Encrypt/flash only the app image (the common case)")
    ap.add_argument("--package", action="store_true",
                    help="Merge into a distributable encrypted image; do not flash")
    ap.add_argument("--out-dir", default="dist", help="Output dir for --package")
    ap.add_argument("--baud", type=int, default=460800,
                    help="Serial baud rate for flashing (try 921600 to go faster)")
    args = ap.parse_args()

    if not args.package and not args.port:
        ap.error("--port is required unless --package")
    if args.key is None:
        # The board's own key when it has one — the per-board scheme — else the
        # shared file older boards were provisioned with. A package is not for
        # one board, so it needs the shared key or an explicit --key.
        per_board = board_key_path(board_mac(args.port)) if args.port else None
        args.key = per_board if (per_board and os.path.isfile(per_board)) else find_key()
    print("key: %s" % args.key)
    if not os.path.isfile(args.key):
        sys.exit("Key not found: %s (burn it once, keep the same file here)" % args.key)
    fa_path = os.path.join(args.build_dir, "flasher_args.json")
    if not os.path.isfile(fa_path):
        sys.exit("No %s — build first with the flash_encryption overlay." % fa_path)

    with open(fa_path) as f:
        fa = json.load(f)
    write_args = fa.get("write_flash_args", [])
    flash_files = fa["flash_files"]               # { "0xADDR": "relative/path.bin" }
    app_rel = fa.get("app", {}).get("file")

    # With Secure Boot enabled, IDF drops the bootloader from flash_files (to
    # avoid an accidental bootloader reflash on a locked device). Add it back
    # explicitly — otherwise the signed bootloader never gets written and
    # Secure Boot is never activated (ABS_DONE_1 stays 0).
    boot_rel = "bootloader/bootloader.bin"
    if "0x1000" not in flash_files and \
            os.path.isfile(os.path.join(args.build_dir, boot_rel)):
        flash_files = dict(flash_files)
        flash_files["0x1000"] = boot_rel

    items = sorted(flash_files.items(), key=lambda kv: int(kv[0], 16))
    if args.app_only:
        items = [(o, r) for (o, r) in items if r == app_rel] or items[-1:]

    out_dir = args.out_dir if args.package else os.path.join(args.build_dir, "encrypted")
    os.makedirs(out_dir, exist_ok=True)

    pairs = []   # [offset, encrypted_path, ...]
    for offset, rel in items:
        src = os.path.join(args.build_dir, rel)
        enc = os.path.join(out_dir, os.path.basename(rel).rsplit(".", 1)[0] + "-enc.bin")
        print("encrypt %-9s %s" % (offset, rel))
        # The address is the XTS/tweak input — each image MUST be encrypted at
        # its real flash offset or it decrypts to garbage on the chip.
        run([sys.executable, "-m", "espsecure", "encrypt_flash_data",
             "--keyfile", args.key, "--flash_crypt_conf", FLASH_CRYPT_CONF,
             "--address", offset, "--output", enc, src])
        pairs += [offset, enc]

    if args.package:
        merged = os.path.join(out_dir, "cryptnox_pos-encrypted-full.bin")
        run([sys.executable, "-m", "esptool", "--chip", "esp32", "merge_bin",
             "-o", merged] + write_args + pairs)
        print("\nDistributable image: %s" % merged)
        print("Recipient flashes it onto a board holding the SAME key:")
        print("  esptool.py --port PORT write_flash --force 0x0 %s" % merged)
    else:
        # Write the already-encrypted images raw (NO --encrypt). --force allows
        # writing to a flash-encryption-enabled chip without re-encrypting.
        run([sys.executable, "-m", "esptool", "--port", args.port,
             "--baud", str(args.baud),
             "write_flash", "--force"] + write_args + pairs)
        print("\nDone — pre-encrypted flash written, FLASH_CRYPT_CNT untouched.")


if __name__ == "__main__":
    main()
