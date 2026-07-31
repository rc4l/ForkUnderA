#!/usr/bin/env python3
"""
Unified, source-derived vocabulary -> tags. Everything below is grepped from the engine's
OWN source (UZDoom + our tree, at HEAD), then exact-matched in each commit's diff/message.
Nothing hand-listed; a new lump/keyword/action/ACS-func upstream or ours self-registers.

  lumps            names passed to lump-lookup calls (minus binary/graphic lumps)      -> lump:MAPINFO
  lump keywords    each lump parser's sc.Compare("...") literals, scoped to that file  -> MAPINFO:sky1
  DECORATE actions A_* from DEFINE_ACTION_FUNCTION                                      -> fn:A_Explode
  ACS builtins     names from the ACSF_ enum                                           -> acs:AddBot

Env: UZDOOM, ANCHOR, UNTIL.  Writes commit-tracker/combined_tags.tsv.
"""
import os, re, subprocess
from collections import defaultdict

UP     = os.environ.get("UZDOOM", "/Users/talhataj/repos/UZDoom")
ANCHOR = os.environ.get("ANCHOR", "ad88cfc5e")
UNTIL  = os.environ.get("UNTIL")
SINCE  = os.environ.get("SINCE")      # parallel worker: efficient date-range walk lower bound
HERE   = os.path.dirname(os.path.abspath(__file__))
REPO   = os.path.dirname(HERE)
OUT     = os.environ.get("OUT", os.path.join(HERE, "combined_tags.tsv"))
SHAFILE = os.environ.get("SHAFILE")   # parallel worker: diff only these shas (one per line)
TREES  = [UP, REPO]

LOOKUP = (r"CheckNumForName|GetNumForName|CheckNumForFullName|GetNumForFullName|"
          r"FindLump[A-Za-z]*|CheckLumpName|GetLumpName")
CALL = re.compile(r"(?:" + LOOKUP + r")\s*\(\s*\"([A-Za-z0-9_./]+)\"")
KW   = re.compile(r"(?:Compare|CheckString|GetString|MatchString)\s*\(\s*\"([a-z_][a-z0-9_]*)\"")
# generic scanner values/types that aren't feature keywords
KEYWORD_STOP = {"true", "false", "int", "float", "bool", "string", "void", "yes", "no",
                "null", "none", "on", "off"}
DAF  = re.compile(r"DEFINE_ACTION_FUNCTION(?:_NATIVE)?\s*\(\s*[A-Za-z_:]+\s*,\s*(A_[A-Za-z0-9]+)")
ACSF = re.compile(r"\bACSF_([A-Za-z][A-Za-z0-9]+)")
# binary / graphic / marker lumps to drop (conventions are stable -> effectively no upkeep)
DENY = re.compile(r"(FONT|^STCFN|^STBFN|^SMALLFNT|^BIGFONT|^CONFONT|^SBIGFONT|^DBIGFONT|^HBIGFONT"
                  r"|^PLAYPAL|^COLORMAP|PAL$|^E\dPAL|^PALVERS|^MAP\d|^E\dM\d|^GL_LEVEL|^MNTRF"
                  r"|^D_LOGO|^BOOTLOGO|^STARTUP|^LOADING|NOTCH|^DSEMPTY|^SECRETS|^XHAIRS|^GENMIDI"
                  r"|^DMXGUS|^SNDCURVE|^S_SKIN|^PNAMES|^TEXTURE[12]$|^SPROFS|^STRFHELP)")


def grep(tree, pattern, paths, only=False):
    flag = "-hoE" if only else "-nE"
    return subprocess.run(["git", "-C", tree, "grep", flag, pattern, "HEAD", "--"] + paths,
                          capture_output=True, encoding="utf-8", errors="replace").stdout


def loc(line):                       # "HEAD:path:lineno:content" -> (path, content)
    p = line.split(":", 3)
    return (p[1], p[3]) if len(p) >= 4 else (None, None)


def alt(words):
    return re.compile(r"\b(" + "|".join(sorted(map(re.escape, words))) + r")\b") if words else None


def target_shas():
    limit = int(os.environ.get("LIMIT", "0"))   # 0 = all commits; set LIMIT=1000 for a quick trial
    shas = set()
    with open(os.path.join(HERE, "coverage.tsv")) as f:
        for i, line in enumerate(f):
            if i >= 2:
                if limit and len(shas) >= limit:
                    break
                shas.add(line.split("\t", 1)[0])
    return shas


