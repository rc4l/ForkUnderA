#!/usr/bin/env python3
"""
commit-tracker-convergence.py -- did the code we claim to have ported actually ARRIVE?

The other two ledger gates check attribution, not arrival:

  commit-tracker-check.sh    the cited sha exists and is reachable from HEAD
  commit-tracker-overlap.py  the cited commit touched a file the upstream commit touched

Both pass on a row that says `ported` while the change was never applied. That is not
hypothetical -- an audit on 2026-08-03 found five such rows (355dd1c2f, 754c96a54,
3d24f58bf, e29fce695, bf03d0222), every one of them a renderer-staircase batch where the
batch commit really did touch the right files, so provenance looked perfect.

WHY THIS IS PER-FILE AND NOT PER-COMMIT
---------------------------------------
The obvious check -- "are this commit's added lines in our tree?" -- does not work. Roughly
40 of 45 flags it produced were legitimate: lines superseded by a later upstream commit we
also ported, or a subsystem vendored as a whole-file snapshot rather than commit by commit.
Annotating each of those individually would make the annotation routine, and a gate everyone
annotates past catches nothing.

So this asks a different question, once per FILE rather than once per commit:

    Does our copy of the file match upstream's copy at the NEWEST commit we claim to have
    ported for that file?

Supersession stops being noise by construction: if a later commit rewrote those lines, they
are not in the newest upstream state either, so there is nothing to flag. A whole-file
snapshot becomes one rule, not one annotation per row.

WHAT REMAINS, AND WHY IT IS TOLERABLE
-------------------------------------
The residual difference is our own divergence -- Zandronum's and ours. That is already
marked in the source with fork tags ([rc4l], [BB], [BC], [AK], ...), so a hunk carrying one
is explained. A hunk where upstream has code, we do not, and nothing on our side claims
responsibility is the actual signal.

This is a heuristic and it is honest about that: it ranks suspicion, it does not prove
absence. Start it ADVISORY, tune the noise on real data, flip it blocking once the report is
quiet -- the same path commit-tracker-overlap.py took.

USAGE
    commit-tracker-convergence.py                 sweep every file with a resolved row
    commit-tracker-convergence.py --changed BASE  only files whose rows changed vs BASE (CI)
    commit-tracker-convergence.py --json          machine-readable
    commit-tracker-convergence.py --file PATH     one file, with the hunks printed

An intentional divergence that this cannot infer is declared in the row note as
    no-converge: <reason>
which exempts that file, the same shape as commit-tracker-overlap.py's `no-file-overlap:`.
"""

import argparse
import difflib
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TSV = os.path.join(ROOT, "commit-tracker", "coverage.tsv")
INDEX = os.path.join(ROOT, "commit-tracker", "index.tsv")
UPSTREAM = os.environ.get("UZDOOM_DIR", os.path.expanduser("~/repos/uzdoom"))

# [rc4l] Flip to False once the sweep is quiet enough that a flag means something.
ADVISORY = True

# A hunk whose OUR-side lines carry one of these is a divergence someone signed for.
FORK_TAGS = re.compile(
    r"\[(rc4l|BB|BC|AK|TP|EP|SB|CW|BL|ZK|RC|WS|Dusk|MGOOOOOO|ZandroX|Zandronum)\]"
)

# Upstream layout -> ours. Longest prefix wins.
PATH_MAP = [
    ("wadsrc/", "src/zandronum/wadsrc/"),
    ("src/", "src/zandronum/src/"),
]

# Only files we deliberately WALK toward upstream can be checked this way.
#
# This is the whole load-bearing assumption. Outside the staircase we port slices: a sweeping
# upstream commit ("removed STACK_ARGS") touches hundreds of files, and taking the part that
# applies to us does NOT mean we adopted upstream's whole copy of each one. Run unscoped, this
# check flagged 207 of 459 files and led with a 1975-line "divergence" in dobjtype.cpp -- all
# of it correct divergence, none of it actionable.
#
# The renderer staircase is different: docs/renderer-staircase.md walks src/gl commit by
# commit precisely so our copy tracks upstream's, so "our file should equal upstream's file at
# the newest commit we claim" is true there by intent. That is also where every miss the audit
# found actually lived.
INCLUDE_PREFIXES = (
    "src/zandronum/src/gl/",
    "src/zandronum/wadsrc/static/shaders/",
)

# Within that scope, still ours wholesale or vendored as a snapshot.
EXEMPT_PREFIXES = (
    "src/zandronum/src/gl/system/gl_framebuffer.cpp",  # video-scale + Cocoa sizing are ours
)


def sh(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, errors="ignore", **kw).stdout


def load_rows():
    rows = []
    with open(TSV, encoding="utf-8", newline="") as fh:
        for line in fh:
            if line.startswith("#"):
                continue
            f = line.rstrip("\n").split("\t")
            if len(f) < 6 or f[3] not in ("ported", "adapted"):
                continue
            rows.append({"sha": f[0], "date": f[1], "title": f[2],
                         "status": f[3], "note": f[4], "ours": f[5]})
    return rows


def our_path(up_path):
    for src, dst in PATH_MAP:
        if up_path.startswith(src):
            return dst + up_path[len(src):]
    return None


