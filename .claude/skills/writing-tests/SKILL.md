---
name: writing-tests
description: How to write and run C++ tests in ZandroX (GoogleTest, colocated/features layout, 100% coverage, sanitizers). Use whenever adding or modifying tests, or when new/ported C++ code needs test coverage.
---

# Writing tests in ZandroX

ZandroX tests use **GoogleTest + GoogleMock**, built as a standalone project in `tests/`
(separate from the heavy engine build). Follow this whenever you add or change tests.

## Where the test file goes

- **Testing a *vendored* engine unit** (e.g. `src/zandronum/src/vectors.h`): put the test
  **right beside it**, colocated: `src/zandronum/src/vectors_test.cpp`.
- **Testing one of our *feature* modules**: put the test **inside that feature folder**:
  `features/<name>/<thing>_test.cpp`, next to `<thing>.cpp`. See `features/README.md`.

The engine build never compiles a `*_test.cpp`, so test files never reach the shipped binary. It does
glob `*_compute.cpp` (`ZX_COMPUTE_SOURCES` in `src/zandronum/src/CMakeLists.txt`), and the test binary
compiles only `*_test.cpp` and `*_compute.cpp` (`tests/CMakeLists.txt`). So a `Compute*` helper lives in
`<name>_compute.cpp` (by convention in the feature's `computation/` folder) with `<name>_compute_test.cpp`
beside it: no CMake edit either side, and a helper defined in any other `.cpp` does not link into the tests.

## Naming — this is enforced by discovery, so get it right

- The file **must** end in `_test.cpp`. The CMake glob (`tests/CMakeLists.txt`) only picks
  up `*_test.cpp`; a misnamed file (`test_foo.cpp`, `foo_tests.cpp`, `foo_spec.cpp`) is
  **silently skipped** with no error.
- Name it after the unit under test: `foo.cpp`/`foo.h` → `foo_test.cpp`. The coverage gate
  (`--auto`) maps `foo_test.cpp` back to `foo.cpp`/`foo.h` and requires them at **100%**.
- Nothing else to wire: `CONFIGURE_DEPENDS` re-globs on build and `gtest_discover_tests`
  registers the cases — drop the file in and rebuild.

## What to test, and how to keep it testable

- **Start at clean leaf seams** — pure/low-dependency code (math, string, table, parser
  helpers). `src/zandronum/src/vectors_test.cpp` is the template.
- **Engine code tangled in globals is hard to link.** Don't try to link the whole engine.
  Instead, when porting, **extract the pure logic into a small free function/helper** and
  unit-test that; leave the glue thin and driven end-to-end via the MCP.
- **Name every extracted helper with a `Compute` prefix** (e.g. `ComputeGravityOffset`,
  `ComputeMidiDeviceDefault`, `ComputeFloatToBytes`). Extract as much logic as humanly
  possible into these pure `Compute*` computations so it's testable without the engine;
  the `Compute` prefix marks "this is a dependency-free, unit-tested computation." When a
  computation must call a C API (OpenAL `al*`/`alc*`, a decoder), mock that one interface
  with GoogleMock rather than linking the engine.
- **Cover every branch** — the gate is 100% lines on the unit. If a branch is unreachable,
  restructure rather than leaving it uncovered ("no copping-out", per `AGENTS.md`).

## Style

- GoogleTest: `TEST(Suite, Case)` / `TEST_F(Fixture, Case)`; put helpers in an anonymous
  `namespace {}`.
- Comments: one sentence, prefixed `// [rc4l]` (per `AGENTS.md`).

## Run it locally (macOS or Linux)

```bash
# build + run under AddressSanitizer + UBSan, then the tests
cmake -S tests -B build-tests -DZANDROX_TESTS_SANITIZE=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure

# 100%-coverage gate (auto-derives which units must be fully covered)
bash tests/coverage.sh --auto
```

Notes:
- **LeakSanitizer is Linux-only** — on macOS ASan/UBSan run but leaks aren't reported; CI
  (Ubuntu) runs the full ASan+UBSan+LSan set, so rely on CI for leak checks.
- Coverage requires the **Clang** toolchain (llvm-cov); `coverage.sh` forces it.

## Run it locally (Windows)

`build-tests/` is a Visual Studio tree. `node tools/fuactl/src/cli.mjs build --no-stage --tests` builds the
engine and the tests with Visual Studio's own cmake (Git Bash has none on PATH), runs them, and prints the
count or the failing names; a running engine holding the binary is named as such rather than left as an
LNK1104. One suite at a time:

```bash
build-tests/Release/zandrox_tests.exe --gtest_filter='Suite.*'
```

`build --tests` reconfigures by itself when a new `*_test.cpp` or `*_compute.cpp` appears; `coverage` does
not, so after adding a file run `build --no-stage --tests` before `coverage`.

Without clang/llvm-cov the `--auto` gate's authority is CI, but the same gate runs here with
OpenCppCoverage over a Debug build of the tests (Release has no symbols):

```bash
export FUACTL_OPENCPPCOVERAGE=F:/ShitDronum/zandronum-vscode-windows/deps/opencppcoverage/OpenCppCoverage.exe
node tools/fuactl/src/cli.mjs coverage --since REF    # every unit whose source, header or test changed since REF, new files included
node tools/fuactl/src/cli.mjs coverage                # every unit a *_test.cpp gates
```

`--since` takes the commit the work started from (the last push, say): `--since main` gates every commit on
the branch, and `--since HEAD` only what is uncommitted. It builds the Debug tests, runs them under OpenCppCoverage, and prints each gated unit under 100% with its
missed lines (exit 1). A missed line that is only a loop's `}` or a `continue;` is MSVC putting the jump
there, which llvm-cov counts as run, so it is not reported. Three units stay under 100% here and pass in
CI: `fixed64`'s soft 128-bit path, whose tests compare against `__int128` that MSVC lacks, and two lines of
`browserfocus_compute`. It measures no branches, so check every branch of a new `Compute*` function against
its tests by reading them, and say so.

## CI

`.github/workflows/_test.yml` runs on pull requests and pushes to `main`: sanitized tests plus
`coverage.sh --auto` on Linux, sanitized tests on macOS, and a plain MSVC build and run on Windows.
Every engine build is gated behind it (`needs: test`), so green tests come before anything builds.

## Misc

Make sure the feature is tested against its intended networking instance. Server, client, both, etc.