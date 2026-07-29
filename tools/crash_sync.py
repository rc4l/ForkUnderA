#!/usr/bin/env python3
# [rc4l] Mirror GlitchTip crash groups into GitHub issues -- symbolicating natively IN-LINE so each
# issue is created already titled at the crash site with the full, readable stack in its body.
#
# GlitchTip can't symbolicate native C/C++ crashes; it only has raw addresses, so it labels a group
# "<unknown>". We do the symbolication here: for each crash we read the raw stack + the image-base
# tag the engine stamped, download that exact build's symbols from its GitHub release asset, run
# llvm-symbolizer, and file/refresh the issue in ONE step. That means:
#   - the issue is born with the right title (crash site) -- no "<unknown>" then rename,
#   - the symbolicated stack is in the body immediately (crash site first) -- no waiting on a comment,
#   - no GitHub-search-index lag, and no separate droplet cron to drift or fail.
#
# If symbols can't be resolved (no asset, missing image-base, symbolizer error), we STILL file the
# issue -- with the raw stack and an explicit note saying *why* symbolication was skipped -- so a
# crash is never dropped. Set DRY_RUN=1 to print what would be filed without touching GitHub.
#
# Env: GLITCHTIP_URL GLITCHTIP_TOKEN GLITCHTIP_ORG  GITHUB_REPOSITORY (or GITHUB_REPO) GITHUB_TOKEN
#      [LLVM_SYMBOLIZER] [DRY_RUN]
import datetime, io, json, os, re, subprocess, sys, glob, shutil, tempfile, zipfile
import urllib.request, urllib.error, urllib.parse

GT_URL  = os.environ["GLITCHTIP_URL"].rstrip("/")
GT_TOK  = os.environ["GLITCHTIP_TOKEN"]
GT_ORG  = os.environ.get("GLITCHTIP_ORG", "zandrox")
GH_REPO = os.environ.get("GITHUB_REPO") or os.environ["GITHUB_REPOSITORY"]
GH_TOK  = os.environ["GITHUB_TOKEN"]
DRY_RUN = os.environ.get("DRY_RUN", "") not in ("", "0", "false")
MARKER_TMPL = "GlitchTip-ID: {}"          # dedup marker embedded (hidden) in each issue body

# platform -> (preferred load base, symbol-file path inside the release symbols zip)
PLAT = {
    "macos":   (0x100000000, "zandronum.dSYM/Contents/Resources/DWARF/zandronum"),
    "linux":   (0,           "zandronum.debug"),
    "windows": (0x140000000, None),        # first *.pdb found in the zip
}

# ---- http helpers ----------------------------------------------------------
def http(url, headers, method="GET", data=None):
    req = urllib.request.Request(url, method=method, data=data, headers=dict(headers))
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            return r.read()
    except urllib.error.HTTPError as e:
        print(f"HTTP {e.code} {method} {url}: {e.read()[:200]!r}", file=sys.stderr)
        raise

def gt(path):  # GlitchTip is behind Cloudflare, which 403s urllib's default UA.
    return json.loads(http(f"{GT_URL}{path}",
        {"Authorization": f"Bearer {GT_TOK}", "User-Agent": "zx-crash-sync"}))

def gh(path, method="GET", data=None):
    body = json.dumps(data).encode() if data is not None else None
    return http(f"https://api.github.com{path}",
        {"Authorization": f"Bearer {GH_TOK}", "Accept": "application/vnd.github+json",
         "User-Agent": "zx-crash-sync"}, method, body)

# ---- privacy ---------------------------------------------------------------
# Home-directory paths embed the player's OS username (C:\Users\aurat\, /home/bob/, /Users/bob/).
_HOME = re.compile(r'([A-Za-z]:\\Users\\)[^\\]+|(/(?:home|Users)/)[^/\s]+')
def scrub(s):
    return _HOME.sub(lambda m: (m.group(1) or m.group(2)) + "<user>", s or "")

# ---- event helpers ---------------------------------------------------------
def tagval(ev, key):
    v = ev.get(key)                          # release / dist can be top-level on the event
    if v:
        return v
    for t in ev.get("tags", []):
        if t.get("key") == key:
            return t.get("value")
    return None

def norm_platform(ev):
    name = ""
    for c in (ev.get("contexts") or {}).values():
        if isinstance(c, dict) and c.get("type") == "os":
            name = (c.get("name") or "").lower()
    if not name:
        name = (tagval(ev, "os.name") or tagval(ev, "os") or "").lower()
    if "mac" in name or "darwin" in name: return "macos"
    if "windows" in name:                 return "windows"
    return "linux"

