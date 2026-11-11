#!/usr/bin/env python3
"""
ledger.py -- the commit tracker's data model, shared by every ledger in ledgers.json.

A ledger is one upstream followed commit by commit: UZDoom at the top of commit-tracker/, Zandronum
under commit-tracker/zandronum/. Each has the same files (coverage.tsv, index.tsv, files.tsv, the tag
files, progress.json) and the same rules, so the rules live here once: reading and writing rows,
progress toward a goal row, twin resolution and agreement, and row validation.

Everything here is pure (no subprocess, no clone), so tools/commit_tracker_test.py covers it.
"""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CONFIG = os.path.join(HERE, "ledgers.json")

STATUSES = ("pending", "ported", "adapted", "skip", "deferred", "partially-deferred")
NEEDS_OURS = ("ported", "adapted", "partially-deferred")   # rows that cite our commit(s)
NO_OURS = ("pending", "skip")                              # rows whose ours must be "/"
BASE = "zandronum-base"
NONE = "/"
SHA40 = re.compile(r"^[0-9a-f]{40}$")
OUR_SHA = re.compile(r"^[0-9a-f]{7,40}$")
BASE_COLUMNS = ["sha", "date", "title", "status", "note", "ours"]


# ---------------------------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------------------------

def load_config(path=CONFIG):
    with open(path, encoding="utf-8") as fh:
        cfg = json.load(fh)
    for name, led in cfg.items():
        led["name"] = name
    return cfg


def columns(led):
    return BASE_COLUMNS + (["twin"] if led.get("twin") else [])


def ledger_dir(led, root=ROOT):
    return os.path.normpath(os.path.join(root, "commit-tracker", led["dir"]))


def clone_path(led, root=ROOT, env=os.environ):
    """The upstream clone: $<clone_env> if set, else the configured path beside our checkout."""
    p = env.get(led.get("clone_env", "")) or led["clone"]
    return os.path.normpath(p if os.path.isabs(p) else os.path.join(root, p))


def commit_url(led, sha):
    return led["commit_url"].replace("{sha}", sha)


def header(led):
    """The coverage.tsv comment line; it documents the columns, so it is generated, not copied."""
    url = led["commit_url"].replace(led["repo_url"], "<repo>").replace("{sha}", "<sha>")
    h = ("# commit tracker | repo %s | commit URL = %s | status = %s | ours = OUR commit(s), "
         '"/" if pending/skip/deferred' % (led["repo_url"], url, "|".join(STATUSES)))
    if led.get("base_rows", True):
        h += ', "zandronum-base" if baseline'
    if led.get("twin"):
        h += ' | twin = the %s commit this was transplanted from, "/" if none' % led["twin"]
    return h


def is_ignored(led, path):
    return any(path == p or path.startswith(p.rstrip("/") + "/") for p in led.get("ignore", ()))


def our_path(led, up_path):
    """Map an upstream path into our tree by the ledger's longest matching prefix."""
    for src, dst in sorted(led.get("tree", ()), key=lambda m: -len(m[0])):
        if up_path.startswith(src):
            return dst + up_path[len(src):]
    return None


# ---------------------------------------------------------------------------------------------
# Rows
# ---------------------------------------------------------------------------------------------

def parse_rows(text, cols):
    """coverage.tsv text -> list of row dicts (comments and the header line dropped)."""
    rows = []
    for line in text.replace("\r\n", "\n").split("\n"):
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        if f[0] == "sha":
            continue
        row = {c: (f[i] if i < len(f) else "") for i, c in enumerate(cols)}
        row["_nf"] = len(f)
        rows.append(row)
    return rows


def format_rows(led, rows):
    cols = columns(led)
    out = [header(led), "\t".join(cols)]
    for r in rows:
        out.append("\t".join(r.get(c, "") for c in cols))
    return "\n".join(out) + "\n"


