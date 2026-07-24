#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

# [rc4l] Build and run the FuzzTest harnesses (src/**/*_fuzz.cpp).
#
# [rc4l] Three modes, because FuzzTest builds differently depending on what you want:
#   tests/fuzz.sh                 properties as bounded random unit tests (~1s each; runs anywhere)
#   tests/fuzz.sh --replay        replay the committed corpus -- the fast presubmit regression gate
#   tests/fuzz.sh --fuzz [DUR]    coverage-guided fuzzing, default 60s per binary (Linux + Clang only)
#
# [rc4l] --fuzz needs a separate build: coverage instrumentation is a compile-time flag
# (-DFUZZTEST_FUZZING_MODE=on), so it gets its own directory rather than thrashing the other one.
# That build links a Linux-only runtime, hence the platform guard below -- on macOS the first two
# modes still work, which is what a local edit-test loop actually needs.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${FUZZ_BUILD_DIR:-$ROOT/build-fuzz}"
FUZZ_BUILD="${FUZZ_MODE_BUILD_DIR:-$ROOT/build-fuzz-mode}"
# [rc4l] In-repo so found inputs are committable and replay as regressions forever after.
CORPUS="${FUZZ_CORPUS_DIR:-$ROOT/tests/corpus}"

mode="unit"
duration="60s"
case "${1:-}" in
  --replay) mode="replay" ;;
  --fuzz)   mode="fuzz"; duration="${2:-60s}" ;;
  "")       ;;
  *)        echo "usage: $0 [--replay | --fuzz [DURATION]]" >&2; exit 2 ;;
esac

# [rc4l] FuzzTest is Clang-only (GCC unsupported upstream), same constraint as coverage.sh.
configure() {
  cmake -S "$ROOT/tests" -B "$1" -DZANDROX_TESTS_FUZZ=ON \
    -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}" "${@:2}" >/dev/null
}

if [[ "$mode" == "fuzz" ]]; then
  if [[ "$(uname -s)" != "Linux" ]]; then
    echo "ERROR: coverage-guided fuzzing needs Linux -- FuzzTest's fuzzing runtime does not link" >&2
    echo "       on macOS. Use 'tests/fuzz.sh' or '--replay' locally; CI fuzzes on ubuntu." >&2
    exit 1
  fi
  configure "$FUZZ_BUILD" -DFUZZTEST_FUZZING_MODE=on
  cmake --build "$FUZZ_BUILD" --target zandrox_fuzz -j"$(getconf _NPROCESSORS_ONLN)" >/dev/null
  BIN="$FUZZ_BUILD/zandrox_fuzz"
else
  configure "$BUILD"
  cmake --build "$BUILD" --target zandrox_fuzz -j"$(getconf _NPROCESSORS_ONLN)" >/dev/null
  BIN="$BUILD/zandrox_fuzz"
fi

mkdir -p "$CORPUS"

case "$mode" in
  unit)
    echo "== Property mode: every FUZZ_TEST as a bounded randomized unit test =="
    "$BIN"
    ;;
  replay)
    # [rc4l] Replays the coverage corpus built by past fuzzing sessions. Deterministic and quick,
    # so it can gate every PR while the real fuzzing runs out-of-band on a schedule.
    echo "== Replay mode: re-running the committed corpus in $CORPUS =="
    # [rc4l] `inf` means "the whole corpus for every FUZZ_TEST" rather than an unbounded run --
    # replay is finite by construction, it only re-executes inputs already on disk.
    "$BIN" --replay_corpus_for=inf --corpus_database="$CORPUS"
    ;;
  fuzz)
    echo "== Fuzzing mode: $duration per fuzz test, corpus in $CORPUS =="
    # [rc4l] --fuzz_for covers every FUZZ_TEST in the binary; findings land in the corpus database
    # so a crasher becomes a committed regression input rather than a line in a CI log.
    "$BIN" --fuzz_for="$duration" --corpus_database="$CORPUS"
    ;;
esac
