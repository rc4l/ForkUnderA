#!/usr/bin/env python3
"""
tracker.py -- the commit tracker's command line, for every ledger in ledgers.json.

  regen    [--ledger L] [--to REV]     rebuild coverage/index/files from the clone, keeping curation
  progress [--ledger L]                recompute progress.json
  check    [--ledger L]                what CI enforces (format, provenance, twins, overlap, progress)
  next     [--ledger L]                the first pending row, with what the walk needs to judge it
  show     SHA [--ledger L]            the same for any row
  diff     SHA [--ledger L] [--apply]  its diff minus ignored paths; --apply lands it in our tree
  mark     SHA STATUS [--ours S,..] [--note TEXT]   set a verdict, and the twin's with it
  pull     [--ledger L]                fetch upstream, regen, tags, progress, check

`fuactl tracker ...` runs this. regen.sh and progress.sh are wrappers kept for muscle memory.
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import tempfile
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ledger as L  # noqa: E402
import overlap  # noqa: E402
import vcs  # noqa: E402

ROOT = L.ROOT


def today():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")


def repo_for(led):
    path = L.clone_path(led)
    if not os.path.isdir(path):
        sys.exit("tracker: no %s clone at %s (clone %s there, or set %s)"
                 % (led["label"], path, led["remote"], led.get("clone_env", "")))
    return vcs.open_repo(led["vcs"], path)


def ledger_commits(led, repo, to=None, renames=True):
    commits = repo.commits(led["from"], to or led["to"], renames)
    if led.get("floor") == "from-date":
        # [rc4l] A git range drags in branches that merged long after the anchor; anything dated before it is floored out.
        floor = repo.git("log", "-1", "--date=short", "--format=%cd", led["from"]).decode().strip()
        commits = [c for c in commits if c.date[:10] >= floor]
    return commits


def twin_resolver(led, cfg):
    tw = cfg[led["twin"]]
    twin_repo = vcs.open_repo(tw["vcs"], L.clone_path(tw)) if os.path.isdir(L.clone_path(tw)) else None
    if twin_repo is None:
        print("tracker: no %s clone, so new transplants get no twin this run" % tw["label"])
        return lambda c: ""
    notes = {e["hg"]: e["git"] for e in backport_notes(led) if e["hg"]}
    order = {r["sha"]: i for i, r in enumerate(L.read_rows(tw))}
    return L.build_twin_resolver(twin_repo.all_by_epoch(), notes, order)


# ---------------------------------------------------------------------------------------------

def cmd_progress(cfg, led, quiet=False):
    rows = L.read_rows(led)
    p = L.progress(led, rows, today())
    L.write_text(os.path.join(L.ledger_dir(led), "progress.json"), L.progress_text(p))
    if not quiet:
        print(L.progress_text(p), end="")
    return 0


def cmd_regen(cfg, led, args):
    repo = repo_for(led)
    commits = ledger_commits(led, repo, args.to)
    out = args.out or L.ledger_dir(led)
    old = L.read_rows(led)
    twin_of = None
    if led.get("twin"):
        resolve = twin_resolver(led, cfg) if not args.no_twins else (lambda c: "")
        if args.retwin:
            for r in old:
                r["twin"] = ""
        twin_of = resolve
    rows, orphans = L.merge_curation(commits, old, twin_of)
    if led.get("twin"):
        inherit_twins(cfg, led, rows, {r["sha"] for r in old})
        L.write_text(os.path.join(out, "left_out.tsv"), L.left_out_text(rows, backport_notes(led)))
    L.write_text(os.path.join(out, "coverage.tsv"), L.format_rows(led, rows))
    L.write_text(os.path.join(out, "index.tsv"), L.index_text(commits))
    L.write_text(os.path.join(out, "files.tsv"), L.files_text(commits))
    print("coverage.tsv: %d commit rows" % len(rows))
    if orphans:
        keep_orphans(led, out, orphans)
    if out == L.ledger_dir(led):
        cmd_progress(cfg, led)
    return 0


def inherit_twins(cfg, led, rows, existing):
    """A new row whose twin is wholly decided takes the twin's verdict, so the two agree from birth.

    A partial twin (adapted, partially-deferred) is left for the walk: the part it did not take may
    be exactly what the transplant carries.
    """
    theirs = {r["sha"]: r for r in L.read_rows(cfg[led["twin"]])}
    n = 0
    for r in rows:
        t = theirs.get(r["twin"])
        if r["sha"] in existing or t is None or t["status"] in ("pending",) + L.PARTIAL:
            continue
        r["status"], r["ours"] = t["status"], t["ours"]
        r["note"] = "twin %s@%s: %s" % (led["twin"], t["sha"][:9], t["note"])
        n += 1
    if n:
        print("tracker: %d new rows took their twin's verdict" % n)


def keep_orphans(led, out, orphans):
    path = os.path.join(out, "orphans.tsv")
    cols = L.columns(led)
    have = set()
    if os.path.exists(path):
        with open(path, encoding="utf-8") as fh:
            have = {line.split("\t", 1)[0] for line in fh if not line.startswith("#")}
    with open(path, "a", encoding="utf-8", newline="\n") as fh:
        if not have:
            fh.write("# curated rows whose commit left upstream history; kept, never silently dropped\n")
        for r in orphans:
            if r["sha"] not in have:
                fh.write("\t".join(r.get(c, "") for c in cols) + "\t" + today() + "\n")
    print("tracker: %d curated rows left %s history and moved to %s:" % (len(orphans), led["label"], path))
    for r in orphans:
        print("  %s %s %s" % (r["sha"][:12], r["status"], r["title"][:60]))


# ---------------------------------------------------------------------------------------------
# check
# ---------------------------------------------------------------------------------------------

def reachable_commits():
    out = subprocess.run(["git", "-C", ROOT, "rev-list", "HEAD"], capture_output=True, text=True).stdout
    return set(out.split())


def resolve_ours(tokens):
    """our token -> full sha (or None) via one `git cat-file --batch-check`."""
    tokens = sorted(set(tokens))
    if not tokens:
        return {}
    p = subprocess.run(["git", "-C", ROOT, "cat-file", "--batch-check=%(objectname) %(objecttype)"],
                       input="\n".join(t + "^{commit}" for t in tokens) + "\n", capture_output=True, text=True)
    out = {}
    for tok, line in zip(tokens, p.stdout.splitlines()):
        f = line.split()
        out[tok] = f[0] if len(f) == 2 and f[1] == "commit" else None
    return out


def check_ledger(cfg, led, reach=None):
    problems = []
    rows = L.read_rows(led)
    if not rows:
        return ["%s: coverage.tsv missing or empty" % led["name"]], 0, 0
    twin_rows = L.read_rows(cfg[led["twin"]]) if led.get("twin") else None
    problems += L.validate_rows(led, rows, twin_rows)

    tokens = [t for r in rows if r["status"] in L.NEEDS_OURS for t in L.our_tokens(r["ours"])]
    bad = [t for t in tokens if not L.OUR_SHA.match(t)]
    for t in sorted(set(bad)):
        problems.append("ours token %r is not a sha or %s" % (t, L.BASE))
    full = resolve_ours([t for t in tokens if L.OUR_SHA.match(t)])
    reach = reachable_commits() if reach is None else reach
    for r in rows:
        if r["status"] not in L.NEEDS_OURS:
            continue
        for t in L.our_tokens(r["ours"]):
            if t not in full:
                continue
            if full[t] is None:
                problems.append("%s -> our commit %s does not exist" % (r["sha"], t))
            elif full[t] not in reach:
                # [rc4l] Reachability, not existence: an amend or a squash merge leaves the cited commit in the local store but nowhere in CI's clone.
                problems.append("%s -> our commit %s is not reachable from HEAD (orphaned by an amend or a squash merge?)"
                                % (r["sha"], t))

    # [rc4l] A provenance tag whose prefix names a row must carry that row's node, since a hand-typed tail reads as real.
    tags = subprocess.run(["git", "-C", ROOT, "grep", "-n", "-I", "-E", "-e", L.full_tag_pattern(led["name"]), "--", "src", "tools", "docs"],
                          capture_output=True, text=True).stdout.splitlines()
    problems += L.misnamed_tags(led["name"], rows, tags)

    mm, checked = overlap.mismatches(rows, L.read_files_map(led))
    for sha, status, ours, title in mm:
        problems.append("%s [%s] -> %s touched none of the upstream files (%s); say so with "
                        "'no-file-overlap: <reason>' if it landed elsewhere" % (sha[:12], status, ours, title))

    ppath = os.path.join(L.ledger_dir(led), "progress.json")
    if os.path.exists(ppath):
        with open(ppath, encoding="utf-8") as fh:
            msg = L.check_progress(led, rows, json.load(fh))
        if msg:
            problems.append(msg)
    else:
        problems.append("progress.json missing -- run commit-tracker/progress.sh --ledger %s" % led["name"])
    return problems, len(rows), checked


def cmd_check(cfg, names):
    reach = reachable_commits()
    fail = 0
    for name in names:
        problems, n, checked = check_ledger(cfg, cfg[name], reach)
        if problems:
            print("commit-tracker-check [%s]: %d problems:" % (name, len(problems)))
            for p in problems:
                print("  " + p)
            fail = 1
        else:
            print("commit-tracker-check [%s]: clean (%d rows, %d cross-checked against their commits)."
                  % (name, n, checked))
    return fail


# ---------------------------------------------------------------------------------------------
# The walk: next / show / diff / mark
# ---------------------------------------------------------------------------------------------

def our_basenames():
    out = subprocess.run(["git", "-C", ROOT, "ls-files", "--", "src/zandronum"], capture_output=True, text=True).stdout
    names = {}
    for p in out.split("\n"):
        if p:
            names.setdefault(os.path.basename(p).lower(), []).append(p)
    return names


def in_our_tree(led, path, have):
    """True if the upstream path exists here, at its mapped path or under the same file name."""
    ours = L.our_path(led, path)
    return bool(ours and os.path.exists(os.path.join(ROOT, ours))) or os.path.basename(path).lower() in have


IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]{2,}")


def added_lines(diff_text):
    """The diff's distinctive added lines: stripped, at least 12 characters, naming something."""
    out = []
    for ln in diff_text.split("\n"):
        if ln.startswith("+") and not ln.startswith("+++"):
            s = ln[1:].strip()
            if len(s) >= 12 and IDENT.search(s):
                out.append(s)
    return list(dict.fromkeys(out))


