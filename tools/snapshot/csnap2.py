#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
"""Decode a .csnap and report update-interval and displacement distributions.

  python3 csnap2.py recording.csnap
  python3 csnap2.py --verify path/to/format.h

The record walk and field widths are snap_payload.py's, the container is
record.h's. Reports distributions only, with no plausibility bound: a bound
is content calibration, not arithmetic (design doc section 9.4).

It reads what one client was sent, after the per-client pass, so client-side
quantities are sized here rather than from the server's .zsnap (section 9.4).
An id's history is dropped at its leave, as ids are recycled.
"""
import struct, sys, math
from collections import defaultdict

import snap_payload
from snap_payload import (DELTA, FRAME, RESET, MIRROR, MAP, G_POS, G_POSZ, G_ANG,
                          G_APPEAR, G_VELMISC, G_ENTER, G_EVENT, G_RENDER,
                          GROUPMASK, GB, EV_MOVER, MV_CLSNAME, MVF_BLOCKED,
                          MVF_GONE, mover_header, mover_plat)

# The container versions read, CSNAP_VERSION_MIN to CSNAP_VERSION
# (docs/net-snapshot/record.md, docs/net-snapshot/tools.md).
CSNAP_EXPECT = (14, 15, 16, 17, 18)
RINGS = [512, 1024, 2048, 4096, 8192]
NAMES = ['<512', '512-1k', '1k-2k', '2k-4k', '4k-8k', '>8k']


def pct(v, p):
    if not v:
        return 0.0
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p / 100))]


