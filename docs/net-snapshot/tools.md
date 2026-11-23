# tools: the offline readers and the anchor comparison

Covers: tools/snapshot/csnap2.py, tools/snapshot/snap_payload.py, tools/snapshot/zsnap_info.py, tools/snapshot/wide_count.py, tools/snapshot/presence_diff.py, tools/snapshot/snap_anchor_compare.py, tools/snapshot/snap_anchor_compare_test.py, tools/snapshot/doccheck.py, tools/snapshot/modmap.py

## Responsibility

Python 3 scripts that read what the module writes without the engine. `zsnap_info.py` validates a
server `.zsnap` ([stream.md](stream.md)); `csnap2.py` summarises a client `.csnap`
([record.md](record.md)); `wide_count.py` prices wide positions on one; all three walk payloads with
`snap_payload.py`, the one Python copy of the record layout. `presence_diff.py` compares the
presence logs of a server and a client. `snap_anchor_compare.py` holds a replay report to the
source tree's. `doccheck.py` holds this guide to the code, and `modmap.py` draws the module map
from it. The design: [The recorders](design.md#94-the-recorders) and [Appendix
B](design.md#appendix-b--the-zsnap-fixture-format).

The source tree's standalone runner and its fuzzer are not here. Step 1 replaced all three of their
jobs inside this repo's own build: the runner became the `*_compute_test.cpp` GoogleTest files, the
fuzzer the `snap_fuzz` target over `tests/snapfuzz/fuzz.cpp`, and the dumper the reference test
with the `snap_dump` command line over one shared body in `tests/snapdump/`
([formattest.md](formattest.md), [fuzz.md](fuzz.md)).

## snap_payload.py

The entity-record walk (`walk`) and the mover body readers, shared by both containers, which differ
only in framing. It restates `format.h`'s group widths, event lengths, id encoding and mover
layouts, and walks the layout of `WIRE_VERSION`, which reads every version in `WIRE_READABLE`: v33
changed only the `.zsnap` header, and v34's payload is v33's in a narrow frame. The tables are the
narrow frame's, and every length or position that depends on the map's frame takes a `Frame` (its
origin and widths; `NARROW` by default): `walk`, `event_bytes`, the group lengths, `mover_header`,
`mover_poly_body_bytes` and `mover_poly`, whose positions come back absolute. `decode_pos` reads a
record's x, y and z the same way.

`verify_header` checks each restated constant against a `format.h`, the id encoding and the frame's
widths included, and names any that differ; both containers' scripts expose it as `--verify`. A
file that is not `format.h`, a recording above all, is refused before anything is compared, so a
wrong path never reads as a layout mismatch. Two of its rules are about the format growing rather
than changing: every event type the header declares must be one this walk can size, and every
mover class in it must be one this decoder places. A fixed-length event or a sizable class added to
the header therefore fails here, rather than reaching a reader as an unknown type.

Event types 0-15 are fixed length and a reader must know each one; 16-31 carry a length byte, so a
reader skips one it has never seen, which keeps a newer build's stream readable -- `SNAPEV_PROPS`
is skipped that way by every reader here. A mover body's length comes from its class (and flags),
never from that length byte, which is for skipping: one class may have more than one body shape. An
EVENT is tested before the groups, since a high event type sets bits that overlap the group flags.

## zsnap_info.py

```
python3 tools/snapshot/zsnap_info.py <file.zsnap> [--decode]
python3 tools/snapshot/zsnap_info.py --verify <path/to/format.h>
```

Without `--decode` it checks the header and record framing and prints a summary: records, tic and
wall coverage, sizes, compression and saturation. With it, every payload is decoded with presence
tracked, and the stream must be self-consistent: a baseline first, enters only for absent ids,
updates and leaves only for present ones, payload sizes exact, and no record's tic before the last
decoded one's. Compressed records need the `zstandard` module, and are only frame-checked without
it. The header's size comes from its version, its frame is the one every payload is walked in, and a
frame off narrow at origin 0 is printed; a width with no encoding, or a wide one before v34, is
refused. A file of a wire version outside `WIRE_READABLE` is refused. The exit code is the gate: 0
pass, 1 usage or header mismatch, 2 decode errors, 3 aborted at an undecodable record, 4 valid but
ending mid-record; `fua_snap_verify` returns the same 0, 2, 3 and 4. A recording left running when
the process ends always reports 4, the recorder having no stop at quit ([stream.md](stream.md)).

## csnap2.py

```
python3 tools/snapshot/csnap2.py <file.csnap>
python3 tools/snapshot/csnap2.py --verify <path/to/format.h>
```

Reads container versions `CSNAP_VERSION_MIN` to `CSNAP_VERSION` and refuses the rest. Prints the
header and cvar block, the record counts and the maps (with their frame when it is off narrow at
origin 0), then distributions with no plausibility bound: movers (records, intervals, outcomes), and
the update interval and per-update displacement by distance ring from the recorded camera and by
class. Each map record's frame is the one the deltas after it are walked and decoded in; a width
with no encoding, or a wide one before v18, is refused. An id's history is dropped at its leave, so
one entity is never differenced against the next occupant of its slot. v14's payloads (wire v28 to
v31) walk as v32's: no enter sets the target bit, and the mover envelope has its generation byte.