_OUR_LINES = None


def our_lines():
    """Every stripped line of 12+ characters under src/zandronum, for the presence hint."""
    global _OUR_LINES
    if _OUR_LINES is None:
        out = subprocess.run(["git", "-C", ROOT, "grep", "-h", "-I", "-e", ".", "--", "src/zandronum"],
                             capture_output=True).stdout.decode("utf-8", "replace")
        _OUR_LINES = {s for s in (ln.strip() for ln in out.split("\n")) if len(s) >= 12}
    return _OUR_LINES


def presence(diff_text):
    """(present, total) distinctive added lines already in our tree: a hint, never a verdict."""
    added = added_lines(diff_text)
    lines = our_lines() if added else set()
    return sum(1 for s in added if s in lines), len(added)


def tagged_in_tree(led, sha):
    """Lines in our tree that already credit this commit (`<ledger>@<sha prefix>`, or a legacy alias)."""
    pat = L.tag_pattern(led["name"], [sha] + L.aliases_of(led, sha))
    p = subprocess.run(["git", "-C", ROOT, "grep", "-n", "-E", "-e", pat, "--", "src", "tools", "docs"],
                       capture_output=True, text=True)
    return p.stdout.splitlines()


def row_report(cfg, led, row):
    """Everything the walk needs to judge one row, as a dict (printed as text or --json)."""
    files = L.read_files_map(led).get(row["sha"], [])
    have = our_basenames()
    rep = {"ledger": led["name"], "row": {k: v for k, v in row.items() if not k.startswith("_")},
           "url": L.commit_url(led, row["sha"]), "files": []}
    for f in files:
        ours = L.our_path(led, f)
        exact = bool(ours and os.path.exists(os.path.join(ROOT, ours)))
        rep["files"].append({"path": f, "ignored": L.is_ignored(led, f), "ours": ours if exact else "",
                             "same_name": [] if exact else have.get(os.path.basename(f).lower(), [])[:3]})
    if led.get("twin") and row.get("twin", L.NONE) != L.NONE:
        tw = cfg[led["twin"]]
        t = L.find_row(L.read_rows(tw), row["twin"])
        rep["twin"] = {"ledger": tw["name"], "sha": row["twin"], "url": L.commit_url(tw, row["twin"]),
                       "status": t["status"] if t else "(no row)", "note": t["note"] if t else ""}
        left = L.read_left_out(led).get(row["sha"], [])
        if left:
            rep["left_out"] = left
    elif led["name"] in [c.get("twin") for c in cfg.values()]:
        for other in cfg.values():
            if other.get("twin") == led["name"]:
                for r in L.read_rows(other):
                    if r.get("twin") == row["sha"]:
                        rep["twin_of"] = {"ledger": other["name"], "sha": r["sha"], "status": r["status"]}
    rep["tagged"] = tagged_in_tree(led, row["sha"])
    return rep


