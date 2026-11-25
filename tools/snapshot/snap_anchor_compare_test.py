#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
#
# [Kaori-Chani] Tests for snap_anchor_compare: which differences between two anchor reports are
# findings and which are the class table's.
#
# REPORT below is the real output of `fua_csnap_analyze ref.csnap anchor` on the ported build
# (23a903fc, Windows), with the logfile stamps left on one copy so the stamp stripping is
# exercised by the same text. Nothing here is a paraphrase.

import sys

import snap_anchor_compare as sac

REPORT = """[15:01:08] fua_csnap_analyze: ref.csnap (v18, 35 Hz, 14 cvars recorded, player 0)
[15:01:08] fua_csnap_analyze: applying recorded settings
[15:01:08]   fua_csnap_localanim = 3
[15:01:08] fua_csnap_analyze: recorded on MAP01, but no level is loaded; the gravity law is off for it
[15:02:22] [seg 0] 210.6 s, 40523 frames
[15:02:22]          3055 active entities | mean delay 492 ms | snaps 0.017/frame (worst 8300 u)
[15:02:22]          plan C: 13965 refills, 7326 on a new tic and 6639 on a delta alone, 2.90 frames each | 0 ghost refills with no start pose, 0 backdated | 4900617 ghost refills anchored, 12741 of them after a snap or a fresh start
[15:02:22]          ballistic draw error 0.10 u mean over 294594 steps, p99 1.3, max 25.3 | 71 past 8 u, 0 past 32 u
[15:02:22]          worst step 25.3 u: id 2057 (?) at 93845 ms, exit extrap, start anchored after a snap or a fresh start, 0 frames after a delta refill
[15:02:22]          smoothing: stepped 0 folds (mean 0.0 u), 0 snaps | extrapolated 2799 folds (mean 0.76 u), 8667 snaps, residual <0.5 11641 <2 817 <8 0 8+ 0 | 2847 tics off target, 0 stepped OUTSIDE its records | REVERSED 93 of 60603 moving tics
[15:02:22]          glide (tripwire) SERVED  0.00% of 0 windows | 0 sustained | worst 0% (id 0)
[15:02:22]          glide STARVED 0.00% of 0 windows | worst 0% (id 0)   <- refresh rate, not the sampler
[15:02:22]            WatcherGibbedGenerator    431 win  mean   12% peak    64% of Speed  bound 55%
[15:02:22]          worst classes: served ?, starved ?
[15:02:22] fua_csnap_analyze: 7379 deltas, 40523 frames, 2 resets, 0 decode errors, 1 segments over 210.6 s
[15:02:22] fua_csnap_analyze: WARNING: 104 of 123 class ids do not resolve; load the recording's files in its order
[15:02:22] fua_csnap_analyze: drawn state: 139681 recorded changes | replay agrees on 2576 of 288507 sampled frames (1%)
[15:02:22] fua_csnap_analyze: restored previous settings
"""

# The same report as a committed fixture would hold it: stamps gone, a header the tool skips.
FIXTURE = ("# The source tree's report at snap-ref-premerge, b8a72a262.\n"
           + "\n".join(sac.STAMP.sub("", l) for l in REPORT.splitlines()) + "\n")

PASSED = []
FAILED = []


def check(name, got, want):
    if got == want:
        PASSED.append(name)
    else:
        FAILED.append("%s\n  got:  %r\n  want: %r" % (name, got, want))


def count(fixture, fresh):
    """[Kaori-Chani] How many differences the tool reports, the fixture's own text by default."""
    return len(sac.differences(fixture, fresh))


def raises(fixture, fresh):
    try:
        sac.differences(fixture, fresh)
    except ValueError:
        return True
    return False


# ---- the same run, stamped and unstamped, agrees with itself ----
check("a report agrees with its own fixture", count(FIXTURE, REPORT), 0)
check("the stamps are not compared", count(FIXTURE, FIXTURE), 0)

# The header names the file as it was passed, and the fixture was taken with it in capitals.
check("the header line is not compared",
      count(FIXTURE.replace("ref.csnap (v18", "REF.csnap (v18"), REPORT), 0)

# ---- the class table's answers are not findings ----
check("the unresolved-class count is not compared",
      count(FIXTURE, REPORT.replace("104 of 123", "116 of 123")), 0)
check("the drawn-state line is not compared",
      count(FIXTURE, REPORT.replace("agrees on 2576 of 288507 sampled frames (1%)",
                                    "agrees on 251932 of 288507 sampled frames (87%)")), 0)
