#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
#
# [Kaori-Chani] Tests for tic_pacing_check.parse_lines and .judge, the pair that decides whether a
# CI pacing run passed.
#
# The lines below are the REAL output of two measured runs on Windows, not a paraphrase, with the
# clock rate the engine no longer prints dropped from each: a server keeping 35.000 a second
# exactly, and a client on a machine with no GPU, whose every pass ran its unthreaded cap of 34
# tics while the counter kept time. The second is why the loop's figure can be made lenient -- a
# client leg that failed on it would be measuring the runner's renderer.
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

# A server, measured 2026-11-15: the time base keeping time.
HOST = """
[11:44:20] tic pacing: 70 of 70 tics run in 2000 ms; run 35.000/s (35 asked), 0 behind
[11:44:22] tic pacing: 71 of 71 tics run in 2029 ms; run 34.992/s (35 asked), 0 behind
[11:44:24] tic pacing: 71 of 70 tics run in 2027 ms; run 35.027/s (35 asked), -1 behind
[11:44:26] tic pacing: 70 of 70 tics run in 2000 ms; run 35.000/s (35 asked), 0 behind
"""

# The same build on a machine with no GPU: the counter still right, the loop far behind it.
STALLED_LOOP = """
[11:44:39] tic pacing: 34 of 115 tics run in 3296 ms; run 10.315/s (35 asked), 81 behind
[11:44:43] tic pacing: 34 of 151 tics run in 4332 ms; run 7.848/s (35 asked), 117 behind
[11:44:46] tic pacing: 34 of 117 tics run in 3351 ms; run 10.146/s (35 asked), 83 behind
[11:44:51] tic pacing: 34 of 147 tics run in 4200 ms; run 8.095/s (35 asked), 113 behind
"""

# What the job exists to catch, and what no single window can show: a counter running fast. Its
# tics and its milliseconds are one span scaled twice, so every line here reads exactly like a
# correct one. Two minutes of counter against one of stamps is what gives it away.
WALL_DRIFT = """
[09:00:00] tic pacing: 2100 of 2100 tics run in 60000 ms; run 35.000/s (35 asked), 0 behind
[09:00:30] tic pacing: 2100 of 2100 tics run in 60000 ms; run 35.000/s (35 asked), 0 behind
[09:01:00] tic pacing: 2100 of 2100 tics run in 60000 ms; run 35.000/s (35 asked), 0 behind
"""

# Half an hour of the Linux dedicated server on an i3-8100, measured 2026-11-15: thirty windows,
# the first carrying three tics of start-up catch-up. The whole-run figures are what the summing
# question turns on -- adding the window counts gives 60900 tics, while the 1740417 ms the run
# took are worth 60914.6 of them, and the loop ran 60915.
LINUX_HALF_HOUR = """
[15:06:34] tic pacing: 2103 of 2100 tics run in 60026 ms; run 35.034/s (35 asked), -3 behind
[15:07:34] tic pacing: 2100 of 2100 tics run in 60018 ms; run 34.989/s (35 asked), 0 behind
[15:08:34] tic pacing: 2101 of 2100 tics run in 60021 ms; run 35.004/s (35 asked), -1 behind
[15:09:34] tic pacing: 2100 of 2100 tics run in 60010 ms; run 34.994/s (35 asked), 0 behind
[15:10:34] tic pacing: 2101 of 2100 tics run in 60011 ms; run 35.010/s (35 asked), -1 behind
[15:11:34] tic pacing: 2101 of 2100 tics run in 60026 ms; run 35.001/s (35 asked), -1 behind
[15:12:34] tic pacing: 2100 of 2100 tics run in 60010 ms; run 34.994/s (35 asked), 0 behind
[15:13:34] tic pacing: 2100 of 2100 tics run in 60000 ms; run 35.000/s (35 asked), 0 behind
[15:14:34] tic pacing: 2100 of 2100 tics run in 60011 ms; run 34.993/s (35 asked), 0 behind
[15:15:34] tic pacing: 2101 of 2100 tics run in 60005 ms; run 35.013/s (35 asked), -1 behind
[15:16:34] tic pacing: 2100 of 2100 tics run in 60001 ms; run 34.999/s (35 asked), 0 behind
[15:17:34] tic pacing: 2100 of 2100 tics run in 60016 ms; run 34.990/s (35 asked), 0 behind
[15:18:34] tic pacing: 2101 of 2100 tics run in 60020 ms; run 35.004/s (35 asked), -1 behind
[15:19:34] tic pacing: 2101 of 2100 tics run in 60019 ms; run 35.005/s (35 asked), -1 behind
[15:20:34] tic pacing: 2100 of 2100 tics run in 60011 ms; run 34.993/s (35 asked), 0 behind
[15:21:34] tic pacing: 2101 of 2100 tics run in 60023 ms; run 35.003/s (35 asked), -1 behind
[15:22:34] tic pacing: 2100 of 2100 tics run in 60016 ms; run 34.990/s (35 asked), 0 behind
[15:23:34] tic pacing: 2101 of 2100 tics run in 60023 ms; run 35.003/s (35 asked), -1 behind
[15:24:35] tic pacing: 2101 of 2100 tics run in 60016 ms; run 35.007/s (35 asked), -1 behind
[15:25:35] tic pacing: 2101 of 2100 tics run in 60021 ms; run 35.004/s (35 asked), -1 behind
[15:26:35] tic pacing: 2100 of 2100 tics run in 60011 ms; run 34.993/s (35 asked), 0 behind
[15:27:35] tic pacing: 2101 of 2100 tics run in 60028 ms; run 35.000/s (35 asked), -1 behind
[15:28:35] tic pacing: 2101 of 2100 tics run in 60013 ms; run 35.009/s (35 asked), -1 behind
[15:29:35] tic pacing: 2100 of 2100 tics run in 60009 ms; run 34.994/s (35 asked), 0 behind
[15:30:35] tic pacing: 2100 of 2100 tics run in 60017 ms; run 34.990/s (35 asked), 0 behind
[15:31:35] tic pacing: 2101 of 2100 tics run in 60015 ms; run 35.007/s (35 asked), -1 behind
[15:32:35] tic pacing: 2100 of 2100 tics run in 60003 ms; run 34.998/s (35 asked), 0 behind
[15:33:35] tic pacing: 2101 of 2100 tics run in 60025 ms; run 35.002/s (35 asked), -1 behind
[15:34:35] tic pacing: 2100 of 2100 tics run in 60005 ms; run 34.997/s (35 asked), 0 behind
[15:35:35] tic pacing: 2101 of 2100 tics run in 60013 ms; run 35.009/s (35 asked), -1 behind
"""

