#!/usr/bin/env python3
"""
Unified, source-derived vocabulary -> tags. Everything below is grepped from the engine's
OWN source (the ledger's upstream + our tree, at their heads), then exact-matched in each
commit's diff/message. Nothing hand-listed; a new lump/keyword/action/ACS-func/cvar upstream or
ours self-registers.

  lumps            names passed to lump-lookup calls (minus binary/graphic lumps)      -> lump:MAPINFO
  lump keywords    each lump parser's sc.Compare("...") literals, scoped to that file  -> MAPINFO:sky1
  DECORATE actions A_* from DEFINE_ACTION_FUNCTION                                      -> fn:A_Explode
  ACS builtins     names from the ACSF_ enum                                           -> acs:AddBot
  cvars            names from CVAR/CUSTOM_CVAR definitions (with an underscore, so      -> cvar:sv_cheats
                   `name` and `team` don't tag every commit)

Usage:  gazetteer.py [--ledger uzdoom|zandronum] [--to REV] [--out FILE] [--limit N]
        writes the ledger's combined_tags.tsv
"""
import argparse
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ledger as L  # noqa: E402
import tracker  # noqa: E402
import vcs  # noqa: E402

LOOKUP = (r"CheckNumForName|GetNumForName|CheckNumForFullName|GetNumForFullName|"
          r"FindLump[A-Za-z]*|CheckLumpName|GetLumpName")
CALL = re.compile(r"(?:" + LOOKUP + r")\s*\(\s*\"([A-Za-z0-9_./]+)\"")
KW   = re.compile(r"(?:Compare|CheckString|GetString|MatchString)\s*\(\s*\"([a-z_][a-z0-9_]*)\"")
# generic scanner values/types that aren't feature keywords
KEYWORD_STOP = {"true", "false", "int", "float", "bool", "string", "void", "yes", "no",
                "null", "none", "on", "off"}
DAF  = re.compile(r"DEFINE_ACTION_FUNCTION[A-Za-z_]*\([^)]*A_[A-Za-z0-9]+")
ADEF = re.compile(r"A_[A-Za-z0-9]+")
ACSF = re.compile(r"\bACSF_([A-Za-z][A-Za-z0-9]+)")
CVARDEF = re.compile(r"\b(?:CUSTOM_)?CVAR\s*\(\s*\w+\s*,\s*([a-z][a-z0-9]*_[a-z0-9_]+)\s*,")
# binary / graphic / marker lumps to drop (conventions are stable -> effectively no upkeep)
DENY = re.compile(r"(FONT|^STCFN|^STBFN|^SMALLFNT|^BIGFONT|^CONFONT|^SBIGFONT|^DBIGFONT|^HBIGFONT"
                  r"|^PLAYPAL|^COLORMAP|PAL$|^E\dPAL|^PALVERS|^MAP\d|^E\dM\d|^GL_LEVEL|^MNTRF"
                  r"|^D_LOGO|^BOOTLOGO|^STARTUP|^LOADING|NOTCH|^DSEMPTY|^SECRETS|^XHAIRS|^GENMIDI"
                  r"|^DMXGUS|^SNDCURVE|^S_SKIN|^PNAMES|^TEXTURE[12]$|^SPROFS|^STRFHELP)")


def alt(words):
    return re.compile(r"\b(" + "|".join(sorted(map(re.escape, words))) + r")\b") if words else None


