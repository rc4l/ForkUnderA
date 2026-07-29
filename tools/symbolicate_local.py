#!/usr/bin/env python3
# [rc4l] Symbolicate a crash against LOCAL symbols -- for dev builds the cloud pipeline can't touch.
#
# The GitHub webhook (crash_sync.py) can only symbolicate builds CI published a dSYM asset for; a
# crash from your own `mac_compile.sh` build has no such asset, so its issue shows a raw stack with
# a "symbolication skipped: no symbols asset published" note. But the symbols are right here on disk
# (build/zandronum.dSYM). This tool does the same rebasing (shared tools/crash_rebase.py) and runs a
# local symbolizer (atos on macOS, or llvm-symbolizer) so you get func + file:line without a release.
#
# Inputs (pick one):
#   --event FILE|-       a GlitchTip event JSON (recommended: it carries the image base + platform).
#   --issue ID           fetch that issue's latest event live from GlitchTip (needs GLITCHTIP_URL /
#                        GLITCHTIP_TOKEN [/ GLITCHTIP_ORG], same env crash_sync uses).
#   --addr HEX [HEX...] --base HEX     rebase raw runtime addresses by hand (e.g. from a crash log).
#
# Symbols:  --obj PATH   symbol file (default: build/zandronum.dSYM DWARF, else build .app binary).
# Platform: --platform macos|linux|windows   (default: inferred from the event, else macos).
#
#   python3 tools/symbolicate_local.py --event crash.json
#   python3 tools/symbolicate_local.py --issue 85
#   python3 tools/symbolicate_local.py --base 0x102abc000 --addr 0x102abd234 0x102abd100
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
import argparse
import json
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from crash_rebase import PLAT, to_addr, rebase_main_module

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# ---- GlitchTip event parsing (same shapes crash_sync.py reads) -------------
def tagval(ev, key):
    if ev.get(key):
        return ev[key]
    for t in ev.get("tags", []):
        if t.get("key") == key:
            return t.get("value")
    return None


def exception_frames(ev):
    for e in ev.get("entries", []):
        if e.get("type") == "exception":
            try:
                return e["data"]["values"][0]["stacktrace"]["frames"] or []
            except (KeyError, IndexError, TypeError):
                return []
    return []


def norm_platform(ev):
    name = ""
    for c in (ev.get("contexts") or {}).values():
        if isinstance(c, dict) and c.get("type") == "os":
            name = (c.get("name") or "").lower()
    if not name:
        name = (tagval(ev, "os.name") or tagval(ev, "os") or "").lower()
    if "mac" in name or "darwin" in name:
        return "macos"
    if "windows" in name:
        return "windows"
    if name:
        return "linux"
    return None


def event_addrs_base_platform(ev):
    """Pull (runtime_addrs outermost-first, image_base int, platform) out of a GlitchTip event."""
    base = to_addr(tagval(ev, "zx_image_base"))
    if base is None:
        sys.exit("event has no zx_image_base tag -- can't rebase (was it sent by a build with the "
                 "crash-report client?)")
    addrs = [to_addr(f.get("instruction_addr")) for f in exception_frames(ev)]
    return addrs, base, norm_platform(ev)


# ---- symbol file resolution ------------------------------------------------
def default_objfile():
    dsym = os.path.join(REPO, "build", "zandronum.dSYM", "Contents", "Resources", "DWARF", "zandronum")
    if os.path.exists(dsym):
        return dsym
    binary = os.path.join(REPO, "build", "ZandroX.app", "Contents", "MacOS", "zandronum")
    if os.path.exists(binary):
        return binary
    return None


# ---- symbolizers -----------------------------------------------------------
def run_atos(objfile, preferred_base, static_addrs):
    atos = shutil.which("atos")
    if not atos:
        return None
    # Static addrs + `-l preferred_base` -> slide 0 -> atos reads the dSYM's own link addresses.
    cmd = [atos, "-o", objfile, "-l", hex(preferred_base)] + [hex(a) for a in static_addrs]
    out = subprocess.run(cmd, capture_output=True, timeout=180).stdout.decode(errors="replace")
    lines = [ln.strip() for ln in out.splitlines() if ln.strip()]
    return lines if len(lines) == len(static_addrs) else None


