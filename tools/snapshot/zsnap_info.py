#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
"""zsnap_info.py - inspect and validate a .zsnap snapshot fixture file.

Usage: python3 zsnap_info.py <file.zsnap> [--decode]
       python3 zsnap_info.py --verify <path/to/format.h>

Without --decode: parses the header and record framing, prints a summary
(records, tic/wall coverage, sizes, compression, saturation histogram).

With --decode: additionally decodes every delta payload with full presence
tracking and verifies the stream is self-consistent (baseline first, enters
only for absent ids, dirty/leave only for present ids, payload sizes exact).
Compressed records need the 'zstandard' module (pip install zstandard).

The record walk is snap_payload.walk, shared with csnap2.py: .zsnap and
.csnap differ only in framing. A file of a wire version outside
snap_payload.WIRE_READABLE is refused, not decoded.

Exit codes (for CI gating):
  0 = PASS: stream valid and file complete
  1 = usage, or a header/version mismatch
  2 = FAIL: decode errors found
  3 = PARTIAL: an undecodable record aborted validation (e.g. no zstandard)
  4 = TRUNCATED: stream valid as far as it goes, but the file ends mid-record
      (the expected signature of a crash-terminated recording - the recorder
      flushes once per stats window). Gate on {0} for strict integrity, or
      {0,4} to accept crash tails.

Framing (little-endian), written by capture.cpp:
  header: "ZSNP" | u16 version | u16 ticrate | u32 flags | u32 reserved
          then from v33 the map's frame: i32 origin x3 | u8 width x3 (2, or 3
          from v34), every position in the payloads relative to it
  record: u32 gametic | u32 wallMS | u16 spanTics | u32 rawSize | u32 storedSize
          + storedSize payload bytes (raw if storedSize == rawSize, else zstd)
"""
import struct
import sys

import snap_payload

ZSNAP_EXPECT = snap_payload.WIRE_READABLE


def payload_walk(buf, present, entered, dirtied, left, frame=snap_payload.NARROW):
    """Decode one payload, mutating the presence set, and return error strings;
    record lengths come from snap_payload."""
    errors = []

    def onrec(rec):
        if rec.kind == 'event':
            return                      # events carry no presence meaning
        if rec.kind == 'leave':
            if rec.id not in present:
                errors.append("leave for absent id %d" % rec.id)
            present.discard(rec.id)
            left.append(rec.id)
            return
        if rec.kind == 'enter':
            if rec.id in present:
                errors.append("enter for id %d which is already present "
                              "(sender/receiver presence disagreement)" % rec.id)
            present.add(rec.id)
            entered.append(rec.id)
            return
        if rec.id not in present:
            errors.append("update for unknown id %d without an enter flag "
                          "(mask %d)" % (rec.id, rec.mask))
        dirtied.append(rec.id)

    snap_payload.walk(buf, onrec, errors.append, frame)
    return errors


