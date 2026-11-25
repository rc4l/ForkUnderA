# format.h: the wire format

Covers: src/zandronum/src/features/net-snapshot/format.h

## Responsibility

Everything that crosses the wire, defined once: the quantizers, the map's position frame, the
entity record and its groups, the event types, the mover records, the property and cosmetic event
codecs and the `.zsnap` file framing. The server's encoder (`capture.cpp`, `clienttrack.cpp`), the
client's decoder (`entities.cpp`), the recorders and the offline tools all use these definitions;
nothing about the layout may be restated on one side. Why the layout is what it is: the design
doc's [The entity state block](design.md#3-the-entity-state-block) and [Appendix
B](design.md#appendix-b--the-zsnap-fixture-format).

The header includes `basictypes.h`, `tables.h` and `<string.h>` and no other engine header, so the
test files and the standalone fuzzer build it without the engine ([formattest.md](formattest.md)).
Its quantizers carry 16.16 through 64-bit arithmetic, so they hold for this engine's 48.16
`fixed_t` as they did for a 32-bit one.

## Data structures

- `SnapFrame`: the map's position frame, an origin in whole units and a width in bytes per axis,
  and the clamp counter its users point at. Positions cross the wire relative to the origin; the
  server picks the frame per map and SnapJoin carries it. A map that fits +-32k has origin 0, so
  its bytes are those of the frameless format. An axis is `SNAP_FRAME_NARROW` (2 bytes) or,
  spanning past 65,535 units, `SNAP_FRAME_WIDE` (3 bytes, +-8.4 million units), and every position
  on a wide axis takes a byte more: POS, POSZ, the branch event, the local block, a polyobject's
  x/y and MOVETO target, the cosmetic events and the point sound.
- `SnapEntityState`: one entity's quantized state, its `qx`/`qy`/`qz` absolute whole units clamped
  into the frame at capture. On the server it is captured from the live actor; on the client it is
  the decoded ghost. `misc` carries the corpse, invisible, floorclip and waterlevel bits, the two
  missile bits, the 4-bit checksum (bits 8-11) and the crouch value (bits 12-15).
- The mask byte: `SNAPGRP_POS`, `SNAPGRP_POSZ`, `SNAPGRP_ANG`, `SNAPGRP_VELMISC`, `SNAPGRP_APPEAR`,
  `SNAPGRP_RENDER`, plus `SNAPGRP_ENTER` (a `classId` follows) and `SNAPGRP_EVENT` (the low five
  bits are an event type). All eight bits are used, and `SNAP_GROUP_COUNT` sizes every per-group
  report.
- APPEAR, the one variable group, takes no mask bit: the top bit of its first byte picks the form.
  Narrow, 2 bytes: `[0ttttttt]` (qtics 0-127), then the state offset at depth 0. Wide, 4 bytes:
  `[1_______]`, qtics, then the 16-bit `qstate`. Nearly every record is narrow; qtics is what forces
  the wide form, often enough in some scenes that it is a real path.
- RENDER: translation index (16 bits) | legacy style index (5) | alpha in 1/63rds (6) | translation
  type (5), the word fully packed.
- Event types: `SNAPEV_BRANCH`, `SNAPEV_PSPRITE`, `SNAPEV_LOCAL` (fixed length), and the
  self-describing `SNAPEV_MOVER` and `SNAPEV_PROPS` (types 16-31 carry their own length byte).
  Payloads are `SnapPSpriteState`, `SnapLocalState`, the mover structs and `SnapProps`.
- `SnapProps`: an entity's material and drawing properties, by flag -- `SNAP_PROPS_SCALE` (scaleX
  and scaleY at 1/256 in two bytes each), `SNAP_PROPS_ROLL` (the top 16 bits of the angle, as its
  own command sends), `SNAP_PROPS_MATERIAL` (a name with its length first, `SNAP_PROPS_MATNAME_MAX`
  at most; a length of 0 means "no material"), and `SNAP_PROPS_UNIFORMS` (a set mask, then four
  floats per set slot, `SNAP_PROPS_UNIFORM_SLOTS` of them). `SNAP_EVBYTES_PROPS` is the longest
  event payload there is, so it, not the mover's, is what `SNAP_EVBYTES_MAX` follows.