def run_llvm_symbolizer(objfile, static_addrs):
    tool = shutil.which("llvm-symbolizer")
    if not tool:
        return None
    inp = "\n".join(hex(a) for a in static_addrs).encode()
    out = subprocess.run([tool, f"--obj={objfile}"], input=inp,
                         capture_output=True, timeout=180).stdout.decode(errors="replace")
    lines = []
    for block in (b for b in out.split("\n\n") if b.strip()):
        rows = [r for r in block.splitlines() if r.strip()]
        func = rows[0].strip() if rows else "?"
        loc = rows[1].strip() if len(rows) > 1 else ""
        lines.append(func + (f"  ({loc})" if loc and loc != "??:0:0" else ""))
    return lines if len(lines) == len(static_addrs) else None


# ---- GlitchTip fetch (optional --issue mode) -------------------------------
def fetch_issue_event(issue_id):
    import urllib.request
    url = os.environ["GLITCHTIP_URL"].rstrip("/")
    tok = os.environ["GLITCHTIP_TOKEN"]
    req = urllib.request.Request(f"{url}/api/0/issues/{issue_id}/events/latest/",
                                 headers={"Authorization": f"Bearer {tok}",
                                          "User-Agent": "zx-symbolicate-local"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read())


def load_event(args):
    if args.issue:
        return fetch_issue_event(args.issue)
    raw = sys.stdin.read() if args.event == "-" else open(args.event, encoding="utf-8").read()
    return json.loads(raw)


def main():
    ap = argparse.ArgumentParser(description="Symbolicate a crash against local symbols.")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--event", help="GlitchTip event JSON file, or - for stdin")
    src.add_argument("--issue", help="GlitchTip issue id to fetch the latest event of")
    src.add_argument("--addr", nargs="+", help="raw runtime addresses (needs --base)")
    ap.add_argument("--base", help="runtime image base (hex), required with --addr")
    ap.add_argument("--obj", help="symbol file (default: local build dSYM/binary)")
    ap.add_argument("--platform", choices=list(PLAT), help="override platform (default: from event)")
    args = ap.parse_args()

    if args.addr:
        if not args.base:
            ap.error("--addr requires --base")
        base = to_addr(args.base)
        if base is None:
            ap.error(f"--base {args.base!r} is not a hex address")
        addrs = [to_addr(a) for a in args.addr]
        platform = args.platform or "macos"
    else:
        ev = load_event(args)
        addrs, base, ev_plat = event_addrs_base_platform(ev)
        platform = args.platform or ev_plat or "macos"

    preferred_base = PLAT[platform][0]
    mapped = rebase_main_module(addrs, base, preferred_base)   # [(orig_index, static_addr), ...]
    if not mapped:
        sys.exit("no frames fell inside the main module after rebasing -- wrong --base or platform?")

    objfile = args.obj or default_objfile()
    if not objfile or not os.path.exists(objfile):
        sys.exit("no symbol file: build the app (mac_compile.sh) or pass --obj PATH to a dSYM/binary")

    static_addrs = [a for _, a in mapped]
    lines = run_atos(objfile, preferred_base, static_addrs) \
        or run_llvm_symbolizer(objfile, static_addrs)
    if lines is None:
        sys.exit("no working symbolizer (need `atos` on macOS or `llvm-symbolizer`), or it errored")

    # Print crash site first (deepest frame). GlitchTip stores frames outermost-first, so the last
    # kept frame is innermost; reverse for a crash-site-first read.
    print(f"# symbolicated {len(lines)} main-module frame(s) via {os.path.basename(objfile)}")
    for n, (orig_i, line) in enumerate(zip([i for i, _ in reversed(mapped)], reversed(lines))):
        print(f"  {n}: {line}    [frame {orig_i}]")


if __name__ == "__main__":
    main()