def exception_frames(ev):
    # GlitchTip nests the stacktrace under entries[type=="exception"].
    for e in ev.get("entries", []):
        if e.get("type") == "exception":
            try:
                return e["data"]["values"][0]["stacktrace"]["frames"] or []
            except (KeyError, IndexError, TypeError):
                return []
    return []

def build_sha(ev):
    # Prefer dist (GetGitHash -> pure 40-hex sha); fall back to a sha embedded in release.
    for cand in (tagval(ev, "dist"), tagval(ev, "release")):
        if cand:
            m = re.search(r'([0-9a-fA-F]{7,40})', cand)
            if m:
                return m.group(1).lower()[:12]
    return None

# ---- symbols ---------------------------------------------------------------
def find_symbolizer():
    env = os.environ.get("LLVM_SYMBOLIZER")
    cands = ([env] if env else []) + ["llvm-symbolizer"]
    cands += [f"llvm-symbolizer-{v}" for v in range(20, 12, -1)]
    for c in cands:
        p = shutil.which(c) if not os.path.isabs(c) else (c if os.path.exists(c) else None)
        if p:
            return p
    for pat in ("/usr/lib/llvm-*/bin/llvm-symbolizer", "/opt/homebrew/opt/llvm/bin/llvm-symbolizer",
                "/usr/local/opt/llvm/bin/llvm-symbolizer"):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[-1]
    return None

def find_asset_url(platform, sha12):
    releases = json.loads(gh(f"/repos/{GH_REPO}/releases?per_page=100"))
    want = re.compile(rf"ZandroX-symbols-{platform}-{re.escape(sha12)}", re.I)
    for rel in releases:
        for a in rel.get("assets", []):
            if want.search(a["name"]):
                return a["browser_download_url"]     # public; download without the token
    return None

_symcache = {}
def ensure_symbols(platform, sha12):
    key = f"{platform}-{sha12}"
    if key in _symcache:
        return _symcache[key]
    url = find_asset_url(platform, sha12)
    if not url:
        _symcache[key] = None
        return None
    dest = tempfile.mkdtemp(prefix=f"zxsym-{key}-")
    raw = http(url, {"User-Agent": "zx-crash-sync"})     # browser_download_url follows to storage cleanly
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        z.extractall(dest)
    _symcache[key] = dest
    return dest

def symfile_path(symdir, platform):
    sub = PLAT[platform][1]
    if sub:
        p = os.path.join(symdir, sub)
        return p if os.path.exists(p) else None
    for root, _, files in os.walk(symdir):               # windows: first pdb
        for f in files:
            if f.lower().endswith(".pdb"):
                return os.path.join(root, f)
    return None

# Returns (frames, reason). frames is a list of dicts {func, loc} innermost-FIRST (crash site at 0),
# or None with a human reason string explaining why symbolication was skipped.
def symbolicate(ev, platform):
    sha12 = build_sha(ev)
    if not sha12:
        return None, "no build sha on the event (release/dist tag missing)"
    base_tag = tagval(ev, "zx_image_base")
    if not base_tag:
        return None, "no zx_image_base tag on the event (can't rebase addresses)"
    raw = exception_frames(ev)
    if not raw:
        return None, "no stack frames in the event"
    symbolizer = find_symbolizer()
    if not symbolizer:
        return None, "llvm-symbolizer not found on the runner"
    symdir = ensure_symbols(platform, sha12)
    if not symdir:
        return None, f"no symbols asset published for {platform}-{sha12}"
    obj = symfile_path(symdir, platform)
    if not obj:
        return None, f"symbol file missing inside the {platform}-{sha12} archive"

    pref, _ = PLAT[platform]
    base = int(base_tag, 16)
    # Keep only main-module frames; carry each frame's rebased static address.
    kept = []
    for f in raw:                                        # GlitchTip order: outermost (main) first
        ia = f.get("instruction_addr")
        if not ia:
            continue
        off = int(ia, 16) - base
        if 0 <= off < (256 << 20):
            kept.append(pref + off)
    if not kept:
        return None, "no frames fell inside the main module after rebasing"

    inp = "\n".join(hex(a) for a in kept).encode()
    try:
        out = subprocess.run([symbolizer, f"--obj={obj}"], input=inp,
                             capture_output=True, timeout=180).stdout.decode(errors="replace")
    except (subprocess.SubprocessError, OSError) as e:
        return None, f"llvm-symbolizer failed: {e}"
    frames = []
    for block in (b for b in out.split("\n\n") if b.strip()):
        rows = [r for r in block.splitlines() if r.strip()]
        func = rows[0].strip() if rows else "?"
        loc = rows[1].strip() if len(rows) > 1 else ""
        frames.append({"func": func, "loc": "" if loc in ("??:0:0", "") else loc})
    frames.reverse()                                     # -> innermost (crash site) FIRST
    return frames, None

