#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
#
# [Kaori-Chani] The tic pacing check: run the engine as a host for a couple of minutes, read the
# pacing lines it prints itself, and judge the time base from them.
#
# There is no Mac and no second machine in the loop here, so this is the only thing that watches
# the tic clock on macOS at all, and the only thing that watches it on every push. It judges the
# CLOCK's rate, which is the time base: tics the counter passed against milliseconds the counter
# advanced, asserted within a small tolerance of TICRATE. Lost tics -- the loop running fewer tics
# than the clock passed -- are reported but only fatal with --strict-loop, because a windowed
# client on a shared runner stalls its own frames for reasons that have nothing to do with the
# clock: a measured run on Windows read 7.7 tics a second run against a clock still keeping
# 35.000, and a job that failed on that would be measuring the runner's GPU.
#
# Stdlib only, no test framework:  python3 tools/tic_pacing_check.py --engine PATH --iwad PATH

import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile
import time

# tic pacing: 70 of 70 tics run in 2000 ms; clock 35.000/s, run 35.000/s (35 asked), 0 behind
LINE = re.compile(
    r"tic pacing: (?P<run>\d+) of (?P<clock>\d+) tics run in (?P<ms>\d+) ms; "
    r"clock (?P<clock_hz>\d+)\.(?P<clock_milli>\d{3})/s, "
    r"run (?P<run_hz>\d+)\.(?P<run_milli>\d{3})/s "
    r"\((?P<asked>\d+) asked\), (?P<behind>-?\d+) behind")


def parse_lines(text):
    """[Kaori-Chani] Every pacing window the engine reported, as plain integers."""
    windows = []
    for m in LINE.finditer(text):
        windows.append({
            'run': int(m.group('run')),
            'clock': int(m.group('clock')),
            'ms': int(m.group('ms')),
            'clock_milli_hz': int(m.group('clock_hz')) * 1000 + int(m.group('clock_milli')),
            'run_milli_hz': int(m.group('run_hz')) * 1000 + int(m.group('run_milli')),
            'asked': int(m.group('asked')),
            'behind': int(m.group('behind')),
        })
    return windows


