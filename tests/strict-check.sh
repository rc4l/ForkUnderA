#!/usr/bin/env bash
# [rc4l] Strong-fixed gate: syntax-check every engine translation unit with -DZX_STRONG_FIXED so
# fixed_t becomes zx::Fixed (see src/zandronum/src/basictypes.h). The shipping build stays on the
# plain 64-bit typedef and is byte-identical; this gate is what keeps that build honest -- any new
# code that silently converts an angle_t/unsigned/double into a fixed_t (the whole class of bugs
# the widening introduced) becomes a hard compile error here instead of a runtime regression.
#
# [rc4l] Usage: `tests/strict-check.sh`. Point it at a configured engine build tree via
# STRICT_BUILD_DIR (default build-mac-arm) -- it needs that tree's compile_commands.json for each
# TU's real flags. Exits non-zero (and lists every site) if any TU fails the strong-fixed build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${STRICT_BUILD_DIR:-$ROOT/build-mac-arm}"
DB="$BUILD/compile_commands.json"

if [[ ! -f "$DB" ]]; then
  echo "ERROR: no compile_commands.json at $DB" >&2
  echo "       Configure an engine build first (e.g. build.sh, or cmake -S src/zandronum -B $BUILD)," >&2
  echo "       or set STRICT_BUILD_DIR to a configured tree." >&2
  exit 2
fi

python3 - "$DB" <<'PY'
import json, re, subprocess, sys, os
from concurrent.futures import ThreadPoolExecutor

db = json.load(open(sys.argv[1]))
# [rc4l] Only our own engine sources -- skip the vendored libraries, which never see fixed_t.
VEND = ('/zlib/', '/game-music-emu/', '/gdtoa/', '/bzip2/', '/lzma/', '/jpeg-6b/',
        '/sfmt/', '/oplsynth/', '/timidity/', '/dumb/', '/gme/')
files = sorted({e['file'] for e in db
                if '/zandronum/src/' in e['file']
                and not any(v in e['file'] for v in VEND)
                and e['file'].endswith('.cpp')})
byfile = {e['file']: e for e in db}

def check(f):
    e = byfile[f]
    # Turn the real per-TU compile line into a strong-fixed syntax-only check.
    cmd = re.sub(r'-o\s+\S+', '', e['command']).replace(
        ' -c ', ' -fsyntax-only -ferror-limit=0 -DZX_STRONG_FIXED ')
    r = subprocess.run(cmd, cwd=e['directory'], shell=True,
                       capture_output=True, text=True)
    return re.findall(r'/zandronum/src/([^:]+:\d+):\d+: error: (.+)', r.stderr)

seen, errors = set(), []
with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
    for f, errs in zip(files, pool.map(check, files)):
        for loc, msg in errs:
            if loc in seen:
                continue
            seen.add(loc)
            errors.append((loc, msg))

if errors:
    print(f"STRICT GATE FAIL: {len(errors)} strong-fixed error(s) across {len(files)} TUs\n",
          file=sys.stderr)
    for loc, msg in errors:
        print(f"  {loc}: {msg[:100]}", file=sys.stderr)
    sys.exit(1)

print(f"STRICT GATE OK: all {len(files)} engine TUs build clean with -DZX_STRONG_FIXED")
PY
