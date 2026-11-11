#!/usr/bin/env python3
"""
Build a candidate-tag vocabulary from a commit range, the bag-of-words way.

Tokenizes THREE streams per commit and counts them into one term table:
  * message  -- the commit title+body (human intent: "dialogue", "sound", "crash")
  * code     -- identifiers on ADDED diff lines, split on snake_case / CamelCase
  * file     -- touched path components + filename subwords

Then ranks terms so you can eyeball keep/toss cutoffs. Deterministic, no ML:
  df   = # of commits the term appears in
  tf   = total occurrences
  idf  = log(N / df);  a term is a good tag when df is neither tiny (noise) nor
         ~N (ubiquitous filler). Output is sorted by df so both ends are visible.

Usage: vocab.py [--ledger NAME] [--limit N]  (first N rows, default 1000).  Writes <ledger>/vocab.tsv.
"""
import argparse, math, os, re, sys
from collections import defaultdict

HERE   = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ledger as L  # noqa: E402
import tracker  # noqa: E402

STOP = set("""
the and for that with this are was not but from have has all use used get set add added new old
one two int char void const return if else for while do class struct public private protected static
bool true false null nullptr include define ifdef ifndef endif else elif pragma typedef enum union
unsigned signed long short float double string std size sizeof this self case break continue switch
default goto extern inline virtual override operator template typename namespace using auto register
volatile mutable friend explicit const_cast dynamic_cast reinterpret_cast static_cast delete sizeof
tmp val ptr idx len str buf num obj arg args ret out src dst pos cur prev next min max sum cnt
now can not into out its via per etc will make made only just also than then when where which what
some more most less few any every each other same such very much many how why who whom because
fixed fix changed change update updated remove removed added adding using use also should would could
""".split())

WORD  = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
CAMEL = re.compile(r"[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z]+|[A-Z]+|[0-9]+")


def subwords(tok):
    out = []
    for part in re.split(r"[_\W]+", tok):
        out += CAMEL.findall(part)
    return [w.lower() for w in out]


def keep(w):
    return len(w) >= 3 and not w.isdigit() and w not in STOP


def tokens(text):
    for m in WORD.findall(text):
        for w in subwords(m):
            if keep(w):
                yield w


def target_shas(led, limit):
    """The first `limit` rows' shas, straight from coverage.tsv (already floored and ordered)."""
    return {r["sha"] for r in L.read_rows(led)[:limit]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ledger", default="uzdoom")
    ap.add_argument("--limit", type=int, default=1000)
    args = ap.parse_args()
    led = L.load_config()[args.ledger]
    repo = tracker.repo_for(led)
    want = target_shas(led, args.limit)
    N = len(want)
    tf = defaultdict(int)                 # term -> total occurrences
    df = defaultdict(set)                 # term -> set of shas
    src = defaultdict(set)                # term -> {'m','c','f'}

    def add(sha, text, s):
        for w in tokens(text):
            tf[w] += 1
            df[w].add(sha)
            src[w].add(s)

    # --- messages ---
    for sha, msg in repo.messages(led["from"], led["to"]):
        if sha in want:
            add(sha, msg, "m")

    # --- code (added diff lines) + filenames ---
    cur, budget = None, 0
    for sha, line in repo.patches(led["from"], led["to"]):
        if sha != cur:
            cur, budget = sha, 0
        if sha not in want:
            continue
        line = line[:2000]                         # cap pathological minified lines
        if line.startswith("+++ b/"):
            add(sha, line[6:].replace("/", " "), "f")   # path components + filename
        elif line and line[0] == "+" and not line.startswith("+++") and budget < 600:
            add(sha, line[1:], "c")                # cap code lines/commit (skip huge imports)
            budget += 1

    rows = []
    for w in tf:
        d = len(df[w])
        idf = math.log(N / d)
        rows.append((w, d, tf[w], round(idf, 2), "".join(sorted(src[w]))))
    rows.sort(key=lambda r: (-r[1], -r[2]))        # by df, then tf

    with open(os.path.join(L.ledger_dir(led), "vocab.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("# candidate-tag vocabulary over %d commits. term<TAB>commits(df)<TAB>total(tf)"
                "<TAB>idf<TAB>source(m=msg c=code f=file)\n" % N)
        for r in rows:
            f.write("%s\t%d\t%d\t%s\t%s\n" % r)
    print("vocab.tsv: %d unique terms over %d commits" % (len(rows), N))
    print("\n--- top 40 by commit-frequency (broad candidates) ---")
    for r in rows[:40]:
        print("  %-18s df=%-4d tf=%-5d idf=%-4s [%s]" % r)


if __name__ == "__main__":
    main()
