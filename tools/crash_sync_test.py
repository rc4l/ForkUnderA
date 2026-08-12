#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Regression tests for crash_sync.should_reopen -- the rule that decides whether a CLOSED
# crash issue should reopen. This exists because the original rule reopened a closed issue
# whenever GlitchTip still listed its group as unresolved, which is not evidence of anything:
# `unresolved` is a triage flag a human sets in GlitchTip, and closing the GitHub issue does not
# clear it. Issues #79 and #85 were reopened repeatedly with no new crash at all.
#
# The cases below are pinned to the REAL timestamps from that incident, so the exact production
# behaviour is what is asserted, not a paraphrase of it.
#
# Stdlib only, no network, no test framework -- runs anywhere python3 does:  python3 tools/crash_sync_test.py
import importlib.util, os, sys

# should_reopen is pure, but importing the module evaluates its env-var config at import time.
os.environ.setdefault("GLITCHTIP_URL", "http://unused")
os.environ.setdefault("GLITCHTIP_TOKEN", "unused")
os.environ.setdefault("GLITCHTIP_ORG", "unused")
os.environ.setdefault("GITHUB_REPOSITORY", "unused/unused")
os.environ.setdefault("GITHUB_TOKEN", "unused")

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("crash_sync", os.path.join(_here, "crash_sync.py"))
crash_sync = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(crash_sync)
should_reopen = crash_sync.should_reopen

FAILURES = []

def check(name, last_seen, closed_at, want):
    got, why = should_reopen(last_seen, closed_at)
    ok = got is want
    print(f"{'PASS' if ok else 'FAIL'}  {name}\n      -> {got} ({why})")
    if not ok:
        FAILURES.append(f"{name}: expected {want}, got {got}")

# --- the production incident these tests exist for -------------------------------------------
# #85: closed 13:28:55 while its newest event was 11:40:33, ~2h EARLIER. Nothing recurred.
check("#85 stale event must not reopen",
      "2026-07-29T11:40:33.390Z", "2026-07-29T13:28:55Z", False)
# #79: closed 13:29:05, reopened with the SAME lastSeen as the previous reopen -- no new crash.
check("#79 repeat reopen with no new event",
      "2026-07-29T13:21:59.312Z", "2026-07-29T13:29:05Z", False)
# #79's FIRST reopen was correct and must stay working: closed 06:58, canary crashed 13:21.
check("#79 genuine regression still reopens",
      "2026-07-29T13:21:59.312Z", "2026-07-29T06:58:07Z", True)

# --- boundary ---------------------------------------------------------------------------------
# Equal timestamps are the close itself, not a new crash: strictly-after is required.
check("event exactly at close does not reopen", "2026-07-29T13:00:00Z", "2026-07-29T13:00:00Z", False)
check("event one second after reopens",         "2026-07-29T13:00:01Z", "2026-07-29T13:00:00Z", True)

# --- timestamp formats seen across the two APIs -----------------------------------------------
check("'+00:00' offset form",      "2026-07-29T13:00:01+00:00", "2026-07-29T13:00:00Z", True)
check("no fractional seconds",     "2026-07-29T11:40:33Z",      "2026-07-29T13:28:55Z", False)
check("naive timestamp is UTC",    "2026-07-29T13:00:01",       "2026-07-29T13:00:00Z", True)
# A non-UTC offset must be converted, not compared as wall-clock: 14:00:01+02:00 is 12:00:01Z.
check("non-UTC offset is converted", "2026-07-29T14:00:01+02:00", "2026-07-29T13:00:00Z", False)

# --- fail-safe: when the comparison is impossible, reopen rather than hide a live crash -------
check("missing lastSeen reopens",   None,          "2026-07-29T13:00:00Z", True)
check("missing closed_at reopens",  "2026-07-29T13:00:00Z", None,          True)
check("unparseable value reopens",  "not-a-date",  "2026-07-29T13:00:00Z", True)
check("empty string reopens",       "",            "2026-07-29T13:00:00Z", True)


# --- resolve_platform: which symbols a crash may be read with ---------------------------------
#
# [rc4l] The case that matters is the refusal. A server crash carries the same release, dist and
# platform as a client crash from the same commit, so before this it matched the CLIENT's symbols
# asset and symbolicated a different binary -- names that are authoritative and wrong. An
# unsymbolicated stack at least admits it is useless.

def ev_with(build=None):
    e = {"tags": []}
    if build is not None:
        e["tags"].append({"key": "build", "value": build})
    return e

def check_plat(label, ev, platform, expected):
    got = crash_sync.resolve_platform(ev, platform)
    ok = got == expected
    print(("PASS  " if ok else "FAIL  ") + label)
    print(f"      -> {got!r}")
    if not ok:
        FAILURES.append(f"{label}: expected {expected!r}, got {got!r}")

check_plat("server crash asks for server symbols", ev_with("server"), "linux", "linux-server")
check_plat("client crash is unchanged",            ev_with("client"), "linux", "linux")
# Absent is not client-shaped guesswork: nothing but a client reported before the tag existed.
check_plat("untagged build is treated as client",  ev_with(None),     "linux", "linux")
# The refusal. No server symbols are built for these platforms, so there is nothing honest to use.
check_plat("server crash on macos refuses",        ev_with("server"), "macos", None)
check_plat("server crash on windows refuses",      ev_with("server"), "windows", None)
# Case and padding come from a remote payload, not from us.
check_plat("tag value is normalised",              ev_with(" Server "), "linux", "linux-server")

if FAILURES:
    print("\nFAILED:")
    for f in FAILURES:
        print(f"  - {f}")
    sys.exit(1)
print("\nall crash_sync cases passed")
