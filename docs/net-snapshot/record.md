# record: the client recording

Covers: src/zandronum/src/features/net-snapshot/record.h, src/zandronum/src/features/net-snapshot/record.cpp, src/zandronum/src/features/net-snapshot/recordread.cpp

## Responsibility

The client's recorder and the reader of what it writes. A `.csnap` holds what one client was told
and when: every snapshot payload as it reached `ApplyDelta`, with its frame number, game tic and
arrival time; every drawn frame, with the render time and the camera's pose; what the mirrors drew;
and the settings the sampler read. It is the client-side counterpart of the server's `.zsnap`
([stream.md](stream.md)), which holds the world before the per-client pass rations it.
`fua_csnap_analyze` plays it back ([csnap_replay.md](csnap_replay.md));
`tools/snapshot/csnap2.py` summarises it offline ([tools.md](tools.md)).

The writer is `record.cpp`. The reader is `recordread.cpp`, apart so that it builds with no engine,
for the standalone fuzzer ([fuzz.md](fuzz.md)). The design: [The
recorders](design.md#94-the-recorders).

## The file

Little-endian. A header: `CSNAP_MAGIC`, the version, the tic rate, flags (0: payloads stored raw),
the recording player's number, and the cvar block. Then records, each a kind byte (`ECsnapKind`) and
its fields:
- `CSNAP_DELTA`: frame number, game tic, arrival time, size, and the payload exactly as decoded.
- `CSNAP_FRAME`: render time, the camera's pose (`CsnapPlayerPose`, its x, y and z 64-bit from v17),
  and a new cvar block when a value changed since the last one.
- `CSNAP_RESET`: the time and a reason of up to 63 bytes, where the ghost table was torn down.
- `CSNAP_MIRROR`: one mirror's drawn sprite, frame, state index and tics, when they changed. It
  carries no time; the frame record before it does. The deltas are what the server instructed and
  local animation draws between them, so this is how a replay checks what the client drew; it
  carries the state as well as the frame, since states share frames. It carries no position, which
  is why the `FillNow` rule is checked by eye rather than by a replay.
- `CSNAP_MAP`: the map's name, the MD5 of its lumps (`RecorderLevelChecksum`) and, from v17, the
  position frame `SnapJoin` carried (`ClientMapFrame`): three 32-bit origins and three widths. The
  reader returns it as `mapFrame`, the narrow frame at origin 0 for an older file, and refuses a
  width this build has no encoding for, or a wide one before v18, whose payloads were only ever
  narrow.

The cvar block is a count, then each setting's name and value; it is matched by name, so its order
means nothing. The set is `g_csnapCvarNames`: every client setting the sampler, the smoothing and
local animation read, and `fua_csnap_recmirror`, which says what fraction of drawn-state changes the
file carries.

The camera's pose is recorded because the render tier, which decides how much work a mirror gets,
is a function of the camera's distance; a replay computes no tiers yet, and the pose is what would
let it.

`CSNAP_VERSION` moves with every wire change a decoder cannot skip, since the payloads carry no wire
version of their own; `CSNAP_VERSION_MIN` is the oldest still read:
- v14: wire v28 payloads, whose mover envelope gained a byte.
- v15: wire v32 payloads, whose enter may carry a target NetID; a v14 file never sets the bit, so
  it still decodes.
- v16: adds `CSNAP_MAP`, payloads unchanged, so v14 and v15 still replay with no map named.
- v17: adds the position frame to `CSNAP_MAP` and widens the frame record's pose to 64 bits,
  payloads unchanged, so v14 to v16 replay in the narrow frame at origin 0 they were recorded in.
- v18: wire v34 payloads, whose positions are wide on a wide frame axis; a narrow frame's payloads
  are v17's.

The property event this port added moved neither version: it is self-describing, so a reader that
does not know it steps over it by its length ([format.md](format.md)).

## Recording

`fua_csnap_record <name>` opens `<name>.csnap` in the module's own folder, stopping any recording in
progress, and writes the header, the map when a level is loaded, and a baseline: every present ghost
as an enter block (`ExportBaseline`), stamped with the newest frame and game tic the client holds,
so the file is self-contained. Then:
- `apply.cpp` calls `RecorderDelta` with each payload it applies;
- `GhostFrame` calls `RecorderFrame` before sampling and `RecorderMirror` as each mirror's drawn
  state changes, one in `fua_csnap_recmirror` of them by id;
- `LevelUnloaded` calls `RecorderReset`;
- `AnchorMapStamp` calls `RecorderMapJoined`, which writes the joined map.

`fua_csnap_stoprecord` closes the file, as does a failed write (`RecorderStopIfActive`).

## Reading

`CsnapOpen` reads the header and refuses another magic, a version outside the supported range, or a
truncated header or cvar block. `CsnapReadRecord` fills the reader's fields for one record and
returns `SNAPREAD_OK`, `SNAPREAD_END` at a clean end, `SNAPREAD_TRUNCATED` for a record cut short,
or `SNAPREAD_ERROR` for an unknown kind, a map name too long, or a payload past 4 MB. A size or
count the rest of the file cannot hold is refused before it is allocated. The payload stays valid
until the next read. `CsnapClose` frees the reader.

## Data structures

- `ECsnapKind`, `CsnapPlayerPose`, `CsnapCvar`: the record kinds, the camera's pose and one recorded
  setting.
- `CsnapReader`: the open file, the header's fields, the live cvar block, and the last record's
  fields by kind.

## Public functions

Recording:
- `RecorderActive`: whether a recording is open, for the hot paths to test first.
- `RecorderDelta`, `RecorderFrame`, `RecorderMirror`, `RecorderReset`, `RecorderMapJoined`: above.
- `RecorderStopIfActive`: closes the recording with a reason.
- `RecorderLevelChecksum`: the loaded level's checksum, false outside a level; also the replay's map
  test.

Reading: `CsnapOpen`, `CsnapReadRecord`, `CsnapClose`.

## Settings

Client commands: `fua_csnap_record`, which takes a file name, and `fua_csnap_stoprecord`. A name
with a path separator in it is refused; the file lands in `M_GetFuaUserPath()/net-snapshot` with the
module's other writes.

## Rules that must hold

- **A recording is self-contained.** It opens with a baseline; without one, every update for a ghost
  entered before the recording is refused on replay.
- **Settings are recorded by name.** A setting added to the set cannot shift another's meaning, and
  one this build lacks is reported by the replay.
- **A cvar change rides the frame that first sees it**, so the replay applies it at the same point.
- **Bump the version for any payload change a decoder cannot skip.** The file names no wire version,
  so an older payload under a newer reader misparses rather than fails.
- **The reader allocates nothing the file cannot hold.** The fuzzer checks it.

## Engine hooks

None. Every caller is a module file: `apply.cpp` for the deltas, `ghostactor.cpp` for the frames,
mirrors and the reset, `entities.cpp` for the joined map, and `csnap_replay.cpp` for the level
checksum.

## Tests

The standalone fuzzer drives `CsnapOpen` and `CsnapReadRecord` with mutated files
([fuzz.md](fuzz.md)); `tools/snapshot/csnap2.py` reads a recording independently, and the committed
`testdata/ref-wide.csnap` is read through this reader by the reference test and by CI's walk
([tools.md](tools.md)); `fua_csnap_analyze` reports a truncated tail and the first decode error.
