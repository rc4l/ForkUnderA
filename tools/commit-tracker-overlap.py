#!/usr/bin/env python3
"""
commit-tracker-overlap.py -- does a `ported`/`adapted` row actually correspond to the commit it cites?

commit-tracker-check.sh already verifies the cited zandrox sha EXISTS. That is a weak claim: a row
can name a real commit that had nothing to do with the upstream change, and the row still passes.
When that happens the upstream commit is silently removed from the queue forever -- a *silent skip*,
which is precisely the failure the tracker exists to prevent, and it is invisible to every other gate.

That is not hypothetical. Migrating the old staircase ledger into this tracker (e3fb767) recorded
03d4f23a6, d925279be and a26fbc74f as `ported` against 0b11227 -- but 0b11227's own message says the
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
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "commit-tracker"))
import ledger as L  # noqa: E402
from overlap import ESCAPE, mismatches  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ledger", default="uzdoom")
    args = ap.parse_args()
    led = L.load_config()[args.ledger]
    found, checked = mismatches(L.read_rows(led), L.read_files_map(led))
    if found:
        print("commit-tracker-overlap [%s]: rows whose cited commit touched none of the upstream files:" % args.ledger)
        for sha, status, ours, title in found:
            print(f"  {sha[:12]} [{status}] -> {ours}   {title}")
        print()
        print(f"{len(found)} of {checked} checked rows. Each is either a mis-attribution (the")
        print("upstream commit is then silently OUT of the queue and must go back to pending), or a")
        print(f"real port that landed elsewhere -- say so with '{ESCAPE} <reason>' in the row's note.")
        return 1
    print(f"commit-tracker-overlap [{args.ledger}]: clean ({checked} rows cross-checked against their commits).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
