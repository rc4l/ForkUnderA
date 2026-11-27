# formattest: the codec's tests, and the two offline targets

Covers: src/zandronum/src/features/net-snapshot/formattest.cpp, src/zandronum/src/features/net-snapshot/format_test.cpp, src/zandronum/src/features/net-snapshot/refdump_test.cpp, src/zandronum/src/features/net-snapshot/moversound_wire_nettest.cpp, tests/snapdump/snapdump.h, tests/snapdump/snapdump.cpp, tests/snapdump/main.cpp, tests/snapdump/snapdump_test.cpp

## Responsibility

Where the format layer is tested, and by what, all inside this repo's own build, so there is no
second copy of the assertions to drift:

- the codec's cases are **GoogleTest files** (`format_test.cpp` and the thirteen
  `computation/*_compute_test.cpp` units), built by `tests/CMakeLists.txt` into `zandrox_tests` and
  held to 100% line coverage;
- the reference decode is **`refdump_test.cpp`**, which decodes the committed recordings through the
  ported format layer and holds each dump to its shipped digest;
- the three sector-keyed mover sounds' bytes are **`moversound_wire_nettest.cpp`**, which models
  SVC2_DOORSOUND, SVC2_MOVERSOUND and SVC2_FLOORSOUND on `BYTESTREAM_s` and runs in
  `zandrox_tests_net` (the `_nettest.cpp` suffix routes it there);
- the fuzzer is the **`snap_fuzz`** target over `tests/snapfuzz/fuzz.cpp` ([fuzz.md](fuzz.md));
- the dumper is the **`snap_dump`** target over `tests/snapdump/`, whose body the reference test
  calls, so a dump taken by hand is the dump the test takes.

`formattest.cpp` is what is left in the engine: one block, the time base, which needs `i_tictime`
and `TICRATE` and so cannot be a pure unit ([i_tictime.md](i_tictime.md)).

## What runs where

| the question | where it is answered |
| --- | --- |
| does the codec round-trip, at every width and frame | `format_test.cpp`, in `zandrox_tests` |
| does each pure unit's arithmetic hold | `computation/*_compute_test.cpp`, 100% line gate |
| does this tree decode the references byte for byte | `refdump_test.cpp` against the shipped digests |
| does malformed input crash a reader | `snap_fuzz`, and `fua_snap_fuzz` in the engine |
| what does a recording decode to, by hand | `SnapDumpWrite`, through `snap_dump <in> <out> [--tag]` |
| does the time base's arithmetic hold in the engine | `formattest.cpp`'s one block, `fua_snap_selftest` |
| does **this** binary decode as the others do | the decode check in `entities.cpp` |

The last two are the only ones a test binary cannot answer, which is why they are in the engine at
all ([selftest.md](selftest.md), [entities.md](entities.md)).

## The reference decode

`refdump_test.cpp` holds three recordings to their dumps' SHA-256, each recording's own digest
checked first so a damaged or mistaken file fails as the wrong file rather than as a decode
difference. `testdata/ref-wide.csnap` ships in the repo at 67 KB and runs on every build, being the
only reference with wide frames and non-zero origins; `ref.zsnap` and `ref.csnap` are 61 MB with
their dumps and stay out, so those two run only with `ZX_SNAP_REFDIR` set and report that a skip is
not a pass ([tools.md](tools.md)).

The dumper takes a `--tag` mode, which decodes only the event types the reference tag knew, so a
type this port added does not change a shipped digest: `SNAPEV_PROPS` is skipped by its length
there, as a reader of the tag's vintage would skip it ([format.md](format.md)).

## Rules that must hold

- **One copy of each assertion.** A codec case lives in the GoogleTest file or in the engine's
  blocks, never in both: two sets built differently are free to drift, and the drift is invisible
  until one of them is wrong.
- **An engine block is one that needs the engine.** Anything a pure unit can answer goes to the
  unit, under the coverage gate.
- **Every block names itself** through `SelfTestBlock`, so a block that did not run is absent from
  the summary rather than silently passing ([selftest.md](selftest.md)).
- **A dump digest changes only with a deliberate format change**, and the `--tag` mode is what keeps
  a new self-describing type from counting as one.

## Engine hooks

None. `formattest.cpp` is reached through `fua_snap_selftest`; the test files are built by
`tests/CMakeLists.txt` and the two targets by the same file.

## Tests

These are the tests. CI builds `zandrox_tests` on every push and runs it under the line gate, runs
`snap_dump_wide_reference` and `snap_fuzz_seeded` as CTest entries, and runs the engine's
`fua_snap_selftest` on three platforms.
