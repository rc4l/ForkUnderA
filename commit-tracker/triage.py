#!/usr/bin/env python3
"""
Evidence-based relevance triage. For each upstream commit, decide ported/skip/candidate from
FACTS, never the title:

  - merge (no unique content)                                  -> skip (merge)
  - every touched file absent from our tree                    -> skip (subsystem absent)
  - the change's distinctive added lines already in our tree   -> ported (we have it; base-inherited)
  - files exist but the change isn't present                   -> candidate (PENDING; real port)
  - partial / too few distinctive lines                        -> REVIEW (hand-examine)

"already in our tree" is content-based (git grep -F the stripped added lines anywhere under
src/zandronum), so it survives upstream renames and is immune to title spin.

Env: UZDOOM, N (how many first-pending commits). Prints one verdict line per commit + a summary.
"""
import os, re, subprocess

UP   = os.environ.get("UZDOOM", "/Users/talhataj/repos/UZDoom")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # repo root (has src/zandronum)
OURS = "src/zandronum"
N    = int(os.environ.get("N", "100"))
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]{2,}")


def sh(args, **kw):
    return subprocess.run(args, capture_output=True, encoding="utf-8", errors="replace", **kw).stdout


def pending(n):
    rows = []
    with open(os.path.join(ROOT, "commit-tracker/coverage.tsv")) as f:
        for i, line in enumerate(f):
            if i < 2:
                continue
            c = line.rstrip("\n").split("\t")
            if len(c) >= 4 and c[3] == "pending":
                rows.append((c[0], c[1][:10], c[2]))
                if len(rows) >= n:
                    break
    return rows


def in_our_tree(text):
    """Is this stripped line present anywhere under src/zandronum? (substring, fixed-string)"""
    r = subprocess.run(["git", "-C", ROOT, "grep", "-F", "-q", "-e", text, "--", OURS],
                       capture_output=True)
    return r.returncode == 0


def basename_exists(path):
    b = os.path.basename(path)
    return bool(sh(["git", "-C", ROOT, "ls-files", "--", "%s/**/%s" % (OURS, b), "%s/%s" % (OURS, b)]).strip())


def triage(sha):
    parents = sh(["git", "-C", UP, "rev-list", "--parents", "-n1", sha]).split()
    if len(parents) > 2:
        return ("skip", "merge")
    files = [f for f in sh(["git", "-C", UP, "show", "--name-only", "--format=", sha]).splitlines() if f]
    if not files:
        return ("skip", "no files")
    have_file = [f for f in files if basename_exists(f)]
    if not have_file:
        return ("skip", "files absent: " + ", ".join(files[:4]))

    # distinctive added lines
    diff = sh(["git", "-C", UP, "show", "--unified=0", "--format=", sha])
    added = []
    for ln in diff.splitlines():
        if ln.startswith("+") and not ln.startswith("+++"):
            s = ln[1:].strip()
            if len(s) >= 12 and IDENT.search(s):
                added.append(s)
    added = list(dict.fromkeys(added))[:25]   # dedupe, cap
    if not added:
        return ("REVIEW", "no distinctive added lines (%d files, have %d)" % (len(files), len(have_file)))

    present = sum(1 for s in added if in_our_tree(s))
    ratio = present / len(added)
    ev = "present %d/%d" % (present, len(added))
    if ratio >= 0.7:
        return ("ported", ev + " (inherited)")
    if ratio <= 0.10:
        return ("candidate", ev + " -> PENDING")
    return ("REVIEW", ev + " (partial)")


def main():
    rows = pending(N)
    print("triaging %d commits...\n" % len(rows))
    tally = {}
    out = []
    for sha, date, subj in rows:
        v, ev = triage(sha)
        tally[v] = tally.get(v, 0) + 1
        out.append((v, sha, date, subj, ev))
        print("%-9s %s %s | %-52.52s | %s" % (v, sha[:9], date, subj, ev))
    print("\n=== summary ===")
    for k in ("ported", "skip", "candidate", "REVIEW"):
        print("  %-10s %d" % (k, tally.get(k, 0)))
    # write proposed verdicts for the writeback step
    with open(os.path.join(ROOT, "commit-tracker/triage_out.tsv"), "w") as f:
        for v, sha, date, subj, ev in out:
            f.write("%s\t%s\t%s\n" % (sha, v, ev))


if __name__ == "__main__":
    main()