check("a class name beside an id is not compared",
      count(FIXTURE, REPORT.replace("id 2057 (?)", "id 2057 (PyroFireSpawner)")), 0)
check("the worst-class names are not compared",
      count(FIXTURE, REPORT.replace("worst classes: served ?, starved ?",
                                    "worst classes: served PyroFireSpawner, starved Zombieman")), 0)
check("the glide tripwire's served line is not compared",
      count(FIXTURE, REPORT.replace("glide (tripwire) SERVED  0.00% of 0 windows | 0 sustained",
                                    "glide (tripwire) SERVED  3.10% of 822 windows | 4 sustained")),
      0)
check("the glide tripwire's starved line is not compared",
      count(FIXTURE, REPORT.replace("glide STARVED 0.00% of 0 windows",
                                    "glide STARVED 1.40% of 71 windows")), 0)
check("a per-class table row is not compared",
      count(FIXTURE, REPORT.replace("WatcherGibbedGenerator    431 win  mean   12% peak    64%",
                                    "PyroFireSpawner           602 win  mean   31% peak   118%")),
      0)
# The tag and the port need not print the same number of table rows.
check("more table rows on one side is not a difference",
      count(FIXTURE, REPORT.replace(
          "[15:02:22]            WatcherGibbedGenerator    431 win  mean   12% peak    64% of Speed  bound 55%\n",
          "[15:02:22]            PyroFireSpawner           602 win  mean   31% peak   118% of Speed  bound 125%\n"
          "[15:02:22]            ArachnotronShooter        118 win  mean    4% peak    22% of Speed  bound 55%\n")),
      0)
check("a glide block gone entirely is not a difference",
      count(FIXTURE, "\n".join(l for l in REPORT.splitlines()
                               if "glide " not in l and " win  mean " not in l
                               and "worst classes:" not in l) + "\n"), 0)

# ---- an interpolation figure differing IS a finding, wherever it sits ----
check("a delay in the segment header line",
      count(FIXTURE, REPORT.replace("mean delay 492 ms", "mean delay 493 ms")), 1)
check("a refill count", count(FIXTURE, REPORT.replace("13965 refills", "13966 refills")), 1)
check("an anchored-refill count",
      count(FIXTURE, REPORT.replace("4900617 ghost refills anchored",
                                    "4900618 ghost refills anchored")), 1)
check("a draw error", count(FIXTURE, REPORT.replace("max 25.3 |", "max 25.4 |")), 1)
check("the worst step's own figure",
      count(FIXTURE, REPORT.replace("worst step 25.3 u", "worst step 31.9 u")), 1)
check("a reversed-tic count", count(FIXTURE, REPORT.replace("REVERSED 93", "REVERSED 94")), 1)
check("two lines differing are two findings",
      count(FIXTURE, REPORT.replace("mean delay 492 ms", "mean delay 493 ms")
            .replace("REVERSED 93", "REVERSED 94")), 2)

# ---- the four summary figures ----
for field, old, new in (("deltas", "7379 deltas", "7380 deltas"),
                        ("frames", "40523 frames, 2 resets", "40524 frames, 2 resets"),
                        ("resets", "2 resets", "3 resets"),
                        ("decode errors", "0 decode errors", "1 decode errors")):
    check("the summary's %s" % field, count(FIXTURE, REPORT.replace(old, new, 1)), 1)

# A compared segment line gone is a difference, not a quiet shorter walk.
check("a missing segment line",
      count(FIXTURE, REPORT.replace(
          "[15:02:22]          ballistic draw error 0.10 u mean over 294594 steps, p99 1.3, max 25.3 | 71 past 8 u, 0 past 32 u\n",
          "")) >= 1, True)

# ---- an empty comparison must never read as agreement ----
check("a report with no segment block raises",
      raises(FIXTURE, "fua_csnap_analyze: ref.csnap: cannot open file\n"), True)
check("a report with no summary raises",
      raises(FIXTURE, "\n".join(REPORT.splitlines()[:11]) + "\n"), True)
check("an empty report raises", raises(FIXTURE, ""), True)
check("an empty fixture raises", raises("", REPORT), True)

# ---- what the output says it skipped ----
# The two outside the block, and inside it the two glide lines, one table row and worst classes.
check("the excluded lines are named", len(sac.excluded(REPORT)), 6)

print("snap_anchor_compare_test: %d passed, %d failed" % (len(PASSED), len(FAILED)))
for f in FAILED:
    print("FAIL " + f)
sys.exit(1 if FAILED else 0)
