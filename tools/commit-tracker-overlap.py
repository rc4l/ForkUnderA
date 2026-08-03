#!/usr/bin/env python3
"""
commit-tracker-overlap.py -- does a `ported`/`adapted` row actually correspond to the commit it cites?

commit-tracker-check.sh already verifies the cited zandrox sha EXISTS. That is a weak claim: a row
can name a real commit that had nothing to do with the upstream change, and the row still passes.
When that happens the upstream commit is silently removed from the queue forever -- a *silent skip*,
which is precisely the failure the tracker exists to prevent, and it is invisible to every other gate.

That is not hypothetical. Migrating the old staircase ledger into this tracker (919beed) recorded
03d4f23a6, d925279be and a26fbc74f as `ported` against a9b178e -- but a9b178e's own message says the
opposite in plain words: "Skipped as inapplicable ... GL adaptations to ZDoom's long-texture-names
change, which Zandronum never took." The migration turned three documented SKIPS into claimed ports,
which is worse than a wrong attribution: a skip carries a dependency that can expire, and recording
it as `ported` deletes both the dependency and any reason to look again. The long-names series has
since been ported, so all three became portable and nothing would have noticed.

The check: an upstream commit's files (from commit-tracker/files.tsv) and our commit's files must
share at least one BASENAME. Basenames, not paths, because upstream renames directories constantly
(src/gl -> src/rendering/hwrenderer) and our tree prefixes everything with src/zandronum.

Zero overlap is not always wrong -- a port can legitimately land somewhere else entirely (rewritten
into a features/ unit, or folded into a differently-named file). So a row can opt out explicitly by
putting `no-file-overlap: <reason>` in its note. The escape is per-row and must say why, so an
unexplained mismatch stays loud.

BLOCKING. It was advisory while ~17 pre-existing rows from the ledger migration sat untriaged, on
the reasoning that a new check failing CI on old debt only teaches people to route around it. That
backlog is now cleared: ten were real ports that landed inside renderer-staircase flight commits and
carry a `no-file-overlap:` note saying so, and seven went back to `pending` because a content check
could not find them in our tree either.

Blocking matters because this is the one check that catches a WRONG provenance rather than a missing
one. The sha-existence and reachability checks are both satisfied by a row citing a real commit that
has nothing to do with the change -- which happened three batches running, always the same way: rows
recorded in a bookkeeping step cite whichever commit was convenient rather than the one holding the
code. That is invisible to every other gate here.

LIMITATION, stated because a checker you over-trust is worse than none: one shared basename is
enough to pass, so a large flight touching many files in a subsystem can coincidentally overlap an
unrelated commit in that same subsystem. That is exactly how 03d4f23a6 slipped past (it shares
gl_material.cpp with a nine-commit buffer-conversion flight). This catches blatant mismatches, not
subtle ones. The stronger signal, when our commit message declares which upstream shas it covers,
is to check membership in that list by hand.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COVERAGE = os.path.join(ROOT, "commit-tracker/coverage.tsv")
FILES = os.path.join(ROOT, "commit-tracker/files.tsv")
ESCAPE = "no-file-overlap:"
# Flip to False once the inherited backlog below is triaged; then this blocks CI.
ADVISORY = False


def upstream_files():
    """upstream sha -> set of basenames it touched."""
    out = {}
    with open(FILES) as fh:
        for line in fh:
            if line.startswith("#"):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 2:
                continue
            out[parts[0]] = {os.path.basename(p) for p in parts[1].split()}
    return out


def our_files(sha, cache):
    """basenames touched by one of our commits; empty set if the sha is unreadable."""
    if sha not in cache:
        res = subprocess.run(
            ["git", "-C", ROOT, "show", "--format=", "--name-only", sha],
            capture_output=True, text=True)
        cache[sha] = {os.path.basename(p) for p in res.stdout.split() if "." in p}
    return cache[sha]


def main():
    up = upstream_files()
    cache = {}
    mismatches = []
    checked = 0

    with open(COVERAGE) as fh:
        for line in fh:
            f = line.rstrip("\n").split("\t")
            if len(f) != 6 or f[3] not in ("ported", "adapted"):
                continue
            sha, note, ours = f[0], f[4], f[5]
            # zandronum-base rows describe inheritance, not a port -- nothing to compare.
            if ours in ("", "/", "zandronum-base"):
                continue
            if sha not in up:          # upstream file list unknown (shallow index); cannot judge
                continue
            if ESCAPE in note:         # explicitly justified
                continue
            checked += 1
            for tok in ours.split(","):
                if up[sha] & our_files(tok.strip(), cache):
                    break
            else:
                mismatches.append((sha, f[3], ours, f[2][:52]))

    if mismatches:
        print("commit-tracker-overlap: rows whose cited commit touched none of the upstream files:")
        for sha, status, ours, title in mismatches:
            print(f"  {sha[:12]} [{status}] -> {ours}   {title}")
        print()
        print(f"{len(mismatches)} of {checked} checked rows. Each is either a mis-attribution (the")
        print("upstream commit is then silently OUT of the queue and must go back to pending), or a")
        print(f"real port that landed elsewhere -- say so with '{ESCAPE} <reason>' in the row's note.")
        if ADVISORY:
            print("\nADVISORY: not failing the build. Triage these, then set ADVISORY = False.")
            return 0
        return 1

    print(f"commit-tracker-overlap: clean ({checked} rows cross-checked against their commits).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
