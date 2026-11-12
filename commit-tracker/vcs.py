#!/usr/bin/env python3
"""
vcs.py -- one way to read an upstream's history, whether it lives in git or Mercurial.

Every commit-tracker script used to shell out to `git -C $UP ...` with its own format string,
which is why the tracker could only follow a git upstream. Zandronum publishes in Mercurial, so
history now comes through here: a ledger names its VCS in ledgers.json and gets a Repo that lists
commits, files, diffs, file contents and greps the same way for both.

The parsers are pure functions over recorded command output, so they are tested without either
tool installed (tools/commit_tracker_test.py). Only the subprocess calls in GitRepo and HgRepo are
glue.
"""
import datetime
import os
import re
import shutil
import subprocess
from dataclasses import dataclass, field

RS, FS, LS = "\x1e", "\x1f", "\x1d"   # record, field and list separators in the log formats


@dataclass
class Commit:
    sha: str                  # 40-hex: a git sha or a Mercurial node
    date: str                 # UTC, "YYYY-MM-DDTHH:MM:SSZ"
    title: str                # one line, tabs and newlines folded to spaces
    files: list = field(default_factory=list)
    parents: list = field(default_factory=list)
    epoch: int = 0            # author time in seconds; hg keeps it across a transplant
    transplant: str = ""      # hg only: the node `hg transplant` copied this from