- `SnapIdCursor`: the previous record's id while one payload is written or read.
- Mover bodies: `SnapMoverPlat`, `SnapMoverCeiling`, `SnapMoverFloor` (two shapes, `SNAPMVF_STAIR`),
  `SnapMoverDoor`, `SnapMoverElevator`, `SnapMoverPillar`, and `SnapMoverPoly` with its
  `SnapPolyEntry` actions. The envelope is class, flags, index, generation (`SNAPMV_HEADER_BYTES`).
  A body carries what its thinker's `Serialize` carries, plus the plane or planes. A ceiling and a
  floor leave out `m_Texture` (a local index into client-extensible content; the texture crosses by
  name on SetSectorFlat) and `m_NewSpecial`, both read only at a terminal pastdest. A floor's stair
  shape adds the six stair fields, `m_OrgDist` among them, since the client runs the stair
  countdown itself. An elevator's or a pillar's GONE carries both planes, since either can end with
  one short of its target. `SNAPMV_ANIMDOOR` is declared and never written: an animated door is
  replicated by its effects.
- Cosmetic events: `SnapCosGeneric`, `SnapCosBlood`, and `SnapCosCursor`, which reads past its end
  the way `BYTESTREAM_s` does. `SVC2_SNAPCOSMETIC` is the map stamp (a byte), a count (a word), then
  each event's flags byte and body. Generic: class, x y z, an angle byte, velocity, then the state
  and extras by flag. Blood: x y z, a direction byte, a damage byte, the originator. Splatter: x y
  z, the originator.
- Sizes that bound buffers, in the widest frame: `SNAP_MAX_RECORD_BYTES` (34 narrow, as
  `SNAP_MAX_RECORD_BYTES_NARROW`; `RecordBytesMax` for a frame), `SNAP_MAX_EVENT_BYTES`,
  `SNAP_MAX_EMIT_BYTES`, `SNAPMV_BODY_MAX`, `ZSNAP_MAX_RAWSIZE`. A compile-time assertion holds
  `SNAP_EVBYTES_MAX` above every type's payload, so a new one cannot undersize an event buffer.
- Versions: `ZSNAP_VERSION` and `ZSNAP_VERSION_MIN` (22, where APPEAR took two widths). What each
  version below the current one changed, and how this reader handles it:
  - v23: event types 16-31 reserved as self-describing.
  - v24: `SNAPEV_MOVER`.
  - v25-27: mover classes CEILING, FLOOR (two body shapes) and DOOR.
  - v28: the envelope's generation byte, read as 0 from an older envelope
    (`MoverHeaderBytes`); a v27 door's `botSpot` is read in the new encoding and
    range-checked.
  - v29-31: ELEVATOR with GONE's NOFLOOR and NOCEILING, PILLAR, and POLY, the first variable-length
    body (`SNAPMV_BODY_MAX` 128; `SNAPMVF_POLY` retired).
  - v32: `SNAP_CLASS_TARGET` on a missile enter; older streams never set it.
  - v33: the map's position frame in the `.zsnap` header (`ZSNAP_HEADER_FRAME` bytes after
    `ZSNAP_HEADER_SIZE`); the payload is v32's, and an older file is read in the narrow frame at
    origin 0.
  - v34: a frame axis may be wide (`SNAP_FRAME_WIDE`), widening every position on it; a narrow
    frame's payload is v33's.

  `SNAPEV_PROPS` arrived after v34 and moved no version: a self-describing type is skipped by a
  reader that does not know it, which is what the version rule below means by not a break.

## Public functions

Quantizers and packing:
- `QuantPos`, `QuantPosIn`, `QuantAngle`, `QuantPitch`, `QuantVel`, `QuantAlpha` and `Dequant*` (6):
  position (one unit, rounded, into a given frame or the narrow one at origin 0), angle (12 bits),
  pitch (10 bits), velocity (1/4 unit, clamped, rounded) and alpha (1/63rds, so opaque round-trips).