def backport_notes(led):
    """Zandronum's own list of transplant hunks it could not apply (docs/zandronum-*-backport-nodes.txt)."""
    docs = os.path.join(L.clone_path(led), "docs")
    out = []
    for name in sorted(os.listdir(docs)) if os.path.isdir(docs) else ():
        if re.fullmatch(r"zandronum-\w+-backport-nodes\.txt", name):
            with open(os.path.join(docs, name), encoding="utf-8", errors="replace") as fh:
                out += L.parse_backport_notes(fh.read())
    return out


def print_report(rep, diffstat=None):
    r = rep["row"]
    print("%s  %s  [%s]" % (r["sha"][:12], r["date"][:10], r["status"]))
    print("  " + r["title"])
    print("  " + rep["url"])
    if r.get("note"):
        print("  note: " + r["note"])
    if r.get("ours", L.NONE) != L.NONE:
        print("  ours: " + r["ours"])
    if "twin" in rep:
        t = rep["twin"]
        print("  twin: %s@%s [%s] %s" % (t["ledger"], t["sha"][:12], t["status"], t["note"][:100]))
    if "twin_of" in rep:
        t = rep["twin_of"]
        print("  twin of %s@%s [%s]" % (t["ledger"], t["sha"][:12], t["status"]))
    print("  files:")
    for f in rep["files"]:
        if f["ignored"]:
            mark = "ignored"
        elif f["ours"]:
            mark = "ours   "
        elif f["same_name"]:
            mark = "moved? "
        else:
            mark = "absent "
        extra = (" -> " + ", ".join(f["same_name"])) if f["same_name"] and not f["ours"] else ""
        print("    %s %s%s" % (mark, f["path"], extra))
    for e in rep.get("left_out", []):
        print("  Zandronum left out (%s): %s" % (", ".join(e["files"]), e["reason"][:160]))
    for t in rep["tagged"][:8]:
        print("  tagged: " + t[:140])
    if rep.get("presence") and rep["presence"][1]:
        print("  presence hint: %d/%d distinctive added lines already in our tree" % rep["presence"])
    if diffstat:
        print(diffstat.rstrip())