def main(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'CSNP', 'not a .csnap'
    ver, tic = struct.unpack_from('<HH', d, 4)
    if ver not in CSNAP_EXPECT:
        # Refused, as a layout mismatch misparses into output that looks like data.
        sys.exit(f'csnap v{ver}, decoder expects v{CSNAP_EXPECT[0]}-v{CSNAP_EXPECT[-1]}. '
                 'Re-record.')
    # The recording player, after the flags word, which the stream cannot give.
    local = struct.unpack_from('<i', d, 12)[0]
    o = 16
    ncv = struct.unpack_from('<H', d, o)[0]; o += 2
    cv = {}
    for _ in range(ncv):
        ln = d[o]; o += 1
        nm = d[o:o + ln].decode(); o += ln
        cv[nm] = struct.unpack_from('<i', d, o)[0]; o += 4
    print(f'csnap v{ver}, {tic} Hz, recorded by player {local}')
    print('cvars: ' + ', '.join(f'{k}={v}' for k, v in cv.items()))

    cls, last, cam = {}, {}, None
    byring, byclass = defaultdict(list), defaultdict(list)
    # `drawn` clears at a reset with everything keyed by id; `drawnids`, a running
    # total, does not, as a teardown reset ends every file.
    drawn, drawnby, drawnids = {}, defaultdict(int), set()
    # Movers, keyed by (class, is-polyobject, index) and kept across a reset
    # (docs/net-snapshot/tools.md).
    mv = defaultdict(int)
    mvlast, mvgap, mvplane = {}, defaultdict(list), defaultdict(list)
    mvstatus = defaultdict(int)
    ndelta = nframe = ndrawn = nerr = 0
    maps = []
    # The frame the deltas after a map record are relative to.
    frame = snap_payload.NARROW
    t0 = t1 = None
    g0 = g1 = None

    while o < len(d):
        k = d[o]; o += 1
        if k == FRAME:
            o += 4
            if ver >= 17:
                x, y, z = struct.unpack_from('<qqq', d, o); o += 24
            else:
                x, y, z = struct.unpack_from('<iii', d, o); o += 12
            o += 8                                   # angle, pitch
            valid = d[o]; o += 1
            changed = d[o]; o += 1
            if changed:
                m = struct.unpack_from('<H', d, o)[0]; o += 2
                for _ in range(m):
                    ln = d[o]; o += 1 + ln + 4
            if valid:
                cam = (x / 65536.0, y / 65536.0)
            nframe += 1
        elif k == DELTA:
            o += 4                                   # frame number: DELIVERY
            gametic = struct.unpack_from('<i', d, o)[0]; o += 4
            arrival = struct.unpack_from('<I', d, o)[0]; o += 4
            size = struct.unpack_from('<I', d, o)[0]; o += 4
            buf = d[o:o + size]; o += size
            if t0 is None:
                t0, g0 = arrival, gametic
            t1, g1 = arrival, gametic
            # The record walk is snap_payload.walk; this file supplies only
            # what to do with each record.
            def onrec(rec, buf=buf, arrival=arrival):
                if rec.kind == 'event':
                    # Movers only, which only the client stream records
                    # (docs/net-snapshot/tools.md).
                    if (rec.evtype & 0x1F) != EV_MOVER:
                        return
                    h = mover_header(rec.body, frame)
                    if h is None:
                        # [Kaori-Chani] A class the header declares with no body of its own is
                        # counted apart: unplaceable, but not a peer disagreeing about a length.
                        if (len(rec.body) > 0) and (rec.body[0] in snap_payload.MV_NOBODY):
                            mv['nobody'] += 1
                        else:
                            mv['bad'] += 1
                        return
                    cls_, flags, index, gen, body = h
                    # The class names the index space: sector, or polyobject.
                    key = (cls_, index)
                    mv['n'] += 1
                    mv['bytes'] += len(rec.body) + 1 + rec.idbytes
                    if flags & MVF_GONE:
                        mv['gone'] += 1
                    if flags & MVF_BLOCKED:
                        mv['blocked'] += 1
                    # Records per mover, and the gap between them.
                    prev = mvlast.get(key)
                    if prev is not None:
                        mvgap[key].append(arrival - prev)
                    mvlast[key] = arrival
                    if cls_ == snap_payload.MV_PLAT:
                        st = mover_plat(body)
                        mvstatus[st['status']] += 1
                        mvplane[key].append(st['planeD'])
                    return
                if rec.kind == 'leave':              # leave: forget the slot
                    last.pop(rec.id, None)
                    cls.pop(rec.id, None)
                    return
                eid = rec.id
                mask = rec.mask
                if rec.kind == 'enter':
                    cls[eid] = rec.classid
                    last.pop(eid, None)              # fresh entity in this slot
                # Z follows POS, or leads when POS is absent, as for anything
                # falling straight down.
                qx, qy, qz = snap_payload.decode_pos(mask, rec.body, frame)
                px = float(qx) if qx is not None else None
                py = float(qy) if qy is not None else None
                pz = float(qz) if qz is not None else None

                # Sampled on either group, or a falling entity would give none.
                if (px is None) and (pz is None):
                    return
                ox, oy, oz, ot = last.get(eid, (None, None, None, None))
                dt = (arrival - ot) if ot is not None else -1
                if 0 < dt < 6000:
                    # Each axis against the last position that carried it, or a
                    # stale Z would invent motion.
                    disp = (math.hypot(px - ox, py - oy)
                            if (px is not None and ox is not None) else None)
                    dz = (abs(pz - oz)
                          if (pz is not None and oz is not None) else None)
                    b = len(RINGS)
                    cx, cy = (px, py) if px is not None else (ox, oy)
                    if cam and (cx is not None):
                        dd = math.hypot(cx - cam[0], cy - cam[1])
                        for i, rr in enumerate(RINGS):
                            if dd <= rr:
                                b = i
                                break
                    byring[b].append((dt, disp, dz))
                    byclass[cls.get(eid, 0)].append((dt, disp, dz))
                last[eid] = (px if px is not None else ox,
                             py if py is not None else oy,
                             pz if pz is not None else oz, arrival)

            nerr += snap_payload.walk(buf, onrec,
                                      lambda m: print(f'payload at delta {ndelta}: {m}'),
                                      frame)
            ndelta += 1
        elif k == MIRROR:
            eid, qstate, msprite, mframe, mtics = struct.unpack_from('<HHHBB', d, o)
            o += 8
            drawn[eid] = (qstate, msprite, mframe, mtics)
            drawnids.add(eid)
            ndrawn += 1
            drawnby[cls.get(eid, 0)] += 1
        elif k == RESET:
            # u32 timestamp, then the reason length.
            o += 4
            ln = d[o]; o += 1 + ln
            cls.clear(); last.clear(); drawn.clear()
        elif k == MAP:
            ln = d[o]; o += 1
            name, crc = d[o:o + ln].decode(errors='replace'), d[o + ln:o + ln + 16].hex()
            o += ln + 16
            origin, widths = (0, 0, 0), (2, 2, 2)
            if ver >= 17:
                origin = struct.unpack_from('<iii', d, o); o += 12
                widths = tuple(d[o:o + 3]); o += 3
                # A width with no encoding, or a wide one before v18, would
                # misparse every payload after it.
                if not all(snap_payload.frame_width_known(w) for w in widths) or (
                        ver < 18 and widths != (2, 2, 2)):
                    sys.exit(f'map {name}: frame widths {widths} in a v{ver} file')
            frame = snap_payload.Frame(origin, widths)
            maps.append((name, crc, origin, widths))
        else:
            print(f'unknown record kind {k} at {o}')
            break

    # `is not None`, not `if t0`: a first arrival at 0 ms is data.
    dur = (t1 - t0) / 1000.0 if t0 is not None else 0.0
    print(f'{ndelta} deltas, {nframe} frames, {ndrawn} drawn-state changes, '
          f'{dur:.1f} s')
    # The frame only off narrow at origin 0, so a map that fits prints as it
    # always has.
    named = [f'{n} ({c}, origin {g[0]} {g[1]} {g[2]}, {w[0]} {w[1]} {w[2]} bytes an axis)'
             if (any(g) or w != (2, 2, 2)) else f'{n} ({c})'
             for n, c, g, w in maps]
    print('maps: ' + (', '.join(named) if named else 'none named'))
    # A parse error is reported, as a walk that stopped early looks plausible.
    if nerr:
        print(f'** {nerr} PAYLOAD PARSE ERRORS - figures below cover only what '
              f'parsed **')
    # Game tics against wall time, which shows a server that ran slow
    # (docs/net-snapshot/tools.md).
    if (g0 is not None) and (dur > 0):
        rate = (g1 - g0) / dur
        print(f'{g1 - g0} game tics over {dur:.1f} s = {rate:.1f}/s'
              f'{"" if rate > tic * 0.95 else "  ** SERVER WAS DILATING **"}')
    if ndrawn:
        rate = (f'{ndrawn / dur:.0f}/s, {ndrawn * 8 / dur / 1024:.1f} kB/s of the file'
                if dur > 0 else 'duration too short to rate')
        print(f'{len(drawnids)} entities changed drawn state; {rate}')
    print()

    # Movers, whenever the stream carried any, read as docs/net-snapshot/tools.md says.
    if mv['n'] or mv['bad'] or mv['nobody']:
        print('MOVERS')
        print(f'{mv["n"]} records over {len(mvlast)} movers, '
              f'{mv["bytes"]} B ({mv["bytes"] / dur / 1024:.2f} kB/s)'
              if dur > 0 else f'{mv["n"]} records over {len(mvlast)} movers')
        print(f'{mv["gone"]} gone, {mv["blocked"]} blocked, {mv["bad"]} unreadable'
              + (f', {mv["nobody"]} of a class with no body' if mv['nobody'] else '')
              + ('  ** UNREADABLE MOVER RECORDS: header mismatch **'
                 if mv['bad'] else ''))
        gaps = sorted(g for v in mvgap.values() for g in v)
        if gaps:
            print(f'interval ms: p50 {pct(gaps, 50):.0f}  p90 {pct(gaps, 90):.0f}'
                  f'  min {gaps[0]:.0f}  max {gaps[-1]:.0f}'
                  + ('   ** near one tic: is the plane being compared? **'
                     if pct(gaps, 50) < 60 else ''))
        if mvstatus:
            names = {0: 'up', 1: 'down', 2: 'waiting', 3: 'in_stasis'}
            print('plat status: ' + ', '.join(
                f'{names.get(k, k)} {v}' for k, v in sorted(mvstatus.items())))
        # Plane travel per mover, whether a record carried motion or only a status.
        moved = sum(1 for v in mvplane.values() if len(set(v)) > 1)
        print(f'{moved} of {len(mvplane)} movers changed plane between records')
        print()

    # XY and Z side by side, each with its own count, as an entity may be sampled
    # on one axis only.
    hdr = (f'{"":8} {"updates":>8} {"Hz":>6} {"gap p50":>8} {"p90":>7} '
           f'{"p99":>7} | {"XY p50":>7} {"p90":>7} {"p99":>7} '
           f'| {"Z n":>6} {"Z p50":>6} {"p90":>7} {"p99":>7}')
    print('BY DISTANCE RING (from the recorded camera each frame)')
    print(hdr)
    for b in range(len(RINGS) + 1):
        v = byring[b]
        if not v:
            continue
        g = [a for a, _, _ in v]
        j = [x for _, x, _ in v if x is not None]
        z = [x for _, _, x in v if x is not None]
        print(f'{NAMES[b]:8} {len(v):8d} {1000.0/(sum(g)/len(g)):6.1f} '
              f'{pct(g,50):8.0f} {pct(g,90):7.0f} {pct(g,99):7.0f} | '
              f'{pct(j,50):7.0f} {pct(j,90):7.0f} {pct(j,99):7.0f} | '
              f'{len(z):6d} {pct(z,50):6.0f} {pct(z,90):7.0f} {pct(z,99):7.0f}')

    print('\nBY CLASS (kind inferred from median XY speed, not from the wire)')
    print(hdr + f' {"u/tic":>7}  kind')
    for c, v in sorted(byclass.items(), key=lambda kv: -len(kv[1]))[:12]:
        g = [a for a, _, _ in v]
        j = [x for _, x, _ in v if x is not None]
        z = [x for _, _, x in v if x is not None]
        spd = [x / (a / 28.57) for a, x, _ in v if a > 0 and x is not None]
        med = pct(spd, 50)
        print(f'{c:8d} {len(v):8d} {1000.0/(sum(g)/len(g)):6.1f} '
              f'{pct(g,50):8.0f} {pct(g,90):7.0f} {pct(g,99):7.0f} | '
              f'{pct(j,50):7.0f} {pct(j,90):7.0f} {pct(j,99):7.0f} | '
              f'{len(z):6d} {pct(z,50):6.0f} {pct(z,90):7.0f} {pct(z,99):7.0f} '
              f'{med:7.1f}  {"missile" if med > 6 else "monster"}')


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[1] == '--verify':
        snap_payload.verify_header(sys.argv[2])
    elif len(sys.argv) > 1:
        main(sys.argv[1])
    else:
        sys.exit('usage: csnap2.py <recording.csnap> | --verify <format.h>')