Movers are keyed by (class, polyobject or not, index), since a mover record's id is zero and its
identity is in the body, and their history is kept across a reset, since a sector index is not
recycled. They are recorded only in the client stream: the per-client pass writes them, after the
world diff the `.zsnap` records. A mover sends on its transitions, so one lift's records should be
seconds apart; a median interval near one tic means the dirty test is firing on the plane. Game
tics against wall time below the tic rate mean the server was dilating
(`fua_snap_maxcatchuptics`), and every rate is then over a world that ran slow.

Two counts stand apart at the end of that line, and they mean different things:

- `unreadable` must read 0. It counts records of a class this decoder knows nothing about, or of a
  length it disagrees with: a peer built from another header, which `--verify` catches before a
  session.
- `of a class with no body` counts a class the header declares and no writer emits. There is one,
  `SNAPMV_ANIMDOOR`: an animated door is replicated by its effects, so `MoverClassTracked` leaves
  it out and nothing ever writes a body under it.

The committed wide reference is where the second shows. `testdata/ref-wide.csnap` carries 264 mover
records -- 43 plat, 34 floor, 37 elevator, 28 pillar, 35 ceiling, 31 door, 27 poly, and 29 under
class 6 -- and those 29 read as the class with no body here. **The C++ decode is the authority and
agrees exactly**: `snap_dump` over the same file marks the same 29 records `bad=1` and prints their
raw bytes, every other class decoding into fields, and the walk steps over each by the event's
length byte. They are an artefact of how that reference was generated, which writes a body under
every class value; `ref.csnap`, a recording a build made, reports 0 of both.

## wide_count.py

```
python3 tools/snapshot/wide_count.py <file.csnap> [<file.csnap> ...]
```

Prices wide positions on a recording: per server frame (game tic), the payload and the records a
wide axis grows (POS and POSZ groups, branch events, the local block, and polyobject x/y in headers
and MOVETO targets), then what a map wide in x and y, or in x, y and z, would add, counting only
the axes the recording's own frame has narrow. Payload bytes, not wire bytes; cosmetic events and
point sounds travel in their own commands and are not in a `.csnap`. Its walk-error count is what
CI reads: the committed wide reference must walk with 0.

## presence_diff.py

```
python3 tools/snapshot/presence_diff.py <snappresence-sv.txt> <snappresence-clN.txt> [-v]
```

