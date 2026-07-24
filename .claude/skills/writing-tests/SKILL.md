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

## Property/fuzz harnesses — `*_fuzz.cpp`

A `TEST` checks the values you thought to list. A `FUZZ_TEST` states a *rule* and lets a
coverage-guided search hunt for a counterexample across the whole input domain. The 100%
coverage gate cannot see the difference — it only asks whether a line ran, not whether it ran
with a value that breaks it — so arithmetic units want both.

**Reach for one when** the unit is a pure `Compute*` over scalars and you can name a rule:

- **Differential** — two implementations that must agree (`ComputeMulShiftS64Soft` vs the
  native `__int128` path; a fast path vs the wide path it skips). The oracle is free.
- **Round-trip** — `encode → decode` is the identity (`WireRoundtripLong`, the voice-chat
  float↔bytes pair).
- **Invariant** — a rule with no reference implementation at all (`AlignDownPow2` lands on a
  multiple of 2^bits, at or below the input, within one step).

```cpp
void MulShiftSoftMatchesNative(int64_t a, int64_t b, unsigned shift) {
    ASSERT_EQ(zx::ComputeMulShiftS64Soft(a, b, shift), zx::ComputeMulShiftS64(a, b, shift));
}
FUZZ_TEST(Wide128Fuzz, MulShiftSoftMatchesNative)
    .WithDomains(fuzztest::Arbitrary<int64_t>(), fuzztest::Arbitrary<int64_t>(),
                 fuzztest::InRange<unsigned>(0, 63));
```

Rules that matter:

- File **must** end in `_fuzz.cpp` — its own glob, its own binary (`zandrox_fuzz`). Misnamed
  files are silently skipped, exactly like `*_test.cpp`.
- Use `ASSERT_*`, not `EXPECT_*` — the property function must stop at the first failure so the
  reported counterexample is the one that actually broke it.
- **Encode preconditions as domains, not as assumptions.** A header that says "shift in [0,63]"
  means `fuzztest::InRange<unsigned>(0, 63)`; "caller guarantees b != 0" means a
  `fuzztest::Filter`. Skip this and you get reports for inputs the engine can never produce.
- **Watch for UB in your own reference.** Left-shifting a negative value is UB — build the
  shift as a multiply (`v * (__int128(1) << s)`). This is the bug `fa93b7f` fixed in a test
  model, and a reference implementation is just as easy to get wrong.
- A found counterexample gets committed to `tests/corpus/` and replays forever after. Treat it
  exactly like the failing test you'd hand-write for a crash report (per `AGENTS.md`).

## Run the harnesses

```bash
bash tests/fuzz.sh              # every property as a bounded random test (~1s each) — works on macOS
bash tests/fuzz.sh --replay     # re-run the committed corpus; the presubmit regression gate
bash tests/fuzz.sh --fuzz 300s  # coverage-guided fuzzing — Linux only, see below
```

**Coverage-guided fuzzing is Linux-only**: FuzzTest's fuzzing runtime does not link on macOS
arm64. The first two modes work everywhere, which is what a local edit-test loop needs; the
nightly `fuzz.yml` workflow does the real searching on Ubuntu. The first build is slow
(FuzzTest pulls abseil/re2/protobuf) — CI caches the build tree, so budget that cost once.

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

`.github/workflows/_fuzz.yml` runs the property harnesses. `test-and-build.yml` calls it in
`unit` mode on every PR (bounded, seconds once cached); `fuzz.yml` calls it nightly in `fuzz`
mode with a real time budget and uploads the corpus — continuous fuzzing pays off over hours,
so it deliberately stays off the PR path.

## Misc

Make sure the feature is tested against its intended networking instance. Server, client, both, etc.