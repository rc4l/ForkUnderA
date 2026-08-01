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

Env: UZDOOM, ANCHOR, UNTIL (date bound).  Writes commit-tracker/vocab.tsv.
"""
import math, os, re, subprocess
from collections import defaultdict

UP     = os.environ.get("UZDOOM", "/Users/talhataj/repos/UZDoom")
ANCHOR = os.environ.get("ANCHOR", "ad88cfc5e")
UNTIL  = os.environ.get("UNTIL")
HERE   = os.path.dirname(os.path.abspath(__file__))
OUT    = os.path.join(HERE, "vocab.tsv")

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


def target_shas():
    """First-1000 shas straight from coverage.tsv (already floored & ordered)."""
    cov = os.path.join(HERE, "coverage.tsv")
    shas = set()
    with open(cov) as f:
        for i, line in enumerate(f):
            if i < 2:  # comment + header
                continue
            if len(shas) >= 1000:
                break
            shas.add(line.split("\t", 1)[0])
    return shas


def main():
    want = target_shas()
    N = len(want)
    tf = defaultdict(int)                 # term -> total occurrences
    df = defaultdict(set)                 # term -> set of shas
    src = defaultdict(set)                # term -> {'m','c','f'}

    def add(sha, text, s):
        for w in tokens(text):
            tf[w] += 1
            df[w].add(sha)
            src[w].add(s)

    # --- messages (NUL-separated so multi-line bodies stay intact) ---
    args = ["git", "-C", UP, "log", "--format=%H%x1f%B%x00"]
    if UNTIL:
        args.append("--until=" + UNTIL)
    args.append(ANCHOR + "..HEAD")
    blob = subprocess.run(args, capture_output=True, encoding="utf-8", errors="replace").stdout
    for rec in blob.split("\0"):
        if "\x1f" not in rec:
            continue
        sha, msg = rec.split("\x1f", 1)
        sha = sha.strip()
        if sha in want:
            add(sha, msg, "m")

    # --- code (added diff lines) + filenames ---
    args = ["git", "-C", UP, "log", "-p", "--unified=0", "--format=\x01%H"]
    if UNTIL:
        args.append("--until=" + UNTIL)
    args.append(ANCHOR + "..HEAD")
    p = subprocess.Popen(args, stdout=subprocess.PIPE, encoding="utf-8", errors="replace", bufsize=1)
    sha, on, budget = None, False, 0
    for line in p.stdout:
        line = line.rstrip("\n")[:2000]           # cap pathological minified lines
        if line.startswith("\x01"):
            sha = line[1:].strip(); on = sha in want; budget = 0
        elif not on:
            continue
        elif line.startswith("+++ b/"):
            add(sha, line[6:].replace("/", " "), "f")   # path components + filename
        elif line and line[0] == "+" and not line.startswith("+++") and budget < 600:
            add(sha, line[1:], "c")                # cap code lines/commit (skip huge imports)
            budget += 1
    p.stdout.close(); p.wait()

    rows = []
    for w in tf:
        d = len(df[w])
        idf = math.log(N / d)
        rows.append((w, d, tf[w], round(idf, 2), "".join(sorted(src[w]))))
    rows.sort(key=lambda r: (-r[1], -r[2]))        # by df, then tf

    with open(OUT, "w") as f:
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
