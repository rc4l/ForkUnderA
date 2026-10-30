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
- **Testing vendored code that must be linked for real** (e.g. `configfile.cpp`,
  `networkshared.cpp`): name it `<unit>_nettest.cpp`. That suffix routes it into the separate
  `zandrox_tests_net` binary, which links those units against small shims; the main binary can't.
  Its source list is in `tests/CMakeLists.txt`, and the 100% gate does not cover it.

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
- Comments: one sentence, prefixed with your GitHub account name, e.g. `// [rc4l]` (per `AGENTS.md`).

## Run it locally (macOS or Linux)

```bash
# build + run under AddressSanitizer + UBSan, then the tests
cmake -S tests -B build-tests -DZANDROX_TESTS_SANITIZE=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure

# 100%-coverage gate (auto-derives which units must be fully covered)
bash tests/coverage.sh --auto
```

- **LeakSanitizer is Linux-only**: on macOS ASan and UBSan run but leaks aren't reported, so rely on CI
  (Ubuntu, the full ASan+UBSan+LSan set) for leak checks.
- **macOS 26 + Apple clang 17**: the CLT's ASan runtime deadlocks in its own init (`AsanInitInternal` →
  `dyld_shared_cache_iterate_text_swift` → malloc → back into ASan), so every sanitized binary hangs
  before `main` and CMake's discovery step deletes it as "timed out". Configure with Homebrew LLVM
  instead: `CC=$(brew --prefix llvm)/bin/clang CXX=$(brew --prefix llvm)/bin/clang++ cmake -S tests
  -B build-tests -DZANDROX_TESTS_SANITIZE=ON`. CI's `macos-latest` is unaffected.
- Coverage needs the **Clang** toolchain (llvm-cov); `coverage.sh` forces it. On macOS and Linux,
  `node tools/fuactl/src/cli.mjs coverage --since REF` gates only what changed, with Homebrew's LLVM and the
  SDK on macOS, in `build-cov-fuactl/`.

## Run it locally (Windows)

`build-tests/` is a Visual Studio tree, which picks the configuration at build time.
`node tools/fuactl/src/cli.mjs build --no-stage --tests` builds the engine and the tests with Visual Studio's
own cmake (Git Bash has none on PATH), runs them, and prints the count or the failing names; a running engine
holding the binary is named as such rather than left as an LNK1104. By hand:

```bash
cmake --build build-tests --config Release --parallel 4
build-tests/Release/zandrox_tests.exe --gtest_brief=1          # add --gtest_filter='Suite.*' for one suite
build-tests/Release/zandrox_tests_net.exe
```

`build --tests` reconfigures by itself when a new `*_test.cpp` or `*_compute.cpp` appears; `coverage` does
not, so after adding a file build the tests before `coverage`.

The sanitizers need Clang and a POSIX toolchain, so here the 100% gate runs with OpenCppCoverage over a Debug
build of the tests (Release has no symbols):

```bash
export FUACTL_OPENCPPCOVERAGE=<OpenCppCoverage install folder>/OpenCppCoverage.exe
node tools/fuactl/src/cli.mjs coverage --since REF    # every unit whose source, header or test changed since REF, new files included
node tools/fuactl/src/cli.mjs coverage                # every unit a *_test.cpp gates
```

`--since` takes the commit the work started from: `--since main` gates every commit on the branch, and
`--since HEAD` only what is uncommitted. It prints each gated unit under 100% with its missed lines (exit 1).
A missed line that is only a loop's `}` or a `continue;` is MSVC putting the jump there, which llvm-cov counts
as run, so it is not reported. A unit whose test needs what MSVC lacks (`fixed64`'s soft 128-bit path compares
against `__int128`) stays under 100% here and passes in CI. It measures no branches, so check every branch of
a new `Compute*` function against its tests by reading them, and say so.

## Run it under Emscripten (the browser's runtime)

The same tests build with Emscripten, threads and wasm exceptions, and run in Node; CI's `test-web`
job does this, so a test that only passes natively (a thread it never joins, a path on the real disk)
fails there:

```bash
emcmake cmake -S tests -B build-tests-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests-web
node build-tests-web/zandrox_tests.js
```

## Page and tool code (JavaScript)

The page's modules in `web/` export their logic to Node (`module.exports` beside the page's global, as
`crashreport.js` does) and are tested in `tools/web/test`; fuactl's are tested in `tools/fuactl/test`.
`node --test` in either folder runs them. Keep the page's DOM wiring in `fua.js` thin and the decisions in
the modules, where a test can reach them.

## Before a commit

```bash
node tools/fuactl/src/cli.mjs coverage --since REF    # 100% on every changed unit
node tools/fuactl/src/cli.mjs comments --since REF    # every [rc4l] comment added is one line of one sentence
```

## CI

`.github/workflows/test-and-build.yml` runs `_test.yml` on pull requests and pushes to `main`: sanitized
tests on Linux and macOS, `coverage.sh --auto` as its own job, a plain MSVC build and run on Windows, the
Emscripten build in Node, and the JS tests. Each test binary runs once (`tests/run_all.py`), since `ctest`
starts it once per test. The platform builds and the browser build run beside the tests rather than after
them, every compile through a cache (ccache, or sccache on Windows), so a run takes as long as its slowest
job.

clang-tidy no longer runs in CI (deleted in 50d64db); `.clang-tidy` stays for editors and clangd.
On Windows, VS Build Tools ships `clang-tidy.exe` under `VC\Tools\Llvm\x64\bin` for running it by hand.

## Misc

Make sure the feature is tested against its intended networking instance. Server, client, both, etc.