def read_rows(led, root=ROOT):
    path = os.path.join(ledger_dir(led, root), "coverage.tsv")
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8") as fh:
        return parse_rows(fh.read(), columns(led))


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def merge_curation(commits, old_rows, twin_of=None):
    """New coverage rows for `commits`, keeping each sha's status, note, ours and twin.

    Returns (rows, orphans): orphans are curated rows whose sha is no longer in the range, which
    regen keeps in orphans.tsv rather than dropping. New rows are born pending; `twin_of(commit)`
    gives a new row's twin.
    """
    old = {r["sha"]: r for r in old_rows}
    rows, seen = [], set()
    for c in commits:
        prev = old.get(c.sha, {})
        row = {"sha": c.sha, "date": c.date, "title": c.title,
               "status": prev.get("status") or "pending", "note": prev.get("note", ""),
               "ours": prev.get("ours") or NONE}
        if twin_of is not None:
            row["twin"] = prev.get("twin") or twin_of(c) or NONE
        rows.append(row)
        seen.add(c.sha)
    orphans = [r for r in old_rows if r["sha"] not in seen and r.get("status", "pending") != "pending"]
    return rows, orphans


def index_text(commits):
    """index.tsv: path -> the commits that touched it, in ledger order."""
    files, order = {}, []
    for c in commits:
        for p in c.files:
            if p in files:
                files[p].append(c.sha)
            else:
                files[p] = [c.sha]
                order.append(p)
    out = ["# path -> commits that touched it (space-separated shas, chronological). Paths are "
           "as-of-commit; git log --follow to trace renames."]
    out += ["%s\t%s" % (p, " ".join(files[p])) for p in order]
    return "\n".join(out) + "\n"


def files_text(commits):
    """files.tsv: sha -> the files it touched, sorted by sha (merges, which list none, left out)."""
    out = ["# sha -> files it touched (space-separated)"]
    out += ["%s\t%s" % (c.sha, " ".join(c.files)) for c in sorted(commits, key=lambda c: c.sha) if c.files]
    return "\n".join(out) + "\n"


def read_files_map(led, root=ROOT):
    path = os.path.join(ledger_dir(led, root), "files.tsv")
    out = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                if line.startswith("#"):
                    continue
                parts = line.rstrip("\r\n").split("\t")
                if len(parts) == 2:
                    out[parts[0]] = parts[1].split()
    return out


# ---------------------------------------------------------------------------------------------
# Progress: rows from the top of the file to the goal row
# ---------------------------------------------------------------------------------------------

def goal_index(rows, goal_sha):
    if goal_sha in ("", "tip"):
        return len(rows) - 1
    for i, r in enumerate(rows):
        if r["sha"] == goal_sha:
            return i
    raise ValueError("goal row %s is not in the ledger" % goal_sha)


def progress(led, rows, generated):
    goal = led["goal"]
    end = goal_index(rows, goal.get("sha", "tip"))
    if end < 0:
        raise ValueError("ledger %s has no rows" % led["name"])
    scope = rows[:end + 1]
    by = {s: 0 for s in STATUSES}
    for r in scope:
        by[r["status"]] = by.get(r["status"], 0) + 1
    done = len(scope) - by["pending"]
    out = {"goal": goal["label"]}
    if goal.get("date"):
        out["goal_date"] = goal["date"]
    out.update({"goal_sha": scope[-1]["sha"], "done": done, "total": len(scope),
                "pct": round(100.0 * done / len(scope), 1)})
    for s in STATUSES[1:]:
        out[s] = by[s]
    out["generated"] = generated
    return out


def progress_text(p):
    # Same layout progress.sh's awk wrote, so the published file keeps its shape.
    lines = []
    for k, v in p.items():
        if isinstance(v, str):
            lines.append('  "%s": "%s"' % (k, v))
        elif isinstance(v, float):
            lines.append('  "%s": %.1f' % (k, v))
        else:
            lines.append('  "%s": %d' % (k, v))
    return "{\n" + ",\n".join(lines) + "\n}\n"


# ---------------------------------------------------------------------------------------------
# Twins: a Zandronum commit transplanted from UZDoom's history
# ---------------------------------------------------------------------------------------------

def norm_title(t):
    return re.sub(r"\W+", " ", t.lower()).strip()