def strip_ignored(led, diff_text):
    """Drop the file sections of a diff whose path the ledger ignores."""
    out, keep = [], True
    for line in diff_text.split("\n"):
        m = re.match(r"diff --git a/(\S+) b/(\S+)", line)
        if m:
            keep = not L.is_ignored(led, m.group(2))
        if keep:
            out.append(line)
    return "\n".join(out)


def diffstat(diff_text):
    stats, cur = [], None
    for line in diff_text.split("\n"):
        m = re.match(r"diff --git a/(\S+) b/(\S+)", line)
        if m:
            cur = [m.group(2), 0, 0]
            stats.append(cur)
        elif cur and line.startswith("+") and not line.startswith("+++"):
            cur[1] += 1
        elif cur and line.startswith("-") and not line.startswith("---"):
            cur[2] += 1
    return "\n".join("  %5d+ %5d-  %s" % (a, d, p) for p, a, d in stats)


def pick_ledger(cfg, args, sha=None):
    if args.ledger:
        return cfg[args.ledger]
    if sha:
        hits = [led for led in cfg.values() if L.find_row(L.read_rows(led), sha)]
        if len(hits) == 1:
            return hits[0]
        if len(hits) > 1:
            sys.exit("tracker: %s is in %s; pass --ledger" % (sha, ", ".join(h["name"] for h in hits)))
        sys.exit("tracker: no row for %s" % sha)
    return cfg["zandronum"] if "zandronum" in cfg else next(iter(cfg.values()))


