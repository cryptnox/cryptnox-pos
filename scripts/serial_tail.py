"""Print the device's serial output for N seconds, then exit.

ponytail: idf.py monitor wants a tty; this is the 15-line version that works
from a pipe. Usage: python scripts/serial_tail.py [PORT] [SECONDS]
"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0

with serial.Serial(port, 115200, timeout=1) as s:
    # ponytail: EN-low pulse via RTS, same as esptool's classic reset, so the
    # boot log lands inside the capture window instead of before it.
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    end = time.time() + secs
    while time.time() < end:
        data = s.read(4096)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
