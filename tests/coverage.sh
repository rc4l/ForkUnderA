#!/usr/bin/env bash
# [rc4l] Build the tests with llvm-cov instrumentation, run them, and report per-file coverage.
#
# [rc4l] Usage: `tests/coverage.sh` reports only; `--enforce <file> …` fails if any listed
# file is under 100% line coverage; `--auto` derives that list from the tests themselves.
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
if [[ "${1:-}" == "--enforce" ]]; then
  shift; enforce=("$@")
elif [[ "${1:-}" == "--auto" ]]; then
  # [rc4l] Derive the enforced set from the tests themselves: each DIR/NAME_test.cpp gates
  # its sibling unit DIR/NAME.{cpp,h}. Adding a test auto-enforces its unit — no list to maintain.
  search=()
  [[ -d "$ROOT/features" ]] && search+=("$ROOT/features")
  [[ -d "$ROOT/src/zandronum/src" ]] && search+=("$ROOT/src/zandronum/src")
  while IFS= read -r t; do
    base="${t%_test.cpp}"
    for ext in cpp h; do
      [[ -f "$base.$ext" ]] && enforce+=("${base#"$ROOT/"}.$ext")
    done
  done < <( ((${#search[@]})) && find "${search[@]}" -name '*_test.cpp' 2>/dev/null )
fi

# [rc4l] llvm-cov instrumentation (-fprofile-instr-generate/-fcoverage-mapping) is
# Clang-only, so force the Clang toolchain — otherwise Ubuntu's default c++ (GCC)
# rejects those flags. macOS's clang++ (AppleClang) works the same way.
cmake -S "$ROOT/tests" -B "$BUILD" -DZANDROX_TESTS_COVERAGE=ON \
  -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}" >/dev/null
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
    # [rc4l] Print the exact uncovered lines so a CI failure is diagnosable without re-running
    # locally -- especially useful when the miss is compiler-version specific (see issue #27).
    $COV show "$BUILD/zandrox_tests" -instr-profile="$BUILD/cov.profdata" "$ROOT/$f" 2>/dev/null \
      | grep -E '^[[:space:]]*[0-9]+\|[[:space:]]*0\|' | sed 's/^/    uncovered| /' >&2
    status=1
  else
    echo "COVERAGE GATE OK:   $f 100%"
  fi
done
exit $status
