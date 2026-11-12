#!/usr/bin/env python3
"""
Generate a ledger's tags.tsv  ->  sha \\t space-separated tags

Tags are DERIVED from history, never hand-written per commit, and RIGID -- they come from
literal path structure, not fuzzy substring matching:

  * category tags -- every literal directory component a commit touches, plus the
                     filename's subsystem (its ZDoom `xxx_` prefix, or the stem).
  * fn:A_* tags   -- action-function identifiers matched on the diff's changed lines
                     (exact token `A_[A-Z][A-Za-z0-9]*`, pattern-discovered).
  * svc:x / clc:x -- server and client commands a changed line names: SVC_/SVC2_/CLC_ enum
                     values, ServerCommands::X, SERVERCOMMANDS_X/CLIENTCOMMANDS_X calls, or a
                     protocolspec `Command X`, folded to lower case without underscores so every
                     spelling of one command is one tag. Only for ledgers with "netcode" set.
  * merge         -- a commit with no files (a merge) is tagged `merge`.

Usage:  gentags.py [--ledger uzdoom|zandronum] [--to REV] [--out FILE]
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ledger as L  # noqa: E402
import tracker  # noqa: E402

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
ACTION = re.compile(r"A_[A-Z][A-Za-z0-9]*")
PREFIX = re.compile(r"^([a-z0-9]+)_")
SVC = re.compile(r"\b(?:SVC2?_|ServerCommands::|SERVERCOMMANDS_)([A-Za-z0-9_]+)|^Command ([A-Za-z0-9]+)")
CLC = re.compile(r"\b(?:CLC_|CLIENTCOMMANDS_)([A-Za-z0-9_]+)")


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


def command_tags(changed):
    """svc: and clc: tags for one changed line (without its +/- marker)."""
    out = set()
    for prefix, rx in (("svc:", SVC), ("clc:", CLC)):
        for m in rx.finditer(changed):
            name = next(g for g in m.groups() if g)
            out.add(prefix + name.replace("_", "").lower())
    return out


def changed_lines(stream, keep):
    for sha, line in stream:
        if sha in keep and line[:1] in "+-" and not line.startswith(("+++", "---")):
            yield sha, line[1:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ledger", default="uzdoom")
    ap.add_argument("--to", help="pin the range's end (a golden run)")
    ap.add_argument("--out", help="write here instead of the ledger's tags.tsv")
    args = ap.parse_args()
    led = L.load_config()[args.ledger]
    repo = tracker.repo_for(led)
    to = args.to or led["to"]
    commits = tracker.ledger_commits(led, repo, args.to, renames=False)
    keep = {c.sha for c in commits}

    # categories: every commit's touched paths, newest first (git log's own order)
    cats = {}
    for c in reversed(commits):
        cats[c.sha] = set()
        # [rc4l] An ignored path (Zandronum's changelog) would tag most of its ledger `docs`; it counts only when it is all there is.
        for p in [f for f in c.files if not L.is_ignored(led, f)] or c.files:
            cats[c.sha].update(path_tags(p))

    syms = {}
    for sha, line in changed_lines(repo.patches(led["from"], to, GAMEPLAY), keep):
        for tok in ACTION.findall(line):
            syms.setdefault(sha, set()).add("fn:" + tok)
    if led.get("netcode"):
        for sha, line in changed_lines(repo.patches(led["from"], to), keep):
            syms.setdefault(sha, set()).update(command_tags(line))

    out = args.out or os.path.join(L.ledger_dir(led), "tags.tsv")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# sha -> derived tags (space-separated). rigid: literal path components + "
                "filename subsystem; fn:A_* from diff tokens; merges tagged 'merge'.\n")
        for sha, tags in cats.items():
            allt = set(tags) | syms.get(sha, set())
            if not allt:
                allt = {"merge"}
            f.write(sha + "\t" + " ".join(sorted(allt)) + "\n")
    print("tags.tsv [%s]: %d commits  (with symbols: %d)" % (args.ledger, len(cats), len([s for s in syms if syms[s]])))


if __name__ == "__main__":
    main()
