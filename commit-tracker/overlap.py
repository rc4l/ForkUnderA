#!/usr/bin/env python3
"""
overlap.py -- does a ported/adapted row's cited commit share a file with the upstream commit?

The reasoning, history and limits are in tools/commit-tracker-overlap.py, which runs this for one
ledger; commit-tracker-check runs it for every ledger.
"""
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ESCAPE = "no-file-overlap:"


def our_files(sha, cache):
    """Basenames one of our commits touched; empty if the sha is unreadable."""
    if sha not in cache:
        res = subprocess.run(["git", "-C", ROOT, "show", "--format=", "--name-only", sha],
                             capture_output=True, text=True)
        cache[sha] = {os.path.basename(p) for p in res.stdout.split() if "." in p}
    return cache[sha]


def mismatches(rows, files_map, files_of=None):
    """(mismatched rows, rows checked) for every ported/adapted row citing commits of ours.

    files_map: upstream sha -> its paths. files_of: our sha -> basenames (defaults to git show).
    """
    cache = {}
    files_of = files_of or (lambda s: our_files(s, cache))
    out, checked = [], 0
    for r in rows:
        if r.get("status") not in ("ported", "adapted"):
            continue
        ours = r.get("ours", "")
        if ours in ("", "/", "zandronum-base") or r["sha"] not in files_map or ESCAPE in r.get("note", ""):
            continue
        checked += 1
        up = {os.path.basename(p) for p in files_map[r["sha"]]}
        if not any(up & files_of(tok.strip()) for tok in ours.split(",")):
            out.append((r["sha"], r["status"], ours, r.get("title", "")[:52]))
    return out, checked
