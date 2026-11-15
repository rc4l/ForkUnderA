#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
#
# [Kaori-Chani] Tests for tic_pacing_check.parse_lines and .judge, the pair that decides whether a
# CI pacing run passed.
#
# The lines below are the REAL output of two measured runs on Windows, not a paraphrase: a host
# keeping 35.000 a second exactly, and an offscreen client whose own frames stalled while the
# clock kept time. The second is why the loop's figure is not fatal by default -- a job that
# failed on it would be measuring the runner's GPU, and would have gone red on the very first run.
#
# Stdlib only, no test framework:  python3 tools/tic_pacing_check_test.py
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("tic_pacing_check",
                                              os.path.join(HERE, "tic_pacing_check.py"))
tpc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tpc)

# A host, measured 2026-11-15: the time base keeping time.
HOST = """
[11:44:20] tic pacing: 70 of 70 tics run in 2000 ms; clock 35.000/s, run 35.000/s (35 asked), 0 behind
[11:44:22] tic pacing: 71 of 71 tics run in 2029 ms; clock 34.992/s, run 34.992/s (35 asked), 0 behind
[11:44:24] tic pacing: 71 of 70 tics run in 2027 ms; clock 34.533/s, run 35.027/s (35 asked), -1 behind
[11:44:26] tic pacing: 70 of 70 tics run in 2000 ms; clock 35.000/s, run 35.000/s (35 asked), 0 behind
"""

# The same build offscreen: the clock still right, the loop far behind it.
STALLED_LOOP = """
[11:44:39] tic pacing: 34 of 115 tics run in 3296 ms; clock 34.890/s, run 10.315/s (35 asked), 81 behind
[11:44:43] tic pacing: 34 of 151 tics run in 4332 ms; clock 34.856/s, run 7.848/s (35 asked), 117 behind
[11:44:46] tic pacing: 34 of 117 tics run in 3351 ms; clock 34.914/s, run 10.146/s (35 asked), 83 behind
[11:44:51] tic pacing: 34 of 147 tics run in 4200 ms; clock 35.000/s, run 8.095/s (35 asked), 113 behind
"""

# What the job exists to catch: the clock itself running at half rate.
STALLED_CLOCK = """
tic pacing: 1050 of 1050 tics run in 60000 ms; clock 17.500/s, run 17.500/s (35 asked), 0 behind
tic pacing: 1050 of 1050 tics run in 60000 ms; clock 17.500/s, run 17.500/s (35 asked), 0 behind
"""

failures = []


def check(what, got, want):
    if got != want:
        failures.append("%s: got %r, wanted %r" % (what, got, want))


def check_true(what, got):
    if not got:
        failures.append("%s: got %r, wanted something truthy" % (what, got))


# ---------------------------------------------------------------- parsing
host = tpc.parse_lines(HOST)
check("host window count", len(host), 4)
check("host first run tics", host[0]['run'], 70)
check("host first clock tics", host[0]['clock'], 70)
check("host first ms", host[0]['ms'], 2000)
check("host first clock rate", host[0]['clock_milli_hz'], 35000)
check("host first run rate", host[0]['run_milli_hz'], 35000)
check("host first asked", host[0]['asked'], 35)
check("host first behind", host[0]['behind'], 0)
# The third window's loop ran a tic past the window's edge, so behind is negative.
check("host third behind", host[2]['behind'], -1)
check("host third clock rate", host[2]['clock_milli_hz'], 34533)

check("nothing to parse", tpc.parse_lines("no pacing here\njust a log\n"), [])

# ---------------------------------------------------------------- judging
check("a host run passes", tpc.judge(host), [])

stalled_loop = tpc.parse_lines(STALLED_LOOP)
check("loop count", len(stalled_loop), 4)
check("a stalled loop is not the time base's fault", tpc.judge(stalled_loop), [])
strict = tpc.judge(stalled_loop, strict_loop=True)
check("a stalled loop fails under --strict-loop", len(strict), 1)
check_true("and it names the unrun tics", "394 tics" in strict[0])

stalled_clock = tpc.parse_lines(STALLED_CLOCK)
problems = tpc.judge(stalled_clock)
# Both windows, and the run as a whole.
check("a stalled clock fails every window and the run", len(problems), 3)
check_true("and it names the rate", "17.500 a second" in problems[0])
check_true("and the whole run too", "whole run" in problems[-1])

# A clock a shade off: inside the tolerance it passes, outside it fails, both per window and over
# the run. Thirty-second windows, so the one-tic edge allowance does not swamp the tolerance.
near = tpc.parse_lines(
    "tic pacing: 1059 of 1059 tics run in 30000 ms; clock 35.300/s, run 35.300/s (35 asked), 0 behind\n"
    "tic pacing: 1059 of 1059 tics run in 30000 ms; clock 35.300/s, run 35.300/s (35 asked), 0 behind\n")
check("a percent off passes the default tolerance", tpc.judge(near), [])
check("and fails a tighter one, window by window and overall",
      len(tpc.judge(near, tolerance=100)), 3)

# [Kaori-Chani] The edge allowance must not swallow a real fault: a 2-second window one whole tic
# light passes, while the same window at half rate does not.
edge = tpc.parse_lines(
    "tic pacing: 70 of 69 tics run in 2000 ms; clock 34.500/s, run 35.000/s (35 asked), -1 behind\n"
    "tic pacing: 71 of 71 tics run in 2000 ms; clock 35.500/s, run 35.500/s (35 asked), 0 behind\n")
check("one tic of window edge passes", tpc.judge(edge), [])

# No windows at all means the engine never paced, which must not read as a pass.
check_true("no windows is a failure", tpc.judge([]))
check_true("one window is a failure", tpc.judge(host[:1]))

if failures:
    print("tic_pacing_check_test: %d failure(s)" % len(failures))
    for f in failures:
        print("  " + f)
    sys.exit(1)

print("tic_pacing_check_test: ok")
