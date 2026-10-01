#!/usr/bin/env python3
"""Hostile NTP server: answers every request with a shifted date.

The real test for item 2/3 — unlike forcing the clock in firmware, this
exercises the actual SNTP client path with an attacker-controlled server.

    python spoof_ntp.py --days -200      # back-date (must be REFUSED by floor)
    python spoof_ntp.py --minutes 30     # forward skew (must be caught by Date)

--answer-honestly-for N serves the true time for the first N seconds, so boot
passes the build-date floor and only a later background resync is hostile. That
is the only way to reach the resync path, which does not go through
net_time_sync() and so is never floor-checked.

    python spoof_ntp.py --days -200 --answer-honestly-for 20

Needs UDP/123, so run as Administrator — or override lwIP's SNTP_PORT and pass
a matching --port to avoid that (see docs/clock-hardening-testing.md). Point the
device at it by temporarily editing the ESP_SNTP_SERVER_LIST in main/net.cpp to
this machine's LAN IP.
"""
import argparse
import socket
import struct
import time

NTP_EPOCH_DELTA = 2208988800  # seconds between 1900-01-01 and 1970-01-01


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--days', type=float, default=0.0)
    ap.add_argument('--minutes', type=float, default=0.0)
    ap.add_argument('--bind', default='0.0.0.0')
    ap.add_argument('--port', type=int, default=123)
    ap.add_argument('--answer-honestly-for', type=float, default=0.0,
                    metavar='SECONDS',
                    help='serve the true time for this long before lying')
    args = ap.parse_args()

    offset = (args.days * 86400.0) + (args.minutes * 60.0)

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((args.bind, args.port))
    started = time.time()
    print(f'spoof NTP on {args.bind}:{args.port}, offset {offset:+.0f}s '
          f'-> claiming {time.ctime(time.time() + offset)}')
    if args.answer_honestly_for > 0.0:
        print(f'  honest for the first {args.answer_honestly_for:.0f}s')

    while True:
        data, addr = s.recvfrom(512)
        if len(data) < 48:
            continue

        honest = (time.time() - started) < args.answer_honestly_for
        lie = time.time() + (0.0 if honest else offset)
        ntp_secs = int(lie) + NTP_EPOCH_DELTA
        ntp_frac = int((lie - int(lie)) * (2 ** 32))

        # LI=0 VN=4 Mode=4 (server), stratum 1, poll 3, precision -20
        pkt = struct.pack('!BBBbIIIQQQ',
                          (0 << 6) | (4 << 3) | 4, 1, 3, -20,
                          0, 0, 0x4C4F434C,          # root delay/disp, refid
                          (ntp_secs << 32) | ntp_frac,   # reference
                          struct.unpack('!Q', data[40:48])[0],  # originate
                          (ntp_secs << 32) | ntp_frac)   # receive
        pkt += struct.pack('!Q', (ntp_secs << 32) | ntp_frac)   # transmit

        s.sendto(pkt, addr)
        print(f'  {"answered" if honest else "lied to"} {addr[0]}: '
              f'{time.ctime(lie)}')


if __name__ == '__main__':
    main()