def build_twin_resolver(twin_commits, notes_map=None, order=None):
    """Return twin_of(commit) for commits carrying a transplant source.

    `twin_commits` is every twin-upstream commit as (sha, author epoch, title). The match is
    exact: Zandronum's backport notes name the git sha for a Mercurial source when they list it;
    otherwise `hg transplant` keeps the author time, so the same epoch identifies the commit, with
    the title breaking a tie. A commit and its cherry-pick share both, so the one earlier in the
    twin ledger (`order`: sha -> row index) wins. Anything else ambiguous resolves to nothing.
    """
    by_epoch = {}
    for sha, epoch, title in twin_commits:
        by_epoch.setdefault(epoch, []).append((sha, title))
    notes_map = notes_map or {}
    order = order or {}

    def twin_of(c):
        if not c.transplant:
            return ""
        if c.transplant in notes_map:
            return notes_map[c.transplant]
        cands = by_epoch.get(c.epoch, [])
        if len(cands) > 1:
            cands = [x for x in cands if norm_title(x[1]) == norm_title(c.title)]
        if len(cands) > 1 and all(x[0] in order for x in cands):
            cands = [min(cands, key=lambda x: order[x[0]])]
        return cands[0][0] if len(cands) == 1 else ""
    return twin_of


def parse_backport_notes(text):
    """Zandronum's docs/zandronum-uzdoom-backport-nodes.txt -> list of entries.

    Each entry: {"git": UZDoom sha, "hg": gzdoom-hg-mirror node, "title", "reason", "files"}, where
    files are the paths of the hunks Zandronum could not apply.
    """
    entries = []
    blocks = re.split(r"^=+\s*$", text.replace("\r\n", "\n"), flags=re.M)
    cur = None
    for b in blocks:
        m = re.search(r"^Git hash:\s*([0-9a-f]{40})\s*$", b, re.M)
        if m:
            hg = re.search(r"^Mercurial hash:\s*([0-9a-f]{40})\s*$", b, re.M)
            after = b[(hg or m).end():].strip("\n")
            title = after.split("\n", 1)[0].strip()
            reason = re.search(r"Reason for documentation:\s*(.+?)(?:\n\n|\Z)", b, re.S)
            cur = {"git": m.group(1), "hg": hg.group(1) if hg else "", "title": title,
                   "reason": " ".join(reason.group(1).split()) if reason else "", "files": []}
            entries.append(cur)
        elif cur is not None:
            for f in re.findall(r"^\+\+\+ (?:b/)?(\S+)", b, re.M):
                if f not in cur["files"]:
                    cur["files"].append(f)
    return entries


def left_out_text(rows, entries):
    """left_out.tsv: per twin row, the hunks Zandronum's backport notes say it could not apply."""
    by_git = {}
    for e in entries:
        by_git.setdefault(e["git"], []).append(e)
    out = ["# sha -> the twin's hunks this ledger's upstream could not apply (from its backport notes)",
           "sha\ttwin\tfiles\treason"]
    for r in rows:
        for e in by_git.get(r.get("twin", NONE), []):
            out.append("\t".join((r["sha"], r["twin"], " ".join(e["files"]), e["reason"])))
    return "\n".join(out) + "\n"


def tag_pattern(name, shas):
    """An extended regex for `<name>@<sha prefix>` tags that a longer name ending in it (qzandronum@) does not match."""
    alts = "|".join(sorted({s[:7] for s in shas}))
    return "(^|[^A-Za-z0-9_-])%s@(%s)" % (re.escape(name), alts)


def aliases_of(led, sha, root=ROOT):
    """Legacy git-mirror shas recorded for a node in the ledger's aliases.tsv (D1)."""
    path = os.path.join(ledger_dir(led, root), "aliases.tsv")
    out = []
    if os.path.exists(path):
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                f = line.rstrip("\r\n").split("\t")
                if len(f) >= 2 and f[1] == sha:
                    out.append(f[0])
    return out


def read_left_out(led, root=ROOT):
    path = os.path.join(ledger_dir(led, root), "left_out.tsv")
    out = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                f = line.rstrip("\r\n").split("\t")
                if len(f) == 4 and SHA40.match(f[0]):
                    out.setdefault(f[0], []).append({"twin": f[1], "files": f[2].split(), "reason": f[3]})
    return out


# ---------------------------------------------------------------------------------------------
# Validation: what commit-tracker-check enforces per ledger
# ---------------------------------------------------------------------------------------------

