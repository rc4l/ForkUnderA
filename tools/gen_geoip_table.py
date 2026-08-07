#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Turn DB-IP's free IP-to-Country CSV into the compact table ZandroX ships.
#
# We ship a table at all because the GeoIP library is compiled in but no database ever was, so every
# internet server resolved to "unknown" and drew a "?" instead of a flag. Relying on the system
# having /usr/share/GeoIP/GeoIP.dat is a Linux-only assumption, and MaxMind discontinued the legacy
# .dat format in 2019, so there is nothing to fetch even if we wanted to.
#
# Aggregating to /16 was measured first, because a 64 KB table would have been free: it puts 6.8% of
# the address space in the wrong country and sends 1.1.1.1 to Thailand. Exact ranges cost more and
# are the only version worth drawing a flag from.
#
# Output goes into wadsrc so it becomes a lump inside zandronum.pk3. That is deliberate: a loose data
# file in the dist folder is exactly the thing a packaging step forgets, which is the bug being
# fixed here. The pk3 also deflates it, so the download grows by far less than the raw size.
#
# Usage:
#   curl -LO https://download.db-ip.com/free/dbip-country-lite-YYYY-MM.csv.gz
#   python tools/gen_geoip_table.py dbip-country-lite-YYYY-MM.csv.gz
#
# Data: DB-IP IP to Country Lite, CC BY 4.0. Attribution is in THIRD-PARTY-NOTICES.txt.

import gzip
import os
import struct
import sys

MAGIC = b"FUAGEO1\0"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                   "src", "zandronum", "wadsrc", "static", "fua_geoip.dat")


def ip2int(text):
    a, b, c, d = (int(x) for x in text.split("."))
    return (a << 24) | (b << 16) | (c << 8) | d


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: gen_geoip_table.py <dbip-country-lite.csv.gz>")

    src = sys.argv[1]
    opener = gzip.open if src.endswith(".gz") else open

    rows = []
    with opener(src, "rt", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            parts = line.rstrip("\n").split(",")
            if len(parts) != 3:
                continue
            lo, hi, code = parts
            # IPv6 is skipped: the browser only ever has IPv4 addresses to colour.
            if ":" in lo or ":" in hi:
                continue
            try:
                rows.append((ip2int(lo), ip2int(hi), code.strip().upper()))
            except ValueError:
                continue

    rows.sort()

    # Index 0 is "unknown" and is also what gaps resolve to.
    codes = ["ZZ"]
    code_index = {"ZZ": 0}

    entries = []
    cursor = 0
    for lo, hi, code in rows:
        if lo > cursor:
            entries.append((cursor, 0))          # gap: nobody claims it
        if code in ("ZZ", "--", ""):
            idx = 0
        else:
            if code not in code_index:
                code_index[code] = len(codes)
                codes.append(code)
            idx = code_index[code]

        # Adjacent runs of the same country collapse: the entry means "from here until the next
        # entry", so a repeat would only cost five bytes to say nothing.
        if entries and entries[-1][1] == idx:
            pass
        else:
            entries.append((lo, idx))
        cursor = hi + 1

    if cursor <= 0xFFFFFFFF and (not entries or entries[-1][1] != 0):
        entries.append((cursor, 0))

    if len(codes) > 255:
        sys.exit("more than 255 country codes; the index no longer fits a byte")

    out = bytearray()
    out += MAGIC
    out += struct.pack("<I", len(entries))
    out += struct.pack("<H", len(codes))
    for code in codes:
        out += code.encode("ascii")[:2].ljust(2, b"?")
    for start, idx in entries:
        out += struct.pack("<IB", start, idx)

    path = os.path.normpath(OUT)
    with open(path, "wb") as fh:
        fh.write(out)

    print("ranges in  : %d" % len(rows))
    print("entries out: %d" % len(entries))
    print("countries  : %d" % len(codes))
    print("bytes      : %d" % len(out))
    print("written    : %s" % path)


if __name__ == "__main__":
    main()
