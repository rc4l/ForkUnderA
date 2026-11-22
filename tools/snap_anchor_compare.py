#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
#
# [Kaori-Chani] The anchor comparison: one `fua_csnap_analyze <file> anchor` report against
# another, by the figures the interpolation decides.
#
# Step 3's last check is that the ported client draws a recording as the source tree does. The
# recording is fixed (ref.csnap, the archived reference beside ref.zsnap, which ZX_SNAP_REFDIR
# names), so a difference between two reports over it is the arithmetic differing and nothing
# else -- which is where a fixed_t -> zx::Fixed raw-value mistake would show. The committed
# fixture is the source tree's own report at its snap-ref-premerge tag, so the comparison needs
# that build once rather than on every platform and after every change here.
#
# What a report says about CLASSES is not the interpolation's and is left out, named in the
# output so a reader is never left guessing what was skipped: the glide tripwire block with its
# per-class table, the worst-class line, the worst step's class name, the drawn-state agreement
# and the unresolved-class warning. A .csnap carries the recording build's class numbering, and
# ForkUnderA's own actors shift the indices, so the same recording resolves id 2057 to
# PyroFireSpawner on the source tree and WatcherGibbedGenerator here -- which says nothing about
# how the poses were drawn. The header line is out too, since it names the file as it was passed
# and the case of that name is not a finding. Every other figure is comparable, with the
# recording's files loaded or without them.
#
# A report with no segment block or no summary line is a failure, not a pass: an empty
# comparison must never read as agreement.
#
# Stdlib only, no test framework:
#   python3 tools/snap_anchor_compare.py <fresh report or logfile> [<fixture>]

import argparse
import os
import re
import sys

# [Kaori-Chani] The default fixture: the tag's report, committed beside the recording the dump
# test reads.
FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "zandronum",
                       "src", "features", "net-snapshot", "testdata", "ref.csnap.anchor-tag.txt")

# A logfile's stamp, which carries a date first under sv_logfiletimestamp_usedate.
STAMP = re.compile(r"^\[(?:\d\d:\d\d:\d\d;)?\d\d:\d\d:\d\d\] ")

SEG = re.compile(r"^\[seg \d+\]")
SUMMARY = re.compile(r"fua_csnap_analyze: (?P<deltas>\d+) deltas, (?P<frames>\d+) frames, "
                     r"(?P<resets>\d+) resets, (?P<errors>\d+) decode errors")

# Every line the class table decides, which this comparison does not cover: the two outside the
# segment block, and inside it the glide tripwire with its per-class rows and the worst-class
# line. A table row is `<class name> <n> win  mean n% peak n% of Speed  bound n%`.
CLASSIDS = re.compile(r"fua_csnap_analyze: WARNING: \d+ of \d+ class ids do not resolve")
DRAWNSTATE = re.compile(r"fua_csnap_analyze: drawn state:")
CLASS_SEG = (
    re.compile(r"^glide \(tripwire\) SERVED"),
    re.compile(r"^glide STARVED"),
    re.compile(r"^worst classes:"),
    re.compile(r"^\S+\s+\d+ win\s+mean\s+\d+% peak\s+\d+% of Speed\s+bound \d+%"),
)


def class_dependent(line):
    """[Kaori-Chani] Whether a segment line is one of the class table's own."""
    text = line.strip()
    return any(p.match(text) for p in CLASS_SEG)

SUMMARY_FIELDS = ("deltas", "frames", "resets", "errors")


def strip_stamp(line):
    """[Kaori-Chani] The line as the engine printed it, without the logfile's stamp."""
    return STAMP.sub("", line.rstrip("\r\n"))


def normalise(line):
    """[Kaori-Chani] A compared segment line with the class table's answer taken out of it: the
    worst step names the class of the id it found, which this build numbers its own way."""
    return re.sub(r"(\bid \d+) \([^)]*\)", r"\1 (?)", line)