def cmd_show(cfg, args, row=None, led=None):
    led = led or pick_ledger(cfg, args, args.sha)
    row = row or L.find_row(L.read_rows(led), args.sha)
    if row is None:
        sys.exit("tracker: no row for %s in %s" % (args.sha, led["name"]))
    rep = row_report(cfg, led, row)
    try:
        d = strip_ignored(led, repo_for(led).diff(row["sha"]))
        rep["diffstat"] = diffstat(d)
        rep["presence"] = presence(d)
    except (RuntimeError, SystemExit) as e:
        rep["diffstat"] = "(no diff: %s)" % e
    if args.json:
        print(json.dumps(rep, indent=1))
    else:
        print_report(rep, rep["diffstat"])
    return 0


def cmd_next(cfg, args):
    led = pick_ledger(cfg, args)
    rows = L.read_rows(led)
    pend = [r for r in rows if r["status"] == "pending"]
    if not pend:
        print("tracker: nothing pending in %s" % led["name"])
        return 0
    print("%s: %d pending of %d; next:" % (led["label"], len(pend), len(rows)))
    return cmd_show(cfg, args, pend[0], led)


def diff_targets(diff_text):
    """The paths a git-style diff writes to, from its `+++ b/` lines."""
    return [ln[6:].strip() for ln in diff_text.splitlines() if ln.startswith("+++ b/")]


def keep_crlf(before, after):
    """`after` with CRLF endings again when `before` had them, since patch writes LF into a CRLF file."""
    if b"\r\n" not in before or b"\n" not in after.replace(b"\r\n", b""):
        return after
    return after.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")


def cmd_diff(cfg, args):
    led = pick_ledger(cfg, args, args.sha)
    row = L.find_row(L.read_rows(led), args.sha)
    d = strip_ignored(led, repo_for(led).diff(row["sha"]))
    if not args.apply:
        sys.stdout.write(d)
        return 0
    # [rc4l] Every ledger's tree map lands under src/zandronum, so the upstream paths apply from there as they are.
    fd, tmp = tempfile.mkstemp(suffix=".diff")
    with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(d)
    base = os.path.join(ROOT, "src", "zandronum")
    before = {}
    for t in diff_targets(d):
        path = os.path.join(base, t)
        if os.path.exists(path):
            with open(path, "rb") as fh:
                before[path] = fh.read()
    try:
        p = subprocess.run([vcs.find_bash(), os.path.join(ROOT, "tools", "apply-upstream-diff.sh"), tmp], cwd=base)
    finally:
        os.remove(tmp)
    for path, old in before.items():
        with open(path, "rb") as fh:
            new = fh.read()
        fixed = keep_crlf(old, new)
        if fixed != new:
            with open(path, "wb") as fh:
                fh.write(fixed)
    return p.returncode