def vocabulary(trees):
    """(lump -> parser basenames, lump -> keywords, actions, acs funcs, cvars) from every tree's head."""
    lump_files = defaultdict(set)
    for repo, rev in trees:
        for path, content in repo.grep(LOOKUP, ["*.cpp", "*.h", "*.mm"], rev):
            for m in CALL.finditer(content):
                name = m.group(1).upper().rsplit("/", 1)[-1]
                if re.fullmatch(r"[A-Z0-9_]{3,16}", name) and not DENY.search(name):
                    lump_files[name].add(os.path.basename(path))
    parser_bases = set().union(*lump_files.values()) if lump_files else set()

    base_kw = defaultdict(set)
    actions, acs, cvars = set(), set(), set()
    for repo, rev in trees:
        for path, content in repo.grep(r"(Compare|CheckString|GetString|MatchString)\(\"", ["*.cpp"], rev):
            if os.path.basename(path) in parser_bases:
                for m in KW.finditer(content):
                    if len(m.group(1)) >= 3 and m.group(1) not in KEYWORD_STOP:
                        base_kw[os.path.basename(path)].add(m.group(1))
        for _, content in repo.grep(r"DEFINE_ACTION_FUNCTION[A-Za-z_]*\([^)]*A_[A-Za-z0-9]+", ["*.cpp"], rev):
            for m in DAF.finditer(content):
                actions.update(ADEF.findall(m.group(0)))
        for _, content in repo.grep("ACSF_[A-Za-z0-9]+", ["*.cpp"], rev):
            acs.update(n for n in ACSF.findall(content) if len(n) >= 4)
        for _, content in repo.grep(r"CVAR *\(", ["*.cpp", "*.h"], rev):
            cvars.update(CVARDEF.findall(content))
    lump_kw = {lp: set().union(*(base_kw.get(b, set()) for b in fs)) for lp, fs in lump_files.items()}
    lump_kw = {k: v for k, v in lump_kw.items() if v}
    return lump_files, lump_kw, actions, acs, cvars


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ledger", default="uzdoom")
    ap.add_argument("--to", help="pin the range's end")
    ap.add_argument("--out", help="write here instead of the ledger's combined_tags.tsv")
    ap.add_argument("--limit", type=int, default=0, help="only the first N rows (a quick trial)")
    args = ap.parse_args()
    led = L.load_config()[args.ledger]
    repo = tracker.repo_for(led)
    to = args.to or led["to"]
    trees = [(repo, to), (vcs.GitRepo(L.ROOT), "HEAD")]
    lump_files, lump_kw, actions, acs, cvars = vocabulary(trees)
    print("gazetteer: %d lumps, %d with keywords (%d keyword defs), %d DECORATE actions, %d ACS funcs, %d cvars"
          % (len(lump_files), len(lump_kw), sum(len(v) for v in lump_kw.values()), len(actions), len(acs), len(cvars)))

    LUMP_RX, ACT_RX, ACS_RX, CVAR_RX = alt(lump_files), alt(actions), alt(acs), alt(cvars)
    base_matchers = defaultdict(list)
    for lp, kws in lump_kw.items():
        rx = alt(kws)
        for b in lump_files[lp]:
            base_matchers[b].append((lp, rx))

    rows = L.read_rows(led)
    want = {r["sha"] for r in (rows[:args.limit] if args.limit else rows)}
    tags = defaultdict(set)

    # messages: lump names only (keywords need parser scope; actions/acs rarely in prose)
    for sha, msg in repo.messages(led["from"], to):
        if sha in want and LUMP_RX:
            for h in LUMP_RX.findall(msg):
                tags[sha].add("lump:" + h)

    # diffs: lump names + actions + acs + cvars everywhere; keywords only on their parser file's lines
    budget, base, cur = 0, "", None
    for sha, line in repo.patches(led["from"], to):
        if sha != cur:
            cur, budget, base = sha, 0, ""
        if sha not in want:
            continue
        line = line[:2000]
        if line.startswith("+++ b/"):
            base = line.rsplit("/", 1)[-1]
        elif line[:1] in "+-" and not line.startswith(("+++", "---")) and budget < 600:
            budget += 1
            for rx, prefix in ((LUMP_RX, "lump:"), (ACT_RX, "fn:"), (ACS_RX, "acs:"), (CVAR_RX, "cvar:")):
                if rx:
                    for h in rx.findall(line):
                        tags[sha].add(prefix + h)
            for lp, rx in base_matchers.get(base, ()):
                for kw in rx.findall(line):
                    tags[sha].add(lp + ":" + kw)

    out = args.out or os.path.join(L.ledger_dir(led), "combined_tags.tsv")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# sha -> source-derived tags: lump:X  X:keyword  fn:A_X  acs:X  cvar:X\n")
        for sha in sorted(tags):
            f.write(sha + "\t" + " ".join(sorted(tags[sha])) + "\n")

    kind = defaultdict(int)
    for s in tags.values():
        for t in s:
            k = "keyword" if re.match(r"[A-Z0-9_]+:[a-z]", t) else t.split(":", 1)[0]
            kind[k] += 1
    print("commits tagged: %d / %d" % (len(tags), len(want)))
    print("tag counts by kind:", dict(sorted(kind.items(), key=lambda x: -x[1])))


if __name__ == "__main__":
    main()
