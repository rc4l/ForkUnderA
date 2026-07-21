#!/usr/bin/env bash
# [rc4l] fixed_t is the strong zx::Fixed type by DEFAULT now (the engine CMake defines
# ZX_STRONG_FIXED; see src/zandronum/src/CMakeLists.txt), so the ordinary `cmake --build` already
# enforces strict fixed-point typing for everyone -- no gate needed for that. What this checks is
# the OTHER direction: that the opt-out build (-DZX_STRONG_FIXED=OFF -> the raw 64-bit typedef,
# used when backporting upstream Zandronum/GZDoom patches) still compiles cleanly. Nobody builds
# the opt-out day-to-day, so a change that only works under the strong type could silently break
# the backport escape hatch; this catches that by syntax-checking every engine TU with the
# ZX_STRONG_FIXED define stripped out.
#
# [rc4l] Usage: `tests/optout-fixed-check.sh`. Point it at a configured engine build tree via
# STRICT_BUILD_DIR (default build-mac-arm) -- it needs that tree's compile_commands.json for each
# TU's real flags. Exits non-zero (and lists every site) if the opt-out build fails anywhere.
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
    # Turn the real per-TU compile line into an opt-out (raw fixed_t) syntax-only check by
    # stripping the default ZX_STRONG_FIXED define, exactly what -DZX_STRONG_FIXED=OFF does.
    cmd = re.sub(r'-o\s+\S+', '', e['command'])
    cmd = re.sub(r'\s-DZX_STRONG_FIXED(?=\s|$)', ' ', cmd)
    cmd = cmd.replace(' -c ', ' -fsyntax-only -ferror-limit=0 ')
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
    print(f"OPT-OUT GATE FAIL: {len(errors)} error(s) with ZX_STRONG_FIXED stripped across {len(files)} TUs\n",
          file=sys.stderr)
    for loc, msg in errors:
        print(f"  {loc}: {msg[:100]}", file=sys.stderr)
    sys.exit(1)

print(f"OPT-OUT GATE OK: all {len(files)} engine TUs still build clean with ZX_STRONG_FIXED stripped")
PY