Counts the mirrors a client holds that the server holds absent, from the logs
`fua_snap_debug_presencelog` and `fua_csnap_debug_presencelog` write
([clienttrack.md](clienttrack.md), [apply.md](apply.md)), both into
`M_GetFuaUserPath()/net-snapshot`. For two consecutive client dumps at frames G1 and G2, an id is
stuck when the client holds it in both and no server dump for that player from G1 to G2 lists it. A
held id is in every server dump over the span, as the client applies a leave before acking it, and
an entity that entered and left between two client dumps drops out of the second, so it cannot read
as stuck. A pair with no server dump inside it is skipped, and a file whose frames go backwards (two
server sessions in one log) is refused -- the server's frame number only ever climbs within a
process, map changes included, so that is the signature of appending to an old log. Prints the pairs
compared and skipped, the pairs with a stuck id, and each stuck id's frames; `-v` prints every pair.
Exits 1 when any id is stuck.

## snap_anchor_compare.py

```
python3 tools/snapshot/snap_anchor_compare.py <logfile> [<fixture>]
```

Holds a `fua_csnap_analyze <file> anchor` report to the one the source tree's build produced over
the same recording, so a difference in the interpolation arithmetic is a finding on its own. What
is compared and what is not is in the module's README (The anchor comparison, and its fixture), and
the fixture names the tag it came from. `snap_anchor_compare_test.py` holds the rules in cases
built from real reports and runs in CI; a log holding more than one report is refused rather than
compared as one.

## doccheck.py

```
python3 tools/snapshot/doccheck.py
```

Checks every page in `docs/net-snapshot/` against the code and exits non-zero on a failure;
what it checks is in the guide's index ([README.md](README.md), Keeping the guide true) and in
its own header. `ALLOWED` lists public functions deliberately left off a page, each with a
reason, and a name there that is no longer public fails too. Beyond the pages it reads the
module's own comments: a `.md` a comment names must exist, and none of the source tree's file
or unit names may appear, the port having renamed every one. A page, a module file and the
module's README cite code by function, never by line number, since line numbers move. It runs
in CI beside the other tool tests.

## modmap.py

```
python3 tools/snapshot/modmap.py [-o OUT.html] [--standalone] [--tag TAG]
```

Writes the module map: one page with every source file of the module and its tools grouped into
parts by column, the public functions and commands each declares, the calls between them, and the
engine functions that call in, counted by site. The page is `modmap.html` with the data filled
in; it is written to be published as a page, and `--standalone` adds the document around it so
the file opens in a browser as it is. The header names the commit it was made at.

What it reads is in its own header. The parts are a table in the script (`COLUMNS`), and a module
source that no part claims fails the run, as does a part that yields nothing, so a new file is
placed before a map is made. Its public functions are the ones `doccheck.py` finds, read the same
way. A function's summary is the comment on or above its declaration, else at its definition,
else the sentence of its guide page naming it or its family; calls are credited through the
file-local helpers a function reaches. It is run by hand before the map is published, not in CI.

## Rules that must hold

- **One Python copy of the layout.** A record change edits `snap_payload.py` beside `format.h`,
  and `--verify` must pass after it.
- **Refuse, never guess.** A reader handed another version stops: a misparse looks like data.
- **The exit codes are an interface.** `fua_snap_verify` shares them; a CI gate reads them.
- **A count that means "I could not read this" is never the same count as "there is nothing to
  read here."** The mover line keeps them apart, and so does every reader's refusal path.

## Engine hooks

None.

## Tests

`_test.yml` runs `--verify` against the tree's `format.h` and `wide_count.py` over the committed
`testdata/ref-wide.csnap`, asserting 0 walk errors, on every push, and runs
`snap_anchor_compare_test.py` beside them. By hand: `zsnap_info.py --decode` passing on a fresh
server recording, with the same entity and presence counts as `fua_snap_verify`; `presence_diff.py`
over a session's two logs. No stock map is wide, so the wide walk is otherwise checked on the
standalone fuzzer's container seeds, narrow and wide, which `snap_fuzz` writes as `.csnap` and
`.zsnap` files beside its corpus: every one walks with no error ([fuzz.md](fuzz.md)).