# A run whose stamps straddle midnight, which must read as four seconds rather than a day short.
MIDNIGHT = """
[23:59:58] tic pacing: 70 of 70 tics run in 2000 ms; run 35.000/s (35 asked), 0 behind
[00:00:00] tic pacing: 70 of 70 tics run in 2000 ms; run 35.000/s (35 asked), 0 behind
[00:00:02] tic pacing: 70 of 70 tics run in 2000 ms; run 35.000/s (35 asked), 0 behind
"""

# The same server with sv_logfiletimestamp off, where the counter has nothing to be checked
# against, and the dated form of the stamp, which is read and its date dropped.
UNSTAMPED = """
tic pacing: 70 of 70 tics run in 2000 ms; run 35.000/s (35 asked), 0 behind
tic pacing: 70 of 70 tics run in 2000 ms; run 35.000/s (35 asked), 0 behind
"""
DATED = """
[26:11:15;11:44:20] tic pacing: 70 of 70 tics run in 2000 ms; run 35.000/s (35 asked), 0 behind
[26:11:15;11:44:22] tic pacing: 70 of 70 tics run in 2000 ms; run 35.000/s (35 asked), 0 behind
"""

failures = []


def check(what, got, want):
    if got != want:
        failures.append("%s: got %r, wanted %r" % (what, got, want))


def check_true(what, got):
    if not got:
        failures.append("%s: got %r" % (what, got))


# ---------------------------------------------------------------- parsing
host = tpc.parse_lines(HOST)
check("host window count", len(host), 4)
check("host first run tics", host[0]['run'], 70)
check("host first clock tics", host[0]['clock'], 70)
check("host first ms", host[0]['ms'], 2000)
check("host first loop rate", host[0]['run_milli_hz'], 35000)
check("host first asked", host[0]['asked'], 35)
check("host third behind", host[2]['behind'], -1)
check("host stamps", [w['stamp'] for w in host][:2], ["11:44:20", "11:44:22"])

check("a dated stamp keeps only its time", [w['stamp'] for w in tpc.parse_lines(DATED)],
      ["11:44:20", "11:44:22"])
check("an unstamped line parses with no stamp", tpc.parse_lines(UNSTAMPED)[0]['stamp'], None)

check("nothing to parse", tpc.parse_lines("no pacing here\njust a log\n"), [])

# ---------------------------------------------------------------- judging
check("a server run passes", tpc.judge(host), [])

stalled_loop = tpc.parse_lines(STALLED_LOOP)
check("loop count", len(stalled_loop), 4)
stalled = tpc.judge(stalled_loop)
check("a stalled loop fails by default", len(stalled), 1)
check_true("and it names the unrun tics", "394 tics" in stalled[0])
check("a stalled loop only reports under --lenient-loop",
      tpc.judge(stalled_loop, lenient_loop=True), [])

# The counter against the one clock it does not provide.
drift = tpc.parse_lines(WALL_DRIFT)
drifting = tpc.judge(drift)
check("a counter that outruns the log's stamps fails", len(drifting), 1)
check_true("and it prints both spans", "120000 ms long where" in drifting[0]
           and "60000 ms" in drifting[0])

check("stamps across midnight read forwards", tpc.wall_seconds("23:59:58", "00:00:02"), 4)
check("a run across midnight passes", tpc.judge(tpc.parse_lines(MIDNIGHT)), [])

unstamped = tpc.judge(tpc.parse_lines(UNSTAMPED))
check("no stamps is a failure, not a skip", len(unstamped), 1)
check_true("and it says which setting to pass", "sv_logfiletimestamp 1" in unstamped[0])

# ------------------------------------------------- the whole run, on a measured half hour
linux = tpc.parse_lines(LINUX_HALF_HOUR)
check("the Linux half hour parses whole", len(linux), 30)
check("a measured half hour of a server passes", tpc.judge(linux), [])
check("its start-up catch-up is the first window's", linux[0]['behind'], -3)
total_ms, clock_tics, total_run, run_rate = tpc.whole_run(linux)
check("the run's milliseconds", total_ms, 1740417)
check("the clock's tics come from that time, not from the windows", clock_tics, 60914)
check("which is not what summing the windows gives", sum(w['clock'] for w in linux[1:]), 60900)
check("the loop's tics", total_run, 60915)
check("so the loop's rate is a ten-thousandth over", run_rate, 35000)

# No windows at all means the engine never paced, which must not read as a pass.
check_true("no windows is a failure", tpc.judge([]))
check_true("one window is a failure", tpc.judge(host[:1]))

if failures:
    print("tic_pacing_check_test: %d failure(s)" % len(failures))
    for f in failures:
        print("  " + f)
    sys.exit(1)

print("tic_pacing_check_test: ok")
