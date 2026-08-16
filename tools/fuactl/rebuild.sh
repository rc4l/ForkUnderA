#!/usr/bin/env bash
# [rc4l] Build the engine and stage it, failing LOUDLY.
#
# Written after two measurements were taken against a stale binary: the build was invoked inside a
# PowerShell one-liner that ended with Copy-Item, so the shell's exit status came from the copy and
# a compile error scrolled past as ordinary output while `&&` happily continued to the launch. A
# silent stale build is worse than a broken one -- it produces numbers and screenshots that look
# real. This checks the compiler's own exit code and refuses to stage anything if it is nonzero.
set -euo pipefail

ROOT="F:/ForkUnderA"
LOG="$ROOT/build-win/build.log"
CMAKE="C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"

export DXSDK_DIR="$ROOT/dxsdk"

if ! "$CMAKE" --build "$ROOT/build-win" --config Release --target zdoom -- -m >"$LOG" 2>&1; then
  echo "BUILD FAILED:" >&2
  grep -E "error C|error LNK|error MSB" "$LOG" | head -20 >&2
  exit 1
fi

# A zero exit with errors in the log has happened too; belt and braces.
if grep -qE "error C[0-9]|error LNK[0-9]" "$LOG"; then
  echo "BUILD REPORTED SUCCESS BUT LOG CONTAINS ERRORS:" >&2
  grep -E "error C|error LNK" "$LOG" | head -20 >&2
  exit 1
fi

powershell.exe -NoProfile -Command "Get-Process -Name forkundera -ErrorAction SilentlyContinue | Stop-Process -Force" >/dev/null 2>&1 || true
sleep 3
cp "$ROOT/build-win/Release/forkundera.exe" "$ROOT/dist-windows/forkundera.exe"

# [rc4l] The addon catalogue, alongside the binary, because the HOST tab reads it from progdir.
#
# This script only ever copied the exe, so every build it staged had no catalogue at all -- the
# presets list drew empty and hosting read as unimplemented rather than unstaged. It is a directory
# of small JSON manifests, so syncing it on every build costs nothing and removes a whole class of
# "why is this feature missing" that is really "the data was never copied".
[ -d "$ROOT/catalogue" ] || { echo "catalogue/ missing -- the HOST tab would have nothing to offer" >&2; exit 1; }
rm -rf "$ROOT/dist-windows/catalogue"
cp -r "$ROOT/catalogue" "$ROOT/dist-windows/catalogue"

echo "build ok, staged $(date -r "$ROOT/dist-windows/forkundera.exe" '+%H:%M:%S')" \
     "(+$(ls "$ROOT/dist-windows/catalogue" | wc -l | tr -d ' ') catalogue entries)"
