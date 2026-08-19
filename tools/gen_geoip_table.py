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
# FORMAT 2 DELTA-CODES THE STARTS, AND THAT IS THE WHOLE REASON IPv6 FITS.
#
# Version 1 wrote an absolute 32-bit start per entry so the file could be binary-searched where it
# lay. 358k absolute addresses share almost no structure, so deflate only got 1.79 MB down to 995 KB.
# Subtracting the previous start turns them into small numbers, and a varint of a small number is
# what deflate is actually good at: the same v4 data lands at 294 KB. IPv6 has 348k ranges -- nearly
# as many as v4 -- and would have cost a further megabyte stored absolutely; delta-coded it costs
# about what v4 does. The client expands the stream once at load and searches arrays, so the only
# thing traded away is searching the mapped bytes directly.
#
# IPv6 IS TRUNCATED TO /64. In the 2026-08 file, 391 of 348,330 ranges split finer than a /64, and
# every one is a small allocation inside a single country's block. Keeping 128-bit keys to place
# those correctly would double the table; they instead resolve to whoever holds the surrounding /64.
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
import socket
import struct
import sys

MAGIC = b"FUAGEO2\0"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                   "src", "zandronum", "wadsrc", "static", "fua_geoip.dat")


def ip2int(text):
    a, b, c, d = (int(x) for x in text.split("."))
    return (a << 24) | (b << 16) | (c << 8) | d


def ip62int(text):
    return int.from_bytes(socket.inet_pton(socket.AF_INET6, text), "big")


def varint(value):
    out = bytearray()
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value & 0x7F)
    return bytes(out)


def build(rows, limit, index_for):
    """Ranges to (start, code index) entries: each runs until the next begins."""
    entries = []
    cursor = 0
    for lo, hi, code in rows:
        if lo > cursor:
            entries.append((cursor, 0))          # gap: nobody claims it
        idx = index_for(code)

        # Adjacent runs of the same country collapse: the entry means "from here until the next
        # entry", so a repeat would only cost bytes to say nothing.
        if not (entries and entries[-1][1] == idx):
            entries.append((lo, idx))
        cursor = hi + 1

    if cursor <= limit and (not entries or entries[-1][1] != 0):
        entries.append((cursor, 0))
    return entries


def encode(entries):
    """Delta-varint the starts. Entries must be sorted and non-decreasing."""
    out = bytearray()
    out += struct.pack("<I", len(entries))
    prev = 0
    for start, idx in entries:
        out += varint(start - prev)
        out.append(idx)
        prev = start
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: gen_geoip_table.py <dbip-country-lite.csv.gz>")

    src = sys.argv[1]
    opener = gzip.open if src.endswith(".gz") else open

    rows4 = []
    rows6 = []
    with opener(src, "rt", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            parts = line.rstrip("\n").split(",")
            if len(parts) != 3:
                continue
            lo, hi, code = parts
            code = code.strip().upper()
            try:
                if ":" in lo:
                    # Truncate to /64; see the header note on why the bottom half is dropped.
                    rows6.append((ip62int(lo) >> 64, ip62int(hi) >> 64, code))
                else:
                    rows4.append((ip2int(lo), ip2int(hi), code))
            except (ValueError, OSError):
                continue

    rows4.sort()
    rows6.sort()

    # Index 0 is "unknown" and is also what gaps resolve to.
    codes = ["ZZ"]
    code_index = {"ZZ": 0}

    def index_for(code):
        if code in ("ZZ", "--", ""):
            return 0
        if code not in code_index:
            code_index[code] = len(codes)
            codes.append(code)
        return code_index[code]

    entries4 = build(rows4, 0xFFFFFFFF, index_for)
    entries6 = build(rows6, 0xFFFFFFFFFFFFFFFF, index_for)

    if len(codes) > 255:
        sys.exit("more than 255 country codes; the index no longer fits a byte")

    out = bytearray()
    out += MAGIC
    out += struct.pack("<H", len(codes))
    for code in codes:
        out += code.encode("ascii")[:2].ljust(2, b"?")
    out += encode(entries4)
    out += encode(entries6)

    path = os.path.normpath(OUT)
    with open(path, "wb") as fh:
        fh.write(out)

    print("v4 ranges in : %d -> %d entries" % (len(rows4), len(entries4)))
    print("v6 ranges in : %d -> %d entries" % (len(rows6), len(entries6)))
    print("countries    : %d" % len(codes))
    print("bytes        : %d" % len(out))
    print("written      : %s" % path)


if __name__ == "__main__":
    main()