def utc_iso(epoch):
    return datetime.datetime.fromtimestamp(int(epoch), datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def clean_title(text):
    return re.sub(r"[\t\r\n]+", " ", text).strip(" ")


# ---------------------------------------------------------------------------------------------
# Parsers (pure)
# ---------------------------------------------------------------------------------------------

GIT_LOG_FORMAT = RS + "%H" + FS + "%P" + FS + "%cd" + FS + "%at" + FS + "%s"


def parse_git_log(text):
    """Parse `git log --name-only --format=GIT_LOG_FORMAT` (dates as UTC ISO) into Commits."""
    out = []
    for rec in text.split(RS)[1:]:
        head, _, rest = rec.partition("\n")
        f = head.split(FS)
        if len(f) < 5:
            continue
        sha = re.sub(r"[^0-9a-f]", "", f[0])
        if len(sha) != 40:
            continue
        files = [p for p in rest.split("\n") if p.strip()]
        out.append(Commit(sha=sha, date=f[2], title=clean_title(FS.join(f[4:])), files=files,
                          parents=f[1].split(), epoch=int(f[3] or 0)))
    return out


HG_LOG_TEMPLATE = (RS + "{node}" + FS + "{p1node} {p2node}" + FS + "{date|hgdate}" + FS
                   + "{desc|firstline}" + FS
                   + '{extras % "{ifeq(key, \\"transplant_source\\", hex(value))}"}' + FS
                   + '{join(files, "' + LS + '")}\n')


def hg_template(t):
    """Mercurial reads \\xNN escapes in a template; pass the separators that way."""
    return t.replace(RS, r"\x1e").replace(FS, r"\x1f").replace(LS, r"\x1d")


def parse_hg_log(text):
    """Parse `hg log -T HG_LOG_TEMPLATE` into Commits."""
    out = []
    null = "0" * 40
    for rec in text.split(RS)[1:]:
        f = rec.rstrip("\n").split(FS)
        if len(f) < 6 or not re.fullmatch(r"[0-9a-f]{40}", f[0]):
            continue
        epoch = int(f[2].split()[0]) if f[2].strip() else 0
        out.append(Commit(sha=f[0], date=utc_iso(epoch), title=clean_title(f[3]),
                          files=[p for p in f[5].split(LS) if p],
                          parents=[p for p in f[1].split() if p != null],
                          epoch=epoch, transplant=f[4].strip()))
    return out


def parse_patch_stream(text):
    """Split `log -p` output whose commits each start with a line "\\x01<sha>" into (sha, line)."""
    sha = None
    for line in text.split("\n"):
        if line.startswith("\x01"):
            sha = line[1:].strip()
        elif sha:
            yield sha, line


def parse_grep(text, rev_prefix):
    """`git grep -n PAT REV` and `hg grep -n -r REV` both print path, rev and line number; yield (path, content)."""
    for line in text.splitlines():
        if rev_prefix == "git":
            parts = line.split(":", 3)          # REV:path:lineno:content
            if len(parts) == 4:
                yield parts[1], parts[3]
        else:
            m = re.match(r"(.+?):(\d+):(\d+):(.*)$", line)   # path:rev:lineno:content
            if m:
                yield m.group(1).replace("\\", "/"), m.group(4)


# ---------------------------------------------------------------------------------------------
# Backends (glue)
# ---------------------------------------------------------------------------------------------

def _run(args, cwd=None, env=None, input_bytes=None):
    p = subprocess.run(args, cwd=cwd, env=env, input=input_bytes, capture_output=True)
    if p.returncode != 0 and input_bytes is None:
        raise RuntimeError("%s failed (%d): %s" % (" ".join(args[:4]), p.returncode,
                                                   p.stderr.decode("utf-8", "replace").strip()[:400]))
    return p.stdout


def _text(b):
    return b.decode("utf-8", "replace").replace("\r\n", "\n")


class GitRepo:
    kind = "git"

    def __init__(self, path):
        self.path = path

    def git(self, *args, **kw):
        env = dict(os.environ, TZ="UTC")
        return _run(["git", "-C", self.path] + list(args), env=env, **kw)

    def resolve(self, rev):
        return _text(self.git("rev-parse", rev + "^{commit}")).strip()

    def commits(self, frm, to, renames=True):
        """Every commit in frm..to, oldest first, in git log's own (reversed) order.

        renames=False lists a rename as its old and its new path, as the tag pass wants.
        """
        out = self.git("log", "--reverse", "--date=format-local:%Y-%m-%dT%H:%M:%SZ",
                       "--format=" + GIT_LOG_FORMAT, "--name-only",
                       *([] if renames else ["--no-renames"]), "%s..%s" % (frm, to))
        return parse_git_log(_text(out))

    def all_by_epoch(self, to="HEAD"):
        """Every commit reachable from `to`, as (sha, author epoch, title), for twin matching."""
        out = _text(self.git("log", "--format=%H" + FS + "%at" + FS + "%s", to))
        rows = []
        for line in out.split("\n"):
            f = line.split(FS)
            if len(f) == 3 and len(f[0]) == 40:
                rows.append((f[0], int(f[1]), clean_title(f[2])))
        return rows

    def patches(self, frm, to, paths=None, unified=0):
        args = ["log", "--no-renames", "-p", "--unified=%d" % unified, "--format=\x01%H", "%s..%s" % (frm, to)]
        if paths:
            args += ["--"] + list(paths)
        return parse_patch_stream(_text(self.git(*args)))

    def diff(self, sha, unified=3):
        return _text(self.git("show", "--no-renames", "--format=", "--unified=%d" % unified, sha))

    def messages(self, frm, to):
        out = _text(self.git("log", "--format=%H" + FS + "%B%x00", "%s..%s" % (frm, to)))
        for rec in out.split("\0"):
            if FS in rec:
                sha, msg = rec.split(FS, 1)
                yield sha.strip(), msg

    def grep(self, pattern, globs, rev="HEAD"):
        p = subprocess.run(["git", "-C", self.path, "grep", "-nE", pattern, rev, "--"] + list(globs),
                           capture_output=True)
        return parse_grep(_text(p.stdout), "git")

    def cat_many(self, keys):
        """{(rev, path): text} for each pair, through one `git cat-file --batch`."""
        keys = list(keys)
        if not keys:
            return {}
        data = _run(["git", "-C", self.path, "cat-file", "--batch"],
                    input_bytes=("\n".join("%s:%s" % k for k in keys) + "\n").encode())
        blobs, pos = {}, 0
        for key in keys:
            nl = data.find(b"\n", pos)
            if nl < 0:
                break
            header = data[pos:nl].decode("utf-8", "replace")
            if header.endswith(("missing", "ambiguous")):
                pos = nl + 1
                continue
            size = int(header.rsplit(" ", 1)[1])
            blobs[key] = data[nl + 1:nl + 1 + size].decode("utf-8", "replace")
            pos = nl + 1 + size + 1
        return blobs

    def fetch(self):
        return _text(self.git("fetch", "--quiet", "origin"))


def find_hg():
    for cand in (os.environ.get("HG"), shutil.which("hg")):
        if cand and (os.path.exists(cand) or shutil.which(cand)):
            return cand
    raise RuntimeError("Mercurial not found: put hg on PATH or set HG to the executable")


def bash_candidates(env, git_exec_path, on_path):
    """Where a POSIX bash may be, best first: $FUA_BASH, Git's own (Git for Windows), then PATH unless it is WSL's."""
    out = [env.get("FUA_BASH")]
    if git_exec_path:
        top = os.path.dirname(os.path.dirname(os.path.dirname(git_exec_path.rstrip("/\\"))))
        out += [os.path.join(top, "bin", "bash.exe"), os.path.join(top, "usr", "bin", "bash.exe")]
    if on_path and "system32" not in on_path.lower().replace("\\", "/"):
        out.append(on_path)
    return [c for c in out if c]


def find_bash():
    """A bash for the tools/*.sh scripts; on Windows a bare `bash` from Python is WSL's, which has no distro here."""
    p = subprocess.run(["git", "--exec-path"], capture_output=True, text=True)
    for cand in bash_candidates(os.environ, p.stdout.strip(), shutil.which("bash")):
        if os.path.exists(cand):
            return cand
    raise RuntimeError("bash not found: install Git for Windows or set FUA_BASH")


class HgRepo:
    kind = "hg"

    def __init__(self, path, hg=None):
        self.path = path
        self.exe = hg or find_hg()

    def hg(self, *args, **kw):
        env = dict(os.environ, HGPLAIN="1", HGENCODING="utf-8")
        return _run([self.exe, "--cwd", self.path] + list(args), env=env, **kw)

    @staticmethod
    def revset(frm, to):
        return "sort(::%s - ::%s, rev)" % (to, frm)

    def resolve(self, rev):
        return _text(self.hg("log", "-r", rev, "-T", "{node}")).strip()

    def commits(self, frm, to, renames=True):
        out = self.hg("log", "-r", self.revset(frm, to), "-T", hg_template(HG_LOG_TEMPLATE))
        return parse_hg_log(_text(out))

    def patches(self, frm, to, paths=None, unified=0):
        args = ["log", "-r", self.revset(frm, to), "-p", "--config", "diff.unified=%d" % unified,
                "--config", "diff.git=1", "-T", r"\x01{node}\n"]
        for p in paths or ():
            args += ["-I", "path:" + p]
        return parse_patch_stream(_text(self.hg(*args)))

    def diff(self, sha, unified=3):
        return _text(self.hg("diff", "--git", "-U", str(unified), "-c", sha))

    def messages(self, frm, to):
        out = _text(self.hg("log", "-r", self.revset(frm, to), "-T", r"{node}\x1f{desc}\x00"))
        for rec in out.split("\0"):
            if FS in rec:
                sha, msg = rec.split(FS, 1)
                yield sha.strip(), msg

    def grep(self, pattern, globs, rev="default"):
        args = ["grep", "--all-files", "-n", "-r", rev, pattern]
        for g in globs:
            args += ["-I", "glob:**" + g.lstrip("*")]
        p = subprocess.run([self.exe, "--cwd", self.path] + args, capture_output=True,
                           env=dict(os.environ, HGPLAIN="1"))
        return parse_grep(_text(p.stdout), "hg")

    def cat_many(self, keys):
        blobs = {}
        for rev, path in keys:
            p = subprocess.run([self.exe, "--cwd", self.path, "cat", "-r", rev, path], capture_output=True)
            if p.returncode == 0:
                blobs[(rev, path)] = _text(p.stdout)
        return blobs

    def fetch(self):
        return _text(self.hg("pull", "--quiet"))


def open_repo(kind, path):
    if kind == "git":
        return GitRepo(path)
    if kind == "hg":
        return HgRepo(path)
    raise ValueError("unknown vcs %r" % kind)