# ---- raw fallback ----------------------------------------------------------
def raw_stack(ev):
    frames = []
    for f in exception_frames(ev):
        fn = (f.get("function") or "").strip()
        ia = f.get("instruction_addr") or ""
        frames.append({"func": fn or ia or "?", "loc": ia if fn else ""})
    frames.reverse()                                     # innermost first, matching the symbolicated view
    return frames

# ---- issue title/body ------------------------------------------------------
def _is_real_name(fn):
    # A resolved function name, not a bare address ("0x1e0b234dc") or the "?" placeholder.
    return bool(fn) and fn != "?" and not fn.startswith("0x")

def crash_site(frames):
    # First frame (innermost) that resolves to a project source line; else the innermost frame.
    for fr in frames:
        if fr.get("loc") and "src/zandronum" in fr["loc"]:
            short = fr["loc"].split("/")[-1]
            return f"{fr['func']} ({short})"
    for fr in frames:
        # A real source location (has a path separator) -- not a bare address the raw fallback parks
        # in loc.
        if fr.get("loc") and "/" in fr["loc"]:
            return f"{fr['func']} ({fr['loc'].split('/')[-1]})"
    # The client names frames via dladdr but without a file:line (system libs, or an un-symbolicated
    # dev build). Take the innermost frame that has a REAL function name -- so a crash in Apple's GL
    # driver reads "storeVecColor_RGB_UB", never "<unknown>".
    for fr in frames:
        if _is_real_name(fr.get("func") or ""):
            return fr["func"]
    # Nothing resolved to a real name (e.g. a pre-fix event that only has raw addresses): never title
    # with a bare address or "<unknown>".
    return "native crash (no symbols)"

def fmt_stack(frames):
    return "\n".join(f"  {i}: {f['func']}" + (f"  ({f['loc']})" if f.get("loc") else "")
                     for i, f in enumerate(frames))

def build_issue(iss, ev, frames, symbolicated, skip_reason):
    gid = iss["id"]
    release = tagval(ev, "release") or ""
    culprit = scrub(iss.get("culprit") or "")
    events  = iss.get("count", "?")
    users   = iss.get("userCount", 0)
    last    = iss.get("lastSeen", "")
    perma   = iss.get("permalink", "")
    marker  = MARKER_TMPL.format(gid)

    title_site = scrub(crash_site(frames)) if frames else scrub(iss.get("title") or "")
    # Never file a "<unknown>" title: fall back to GlitchTip's own group title (now named, since the
    # client resolves frames), then to a stable no-symbols label.
    if not title_site or title_site == "<unknown>":
        gt_title = scrub(iss.get("title") or "")
        title_site = gt_title if gt_title and gt_title != "<unknown>" else "native crash (no symbols)"
    title = f"[crash] {title_site}"

    head = "**Symbolicated stack**" if symbolicated else "**Raw stack** (not symbolicated)"
    note = "" if symbolicated else f"\n> ⚠️ symbolication skipped: {sktrim(skip_reason)}\n"
    stack = fmt_stack(frames) if frames else "(no stack frames in the event)"
    body = (
        f"**Crash captured by GlitchTip**\n\n"
        f"- **Where:** {culprit or title_site}\n"
        f"- **Release:** `{scrub(release)}`\n"
        f"- **Events:** {events}\n- **Users affected:** {users}\n"
        f"- **Last seen:** {last}\n- **Details:** {perma}\n"
        f"{note}\n{head} (release `{scrub(release)}`):\n\n```\n{stack}\n```\n\n"
        f"<!-- do not edit: {marker} -->"
    )
    return title, body

def sktrim(s):
    return (s or "").strip()

# ---- main ------------------------------------------------------------------
def existing_issue(gid):
    marker = MARKER_TMPL.format(gid)
    q = urllib.parse.quote(f'repo:{GH_REPO} is:issue "{marker}" in:body')
    try:
        items = json.loads(gh(f"/search/issues?q={q}")).get("items", [])
    except urllib.error.HTTPError:
        items = []
    return items[0]["number"] if items else None

def ensure_label():
    try:
        gh(f"/repos/{GH_REPO}/labels/crash")
    except urllib.error.HTTPError:
        try:
            gh(f"/repos/{GH_REPO}/labels", "POST",
               {"name": "crash", "color": "B60205", "description": "Auto-filed from GlitchTip"})
        except urllib.error.HTTPError:
            pass

