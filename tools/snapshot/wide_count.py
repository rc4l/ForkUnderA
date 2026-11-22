#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
"""wide_count.py - price wide positions on recorded client streams.

Usage (from tools/snapshot/, beside snap_payload.py):
  python3 wide_count.py <file.csnap> [<file.csnap> ...]

Counts, per server frame, the records a wide axis grows: POS and POSZ groups,
branch events, the local block, and polyobject x/y in mover records' headers
and MOVETO targets. Prints the payload a frame carries and what a map wide in
x and y, or in x, y and z, would add at 3 bytes an axis, counting only the
axes the recording's own frame has narrow. Payload bytes, not wire bytes.
Cosmetic events and point sounds travel in their own commands and are not in
a .csnap.

The record kinds come from snap_payload, as csnap2.py's do, and payloads are
grouped by game tic, so a server frame split across pumps counts once.
"""
import struct
import sys
from collections import defaultdict

import snap_payload
from snap_payload import (G_POS, G_POSZ, DELTA, FRAME, RESET, MIRROR, MAP, EV_MOVER,
                          EV_BRANCH, EV_LOCAL)

CSNAP_EXPECT = (14, 15, 16, 17, 18)


def count(path):
    d = open(path, 'rb').read()
    if d[:4] != b'CSNP':
        sys.exit(f'{path}: not a .csnap')
    ver = struct.unpack_from('<H', d, 4)[0]
    if ver not in CSNAP_EXPECT:
        sys.exit(f'{path}: csnap v{ver}, this script reads v{CSNAP_EXPECT[0]}-v{CSNAP_EXPECT[-1]}')
    o = 16
    ncv = struct.unpack_from('<H', d, o)[0]; o += 2
    for _ in range(ncv):
        ln = d[o]; o += 1 + ln + 4

    # Keyed by game tic, since a server frame spans several payloads (chunks):
    # the counts, then the bytes a wide x and y, and a wide x, y and z, would add.
    per = defaultdict(lambda: [0, 0, 0, 0, 0, 0, 0, 0, 0])
    # payload, records, POS, POSZ, branch, local, poly x/y pairs, +xy, +xyz
    errors = 0
    frame = snap_payload.NARROW
    already = set()
    while o < len(d):
        k = d[o]; o += 1
        if k == FRAME:
            o += 4 + (24 if ver >= 17 else 12) + 8     # time, pose (64-bit from v17), angle and pitch
            o += 1                                      # valid
            changed = d[o]; o += 1
            if changed:
                m = struct.unpack_from('<H', d, o)[0]; o += 2
                for _ in range(m):
                    ln = d[o]; o += 1 + ln + 4
        elif k == DELTA:
            o += 4                                      # delivery frame
            tic = struct.unpack_from('<i', d, o)[0]; o += 4
            o += 4                                      # arrival
            size = struct.unpack_from('<I', d, o)[0]; o += 4
            buf = d[o:o + size]; o += size
            f = per[tic]
            f[0] += size
            # One byte per position on each axis the frame has narrow.
            nx, ny, nz = (1 if w == snap_payload.FRAME_NARROW else 0 for w in frame.width)

            def onrec(rec, f=f, fr=frame, nx=nx, ny=ny, nz=nz):
                f[1] += 1
                if rec.kind == 'event':
                    et = rec.evtype & 0x1F
                    if et in (EV_BRANCH, EV_LOCAL):
                        f[4 if et == EV_BRANCH else 5] += 1
                        f[7] += nx + ny
                        f[8] += nx + ny + nz
                    elif et == EV_MOVER:
                        h = snap_payload.mover_header(rec.body, fr)
                        if h is not None and h[0] == snap_payload.MV_POLY:
                            p = snap_payload.mover_poly(h[4], fr)
                            pairs = 1 + sum(1 for e in p['entries']
                                            if e['type'] == snap_payload.PO_TYPENAME[2])
                            f[6] += pairs
                            f[7] += (nx + ny) * pairs
                            f[8] += (nx + ny) * pairs
                    return
                m = rec.mask or 0
                if m & G_POS:
                    f[2] += 1
                    f[7] += nx + ny
                    f[8] += nx + ny
                if m & G_POSZ:
                    f[3] += 1
                    f[8] += nz
            errors += snap_payload.walk(buf, onrec, lambda msg: None, frame)
        elif k == MIRROR:
            o += 8
        elif k == RESET:
            o += 4
            ln = d[o]; o += 1 + ln
        elif k == MAP:
            ln = d[o]; o += 1 + ln + 16
            if ver >= 17:
                origin = struct.unpack_from('<iii', d, o)
                widths = tuple(d[o + 12:o + 15])
                o += 15
                if not all(snap_payload.frame_width_known(w) for w in widths):
                    sys.exit(f'{path}: frame widths {widths}, which no encoding has')
                frame = snap_payload.Frame(origin, widths)
                already.update(a for a in range(3) if widths[a] != snap_payload.FRAME_NARROW)
        else:
            print(f'{path}: unknown record kind {k} at {o}; stopping')
            break

    frames = [v for v in per.values() if v[1] > 0]
    n = len(frames)
    if n == 0:
        print(f'{path}: no frames')
        return
    tot = [sum(v[i] for v in frames) for i in range(9)]
    xy = sorted(v[7] for v in frames)
    xyz = sorted(v[8] for v in frames)
    p99 = lambda s: s[min(len(s) - 1, (len(s) * 99) // 100)]
    print(f'{path}: {n} frames, {errors} walk errors')
    if already:
        print('  already wide on ' + ' '.join('xyz'[a] for a in sorted(already))
              + '; those axes add nothing below')
    print(f'  per frame: {tot[0] / n:.0f} B payload, {tot[1] / n:.1f} records, '
          f'{tot[2] / n:.1f} POS ({100.0 * tot[2] / max(tot[1], 1):.0f}%), '
          f'{tot[3] / n:.1f} POSZ, {tot[4] / n:.2f} branch, {tot[5] / n:.2f} local, {tot[6] / n:.2f} poly x/y')
    print(f'  wide in x and y:     +{tot[7] / n:.0f} B a frame '
          f'(+{100.0 * tot[7] / tot[0]:.1f}% of payload), p99 +{p99(xy)} B')
    print(f'  wide in x, y and z:  +{tot[8] / n:.0f} B a frame '
          f'(+{100.0 * tot[8] / tot[0]:.1f}% of payload), p99 +{p99(xyz)} B')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for p in sys.argv[1:]:
        count(p)
