#!/usr/bin/env bash
# [rc4l] Build the tests with llvm-cov instrumentation, run them, and report per-file coverage.
#
# [rc4l] Usage: `tests/coverage.sh` reports only; `tests/coverage.sh --enforce <file> …` fails if any listed file is under 100% line coverage.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${COVERAGE_BUILD_DIR:-$ROOT/build-coverage}"

# [rc4l] Resolve llvm tools from PATH (Linux CI) or fall back to xcrun (macOS).
llvm_tool() {
  if command -v "$1" >/dev/null 2>&1; then echo "$1";
  elif xcrun --find "$1" >/dev/null 2>&1; then echo "xcrun $1";
  else echo "ERROR: $1 not found" >&2; exit 1; fi
}
PROFDATA="$(llvm_tool llvm-profdata)"
COV="$(llvm_tool llvm-cov)"

enforce=()
if [[ "${1:-}" == "--enforce" ]]; then shift; enforce=("$@"); fi

cmake -S "$ROOT/tests" -B "$BUILD" -DZANDROX_TESTS_COVERAGE=ON >/dev/null
cmake --build "$BUILD" --target zandrox_tests -j"$(getconf _NPROCESSORS_ONLN)" >/dev/null

LLVM_PROFILE_FILE="$BUILD/cov.profraw" "$BUILD/zandrox_tests" >/dev/null
$PROFDATA merge -sparse "$BUILD/cov.profraw" -o "$BUILD/cov.profdata"

echo "== Coverage report =="
# [rc4l] Exclude the fetched GoogleTest sources from the report.
$COV report "$BUILD/zandrox_tests" -instr-profile="$BUILD/cov.profdata" \
  -ignore-filename-regex='.*/_deps/.*'

# [rc4l] Every enforced file must be at 100% line coverage or the gate fails.
status=0
for f in "${enforce[@]:-}"; do
  [[ -z "$f" ]] && continue
  pct="$($COV export "$BUILD/zandrox_tests" -instr-profile="$BUILD/cov.profdata" \
        -sources "$ROOT/$f" 2>/dev/null \
        | grep -o '"lines":{[^}]*"percent":[0-9.]*' | grep -o '[0-9.]*$' | head -1)"
  if [[ "$pct" != "100" && "$pct" != "100.0" ]]; then
    echo "COVERAGE GATE FAIL: $f is at ${pct:-0}% line coverage (need 100%)" >&2
    status=1
  else
    echo "COVERAGE GATE OK:   $f 100%"
  fi
done
exit $status
