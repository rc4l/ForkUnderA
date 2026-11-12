#!/usr/bin/env python3
"""
Evidence-based relevance triage. For each upstream commit, propose ported/skip/candidate from
FACTS, never the title:

  - merge (no unique content)                                  -> skip (merge)
  - every touched file absent from our tree                    -> skip (subsystem absent)
  - the change's distinctive added lines already in our tree   -> ported (we have it)
  - files exist but the change isn't present                   -> candidate (PENDING; real port)
  - partial / too few distinctive lines                        -> REVIEW (hand-examine)

Paths the ledger ignores (Zandronum's changelog and backport notes, .hgtags) do not count. "Already
in our tree" is content-based: the stripped added line appears as a line anywhere under
src/zandronum, so it survives renames and is immune to title spin. It is a hint, never a verdict
on its own: the ported zstd commit scored 27% because the code around it had moved.

Usage: triage.py [--ledger NAME] [-n N]   (the first N pending rows; default 100)
Prints one verdict line per commit plus a summary, and writes <ledger>/triage_out.tsv.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ledger as L  # noqa: E402
import tracker  # noqa: E402


def triage(led, repo, row, files_map, have, ours_lines):
    sha = row["sha"]
    files = [f for f in files_map.get(sha, []) if not L.is_ignored(led, f)]
    if not files_map.get(sha):
        return ("skip", "merge or no files")
    if not files:
        return ("skip", "only ignored paths: " + ", ".join(files_map[sha][:4]))
    have_file = [f for f in files if tracker.in_our_tree(led, f, have)]
    if not have_file:
        return ("skip", "files absent: " + ", ".join(files[:4]))
    added = tracker.added_lines(tracker.strip_ignored(led, repo.diff(sha, unified=0)))[:25]
    if not added:
        return ("REVIEW", "no distinctive added lines (%d files, have %d)" % (len(files), len(have_file)))
    present = sum(1 for s in added if s in ours_lines)
    ratio = present / len(added)
    ev = "present %d/%d" % (present, len(added))
    if ratio >= 0.7:
        return ("ported", ev + " (inherited?)")
    if ratio <= 0.10:
        return ("candidate", ev + " -> PENDING")
    return ("REVIEW", ev + " (partial)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ledger", default="uzdoom")
    ap.add_argument("-n", type=int, default=100)
    args = ap.parse_args()
    led = L.load_config()[args.ledger]
    repo = tracker.repo_for(led)
    rows = [r for r in L.read_rows(led) if r["status"] == "pending"][:args.n]
    files_map, have, ours_lines = L.read_files_map(led), tracker.our_basenames(), tracker.our_lines()
    print("triaging %d commits...\n" % len(rows))
    tally, out = {}, []
    for r in rows:
        v, ev = triage(led, repo, r, files_map, have, ours_lines)
        tally[v] = tally.get(v, 0) + 1
        out.append((r["sha"], v, ev))
        print("%-9s %s %s | %-52.52s | %s" % (v, r["sha"][:9], r["date"][:10], r["title"], ev))
    print("\n=== summary ===")
    for k in ("ported", "skip", "candidate", "REVIEW"):
        print("  %-10s %d" % (k, tally.get(k, 0)))
    L.write_text(os.path.join(L.ledger_dir(led), "triage_out.tsv"), "".join("%s\t%s\t%s\n" % o for o in out))


if __name__ == "__main__":
    main()