def newest_claim_per_file(rows):
    """file -> (upstream sha, date, row) for the newest resolved row touching it.

    Reads commit-tracker/index.tsv (path -> shas that touched it) rather than shelling out
    per row. The naive version cost one `git show --name-only` per resolved row: seven
    minutes at 638 rows, and the ledger has 19k. The index is already generated by regen.sh
    and is the same data.
    """
    by_sha = {r["sha"]: r for r in rows}
    best = {}
    with open(INDEX, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("#"):
                continue
            up_path, _, shas = line.rstrip("\n").partition("\t")
            ours = our_path(up_path)
            if not ours:
                continue
            for sha in shas.split():
                r = by_sha.get(sha)
                if r is None:
                    continue
                cur = best.get(ours)
                if cur is None or r["date"] > cur["date"]:
                    best[ours] = {"date": r["date"], "sha": sha, "up_path": up_path, "row": r}
    return best


def batch_blobs(claims):
    """One `git cat-file --batch` for every upstream blob we need, instead of one per file."""
    keys = [f"{c['sha']}:{c['up_path']}" for c in claims.values()]
    if not keys:
        return {}
    proc = subprocess.run(["git", "-C", UPSTREAM, "cat-file", "--batch"],
                          input=("\n".join(keys) + "\n").encode(),
                          capture_output=True)
    out, blobs, pos = proc.stdout, {}, 0
    for key in keys:
        nl = out.find(b"\n", pos)
        if nl < 0:
            break
        header = out[pos:nl].decode("utf-8", "replace")
        if header.endswith(("missing", "ambiguous")):
            pos = nl + 1
            continue
        size = int(header.rsplit(" ", 1)[1])
        blobs[key] = out[nl + 1:nl + 1 + size].decode("utf-8", "replace")
        pos = nl + 1 + size + 1  # payload plus its trailing newline
    return blobs


def compare(ours_rel, claim, blobs):
    """Return (missing_line_count, sample_hunks) for lines upstream has that we do not."""
    abs_ours = os.path.join(ROOT, ours_rel)
    if not os.path.exists(abs_ours):
        return None  # subsystem we do not carry; the skip is derived elsewhere
    up_text = blobs.get(f"{claim['sha']}:{claim['up_path']}")
    if not up_text:
        return None
    try:
        our_text = open(abs_ours, encoding="utf-8", errors="ignore").read()
    except OSError:
        return None

    up_lines = [l.rstrip() for l in up_text.splitlines()]
    our_lines = [l.rstrip() for l in our_text.splitlines()]
    sm = difflib.SequenceMatcher(None, our_lines, up_lines, autojunk=False)

    missing = 0
    hunks = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag not in ("delete", "replace", "insert"):
            continue
        up_only = [l for l in up_lines[j1:j2] if l.strip() and not l.strip().startswith("//")]
        if not up_only:
            continue
        # Did we sign for this divergence? Look at our side of the hunk plus a little context.
        ctx = our_lines[max(0, i1 - 3):i2 + 3]
        if any(FORK_TAGS.search(l) for l in ctx):
            continue
        missing += len(up_only)
        if len(hunks) < 3:
            hunks.append({"our_line": i1 + 1, "upstream": up_only[:4]})
    return missing, hunks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--changed", metavar="BASE",
                    help="only files whose coverage.tsv rows changed vs BASE (for CI)")
    ap.add_argument("--file", help="inspect a single repo-relative path")
    ap.add_argument("--top", type=int, default=25, help="how many to print (default 25)")
    args = ap.parse_args()

    if not os.path.isdir(UPSTREAM):
        print(f"commit-tracker-convergence: no upstream clone at {UPSTREAM}; skipping.")
        return 0

    rows = load_rows()

    if args.changed:
        diff = sh(["git", "-C", ROOT, "diff", args.changed, "--unified=0", "--",
                   "commit-tracker/coverage.tsv"])
        touched = {l[1:].split("\t")[0] for l in diff.splitlines()
                   if l.startswith("+") and not l.startswith("+++") and "\t" in l}
        rows = [r for r in rows if r["sha"] in touched]
        if not rows:
            print("commit-tracker-convergence: no resolved rows changed; nothing to check.")
            return 0

    exempt = {r["sha"] for r in rows if "no-converge:" in r["note"]}
    rows = [r for r in rows if r["sha"] not in exempt]

    claims = newest_claim_per_file(rows)
    if args.file:
        claims = {k: v for k, v in claims.items() if k == args.file}

    claims = {k: v for k, v in claims.items()
              if k.startswith(INCLUDE_PREFIXES) and not k.startswith(EXEMPT_PREFIXES)}
    blobs = batch_blobs(claims)

    findings = []
    for ours_rel, claim in sorted(claims.items()):
        res = compare(ours_rel, claim, blobs)
        if not res:
            continue
        missing, hunks = res
        if missing:
            findings.append({"file": ours_rel, "missing_lines": missing,
                             "claimed_upstream": claim["sha"][:9], "as_of": claim["date"][:10],
                             "title": claim["row"]["title"][:60], "hunks": hunks})

    findings.sort(key=lambda f: -f["missing_lines"])

    if args.json:
        print(json.dumps({"checked_files": len(claims), "findings": findings}, indent=1))
    else:
        print(f"commit-tracker-convergence: {len(claims)} files carry a resolved row; "
              f"{len(findings)} diverge from the upstream state they claim.\n")
        for f in findings[:args.top]:
            print(f"{f['missing_lines']:5d} lines  {f['file']}")
            print(f"            claims {f['claimed_upstream']} ({f['as_of']}) {f['title']}")
            if args.file:
                for h in f["hunks"]:
                    print(f"            our line {h['our_line']}: upstream has")
                    for l in h["upstream"]:
                        print(f"                + {l[:100]}")
        if len(findings) > args.top:
            print(f"\n... {len(findings) - args.top} more (use --top or --json)")
        if findings:
            print("\nEach is either a port that never landed, or a divergence nobody signed for.")
            print("Tag the code with a fork marker, or add 'no-converge: <reason>' to the row note.")

    if findings and not ADVISORY:
        return 1
    if findings and ADVISORY and not args.json:
        print("\nADVISORY: not failing the build. Tune the noise, then set ADVISORY = False.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