- `PosToUnits`: a quantized position in map units, for the server's distance tests.
- `WirePitch`: the 8 pitch bits that cross; every held or compared pitch goes through it.
- `PackSpriteFrame`, `SpriteFromPack`, `FrameFromPack`: sprite and frame in 16 bits.
- `PackRenderState`, `TranslationFromState`, `StyleIndexFromState`, `AlphaFromState`: translation,
  legacy style index and alpha in 32 bits.
- `PackEffects`, `UnpackEffects`: `AActor::effects` in one byte.
- `QuantCrouch`, `CrouchFromWire`: the crouch factor as 16 steps; `FRACUNIT` is passed in.

The map's position frame:
- `FrameNarrow`, `FrameFromExtent`: the frame of a map that fits +-32k, and the frame for a map's
  extent within the engine's range: origin 0 while the extent and its margin
  (`SNAP_FRAME_MARGIN_XY`, `SNAP_FRAME_MARGIN_Z`) fit, else centred on it, wide on an axis spanning
  past 65,535 units, and flagged as overflowing past 16,777,215.
- `FrameWidest`: every axis wide, for a budget that must hold in any frame (the polyobject
  capture's, and the buffer constants'). `FrameWidthKnown`: whether a width read from a file
  or a command has an encoding. `FrameExtra`: the bytes an axis adds over narrow.
- `FrameLo`, `FrameHi`, `FrameClamp`: an axis's whole-unit bounds, and a value clamped into them,
  never wrapped, and counted through `pClamped`.
- `FloorPosIn`: a position floored into the frame, for the fields that have always floored.
- `PosBytes`, `PosRel`, `PosAbs`, `PutPosIn`, `ReadPosIn`: a whole-unit position relative to the
  origin in its axis's width, as a value or on the wire.
- `FixedBytes`, `OriginFixed`, `PutFixedIn`, `ReadFixedIn`: a 16.16 position relative to the origin,
  the axis's width plus two bytes of fraction, clamped and counted.
- `PutSigned`, `ReadSigned`: a signed little-endian value of two to five bytes. `ReadFixed32`
  reads a whole 16.16 word as this engine's `fixed_t`.
- `ZsnapHeaderBytes`: the `.zsnap` header's size for a file's version.

Entity records:
- `DiffMask`: the one definition of what changed between two states.
- `StateChecksum`: the 4-bit checksum of a state's full post-decode value.
- `GroupBytesBeforeAppear`: bytes before APPEAR, a function of the mask and the frame. The group
  length functions all take the frame, since POS and POSZ widen on a wide axis.
- `GroupBytes`: the widest a mask can be; the only one that may size a buffer.
- `GroupBytesFor`: what the writer will emit for this state; what a census adds up.
- `GroupBytesRead`: what a reader must consume, read from the record's bytes.
- `AppearIsNarrow`, `AppearBytes`: the 2- or 4-byte APPEAR form.
- `WriteGroups`, `ReadGroups`, `MergeGroups`: write, read and overlay groups through the codec.
  `WriteGroups` takes the state the receiver will hold, for the checksum.
- `WriteRecordBody`, `WriteRecord`: a body (mask, `classId` on an enter, shooter NetID behind
  `SNAP_CLASS_TARGET`, groups), or a whole record with its id for one-pass producers.
- `IdGapBytes`, `PutIdGap`, `ReadIdGap`, `IdCursorReset`, `RecordIdBytes`, `WriteRecordId`,
  `ReadRecordId`: the id encoding (raw first id, then varint gaps mod 2^16).
- `Read16`, `Read32`, `Put16`, `Put32`: little-endian primitives.

Missiles and extrapolation:
- `MissileFlagged` and the predicates `Is*` (4): ballistic, gravity-ballistic, suppressible and
  steering. The two misc bits are a three-way class; read them through these.
- `ExtrapBallistic`, `ExtrapLaw`: where a ghost flying unaided is after n tics, straight or under
  the gravity law. One implementation for the client's sampler and the server's suppression.
- `AppearExempt`: the appearance-suppression control group (one id in
  `SNAP_APPEAR_EXEMPT_STRIDE`).