def judge(windows, tolerance=350, max_behind=70, strict_loop=False, min_windows=2):
    """[Kaori-Chani] What is wrong with these windows, as a list of lines; empty means pass."""
    problems = []

    if len(windows) < min_windows:
        problems.append("only %d pacing window(s) reported, wanted %d: the engine never paced, "
                        "or the report never fired" % (len(windows), min_windows))
        return problems

    for i, w in enumerate(windows):
        want = w['asked'] * 1000
        off = w['clock_milli_hz'] - want
        # [Kaori-Chani] A window's tics are counted whole, so its rate carries one tic of edge
        # either way: a measured 2-second window read 34.533 for that reason alone, 1.3% low with
        # nothing wrong. Short windows therefore get a wider allowance, as 1000/ms a second.
        allowed = tolerance + 1000000 // max(w['ms'], 1)
        if abs(off) > allowed:
            problems.append(
                "window %d: the clock ran at %d.%03d a second, %+d.%03d off the %d asked "
                "(allowed %d.%03d) -- %d tics in %d ms"
                % (i + 1, w['clock_milli_hz'] // 1000, w['clock_milli_hz'] % 1000,
                   off // 1000, abs(off) % 1000, w['asked'],
                   allowed // 1000, allowed % 1000, w['clock'], w['ms']))

    # [Kaori-Chani] And the whole run together, where those edges cancel and a rate that is
    # quietly wrong by less than one window's allowance still shows up.
    total_ms = sum(w['ms'] for w in windows)
    total_tics = sum(w['clock'] for w in windows)
    if total_ms > 0:
        rate = total_tics * 1000000 // total_ms
        want = windows[0]['asked'] * 1000
        if abs(rate - want) > tolerance:
            problems.append(
                "over the whole run the clock ran at %d.%03d a second, %+d.%03d off the %d asked "
                "(tolerance %d.%03d) -- %d tics in %d ms"
                % (rate // 1000, rate % 1000, (rate - want) // 1000, abs(rate - want) % 1000,
                   windows[0]['asked'], tolerance // 1000, tolerance % 1000,
                   total_tics, total_ms))

    lost = sum(w['behind'] for w in windows)
    if lost > max_behind:
        note = ("%d tics the clock passed were never run, over %d windows (allowed %d)"
                % (lost, len(windows), max_behind))
        if strict_loop:
            problems.append(note)
        else:
            print("tic-pacing: NOTE: " + note)
            print("tic-pacing: the clock kept time, so this is the loop, not the time base; "
                  "pass --strict-loop to fail on it.")

    return problems


def run_engine(engine, iwad, seconds, interval, extra, host=True):
    """[Kaori-Chani] Host a map for a while and give back whatever the engine logged."""
    logdir = tempfile.mkdtemp(prefix="tic-pacing-")
    stem = os.path.join(logdir, "pacing")

    env = dict(os.environ)
    # [Kaori-Chani] No audio device on a runner, and no display on a Linux one.
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    if sys.platform.startswith("linux") and not env.get("DISPLAY"):
        env.setdefault("SDL_VIDEODRIVER", "dummy")

    # [Kaori-Chani] A client needs -host to run tics on its own clock; the dedicated server is one
    # already, and takes the flag for a client it is not.
    args = [engine, "-iwad", iwad] + (["-host"] if host else [])
    args += ["-nosound", "-nomusic", "+logfile", stem,
             "+fua_tictime_report", str(interval), "+map", "MAP01"] + extra
    print("tic-pacing: " + " ".join(args))

    # [Kaori-Chani] The engine's console output goes to a file rather than a pipe nobody drains,
    # because a pipe filling up would stall the engine and read as a stalled time base.
    console = os.path.join(logdir, "console.txt")
    early = False
    with open(console, "w", encoding="utf-8") as sink:
        proc = subprocess.Popen(args, env=env, stdout=sink, stderr=subprocess.STDOUT)
        deadline = time.time() + seconds
        while time.time() < deadline:
            if proc.poll() is not None:
                early = True
                break
            time.sleep(1)

        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    # [Kaori-Chani] An early exit is reported with the engine's last words, since a loader error
    # names the missing library there and never reaches the engine's own log.
    if early:
        print("tic-pacing: the engine exited early with code %d" % proc.returncode)
        with open(console, encoding="utf-8", errors="replace") as fh:
            tail = fh.read().splitlines()[-20:]
        for line in tail or ["(the engine printed nothing)"]:
            print("  " + line)

    # [Kaori-Chani] The engine stamps the name it was given with the date, so take what it wrote.
    text = ""
    for path in sorted(glob.glob(stem + "*")):
        with open(path, encoding="utf-8", errors="replace") as fh:
            text += fh.read()
    return text


def main(argv):
    ap = argparse.ArgumentParser(description="Judge the tic time base from the engine's own "
                                             "pacing lines.")
    ap.add_argument("--engine", required=True, help="the engine binary to run")
    ap.add_argument("--iwad", required=True, help="an IWAD to host, such as tools/freedoom/freedoom2.wad")
    ap.add_argument("--seconds", type=int, default=120, help="how long to pace for (default 120)")
    ap.add_argument("--interval", type=int, default=10, help="seconds a pacing window covers")
    ap.add_argument("--tolerance", type=int, default=350,
                    help="how far the clock's rate may sit from TICRATE, in thousandths of a tic "
                         "a second (default 350, which is 1%% of 35)")
    ap.add_argument("--max-behind", type=int, default=70,
                    help="tics the loop may leave unrun across the whole run (default 70)")
    ap.add_argument("--strict-loop", action="store_true",
                    help="fail on unrun tics as well as on the clock's rate")
    ap.add_argument("--no-host", action="store_true",
                    help="the binary is a dedicated server, so do not pass -host")
    ap.add_argument("--log", help="judge this log instead of running the engine")
    args = ap.parse_args(argv[1:])

    if args.log:
        with open(args.log, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    else:
        text = run_engine(args.engine, args.iwad, args.seconds, args.interval, [],
                          host=not args.no_host)

    windows = parse_lines(text)
    for i, w in enumerate(windows):
        print("tic-pacing: window %d: %d of %d tics run in %d ms, clock %d.%03d/s, %d behind"
              % (i + 1, w['run'], w['clock'], w['ms'],
                 w['clock_milli_hz'] // 1000, w['clock_milli_hz'] % 1000, w['behind']))

    problems = judge(windows, args.tolerance, args.max_behind, args.strict_loop)
    if problems:
        print("TIC PACING: the time base did not keep time:", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1

    print("tic-pacing: clean, %d windows, the clock within %d.%03d of %d a second."
          % (len(windows), args.tolerance // 1000, args.tolerance % 1000,
             windows[0]['asked'] if windows else 0))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