# ---- regression detection --------------------------------------------------
# [rc4l] Whether a closed issue should reopen is decided by comparing timestamps, NOT by the
# group merely being unresolved in GlitchTip. This is the standard "regression" rule (the same
# one Sentry's own issue-tracker integrations use): an issue regresses when an event arrives
# AFTER it was closed.
#
# The previous rule reopened whenever GlitchTip still listed the group as unresolved, reasoning
# that the is:unresolved filter meant "active". It does not: `unresolved` is a triage flag a
# human sets in GlitchTip's UI, and closing the GitHub issue does not clear it, so the two
# systems drift apart immediately. Every closed issue whose group was never resolved upstream
# got reopened on the next run -- every 6h on cron plus on every webhook. Issues #79 and #85
# were each reopened with an event count and lastSeen IDENTICAL to the previous reopen, i.e.
# with no new crash at all, and #85's newest event predated its close by two hours.
def parse_ts(s):
    """Parse an ISO-8601 UTC timestamp from either API ('...Z' or '+00:00', optional
    fractional seconds) into an aware datetime. Returns None if absent or unparseable."""
    if not s:
        return None
    try:
        dt = datetime.datetime.fromisoformat(str(s).strip().replace("Z", "+00:00"))
    except ValueError:
        return None
    # A naive timestamp from either API is documented as UTC; make that explicit so the
    # comparison below can never raise on mixed aware/naive operands.
    return dt if dt.tzinfo else dt.replace(tzinfo=datetime.timezone.utc)

def should_reopen(last_seen, closed_at):
    """Decide whether a closed issue has genuinely regressed.

    Returns (bool, reason). Reopen only when the crash was last seen strictly after the issue
    was closed. When a timestamp is missing or unparseable we cannot prove the crash is stale,
    so we reopen -- keeping the original guarantee that a real recurrence is never silently
    dropped -- and say so, rather than failing closed and hiding a live crash."""
    seen, closed = parse_ts(last_seen), parse_ts(closed_at)
    if seen is None or closed is None:
        return True, "could not compare timestamps (missing or unparseable); reopening to be safe"
    if seen > closed:
        return True, f"last seen {seen.isoformat()} is after close {closed.isoformat()}"
    return False, f"last seen {seen.isoformat()} predates close {closed.isoformat()}"

def main():
    if not DRY_RUN:
        ensure_label()
    # Only active (unresolved) crashes: a resolved group is one someone has already triaged, so it
    # must not trigger a spurious reopen of its closed GitHub issue.
    issues = gt(f"/api/0/organizations/{GT_ORG}/issues/?query=is:unresolved&limit=50")
    print(f"GlitchTip returned {len(issues)} group(s)")
    filed = 0
    for iss in issues:
        gid = iss["id"]
        num = None if DRY_RUN else existing_issue(gid)
        if num:
            # Already mirrored. Reopen only on a genuine regression -- a crash seen AFTER the issue
            # was closed. See should_reopen(): "still unresolved in GlitchTip" is not evidence of a
            # recurrence, and treating it as such reopened closed issues indefinitely.
            try:
                cur = json.loads(gh(f"/repos/{GH_REPO}/issues/{num}"))
                if cur.get("state") == "closed":
                    reopen, why = should_reopen(iss.get("lastSeen"), cur.get("closed_at"))
                    if not reopen:
                        print(f"  group {gid}: #{num} closed, no new events ({why}); leaving closed")
                        continue
                    gh(f"/repos/{GH_REPO}/issues/{num}", "PATCH", {"state": "open"})
                    gh(f"/repos/{GH_REPO}/issues/{num}/comments", "POST",
                       {"body": f"↩️ Recurred — this crash was seen again after this issue was closed "
                                f"({iss.get('count','?')} events, last seen {iss.get('lastSeen','')}). Reopened."})
                    print(f"  group {gid}: reopened #{num} ({why})")
                    filed += 1
                else:
                    print(f"  group {gid}: already mirrored (#{num})")
            except urllib.error.HTTPError:
                print(f"  group {gid}: already mirrored (#{num})")
            continue
        try:
            ev = gt(f"/api/0/issues/{gid}/events/latest/")
        except urllib.error.HTTPError:
            print(f"  group {gid}: could not fetch latest event; skipping")
            continue
        platform = norm_platform(ev)
        frames, reason = symbolicate(ev, platform)
        symbolicated = frames is not None
        if not symbolicated:
            frames = raw_stack(ev)
            print(f"  group {gid}: raw fallback ({reason})")
        title, body = build_issue(iss, ev, frames, symbolicated, reason)
        if DRY_RUN:
            print(f"\n--- would file for group {gid} ({platform}) ---\nTITLE: {title}\n{body}\n")
        else:
            resp = json.loads(gh(f"/repos/{GH_REPO}/issues", "POST",
                       {"title": title, "labels": ["crash"], "body": body}))
            print(f"  group {gid}: filed #{resp['number']} ({'symbolicated' if symbolicated else 'raw'})")
        filed += 1
    print(f"done, {filed} issue(s) {'previewed' if DRY_RUN else 'filed'}")

if __name__ == "__main__":
    main()