def main():
    lump_files = defaultdict(set)
    for tree in TREES:
        for line in grep(tree, LOOKUP, ["*.cpp", "*.h", "*.mm"]).splitlines():
            path, content = loc(line)
            if not path:
                continue
            for m in CALL.finditer(content):
                name = m.group(1).upper().rsplit("/", 1)[-1]
                if re.fullmatch(r"[A-Z0-9_]{3,16}", name) and not DENY.search(name):
                    lump_files[name].add(os.path.basename(path))
    parser_bases = set().union(*lump_files.values()) if lump_files else set()

    base_kw = defaultdict(set)
    for tree in TREES:
        for line in grep(tree, r"(Compare|CheckString|GetString|MatchString)\(\"", ["*.cpp"]).splitlines():
            path, content = loc(line)
            if not path or os.path.basename(path) not in parser_bases:
                continue
            for m in KW.finditer(content):
                if len(m.group(1)) >= 3 and m.group(1) not in KEYWORD_STOP:
                    base_kw[os.path.basename(path)].add(m.group(1))
    lump_kw = {lp: set().union(*(base_kw.get(b, set()) for b in fs))
               for lp, fs in lump_files.items()}
    lump_kw = {k: v for k, v in lump_kw.items() if v}

    actions, acs = set(), set()
    ADEF = re.compile(r"A_[A-Za-z0-9]+")
    for tree in TREES:
        out = grep(tree, r"DEFINE_ACTION_FUNCTION[A-Za-z_]*\([^)]*A_[A-Za-z0-9]+", ["*.cpp"], only=True)
        actions.update(ADEF.findall(out))
        for m in ACSF.finditer(grep(tree, "ACSF_[A-Za-z0-9]+", ["*.cpp"], only=True)):
            if len(m.group(1)) >= 4:
                acs.add(m.group(1))

    print("gazetteer: %d lumps, %d with keywords (%d keyword defs), %d DECORATE actions, %d ACS funcs"
          % (len(lump_files), len(lump_kw), sum(len(v) for v in lump_kw.values()), len(actions), len(acs)))

    LUMP_RX, ACT_RX, ACS_RX = alt(lump_files), alt(actions), alt(acs)
    base_matchers = defaultdict(list)
    for lp, kws in lump_kw.items():
        rx = alt(kws)
        for b in lump_files[lp]:
            base_matchers[b].append((lp, rx))

    # A worker gets its sha chunk (want) via SHAFILE and an efficient date-range walk via
    # SINCE/UNTIL. The date window is only the walk bound; `want` does the precise assignment,
    # so overlapping windows are harmless (each commit is tagged by exactly one worker).
    want = set(open(SHAFILE).read().split()) if SHAFILE else target_shas()
    tags = defaultdict(set)
    walk = ((["--since=" + SINCE] if SINCE else []) +
            (["--until=" + UNTIL] if UNTIL else []) + [ANCHOR + "..HEAD"])

    # messages: lump names only (keywords need parser scope; actions/acs rarely in prose)
    args = ["git", "-C", UP, "log", "--format=%H\x1f%B%x00"] + walk
    blob = subprocess.run(args, capture_output=True, encoding="utf-8", errors="replace").stdout
    for rec in blob.split("\0"):
        if "\x1f" in rec:
            sha, msg = rec.split("\x1f", 1)
            if sha.strip() in want:
                for h in LUMP_RX.findall(msg):
                    tags[sha.strip()].add("lump:" + h)

    # diffs: lump names + actions + acs everywhere; keywords only on their parser file's lines
    args = ["git", "-C", UP, "log", "-p", "--unified=0", "--format=\x01%H"] + walk
    p = subprocess.Popen(args, stdout=subprocess.PIPE, encoding="utf-8", errors="replace", bufsize=1)
    sha, on, budget, base = None, False, 0, ""
    for line in p.stdout:
        line = line.rstrip("\n")[:2000]
        if line.startswith("\x01"):
            sha = line[1:].strip(); on = sha in want; budget = 0; base = ""
        elif not on:
            continue
        elif line.startswith("+++ b/"):
            base = line.rsplit("/", 1)[-1]
        elif line[:1] in "+-" and not line.startswith(("+++", "---")) and budget < 600:
            budget += 1
            if LUMP_RX:
                for h in LUMP_RX.findall(line):
                    tags[sha].add("lump:" + h)
            if ACT_RX:
                for h in ACT_RX.findall(line):
                    tags[sha].add("fn:" + h)
            if ACS_RX:
                for h in ACS_RX.findall(line):
                    tags[sha].add("acs:" + h)
            for lp, rx in base_matchers.get(base, ()):
                for kw in rx.findall(line):
                    tags[sha].add(lp + ":" + kw)
    p.stdout.close(); p.wait()

    with open(OUT, "w") as f:
        f.write("# sha -> source-derived tags: lump:X  X:keyword  fn:A_X  acs:X\n")
        for sha in sorted(tags):
            f.write(sha + "\t" + " ".join(sorted(tags[sha])) + "\n")

    kind = defaultdict(int)
    for s in tags.values():
        for t in s:
            k = "keyword" if re.match(r"[A-Z0-9_]+:[a-z]", t) else t.split(":", 1)[0]
            kind[k] += 1
    print("commits tagged: %d / 1000" % len(tags))
    print("tag counts by kind:", dict(sorted(kind.items(), key=lambda x: -x[1])))
    print("\n--- sample tagged commits ---")
    for sha in list(tags)[:8]:
        print("  %s  %s" % (sha[:9], " ".join(sorted(tags[sha]))[:110]))


if __name__ == "__main__":
    main()