def segment_lines(text):
    """[Kaori-Chani] Every segment block, as a flat list of normalised lines: the `[seg n]` line
    and the indented lines under it, in order."""
    out = []
    inside = False
    for raw in text.splitlines():
        if raw.lstrip().startswith("#"):
            continue
        line = strip_stamp(raw)
        if SEG.match(line):
            inside = True
            out.append(normalise(line))
            continue
        if inside:
            # An indented line belongs to the block above it; anything else ends it.
            if line.startswith(" ") and line.strip() != "":
                if class_dependent(line) is False:
                    out.append(normalise(line))
            else:
                inside = False
    return out


def summary(text):
    """[Kaori-Chani] The four summary figures this comparison covers, or None where the report
    has no summary line at all."""
    for raw in text.splitlines():
        if raw.lstrip().startswith("#"):
            continue
        m = SUMMARY.search(strip_stamp(raw))
        if m:
            return {k: int(m.group(k)) for k in SUMMARY_FIELDS}
    return None


def excluded(text):
    """[Kaori-Chani] What was present and deliberately not compared, for the output to name."""
    out = []
    for raw in text.splitlines():
        if raw.lstrip().startswith("#"):
            continue
        line = strip_stamp(raw)
        if CLASSIDS.search(line) or DRAWNSTATE.search(line) or class_dependent(line):
            out.append(line.strip())
    return out


def differences(fixture_text, fresh_text):
    """[Kaori-Chani] Every difference in the compared parts, as readable lines. An empty list
    means they agree; a report missing its block or summary raises instead, since an empty
    comparison must not read as agreement."""
    a, b = segment_lines(fixture_text), segment_lines(fresh_text)
    if not a:
        raise ValueError("the fixture has no [seg] block; it is not an anchor report")
    if not b:
        raise ValueError("the fresh report has no [seg] block: the analyze did not run, or the "
                         "recording could not be opened")
    sa, sb = summary(fixture_text), summary(fresh_text)
    if sa is None:
        raise ValueError("the fixture has no summary line")
    if sb is None:
        raise ValueError("the fresh report has no summary line: the analyze did not finish")

    out = []
    if len(a) != len(b):
        out.append("segment lines: fixture has %d, this report has %d" % (len(a), len(b)))
    for i, (la, lb) in enumerate(zip(a, b)):
        if la != lb:
            out.append("segment line %d:\n  fixture: %s\n  here:    %s"
                       % (i + 1, la.strip(), lb.strip()))
    for k in SUMMARY_FIELDS:
        if sa[k] != sb[k]:
            out.append("summary %s: fixture %d, here %d" % (k, sa[k], sb[k]))
    return out


def main(argv=None):
    p = argparse.ArgumentParser(description="Compare a fua_csnap_analyze ... anchor report with "
                                            "the committed reference report.")
    p.add_argument("fresh", help="the report to check, or the logfile holding it")
    p.add_argument("fixture", nargs="?", default=FIXTURE,
                   help="the reference report (default: the committed tag fixture)")
    args = p.parse_args(argv)

    try:
        fresh_text = open(args.fresh, encoding="utf-8", errors="replace").read()
        fixture_text = open(args.fixture, encoding="utf-8", errors="replace").read()
    except OSError as e:
        print("snap_anchor_compare: %s" % e)
        return 1

    # [Kaori-Chani] One report a file: a logfile holding several would mix their blocks.
    reports = sum(1 for raw in fresh_text.splitlines() if SUMMARY.search(strip_stamp(raw)))
    if reports > 1:
        print("snap_anchor_compare: %s holds %d reports; pass one, cut from the log"
              % (args.fresh, reports))
        return 1

    try:
        diffs = differences(fixture_text, fresh_text)
    except ValueError as e:
        print("snap_anchor_compare: %s" % e)
        return 1

    skipped = excluded(fresh_text)
    print("not compared, the recording build's class numbering decides these (%d lines):"
          % len(skipped))
    for line in skipped:
        print("  " + line)

    if diffs:
        print("snap_anchor_compare: FAIL, %d difference(s) from %s"
              % (len(diffs), os.path.normpath(args.fixture)))
        for d in diffs:
            print("  " + d)
        return 2

    print("snap_anchor_compare: PASS, %d segment lines and the four summary figures agree with %s"
          % (len(segment_lines(fixture_text)), os.path.normpath(args.fixture)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
