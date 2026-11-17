# net-snapshot

The snapshot netcode: a server sends each client the world as a diff of entity state per frame over
its own unreliable datagrams, and a client draws mirrors of what it has not simulated. It is a sixth
netcode module beside `net-transport`, `net-protocol`, `net-replication`, `net-client` and
`net-session`.

Ported from the `snapshot` branch of Kaori-Chani/ZanZan, from its `snap-ref-premerge` tag at
`b8a72a262`, where it was written against a 32-bit 16.16 `fixed_t`. The quantizers here are rewritten
for `zx::Fixed`; what that found is in the port's own notes, and the shape to watch for is an
unsigned value widened into a `fixed_t` without saying which sign it carries.

**The format layer and the server layer are here so far**, with every header the rest will need.
The client, the movers and the recorder follow as their own commits, and the hooks that call them
after that. **Nothing calls into this module from stock code yet**, so the game plays exactly as it
did without it.

What is in the engine's build, precisely, because "nothing" was not true:

- The twelve `computation/*_compute.cpp` units are compiled into the game, and have been since they
  landed. `src/CMakeLists.txt` globs `*_compute.cpp` recursively so a new computation needs no CMake
  edit, and that glob reaches here too. They register no classes and nothing calls them, so they are
  dead weight in the binary until the layers arrive, not behaviour.
- `capture.cpp` and `clienttrack.cpp` are compiled by the `netsnapshot_layers` object library and
  **not linked into the game**. The layers call each other, so none of them links until all of them
  are here; compiling each as it lands is what gives it a compiler's opinion rather than a reading.
  Step 3 links what has already compiled.
- The format layer is also reached by the test target, which is what holds it to the references.

## Owns

No vendored file. The module is its own folder, and each hook in a stock file is one call into it
with any role test or send behind that call, so no stock file's crossings grow and
`tools/boundary-allow.json` needs no raised allowance. The module's README joins the `owners` of the
netcode rules in `tools/boundary-rules.json` when those hooks land, not before.

## Public header

`format.h` is the wire: every record and event, the position frame, the quantizers and the
checksum. `stream.h` reads a `.zsnap`, the server's recording, whose records are zstd frames.
`record.h` is the `.csnap`, the client's recording, with its reader here and its writer to come.

## Files

- `format.h`: the format itself, header-only. Positions are quantized into a frame per map (an
  origin in whole units and a width per axis), so a map past 65,535 units a side sends two or three
  bytes an axis instead of wrapping. Its own wire stays 32-bit on purpose: a 48.16 `fixed_t` is read
  through `ReadFixed32`, which sign-extends, and mover planes, distances and polyobject speeds stay
  absolute 32-bit 16.16 like ForkUnderA's own sector commands.
- `stream.{h,cpp}`: the `.zsnap` container's reader, which needs the vendored zstd.
- `record.h`, `recordread.cpp`: the `.csnap` container and its engine-free reader, which the fuzzer
  and the replay both read through.
- `fuzzgen.h`: the generator the fuzzer builds inputs with, shared with the in-engine fuzzer to come.
- `capture.{h,cpp}`: the server's snapshot tick — the world capture and its censuses, legacy
  suppression, mover capture, cosmetic events, snapshot sounds and the stats report. Was
  `sv_snapshot`.
- `clienttrack.{h,cpp}`: the per-client delta — what each client has been told, its ack ledger and
  the interest scheduling that decides what its next datagram carries. Was `sv_snapclient`.
- `computation/`: twelve pure units — the ack ledger, the animation census, the budget and chunk
  cuts, the clock model, the interest tiers, the join's frame and map stamp, the gravity law and run
  steps, the mover write order, the replay's histogram, the ghost sampler, the smoothing, and the
  wall sweep — each with its colocated `*_test.cpp`.
  - `ComputeAnchorStart`, in the smoothing unit, is the mirror interpolation rule's start pose, and
    both the live frame and the replay's analysis call it so that `fua_csnap_analyze ... anchor`
    measures what a client actually draws. Two things about it are deliberate and easy to undo by
    accident. It takes the offset from the end and rounds it, rather than dividing the pose as
    `(h - f*x) / (1 - f)`: the truncated form drew a resting monster a unit low under this build's
    floating-point settings, because a fold that did not move has to come back on the end exactly,
    not near it. And `here` arrives in raw units, as the sampler leaves it, with `frac` the same
    `fixed_t` both callers already hold, so neither converts before the division — a comparison
    against the reference tag must not be able to fail on a conversion alone.
