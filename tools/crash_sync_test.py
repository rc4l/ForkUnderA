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

if FAILURES:
    print("\nFAILED:")
    for f in FAILURES:
        print(f"  - {f}")
    sys.exit(1)
print("\nall crash_sync.should_reopen cases passed")
