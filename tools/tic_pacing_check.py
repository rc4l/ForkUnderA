#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
#
# [Kaori-Chani] The tic pacing check: run the engine for a couple of minutes, read the pacing
# lines it prints itself, and judge the time base from them.
#
# There is no Mac and no second machine in the loop here, so this is the only thing that watches
# the tic clock on macOS at all, and the only thing that watches it on every push. Two things are
# judged, because a pacing line's own figures cannot judge a third: LOST TICS, the loop running
# fewer tics than the counter passed, which is fatal unless --lenient-loop; and the COUNTER
# against the logfile's stamps, the one clock here the time base does not provide. A window's
# tics and its milliseconds are a single counter span scaled twice (pacing.cpp), so no rate
# between them can show a counter that runs fast -- only the stamps can.
#
# A client rendering in software runs fewer tics than the counter passes for reasons that have
# nothing to do with the clock: a measured run on Windows read 7.7 tics a second against a
# counter still keeping time, so a leg that failed on that would be measuring the renderer.
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

# [11:44:20] tic pacing: 70 of 70 tics run in 2000 ms; run 35.000/s (35 asked), 0 behind
# The stamp is the logfile's (sv_logfiletimestamp), and carries a date first under
# sv_logfiletimestamp_usedate, which is read and dropped.
LINE = re.compile(
    r"(?:\[(?:\d\d:\d\d:\d\d;)?(?P<stamp>\d\d:\d\d:\d\d)\] )?"
    r"tic pacing: (?P<run>\d+) of (?P<clock>\d+) tics run in (?P<ms>\d+) ms; "
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
            'run_milli_hz': int(m.group('run_hz')) * 1000 + int(m.group('run_milli')),
            'asked': int(m.group('asked')),
            'behind': int(m.group('behind')),
            # [Kaori-Chani] The logfile's own stamp, which is the one clock here that the time
            # base does not provide and so the only thing its counter can be checked against.
            'stamp': m.group('stamp'),
        })
    return windows


def wall_seconds(first, last):
    """[Kaori-Chani] Seconds between two [HH:MM:SS] stamps, midnight included."""
    def secs(t):
        h, m, s = (int(p) for p in t.split(":"))
        return h * 3600 + m * 60 + s
    span = secs(last) - secs(first)
    return span + 86400 if span < 0 else span


# [Kaori-Chani] Tics of catch-up that a window after the first should not need; start-up runs to
# tens and steady pacing to one, the edge tic a window's whole count carries.
CATCHUP_TICS = 5


def whole_run(windows):
    """[Kaori-Chani] The run after its first window: milliseconds, the clock's tics, the loop's
    tics, and the loop's rate in thousandths."""
    total_ms = sum(w['ms'] for w in windows[1:])
    total_run = sum(w['run'] for w in windows[1:])
    if total_ms == 0:
        return 0, 0, total_run, 0
    # [Kaori-Chani] The clock's tics come from the elapsed time rather than from adding up window
    # counts, each of which is floored: a measured half hour summed to 60900 where the time it
    # took was worth 60914.6, a shortfall of half a tic a window that the clock never had.
    clock_tics = total_ms * windows[0]['asked'] // 1000
    return total_ms, clock_tics, total_run, total_run * 1000000 // total_ms