def validate_rows(led, rows, twin_rows=None):
    """Format, status and twin rules. Returns a list of problems (strings)."""
    problems = []
    ncols = len(columns(led))
    seen = set()
    for r in rows:
        sha = r["sha"]
        if r["_nf"] != ncols:
            problems.append("NF=%d (want %d): %s" % (r["_nf"], ncols, sha[:60]))
            continue
        if not SHA40.match(sha):
            problems.append("bad sha: " + sha)
            continue
        if sha in seen:
            problems.append("duplicate row: " + sha)
        seen.add(sha)
        st, ours = r["status"], r["ours"]
        if st not in STATUSES:
            problems.append("bad status: %s (%s)" % (st, sha))
            continue
        if st in NO_OURS and ours != NONE:
            problems.append("%s row must have ours=/ : %s (ours=%s)" % (st, sha, ours))
        if st in NEEDS_OURS and ours in ("", NONE):
            problems.append("%s is %s but ours column is empty" % (sha, st))
        if ours == BASE and not led.get("base_rows", True):
            problems.append("%s cites %s, but every %s row is after our base" % (sha, BASE, led["label"]))
        if led.get("twin"):
            tw = r.get("twin", "")
            if tw != NONE and not SHA40.match(tw):
                problems.append("bad twin %r: %s" % (tw, sha))
    if led.get("twin") and twin_rows is not None:
        problems += twin_disagreements(rows, twin_rows)
    return problems


PARTIAL = ("adapted", "partially-deferred")   # verdicts that can leave part of a change untaken


def twins_agree(a, b):
    """Same status and ours, or one side pending while the other says part of the change was not taken."""
    if (a["status"], a["ours"]) == (b["status"], b["ours"]):
        return True
    pair = {a["status"], b["status"]}
    return "pending" in pair and bool(pair & set(PARTIAL))


def twin_disagreements(rows, twin_rows):
    """A twin and its row in the twin ledger must agree (twins_agree)."""
    theirs = {r["sha"]: r for r in twin_rows}
    out = []
    for r in rows:
        t = theirs.get(r.get("twin", NONE))
        if t is None:
            continue
        if not twins_agree(r, t):
            out.append("twin disagrees: %s is %s (ours %s) but its twin %s is %s (ours %s)"
                       % (r["sha"][:12], r["status"], r["ours"], t["sha"][:12], t["status"], t["ours"]))
    return out


def our_tokens(ours):
    """The commits a row cites, or [] for "/" and zandronum-base."""
    if ours in ("", NONE, BASE):
        return []
    return [t.strip() for t in ours.split(",")]


def check_progress(led, rows, have):
    """None if progress.json matches the rows, else a message."""
    try:
        want = progress(led, rows, have.get("generated", ""))
    except ValueError as e:
        return str(e)
    if (want["done"], want["total"], want["goal_sha"]) != (have.get("done"), have.get("total"), have.get("goal_sha")):
        return ("progress.json is stale (says %s/%s to %s, coverage.tsv says %d/%d to %s) -- run "
                "commit-tracker/progress.sh --ledger %s"
                % (have.get("done"), have.get("total"), str(have.get("goal_sha", ""))[:12], want["done"],
                   want["total"], want["goal_sha"][:12], led["name"]))
    return None


# ---------------------------------------------------------------------------------------------
# Marking a row (and its twin) in one step
# ---------------------------------------------------------------------------------------------

def mark(rows, sha_prefix, status, ours=None, note=None, append_note=False):
    """Set a row's verdict. Returns the row. Raises on an unknown or ambiguous sha."""
    if status not in STATUSES:
        raise ValueError("unknown status %r (one of %s)" % (status, ", ".join(STATUSES)))
    hits = [r for r in rows if r["sha"].startswith(sha_prefix)]
    if len(hits) != 1:
        raise ValueError("%s matches %d rows" % (sha_prefix, len(hits)))
    r = hits[0]
    r["status"] = status
    if ours is not None:
        r["ours"] = ours
    elif status in NO_OURS or status == "deferred":
        r["ours"] = NONE
    if note is not None:
        r["note"] = (r["note"] + "; " + note) if append_note and r["note"] else note
    if status in NEEDS_OURS and r["ours"] in ("", NONE):
        raise ValueError("%s needs --ours (the commit(s) that carry it)" % status)
    if status in NO_OURS and r["ours"] != NONE:
        raise ValueError("%s rows cite no commit of ours" % status)
    return r


def find_row(rows, sha_prefix):
    hits = [r for r in rows if r["sha"].startswith(sha_prefix)]
    return hits[0] if len(hits) == 1 else None
