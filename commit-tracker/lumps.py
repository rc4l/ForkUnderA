#!/usr/bin/env python3
"""
Trial: lump tags, with the lump vocabulary auto-derived from UZDoom's OWN source
(at HEAD, so it covers every lump the engine has ever/now/will know) -- then matched
as EXACT tokens in each commit's diff + message. No hardcoded lump list.

Gazetteer = the string literals passed to the engine's lump-lookup calls
(CheckNumForName / GetNumForName / FindLump / ...). A new lump upstream shows up in
one of those calls -> next run discovers it automatically.

Env: UZDOOM, ANCHOR, UNTIL.  Writes commit-tracker/lump_tags.tsv.
"""
import os, re, subprocess
from collections import defaultdict

UP     = os.environ.get("UZDOOM", "/Users/talhataj/repos/UZDoom")
ANCHOR = os.environ.get("ANCHOR", "ad88cfc5e")
UNTIL  = os.environ.get("UNTIL")
HERE   = os.path.dirname(os.path.abspath(__file__))
OUT    = os.path.join(HERE, "lump_tags.tsv")

# plain ERE alternation for git grep (no (?:...), which POSIX ERE rejects)
GREP   = (r"CheckNumForName|GetNumForName|CheckNumForFullName|GetNumForFullName|"
          r"FindLump[A-Za-z]*|CheckLumpName|GetLumpName")
CALL   = re.compile(r"(?:" + GREP + r")\s*\(\s*\"([A-Za-z0-9_./]+)\"")


REPO = os.path.dirname(HERE)   # our own ZandroX tree (Zandronum lumps + anything we add)


def grep_lumps(repo, lumps):
    # grep just the lookup-function names (valid ERE); let Python's regex do the precise
    # string extraction (git grep -E has no \s).
    out = subprocess.run(
        ["git", "-C", repo, "grep", "-hE", GREP, "HEAD", "--", "*.cpp", "*.h", "*.mm"],
        capture_output=True, encoding="utf-8", errors="replace").stdout
    for line in out.splitlines():
        for m in CALL.finditer(line):
            name = m.group(1).upper().rsplit("/", 1)[-1]
            if re.fullmatch(r"[A-Z0-9_]{3,16}", name):
                lumps.add(name)


def build_gazetteer():
    """Every lump the engine looks up -- from UZDoom AND our own tree, so upstream lumps,
    Zandronum's own lumps, and any we add later all self-register."""
    lumps = set()
    grep_lumps(UP, lumps)       # upstream (past/present/future upstream lumps)
    grep_lumps(REPO, lumps)     # ours (Zandronum + ZandroX lumps)
    if not lumps:
        raise SystemExit("gazetteer empty -- lookup-function grep matched nothing; check LOOKUP names")
    return lumps


def target_shas():
    shas = []
    with open(os.path.join(HERE, "coverage.tsv")) as f:
        for i, line in enumerate(f):
            if i < 2:
                continue
            if len(shas) >= 1000:
                break
            shas.append(line.split("\t", 1)[0])
    return set(shas)


def main():
    gaz = build_gazetteer()
    print("gazetteer: %d lumps discovered from UZDoom source" % len(gaz))
    print("  " + "  ".join(sorted(gaz)))
    RX = re.compile(r"\b(" + "|".join(sorted(map(re.escape, gaz))) + r")\b")

    want = target_shas()
    tags = defaultdict(set)

    # messages (NUL-separated)
    args = ["git", "-C", UP, "log", "--format=%H\x1f%B%x00"]
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
            for h in RX.findall(msg):
                tags[sha].add(h)

    # diffs (added/removed lines), capped per commit
    args = ["git", "-C", UP, "log", "-p", "--unified=0", "--format=\x01%H"]
    if UNTIL:
        args.append("--until=" + UNTIL)
    args.append(ANCHOR + "..HEAD")
    p = subprocess.Popen(args, stdout=subprocess.PIPE, encoding="utf-8", errors="replace", bufsize=1)
    sha, on, budget = None, False, 0
    for line in p.stdout:
        line = line.rstrip("\n")[:2000]
        if line.startswith("\x01"):
            sha = line[1:].strip(); on = sha in want; budget = 0
        elif not on:
            continue
        elif line[:1] in "+-" and not line.startswith(("+++", "---")) and budget < 600:
            budget += 1
            for h in RX.findall(line):
                tags[sha].add(h)
    p.stdout.close(); p.wait()

    with open(OUT, "w") as f:
        f.write("# sha -> lump tags (exact lump-name tokens found in the commit's diff/message).\n")
        for sha in sorted(tags):
            f.write(sha + "\t" + " ".join("lump:" + t for t in sorted(tags[sha])) + "\n")

    freq = defaultdict(int)
    for s in tags.values():
        for t in s:
            freq[t] += 1
    print("\ncommits with >=1 lump tag: %d / 1000" % len(tags))
    print("--- lump tag frequency ---")
    for t, c in sorted(freq.items(), key=lambda x: -x[1]):
        print("  %-12s %d commits" % (t, c))


if __name__ == "__main__":
    main()