Events:
- `EventSelfDesc*` (2): whether a type carries its own length, and whether this build knows it.
- `EventBytes`, `EventRecordBytes`: a fixed type's payload, and how far any event reaches
  (`SNAPEVLEN_OK`: skip it, or read `n` bytes; `SNAPEVLEN_UNKNOWN`: a fixed-length type this build
  does not know, which stops the parse; `SNAPEVLEN_BAD`: truncated, or a self-describing length of
  0). It is the one skip contract, and every walk over a payload goes through it.
- `WriteEventBody`, `ReadEvent`: a branch event, with the game tic it was observed on.
- `WritePSpriteBody`, `ReadPSprite`, `WriteLocalBody`, `ReadLocal`: the psprite and local-block
  payloads.
- `WritePropsBody`, `ReadProps`: the property payload, by set flag. `QuantScale` is a scale at
  1/256 in two bytes, `PropsUniformCount` the slots a set mask names, and `PutFloat32` and
  `ReadFloat32` one uniform's float, the only float on the wire.

Movers:
- `WriteMover*` (9) and `ReadMover*` (8): the envelope, one per class, and on the write side the
  whole event body with its length byte.
- `MoverHeaderBytes`: the envelope's width for a stream version (pre-v28 has no generation).
- `MoverBodyBytes`, `MoverRecordBodyBytes`: a body's length by class and flags, walked for a
  polyobject; 0 refuses the record.
- `PolyHeaderBytes`, `PolyEntryBytes`, `MoverPolyBodyBytes`: a polyobject body's header, one action,
  and the walked body, each in the map's frame.
- `MoverPlanes`, `RecordPlanes`: the planes a class moves, and those one record speaks for.
- `MoverClassTracked`: the classes whose legacy state a gating server stops.