def cmd_mark(cfg, args):
    led = pick_ledger(cfg, args, args.sha)
    rows = L.read_rows(led)
    r = L.mark(rows, args.sha, args.status, ours=args.ours, note=args.note, append_note=args.append)
    changed = [(led, rows)]
    twin_led, twin_sha = None, None
    if led.get("twin") and r.get("twin", L.NONE) != L.NONE:
        twin_led, twin_sha = cfg[led["twin"]], r["twin"]
    else:
        for other in cfg.values():
            if other.get("twin") == led["name"]:
                for o in L.read_rows(other):
                    if o.get("twin") == r["sha"]:
                        twin_led, twin_sha = other, o["sha"]
    if twin_led is not None:
        trows = L.read_rows(twin_led)
        t = L.find_row(trows, twin_sha)
        if t is not None:
            tnote = "twin %s@%s: %s" % (led["name"], r["sha"][:9], args.note) if args.note else None
            L.mark(trows, t["sha"], r["status"], ours=r["ours"], note=tnote, append_note=args.append)
            changed.append((twin_led, trows))
    for lg, rs in changed:
        problems = L.validate_rows(lg, rs)
        if problems:
            sys.exit("tracker: refusing to write %s:\n  %s" % (lg["name"], "\n  ".join(problems)))
    for lg, rs in changed:
        L.write_text(os.path.join(L.ledger_dir(lg), "coverage.tsv"), L.format_rows(lg, rs))
        cmd_progress(cfg, lg, quiet=True)
        print("%s@%s -> %s%s" % (lg["name"], [x for x in rs if x["sha"].startswith(
            args.sha if lg is led else twin_sha)][0]["sha"][:12], r["status"],
            "" if r["ours"] == L.NONE else " (ours %s)" % r["ours"]))
    return 0


def cmd_pull(cfg, args):
    names = [args.ledger] if args.ledger else list(cfg)
    for name in names:
        led = cfg[name]
        repo = repo_for(led)
        print("tracker: fetching %s" % led["label"])
        repo.fetch()
    for name in sorted(names, key=lambda n: bool(cfg[n].get("twin"))):   # twin ledgers after the ledger they twin
        led = cfg[name]
        a = argparse.Namespace(to=None, out=None, retwin=False, no_twins=False)
        cmd_regen(cfg, led, a)
        for script in ("gentags.py", "gazetteer.py"):
            subprocess.run([sys.executable, os.path.join(HERE, script), "--ledger", name], check=True)
    return cmd_check(cfg, names)


def main(argv=None):
    cfg = L.load_config()
    ap = argparse.ArgumentParser(prog="tracker", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def add(name, **kw):
        p = sub.add_parser(name, **kw)
        p.add_argument("--ledger", choices=sorted(cfg))
        return p

    p = add("regen")
    p.add_argument("--to", help="pin the range's end (a golden run reproduces a past regen)")
    p.add_argument("--out", help="write here instead of the ledger's folder")
    p.add_argument("--retwin", action="store_true", help="re-resolve every twin, not just new rows'")
    p.add_argument("--no-twins", action="store_true")
    add("progress")
    add("check")
    p = add("next")
    p.add_argument("--json", action="store_true")
    for name in ("show", "diff"):
        p = add(name)
        p.add_argument("sha")
        if name == "show":
            p.add_argument("--json", action="store_true")
        else:
            p.add_argument("--apply", action="store_true")
    p = add("mark")
    p.add_argument("sha")
    p.add_argument("status", choices=L.STATUSES)
    p.add_argument("--ours")
    p.add_argument("--note")
    p.add_argument("--append", action="store_true", help="append --note to the existing note")
    add("pull")
    args = ap.parse_args(argv)

    if args.cmd == "regen":
        return cmd_regen(cfg, cfg[args.ledger or "uzdoom"], args)
    if args.cmd == "progress":
        for name in ([args.ledger] if args.ledger else list(cfg)):
            cmd_progress(cfg, cfg[name])
        return 0
    if args.cmd == "check":
        return cmd_check(cfg, [args.ledger] if args.ledger else list(cfg))
    return {"next": cmd_next, "show": cmd_show, "diff": cmd_diff, "mark": cmd_mark, "pull": cmd_pull}[args.cmd](cfg, args)


if __name__ == "__main__":
    sys.exit(main())