def judge(windows, max_behind=70, lenient_loop=False, min_windows=2):
    """[Kaori-Chani] What is wrong with these windows, as a list of lines; empty means pass."""
    problems = []

    if len(windows) < min_windows:
        problems.append("only %d pacing window(s) reported, wanted %d: the engine never paced, "
                        "or the report never fired" % (len(windows), min_windows))
        return problems

    lost = sum(w['behind'] for w in windows)
    if lost > max_behind:
        note = ("%d tics the clock passed were never run, over %d windows (allowed %d)"
                % (lost, len(windows), max_behind))
        if lenient_loop:
            print("tic-pacing: NOTE: " + note)
            print("tic-pacing: reported rather than failed, because --lenient-loop was passed.")
        else:
            problems.append(note)

    # [Kaori-Chani] The counter against a clock that is not itself. Both of a window's figures are
    # the same counter span scaled two ways (pacing.cpp), so their ratio is the asked rate whatever
    # the counter does; only the logfile's stamps can say the counter kept real time.
    stamped = [w for w in windows if w['stamp']]
    if len(stamped) < 2:
        # [Kaori-Chani] A failure rather than a skip: with no stamps this is the counter against
        # itself, and sv_logfiletimestamp is archived, so a config can switch the check off.
        problems.append("%d of %d pacing lines carry a logfile stamp, so the counter cannot be "
                        "checked against real time -- pass +sv_logfiletimestamp 1"
                        % (len(stamped), len(windows)))
    else:
        wall = wall_seconds(stamped[0]['stamp'], stamped[-1]['stamp'])
        engine_ms = sum(w['ms'] for w in stamped[1:])
        # A stamp is whole seconds, so two of them carry two seconds of slack between them, and
        # 1% on top. A clock stepped mid-run by NTP or a DST change fails this, which is rare and
        # plain enough from the two spans printed below.
        allowed = max(2000, wall * 10)
        if wall > 0 and abs(engine_ms - wall * 1000) > allowed:
            problems.append(
                "the counter made the run %d ms long where the log's own stamps make it %d ms, "
                "%+d ms out (allowed %d) -- the time base is not keeping real time"
                % (engine_ms, wall * 1000, engine_ms - wall * 1000, allowed))

    # [Kaori-Chani] A window that ran more tics than the clock counted is catching up on time it
    # spent elsewhere, which the first window does while the map loads and a later one should not.
    for i, w in enumerate(windows[1:], start=2):
        if w['behind'] <= -CATCHUP_TICS:
            print("tic-pacing: NOTE: window %d ran %d tics the clock had not reached, which is "
                  "catch-up after a stall rather than steady pacing" % (i, -w['behind']))

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
    # [Kaori-Chani] The stamps are asked for rather than assumed, both being archived settings,
    # and asked for undated so one format reaches the check.
    args += ["-nosound", "-nomusic", "+logfile", stem,
             "+sv_logfiletimestamp", "1", "+sv_logfiletimestamp_usedate", "0",
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
    ap.add_argument("--max-behind", type=int, default=70,
                    help="tics the loop may leave unrun across the whole run (default 70)")
    ap.add_argument("--lenient-loop", action="store_true",
                    help="report unrun tics instead of failing on them")
    ap.add_argument("--no-host", action="store_true",
                    help="do not pass -host: the binary is a dedicated server already, or a "
                         "client is wanted rather than the server -host would make it")
    ap.add_argument("--log", help="judge this log instead of running the engine")
    # [Kaori-Chani] A hosting client only waits on the tic timer with cl_capfps set, so a leg that
    # means to measure the wait has to be able to say so.
    ap.add_argument("--engine-arg", action="append", default=[], metavar="ARG",
                    help="an extra argument for the engine, repeatable")
    # [Kaori-Chani] What a client leg is for: proof that this platform's wait ran, and which one,
    # since a leg can pace perfectly without ever reaching the wait.
    ap.add_argument("--want-wait-path", metavar="REGEX",
                    help="fail unless the engine's one-time 'tic wait:' line matches this")
    args = ap.parse_args(argv[1:])

    if args.log:
        with open(args.log, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    else:
        text = run_engine(args.engine, args.iwad, args.seconds, args.interval, args.engine_arg,
                          host=not args.no_host)

    windows = parse_lines(text)
    for i, w in enumerate(windows):
        print("tic-pacing: window %d: %d of %d tics run in %d ms, loop %d.%03d/s, %d behind"
              % (i + 1, w['run'], w['clock'], w['ms'],
                 w['run_milli_hz'] // 1000, w['run_milli_hz'] % 1000, w['behind']))

    problems = judge(windows, args.max_behind, args.lenient_loop)

    if args.want_wait_path:
        taken = re.search(r"tic wait: (.+)", text)
        if taken is None:
            problems.append("the engine never printed a 'tic wait:' line, so this loop never "
                            "waited on the tic timer -- wanted one matching %r"
                            % args.want_wait_path)
        else:
            path = taken.group(1).strip()
            print("tic-pacing: the wait took the %s" % path)
            if not re.search(args.want_wait_path, path):
                problems.append("the wait took the %s, which does not match %r"
                                % (path, args.want_wait_path))

    if problems:
        print("TIC PACING: the time base did not keep time:", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1

    print("tic-pacing: clean, %d windows: the loop ran the tics the counter passed, and the "
          "counter kept the logfile's own time." % len(windows))
    total_ms, clock_tics, total_run, run_rate = whole_run(windows)
    if total_ms > 0:
        print("tic-pacing: windows 2 to %d together: %d ms, %d tics of clock, loop %d tics at "
              "%d.%03d/s." % (len(windows), total_ms, clock_tics, total_run,
                              run_rate // 1000, run_rate % 1000))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