Cosmetic events:
- `CosWrite*` (2), `CosReadGeneric` and `CosReadBlood`: generic and blood or splatter bodies.
- `CosRead8`, `CosRead16`, `CosRead32`, `CosReadPos`, `CosClamp`, `CosScale`: the readers' cursor
  reads (a position in its axis's width among them), clamp and shift-safe scale.
  `SNAP_COSE_GENERIC_MAX` is the longest generic body in any frame.

## Rules that must hold

- **Every position is in the map's frame.** Each writer and reader of a position takes the frame
  after its buffer pointer, and each field keeps its own rounding: POS, POSZ and the branch event
  round, the cosmetic events and the point sound floor, and the local block and polyobject x/y and
  targets are 16.16. The origin is whole units, so rounding commutes with it. Out of the frame a
  value clamps and is counted, never wraps. Mover planes and polyobject distances and speeds stay
  absolute.
- **Origin arithmetic is defined for any origin.** `PosRel` and `PosAbs` wrap through `DWORD`, and
  `OriginFixed` multiplies rather than shifts, since a file or a command can carry any origin and
  signed overflow or a negative shift is undefined.
- **The quantizers are 16.16 whatever `fixed_t` is wide.** This engine's `fixed_t` is 48.16 in 64
  bits, so every shift and product here is written to hold at that width; a value out of a frame
  clamps rather than truncating into it.
- **A length is a function of the frame.** Every length function that covers a position takes the
  map's frame (`EventBytes`, the group lengths, `PolyEntryBytes`, `MoverPolyBodyBytes`), so a reader
  cannot size a record in one frame and read it in another. The buffer constants
  (`SNAP_MAX_RECORD_BYTES`, `SNAP_COSE_GENERIC_MAX`) take the widest frame; a budget test takes the
  map's (`RecordBytesMax`), or a narrow map would stop short.
- **The checksum ignores a narrow value's high half.** `StateChecksum` mixes in a position's high 16
  bits only when they are not its sign extension, so a narrow map's checksums are those of the
  frameless format; the tests pin them.
- **One definition.** A field list, a size or a predicate exists here once. `DiffMask` and
  `StateChecksum` cover the same fields; a field in one and not the other is a defect either way.
  `MergeGroups` goes through the codec rather than a fourth field list.
- **Read the event bit first.** A type's bits overlap the group flags, so a reader that tests groups
  first parses an event as state and misaligns the rest of the payload.
- **A branch event carries its game tic**, the low 16 bits compared as a signed difference, so an
  event applied after a newer record cannot wind the entity back. A game tic, not a frame number:
  under catch-up one frame spans several tics. The wrap, about 31 minutes, is harmless over an
  event's deadline.
- **A psprite record is state on the event escape**, delta'd against a per-player reference and
  re-sent while unacked. It carries the weapon class, since `qstate` resolves against the class that
  owns the state and the two ends disagree about `ReadyWeapon` while a switch is in flight.
- **A property event is state too**, re-sent while unacked and reverted on a lost datagram, which is
  why its payload is whole rather than a diff: a client that missed one must not hold half a set.
- **The local block is sent whole and unquantized.** Nearly every field moves each tic, the
  reconciliation census measures corrections below one unit, and it carries no server gametic, which
  the datagram already does.
- **The render state carries the translation's type and a legacy style index.** Without the type,
  every translated actor draws untranslated; its five bits are asserted against
  `NUM_TRANSLATION_TABLES` in `capture.cpp`. The style is an `ERenderStyle` index, not
  `FRenderStyle::AsDWORD`, whose truncation draws every actor as a black silhouette; a custom style
  falls back to `STYLE_Normal`.
- **The suppression control group is compiled in, by id modulo.** Both ends must agree on its
  membership, which a cvar cannot give (`CVAR_SERVERINFO` does not reach gameplay clients), and a
  range would pick map-load actors, since id order is spawn order.
- **Bounds-check with the reader's length.** `GroupBytes` sizes buffers; checking a record
  against it rejects a valid narrow record that ends a payload. Readers use `GroupBytesRead`.
- **Group order carries length.** APPEAR is the one variable group and RENDER the only one after it.
  A second variable group goes after APPEAR, or the prefix length stops being a function of the mask
  and the map's frame.
- **Ids are a gap mod 2^16 from the previous record, the first raw**, and the base restarts at every
  payload. Writers and readers advance the cursor themselves.
- **Enter is declared, never inferred.** `SNAPGRP_ENTER` says a `classId` follows; mask 0 is a
  leave. Every reader masks `SNAP_CLASS_TARGET` off the `classId`.
- **A mover reader dispatches on class and flags**, never on the length byte, which is for skipping.
  Mover class values and the spare flag bits are append-only; 0x08 is retired and stays unused.
- **Version by meaning.** A change that moves no byte but changes what a field means is a version
  bump. A new fixed-length event type is a break; a new self-describing type or mover class is not.
  Raise `ZSNAP_VERSION_MIN` when an older stream would be misread rather than skipped, and
  re-record the `.zsnap` and `.csnap` fixtures.
- **No engine headers.** Engine values the format restates (effect bits, `TRANSLATION_SHIFT`,
  translation table count) are asserted against the engine where they are captured.
- `SNAP_BALLISTIC_MAX_TICS` must stay under the client's missile extrapolation cap; that is asserted
  in `entities.cpp`.

## Engine hooks

None: a header of definitions. Its consumers are the module's own files; `p_local.h` includes it for
the polyobject classes' snapshot members, and stock files otherwise take only constants from it
(`cl_main.cpp` the sound flags and mover classes, `sv_commands.cpp` the mover classes and the
`SnapFrame` it sends in SnapJoin, and the mover sources `p_plats.cpp`, `p_ceiling.cpp`,
`p_floor.cpp`, `p_doors.cpp`, `p_pillar.cpp` and `po_man.cpp` their own class values).

## Tests

`format_test.cpp` holds the codec cases as GoogleTest: the quantizers, the map's frame on a nonzero
origin and the checksum's narrow pin, the id codec, the record and event lengths, the group-length
agreement, the record size constants, the mover records, the local block, the event codec and the
cosmetic event codec. In the engine, `fua_snap_selftest` keeps what needs a game binary -- the time
base, and the decode check through the game's own `ApplyDelta` ([selftest.md](selftest.md)) -- and
the reference test decodes the committed recordings through this header byte for byte
([tools.md](tools.md)). The decoder fuzzer drives the readers with mutated input
([fuzz.md](fuzz.md)). `tools/snapshot/csnap2.py --verify` checks the Python readers' group table, id
encoding, event types and mover classes against this header.