- `ghostactor.{h,cpp}`: the client's mirrors — the ghost table sampled into actors the render
  copy carries, their skins and states, the legacy copies they stand in front of, and the
  mirror interpolation rule's live half. Was `cl_ghostactor`.
  - The far rotor refreshes one slice of distant mirrors at a time, and its phase is keyed
    on the game tic rather than advanced per call, because this fork's render copy refills
    on a tic *or* a packet: advancing per call moved the phase twice in a tic that carried
    two packets, leaving a far mirror's turn to the traffic rather than to the clock. The
    phase count is two, not the reference's eight, because the reference advanced per drawn
    frame at around 140 fps and so refreshed a far mirror about 18 times a second; eight
    phases off a 35 Hz tic clock would give 4.4, where two give 17.5.
- `format_test.cpp`: the format layer's own tests, narrow frame and wide.
- `refdump_test.cpp`: the reference decode, below. The dumper it decodes with is shared, in
  `tests/snapdump/`, with the `snap_dump` command line beside it.

## In-engine hooks

Each hook is one call from a stock file into the module, listed here as it lands.

- `cl_main.cpp`, `ServerCommands::SnapMoverGate::Execute`: `MoverSetServerGate( legacyOff, frame )`,
  taken ahead of every legacy mover command under it. The first hook, and the only kind a
  protocol-spec command brings: a spec command is four parts — the `protocolspec` entry, the
  server's sender, this `Execute`, and `tools/data/protocol-snapshot.txt` written again — and the
  `Execute` is where it reaches the module, so no spec command can land before the module links.
  The golden is the part a compiler cannot catch; `tools/protocol-snapshot.py --check` does.

## The reference recordings

Three recordings made on the source tree at its `snap-ref-premerge` tag pin what the format layer
decodes to, byte for byte, through the dumper that wrote their shipped dumps. That dumper is
`snap_dump`'s own body, shared from `tests/snapdump/`: `refdump_test.cpp` holds the three
references to their digests through it, and the `snap_dump` target is a command line over the same
call, so a dump taken by hand is the dump the test takes.

`testdata/ref-wide.csnap` is committed, at 67 KB, and so is checked on every build: it is the only
reference carrying wide frames and non-zero origins, where 48.16 is likeliest to go wrong. The two
large recordings, `ref.zsnap` and `ref.csnap`, are 61 MB with their dumps and stay out of the repo;
set `ZX_SNAP_REFDIR` to the folder holding them and their `.dump.txt` files to run those two:

```
ZX_SNAP_REFDIR=/path/to/references ctest --test-dir build-tests -R SnapshotRefDump
```

Unset, those two report that they did not run, and say that a skip is not a pass. Set but wrong,
they fail: each recording's own SHA-256 is checked before it is decoded, so a damaged or mistaken
file fails as the wrong file rather than as a decode difference. A mismatching dump is written into
the build folder and the first differing line is named.

## The dumper

`snap_dump <recording> [out.txt]` writes any `.zsnap` or `.csnap` as text, telling the container
from the file's own magic. It is the tool that made the shipped reference dumps, and it is how a
new reference dump is made after a deliberate format change, or a recording read when a later
layer goes wrong. Its body is `tests/snapdump/snapdump.cpp`, which the reference test calls too;
the standalone `formattest` tree it came from is not ported, since step 1 replaced all three of
its jobs inside this build. Unlike the fuzzer it needs no sanitizer, so it builds everywhere,
MSVC included.

What it does with a recording it cannot read is pinned by `tests/snapdump/snapdump_test.cpp`: a
file that is no container, one shorter than the magic, one whose cvar block is cut, a recording that
stops mid-record, and a payload whose id gap cannot be read. Each is held to the exact line the dumper
writes, since those refusals are part of what decoding byte for byte means.

Its exit status is its answer rather than a mapping beside it: `0` the recording read whole with no
error, `1` the bytes could not be got at all or the arguments were wrong, `2` the bytes were read and
are wrong — no container this build knows, a header that would not open, or a record that did not
decode, with the dump written so far naming which. `SnapDumpWrite` returns those three values and
`main` returns what it was handed, so the status cannot drift from the result; one CTest case runs
the tool for each of the three.

## The fuzzer

`snap_fuzz` lives at `tests/snapfuzz/fuzz.cpp` rather than here: a non-engine program with its own
`main`, whose sanitizers are build flags that must not reach the rest of the test binary. It is
built behind `ZANDROX_SNAP_FUZZ` on GCC and Clang, with `ZANDROX_SNAP_FUZZ_LIBFUZZER` for the
libFuzzer mode, and CTest runs 20,000 inputs at its fixed seed plus the wide-reference mode on every
build. The long runs on both widths are a gate someone runs by hand.

`--wide-reference` writes a fresh wide reference and reads it back. It is not compared with the
committed `ref-wide.csnap` and cannot be: the generator fills its structs a byte at a time from one
deterministic stream, and `SnapMoverPlat` is 48 bytes here where a 32-bit `fixed_t` made it 32, so
every draw from the first mover record on differs. The committed copy stays the reference.
