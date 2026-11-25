# fuzz: decoder fuzzing

Covers: src/zandronum/src/features/net-snapshot/fuzz.cpp, src/zandronum/src/features/net-snapshot/fuzzgen.h, tests/snapfuzz/fuzz.cpp

## Responsibility

Malformed input must never crash a client: a snapshot payload, a cosmetic events command, a `.zsnap`
or a `.csnap`. Two fuzzers feed mutated input through the real readers. `fua_snap_fuzz` in the
engine drives the client's own entry points (`ApplyDelta`, `CosmeticEvents`) and can reach the
sinks; `tests/snapfuzz/fuzz.cpp`, built as the `snap_fuzz` target, builds with no engine, under the
address and undefined-behaviour sanitizers or as a libFuzzer target, and also fuzzes the two file
readers. Both draw their seeds and mutations from `fuzzgen.h`. The design's note on it: [Migration
plan](design.md#9-migration-plan-dual-stack).

## The generator (fuzzgen.h)

A xorshift generator (`SnapFuzzRng`, `FuzzRand`), the same sequence on every platform for a seed; 0
is not a seed. A seed (`SnapFuzzSeed`) is a valid input plus the offsets of the bytes its readers
branch on:
- `FuzzSeedPayload`: one payload holding entity records of every group combination, each fixed event
  type, every mover class (`FuzzSeedMover`), and a self-describing type this build skips. Mover
  indices are drawn from `SnapFuzzLevel`'s counts.
- `FuzzSeedCosmetic`: a cosmetic events command after its header: the map stamp, the count, and
  generic and blood events with their ages.
- `FuzzFrame`: the frame a seed is written and read in, from three bits, one an axis made wide; 0 is
  the narrow frame every recorded map has. Both builders take the bits and record them on the seed
  (`frameBits`).
- `FuzzMutate`: one to four edits, favouring the branch bytes.
- `FuzzFill`, `FuzzAppend`: random bytes, and a byte run appended to a seed.

The seed builders compile out of line and, under MSVC, unoptimized, since MSVC runs out of heap
optimizing them.

## In the engine (fuzz.cpp)

```
fua_snap_fuzz [inputs [seed]] [sinks|spawn]
```

200,000 inputs by default. It refuses while connected or playing a demo (it clears the ghost table)
and on a server. Then:
1. Saves the hooks and clears them. In "sinks" mode the mover sink stays installed, since
   `MoverInstallSinks` runs once per process and a later connect relies on it; the mover indices are
   drawn within the loaded level, and the mover gate is opened if shut, and shut again after, so
   records reach the sink's apply code. In "spawn" mode the run joins map stamp 0, so cosmetic
   events make their actors; otherwise their stamp is never the joined one and they only parse. Both
   modes change the loaded level, and say so.
2. Builds a pool of 64 seeds, payloads and cosmetic commands alternating, every other pair in a
   frame with wide axes.
3. For each input: one in 16 is raw random bytes, the rest a mutated pool seed, read in its seed's
   frame (`UseMapFrame`; the session's is put back at the end). A cosmetic one goes to
   `CosmeticEvents`; a payload goes to `ApplyDelta`, half of them read as a pre-v28 stream and one
   in eight arriving behind the newest frame, as a reordered datagram does. The ghost table resets
   every 1,024 inputs.
4. Prints the counts (payloads, those with errors and the first, records by kind, mover outcomes,
   cosmetic streams), then the `[snap]` report at once (`GhostReportNow`) and clears it, since the
   run's counts would read as failures in the next one. Everything saved is put back.

A crash is the finding; errors are expected, since most inputs are malformed.

## Standalone (tests/snapfuzz/fuzz.cpp)

Built by `tests/CMakeLists.txt` as `snap_fuzz`, with `recordread.cpp`, and with `stream.cpp` when
zstd is found ([formattest.md](formattest.md)). It always takes ASan and UBSan where the compiler
has them, and `ZANDROX_SNAP_FUZZ_LIBFUZZER` (off) builds it as a libFuzzer target on Clang instead.
Either way it takes `-fno-sanitize-recover=all`, so a finding stops the run with a nonzero exit:
UBSan otherwise reports and carries on, and a run that still prints PASS hides it. CTest runs
`snap_fuzz_seeded` (20,000 inputs) and `snap_fuzz_wide_reference` wherever the target is built.

```
snap_fuzz [inputs [seed [corpus-dir]]]
snap_fuzz --wide-reference <out.csnap>
```

`--wide-reference` writes the reference set's wide recording and exits: a current `.csnap` with a
map record in each of the eight frames `FuzzFrame` names, the narrow one first, a reset between
them, and after each 48 deltas from `FuzzSeedPayload` in that map's frame, with frames, mirrors and
a changed cvar block between them. It draws from a fixed seed (`WIDE_REFERENCE_SEED`), so the file
is the same on every run. A frame's camera pose is drawn within 32 bits, which the reader's
`fixed_t` holds before and after the 48.16 port alike. The file and its `snap_dump` output are the
reference for the paths the two recorded maps never reach: a nonzero origin and the 3-byte axes.
That is the file `testdata/ref-wide.csnap` holds, and the file CI walks with the Python readers
([tools.md](tools.md)).

With a directory it writes the seeds there for a libFuzzer run and exits, and writes each container
seed again without its selector byte, as `seedNN.csnap` or `seedNN.zsnap`, so the offline readers
can open the same files, narrow and wide. The low three bits of each input's first byte pick the
target, the next bit the payload's stream version, current or pre-v28, and the three above it the
axes a payload or cosmetic input is wide on (`FuzzFrame`); a container's frame comes from its file.
Every other round of seeds is wide, and the reached line counts the walks in a wide frame:
- **a payload** (four of the eight values): a walk that mirrors `ApplyDelta`'s and
  `clent_ApplyEvent`'s call for call, running every reader the client may reach whatever ghost state
  would have skipped it.
- **a cosmetic events command** (two): every event through the codec's cursor, as `CosmeticEvents`
  reads them.
- **a `.zsnap`**, when built with zstd: through `ZsnapOpen` and `ZsnapReadRecord` from a file, each
  payload through the walk in the header's frame. Its seeds are of the current version and the two
  before, so both header sizes are read, and a current one's header may be wide.
- **a `.csnap`**: through `CsnapOpen` and `CsnapReadRecord` from a file, each delta through the walk
  in the frame of the map record before it. Its seeds span `CSNAP_VERSION_MIN` to `CSNAP_VERSION`,
  so both pose widths and every map record shape are read, and open with a map record, which from
  v18 may be wide.

A seed's frame widths are marked hot.

Each reader runs twice, on an exact-size copy of the bytes its length function granted, where an
overread is a sanitizer report, and on a padded copy whose result must match, which catches an
overread without one. A file reader must never allocate more than the file holds. A failure prints
FINDING with the input in hex and aborts; `g_known` lists findings already queued for a fix,
reported once instead, and is empty. A pass prints how many inputs reached each reader, so a run
that never gets past the id shows.

## Public functions

`FuzzRand`, `FuzzFill`, `FuzzAppend`, `FuzzSeedMover`, `FuzzSeedPayload`, `FuzzSeedCosmetic`,
`FuzzMutate`, above.

## Settings

The engine command `fua_snap_fuzz`, for client or offline sessions.

## Rules that must hold

- **The walk follows the client.** The standalone walk restates `ApplyDelta`'s and
  `clent_ApplyEvent`'s order and readers; a change to either changes it in the same patch.
- **One generator.** Both fuzzers take their seeds and mutation from `fuzzgen.h`, so a seed added
  there reaches both.
- **A new reader gets a seed.** A record kind, event type, mover class or file record with no seed
  is reached only by chance.
- **The engine run leaves nothing behind**: hooks, the join and the gate are put back, and its
  counts are reported and cleared.

## Engine hooks

None.

## Tests

These are the tests. The standalone build passes a default run under the sanitizers with every
reader reached; CTest runs the seeded pass and the wide reference on every build; the engine command
runs clean in each mode on a loaded test map.