def main():
    if "--verify" in sys.argv:
        rest = [a for a in sys.argv[1:] if not a.startswith("--")]
        if not rest:
            print(__doc__)
            return 1
        snap_payload.verify_header(rest[0])
        return 0

    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    decode = "--decode" in sys.argv
    if not args:
        print(__doc__)
        return 1
    path = args[0]

    dctx = None
    if decode:
        try:
            import zstandard
            dctx = zstandard.ZstdDecompressor()
        except ImportError:
            print("note: 'zstandard' not installed; compressed records will be "
                  "frame-checked but not decoded (pip install zstandard)")

    with open(path, "rb") as f:
        head = f.read(16)
        if len(head) < 16 or head[:4] != b"ZSNP":
            print("not a zsnap file (bad magic)")
            return 1
        version, ticrate, flags, _ = struct.unpack("<HHII", head[4:])
        print("zsnap v%d  ticrate=%d  flags=0x%x" % (version, ticrate, flags))

        # Refused, not warned about: a version mismatch misparses rather than
        # fails, and the output looks like data.
        if version not in ZSNAP_EXPECT:
            print("this reader decodes wire v%d-v%d only. Re-record the fixture, or "
                  "use a reader built for v%d."
                  % (ZSNAP_EXPECT[0], ZSNAP_EXPECT[-1], version))
            return 1

        # The header's size comes from its version; an older file's frame is
        # narrow at origin 0, and a v33 one was only ever narrow.
        frame = snap_payload.NARROW
        if version >= 33:
            fr = f.read(15)
            if len(fr) < 15:
                print("truncated zsnap header")
                return 1
            origin = struct.unpack("<iii", fr[:12])
            widths = tuple(fr[12:15])
            if (not all(snap_payload.frame_width_known(w) for w in widths)
                    or (version < 34 and widths != (2, 2, 2))):
                print("frame widths %s in a v%d file" % (widths, version))
                return 1
            frame = snap_payload.Frame(origin, widths)
            if any(origin) or frame.wide():
                print("map frame origin %d %d %d, %d %d %d bytes an axis"
                      % (origin + widths))

        records = 0
        raw_total = stored_total = 0
        empty = 0
        first_tic = last_tic = None
        first_ms = last_ms = None
        span_hist = {}
        present = set()
        decode_errors = 0
        applied_tic = None
        baseline_rec = None
        baseline_enters = None
        validated_through = 0
        decode_active = decode
        abort_reason = None

        truncated = False
        while True:
            rh = f.read(18)
            if len(rh) == 0:
                break
            if len(rh) < 18:
                print("WARNING: truncated record header after record #%d "
                      "(crash tail? recorder flushes once per stats window)" % records)
                truncated = True
                break
            tic, wall, span, raw_sz, stored_sz = struct.unpack("<IIHII", rh)
            payload = f.read(stored_sz)
            if len(payload) < stored_sz:
                print("WARNING: truncated payload in record #%d "
                      "(crash tail? recorder flushes once per stats window)"
                      % (records + 1))
                truncated = True
                break

            if first_tic is None:
                first_tic, first_ms = tic, wall
            last_tic, last_ms = tic, wall
            records += 1
            raw_total += raw_sz
            stored_total += stored_sz
            if raw_sz == 0:
                empty += 1
            span_hist[span] = span_hist.get(span, 0) + 1

            if decode and raw_sz > 0 and decode_active:
                # An undecodable record aborts, as presence after it is unsound.
                if stored_sz == raw_sz:
                    data = payload
                elif dctx is not None:
                    try:
                        data = dctx.decompress(payload, max_output_size=raw_sz)
                    except Exception as exc:
                        print("record #%d: decompression failed (%s)" % (records, exc))
                        decode_errors += 1
                        decode_active = False
                        abort_reason = "corrupt zstd frame at record #%d" % records
                        continue
                    if len(data) != raw_sz:
                        print("record #%d: decompressed size mismatch" % records)
                        decode_errors += 1
                        decode_active = False
                        abort_reason = "size mismatch at record #%d" % records
                        continue
                else:
                    decode_active = False
                    abort_reason = ("record #%d is zstd-compressed and 'zstandard' "
                                    "is not installed" % records)
                    continue
                # A recording's tics only go forward, so one before the last
                # decoded record's means its records are out of order.
                if applied_tic is not None and tic < applied_tic:
                    print("record #%d: tic %d is before tic %d, already decoded"
                          % (records, tic, applied_tic))
                    decode_errors += 1
                else:
                    applied_tic = tic
                ent, dirt, left = [], [], []
                errs = payload_walk(data, present, ent, dirt, left, frame)
                if baseline_rec is None:
                    # The baseline is the first NON-EMPTY record, wherever it
                    # falls - report its true number, never a hardcoded #1.
                    baseline_rec = records
                    baseline_enters = len(ent)
                    if dirt or left:
                        print("record #%d: expected pure baseline (first non-empty "
                              "record), got %d dirty / %d leave"
                              % (records, len(dirt), len(left)))
                        decode_errors += 1
                for e in errs:
                    print("record #%d: %s" % (records, e))
                decode_errors += len(errs)
                validated_through = records

    tics = (last_tic - first_tic) if records else 0
    walls = (last_ms - first_ms) / 1000.0 if records else 0.0
    print("records: %d (%d empty) | tics %d..%d (%d, %.1fs of game time)"
          % (records, empty, first_tic or 0, last_tic or 0, tics,
             tics / float(ticrate or 35)))
    print("wall coverage: %.1fs | avg cadence: %.1f records/s"
          % (walls, records / walls if walls > 0 else 0.0))
    print("raw: %.2f MB | stored: %.2f MB | file ratio: %.2fx"
          % (raw_total / 1048576.0, stored_total / 1048576.0,
             raw_total / float(stored_total) if stored_total else 0.0))
    sat = sum(c for s, c in span_hist.items() if s > 1)
    top = sorted(span_hist.items(), key=lambda kv: -kv[1])[:5]
    print("span histogram (top): %s | saturated records: %d/%d"
          % (", ".join("%dt x%d" % kv for kv in top), sat, records))
    if decode:
        if baseline_rec is not None:
            note = "" if baseline_rec == 1 else \
                " (records 1..%d were empty/timing-only)" % (baseline_rec - 1)
            print("baseline: %d entities entered in record #%d%s"
                  % (baseline_enters, baseline_rec, note))
        else:
            print("baseline: no record was decoded")
        if abort_reason is not None:
            print("stream decode: PARTIAL - validated records 1..%d, then stopped: %s"
                  % (validated_through, abort_reason))
            print("  (presence tracking cannot soundly continue past an undecoded record)")
            return 3 if decode_errors == 0 else 2
        verdict = "FAIL" if decode_errors else \
            ("PASS (file truncated mid-record)" if truncated else "PASS")
        print("stream decode: %s (%d errors) | entities present at end: %d"
              % (verdict, decode_errors, len(present)))
        if decode_errors:
            return 2
        return 4 if truncated else 0
    return 4 if truncated else 0


if __name__ == "__main__":
    sys.exit(main())
