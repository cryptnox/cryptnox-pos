# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (c) 2026 Cryptnox SA
"""Per-board flash-encryption key files.

One key per board, named by its factory MAC:
    secure_keys/flash_encryption_key_<mac>.bin
so a key that leaks decrypts one unit's flash, not every unit ever provisioned.
tools/secure_provision.py creates it; tools/secure_flash.py finds it again by
asking the board for its MAC. The older single shared file
(secure_keys/flash_encryption_key.bin) is still honoured as a fallback for
boards provisioned before this existed.
"""

import os
import re
import subprocess
import sys

KEY_DIR = "secure_keys"


def board_mac(port):
    """The board's factory MAC as 12 lower-case hex digits."""
    out = subprocess.check_output(
        [sys.executable, "-m", "esptool", "--port", port, "read_mac"],
        text=True, stderr=subprocess.STDOUT)
    m = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", out)
    if not m:
        sys.exit("Could not read the board's MAC on %s:\n%s" % (port, out[-400:]))
    return m.group(1).replace(":", "").lower()


def board_key_path(mac):
    return os.path.join(KEY_DIR, "flash_encryption_key_%s.bin" % mac)
