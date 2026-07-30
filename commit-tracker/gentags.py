#!/usr/bin/env python3
"""
Generate commit-tracker/tags.tsv  ->  sha \\t space-separated tags

Tags are DERIVED from git, never hand-written per commit, and RIGID -- they come from
literal path structure, not fuzzy substring matching:

  * category tags -- every literal directory component a commit touches, plus the
                     filename's subsystem (its ZDoom `xxx_` prefix, or the stem).
  * fn:A_* tags   -- action-function identifiers matched on the diff's changed lines
                     (exact token `A_[A-Z][A-Za-z0-9]*`, pattern-discovered).
  * merge         -- a commit with no files (a merge) is tagged `merge`.

Env:  UZDOOM (clone path), ANCHOR (parity commit), SYM_UNTIL / UNTIL (bound the passes to a
      date window for a fast partial run).  Categories cover every commit up to UNTIL;
      symbols cover up to SYM_UNTIL (the -p diff pass is the slow one).
"""
import os, re, subprocess, sys
from collections import OrderedDict

UP      = os.environ.get("UZDOOM", "/Users/talhataj/repos/UZDoom")
ANCHOR  = os.environ.get("ANCHOR", "ad88cfc5e")
UNTIL   = os.environ.get("UNTIL")
SYM_UNTIL = os.environ.get("SYM_UNTIL")
HERE    = os.path.dirname(os.path.abspath(__file__))
OUT     = os.path.join(HERE, "tags.tsv")

SOURCE_ROOTS = ("src/", "wadsrc/static/", "wadsrc/")
# ZDoom's file-prefix convention -> readable subsystem. Exact-key lookup, not substring.
ZDOOM_PREFIX = {
    "p": "playsim", "g": "game", "r": "swrender", "s": "sound", "d": "main",
    "m": "misc", "c": "console", "v": "video", "i": "system", "w": "wad",
    "f": "finale", "b": "bot", "a": "actors", "hu": "hud", "st": "statusbar",
    "am": "automap", "po": "polyobj", "t": "fragglescript",
}
# gameplay dirs where DECORATE action functions live (keeps the -p diff small)
GAMEPLAY = ["src/g_doom", "src/g_heretic", "src/g_hexen", "src/g_strife", "src/g_shared",
            "src/g_raven", "src/thingdef", "src/scripting", "src/p_enemy.cpp",
            "src/p_mobj.cpp", "src/p_pspr.cpp", "wadsrc/static/zscript"]

SEP     = "\x1f"
MARK    = "\x01"
ACTION  = re.compile(r"A_[A-Z][A-Za-z0-9]*")
PREFIX  = re.compile(r"^([a-z0-9]+)_")


def file_subsystems(fname):
    """Subsystem tags from a filename: the ZDoom prefix (readable) PLUS the specific name
    token after it -- so d_net -> {main, net}, g_level -> {game, level}, p_setup -> {playsim, setup}.
    Broad prefix stays for grouping; the name breaks it down."""
    stem = fname.rsplit(".", 1)[0] or fname.lstrip(".")   # dotfile (.gitignore) -> "gitignore"
    m = PREFIX.match(stem)
    if not m:
        return {stem} if stem else set()
    prefix = m.group(1)
    out = {ZDOOM_PREFIX.get(prefix, prefix)}
    name = re.match(r"[a-z0-9]+", stem[len(prefix) + 1:])
    if name and len(name.group(0)) >= 2:
        out.add(name.group(0))
    return out


def path_tags(path):
    """Rigid tags for one touched path: literal dir components + filename subsystem."""
    base = path.rsplit("/", 1)[-1]
    if base == "CMakeLists.txt" or path.endswith(".cmake"):
        return {"build"}
    if path.startswith(".github/"):
        return {"ci"}
    if path.startswith("docs/"):
        return {"docs"}
    tags = set()
    if path.endswith(".zs"):
        tags.add("zscript")
    q = path
    for root in SOURCE_ROOTS:
        if q.startswith(root):
            q = q[len(root):]
            break
    parts = q.split("/")
    dirs = {d for d in parts[:-1] if d}       # every directory component (literal)
    tags |= dirs
    if parts[-1]:
        for sub in file_subsystems(parts[-1]):   # subsystem + name token; "f:" marks file-derived
            tags.add(sub if sub in dirs else "f:" + sub)
    return tags


def git_stream(args):
    p = subprocess.Popen(["git", "-C", UP] + args, stdout=subprocess.PIPE,
                         encoding="utf-8", errors="replace", bufsize=1)
    for line in p.stdout:
        yield line.rstrip("\n")
    p.stdout.close()
    p.wait()


def floor_date():
    return subprocess.check_output(
        ["git", "-C", UP, "log", "-1", "--date=short", "--format=%cd", ANCHOR],
        encoding="utf-8", env={**os.environ, "TZ": "UTC"}).strip()


def main():
    FLOOR = floor_date()
    fmt = MARK + "%H" + SEP + "%cs"

    # --- categories: every commit's touched paths (all files) ---
    cats = OrderedDict()          # sha -> set(tags); OrderedDict keeps commit order
    args = ["log", "--no-renames", "--name-only", "--format=" + fmt]
    if UNTIL:
        args.append("--until=" + UNTIL)
    args.append(ANCHOR + "..HEAD")
    sha, skip = None, True
    for line in git_stream(args):
        if line.startswith(MARK):
            sha, date = line[1:].split(SEP)
            skip = date < FLOOR
            if not skip:
                cats.setdefault(sha, set())   # register (so merges get an entry)
        elif line and not skip and sha:
            cats[sha].update(path_tags(line))

    # --- symbols: fn:A_* from changed lines in the gameplay dirs (targeted -p, windowed) ---
    syms = {}
    args = ["log", "--no-renames", "-p", "--unified=0", "--format=" + fmt]
    if SYM_UNTIL:
        args.append("--until=" + SYM_UNTIL)
    args += [ANCHOR + "..HEAD", "--"] + GAMEPLAY
    sha, skip = None, True
    for line in git_stream(args):
        if line.startswith(MARK):
            sha, date = line[1:].split(SEP)
            skip = date < FLOOR
        elif not skip and sha and line[:1] in "+-" and not line.startswith(("+++", "---")):
            for tok in ACTION.findall(line):
                syms.setdefault(sha, set()).add("fn:" + tok)

    # --- write merged ---
    with open(OUT, "w") as f:
        f.write("# sha -> derived tags (space-separated). rigid: literal path components + "
                "filename subsystem; fn:A_* from diff tokens; merges tagged 'merge'.\n")
        n = 0
        for sha, tags in cats.items():
            allt = set(tags) | syms.get(sha, set())
            if not allt:
                allt = {"merge"}
            f.write(sha + "\t" + " ".join(sorted(allt)) + "\n")
            n += 1
    print("tags.tsv: %d commits  (with symbols: %d)" % (len(cats), len(syms)))


if __name__ == "__main__":
    main()
