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

The engine build ignores every `*_test.cpp` (it compiles an explicit source list, not a
glob), so test files never reach the shipped binary.

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

## CI

`.github/workflows/_test.yml` runs the same three steps (sanitized build+run, coverage
`--auto`, clang-tidy) on every push/PR, and the macOS engine build is gated behind it
(`needs: test`). Green tests are required before anything builds.
