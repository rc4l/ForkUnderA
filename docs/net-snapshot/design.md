# Snapshot/Delta Netcode for a Zandronum Fork
## Design document — written against `zandronum-stable`, branch `default`
**Status (November 2026).** Phases 0-5 complete (§9). Movers (§7.1): `DPlat`, `DCeiling`, `DFloor`,
`DDoor`, `DElevator`, `DPillar` and polyobjects are on the wire, the scroller and pusher fields ride
their commands (§7), and under the mover gate the server sends none of their legacy state. Phase 6
is under way: cosmetic spawns travel as events and mirrors make their own client-side spawns (§6.3).
Ghost ages run from the server's clock (§6, §8). §9.1 carries the measured baseline, §9.2 the
invariants implementation established, §9.3 what is open, §9.4 the recorders those measurements
depend on, and §10.1 the cvar triage.

This document specifies a replacement for the event-driven replication layer (the ~1,884 hand-placed
`SERVERCOMMANDS_*` calls across 121 files) with a server-authoritative snapshot/delta architecture
in the Quake 3 / Source lineage. It assumes the fork explicitly does not care about protocol, demo,
or behavior parity with Zandronum, GZDoom, or any upstream.

---

## 1. Goals and non-goals

**Goals.** Eliminate the missing-update class of desync bugs structurally rather than
instance-by-instance; make replication a property of *state*, not of code paths; reduce the client's
gameplay surface to rendering plus one predicted entity; keep 64-player servers on actor-heavy maps
inside a configurable bandwidth budget; migrate incrementally on a dual stack so the game is
playable at every phase.

**Non-goals.** Wire compatibility with any existing client or server; demo compatibility with old
demos; preserving client-side simulation of remote actors; lockstep determinism (explicitly rejected
— input delay scales with ping, rollback requires per-tic world checkpointing that this codebase's
serialization cannot do at 35 Hz, and every client pays full-sim CPU that slaughter maps weaponize).

**The core inversion.** Today the server and every client all run the world, and the server sends
corrections when someone remembered to write one. After this change, the server is the only machine
that runs game logic. Clients hold a table of *entity ghosts* — render-state records, not `AActor`s
— drawn at their newest authoritative state. The only predicted entity is the local player, using
the existing reconciliation loop in `cl_pred.cpp`. There is no second simulation, therefore nothing
to desync.

---

## 2. Architecture overview

```
SERVER (35 Hz)                              CLIENT
──────────────                              ──────
G_Ticker() — full sim                       input sampling → usercmds (unchanged)
   │                                            │
BuildFrame()                                render loop (uncapped)
   ├─ sweep g_ActorNetIDList                    │
   ├─ per-client: interest filter           ClientReceive()
   ├─ delta vs last-acked frame                ├─ decode delta vs ref frame
   ├─ quantize + write                         ├─ update ghost table
   └─ zstd → UDP                               ├─ draw at newest state (§6)
                                               └─ ack frame number (piggyback on usercmd)
RELIABLE CHANNEL (kept, shrunk)
   events: sounds, obituaries, chat,        local player: cl_pred.cpp loop vs
   ACS HUD, map change, join/leave          server-acked state (existing machinery)
```

Two channels, cleanly separated by a single rule: **state goes in snapshots, events go on the
reliable channel.** State is anything with a current value that a late-joining observer needs
(position, health, sector height). An event is a thing that *happened* (a sound played, a line
printed, a script pulsed the screen). Today's protocol mixes these freely; the mixture is why the
full-update path (`SERVER_SendFullUpdate`, sv_main.cpp) is a separate, drift-prone
reimplementation of world state. In the new model, a full update **is** a snapshot with a null
reference frame — one code path, always exercised, never stale.

---

## 3. The entity state block

A ghost needs what the renderer needs, plus what prediction and audio need. Nothing else — no
states, no flags words for gameplay, no AI fields.

Fields are grouped; the mask byte marks which groups a record carries.

| Group | Bytes | Fields |
|---|---|---|
| header | 2-4 (+2 on enter, +2 more with a shooter, §6.3) | id (varint gap, below), mask (8), classId on enter |
| POS | 4 | x, y at 1 map unit, signed 16-bit each, relative to the map's frame (below) |
| POSZ | 2 | z, separate because it is unchanged on 78.6% of position records |
| ANG | 3 | angle (16), pitch (top 8 of 10 — `WirePitch` is what compares it, §9.2) |
| VELMISC | 9 | velx/vely/velz at 1/4 unit, misc (16 bits), packed `effects` |
| EVENT | 14 | id, mask, tic, `qstate`, `qtics`, x/y/z — a branch, not a state record |
| APPEAR | 2 or 4 | qtics, qstate; narrow when both are small (§9 item 4) |
| RENDER | 4 | translation (16), renderstyle index (5), alpha (6) |

Worst case 32 B; the emitted stream measures 8.0 B a record and 6.2 B on the wire (§9). The checksum
covers exactly the fields `DiffMask` compares - `qtics` and `spriteframe` in neither (§9.2).

**`classId` is `getActorNetworkIndex()`, and nothing else about a class crosses the wire.** The
client resolves it through Zandronum's existing `NETWORK_GetClassFromIdentification()`, an index both
ends already share because it derives from the loaded WADs, so defaults, `Speed`, radius and height
are locally available - and the render shim (§6) makes each mirror a real `AActor` of that class, so
they are simply present. An earlier design serialized a class table at map load; the shim removed the
premise. `ghost_SpeedForClass` caches by class id on that basis: the mapping is fixed for the process,
unlike anything keyed on entity id.

**`misc` is 16 bits and FULL in the low byte**: corpse, invisible, floorclip, two of waterlevel,
ballistic, steering, crouch-present. Bits 8-11 are the state checksum, 12-15 the crouch factor. All
eight low bits are named in one place; four were bare literals in two files, which nearly handed one
out twice.

**The mask byte is full, and that is no longer a constraint worth designing around.** Two escapes
exist and neither grows it. **A new bit needed on MANY records widens an existing group** - one byte
on the records carrying it, which for VELMISC is 5-6% of them; this is how `effects` landed. **A whole
new record shape needed RARELY becomes a self-describing event type** (§9 item 2), which is how a
second id space or a looping-sound record arrives. What would actually need a wider entity mask is a
new per-entity state field, present on most records, that fits in no existing group - and that has not
appeared in four phases.

**The id is a varint gap from the previous record in the same payload**, the first record of a
payload carrying a raw 16 bits. Three properties are load-bearing. The gap is **mod 2^16**, so one
rule covers every record rather than only the sorted ones - departures, psprites and events share the
first chunk in their own orders, and a backwards step is then a large gap rather than an error. The
base **restarts at every chunk**, or a chunk becomes undecodable without its predecessor and
independent datagrams become an ordered stream. And the first record is **raw rather than a varint of
its own id**, which is what makes the chunker's overflow bound exact: cuts are drawn over ESTIMATED
lengths, so a record promoted to open a chunk changes by +1 or -1 against a fixed 2 bytes and
`cut = chunkBytes - 1` bounds it. A varint could cost 3 against a 1-byte estimate. The reader bounds
the varint at three bytes rather than trusting the writer, which is what keeps decoder
fuzzing meaningful.

**APPEAR has two widths**, 2 bytes when the state's depth is 0, its offset < 256 and `qtics` < 128,
and 4 otherwise, selected by the top bit of its first byte. Self-delimiting, so it costs no mask bit.
Both sentinels are safe by construction rather than by a clause: `SNAP_STATE_UNKNOWN` is `0xFFFF` so
its depth field is 7, `qtics` 255 fails the qtics test, and the narrow form can only produce `qtics`
0-127 and `qstate` 0-255, so neither can be manufactured by it.

**A record's length is therefore no longer a pure function of its header, and that is permanent.**
What every reader owes:

- **Bounds-check with the reader's length, never the mask's.** `GroupBytes` is the WIDEST a mask
  can be and sizes buffers; checking a record against it rejects a valid narrow record that ends a
  payload, and the narrow width under-reads and misaligns everything after it. Three functions answer
  what was one question - widest, what the writer will emit, what a reader must consume - and they
  agreed while every group was fixed, which is why they were separated then.
- **A group cannot be skipped by a constant**, and anything reading a group after APPEAR - RENDER is
  the only one - must compute APPEAR's width first.
- **Offset arithmetic into a payload is gone.** Finding record N means walking 0..N-1, which Phase 7's
  demo format has to account for if it wants random access.
- **A second variable group must be appended after the existing one**, or the fixed prefix computation
  becomes a full sequential walk. Group ORDER now carries length as well as the Z-after-POS reasoning
  of §9.4.

**Z has its own mask bit because it usually does not change** - monsters walk on flat floors - and
VEL merged with MISC to pay for it, the mask byte having been full. VEL+MISC costs 0.87% of payload
against the 8.8% the Z split saves, cheapest of six pairs measured. A presence flag inside POS was rejected because it would make a group's
length depend on its contents. **v22 spends that property deliberately on APPEAR** and it is now a
constraint rather than an invariant: see §9 item 4 for what every reader owes as a result.

**Position is 1 unit and ROUNDS to nearest.** A 1-unit grid guarantees half a unit of error either
way; the quantizer only chooses its SHAPE, a constant bias or one whose sign flips across a boundary.
Truncation's one-sidedness was a diagnostic asset while animation defects were being found by the sign
of an error rather than its size; those are found. In 3.3's 32-bit `fixed_t` the rounded domain
spans the narrow frame at origin 0 exactly, with only the top value clamped. **Velocity rounds the same
way**, at 1/4 unit: truncating read every negative component up to a step low and gave zero an
asymmetric band, so a still actor with a tiny negative velocity crossed as -0.25 u/t.

**Crouch is a VALUE, four bits, not the direction the legacy path sends.** `MovePlayer` carries a
boolean and each client integrates `crouchfactor` through `P_CrouchMove`, whose step is gated on a
local `P_TryMove` - so what got drawn depended on a collision test the client has no authority to
run. How far a player is crouched decides whether a shot passes over them. Worst round-trip error is
half a step, ~0.9 units on a 56-unit player, finer than the ~12 tic motion. The client ASSIGNS it
rather than integrating, which is idempotent at frame rate where `P_CrouchMove` would crouch three
times too fast.

**Positions are relative to the map's frame**, an origin in whole units and a width per axis that the
server picks once per map from its extent and SnapJoin carries (`SnapFrame`). The origin is 0 while
the extent and its margin fit ±32k, which in 3.3 is every map, so the bytes are those of the
frameless format. It exists for ForkUnderA's 48.16 `fixed_t`, whose maps can pass 32,767 units: an
axis spanning past 65,535 units is wide, 3 bytes (±8.4 million units), and every position on it takes
a byte more, which on the six reference recordings would grow a frame's payload by about 15-22% wide
in x and y, at no cost while a map fits. A width is per map rather than per record, so a group's
offset stays a function of the mask, given the map's frame. Every position field takes the frame and
keeps its own rounding: POS, POSZ and the branch event round, the cosmetic events and the point sound
floor, and the local block and polyobject x/y and targets are 16.16. Because the origin is whole
units, rounding `v - O` is rounding `v`, minus `O`. A value outside the frame clamps and is counted on
the `[snap]` line, never wraps, and the state checksum mixes in a position's high half only when it
is not the sign extension, so a narrow map's checksums are unchanged. Mover planes and polyobject
distances and speeds stay absolute, which leaves a documented limit (§9.3).

Quantization is global rather than graded by distance, since a near/far encoding needs a per-record
form flag POS has no room for. Unmeasured, and coarsening position also coarsens the DIRTY TEST - an
actor moving under a map unit does not dirty POS at all - so the win is not obviously positive.
Quantization does not accumulate, being absolute.

**What is deliberately absent:** DECORATE state pointers, gameplay flags, target/tracer, inventory,
tid. Clients animate from the state index and phase (§5.5), streamed by the server as the
server-side state machine runs. This is the radical simplification: every `NETWORK_InClientMode()`
early-return in p_enemy.cpp, p_interaction.cpp, p_mobj.cpp, g_doom/*, g_heretic/*, etc. — the entire
"client runs gameplay code but not really" layer — gets deleted, not maintained.

### 3.1 Entity ids and generations

`AActor::NetID` is `unsigned short` with a monotonically cycling allocator (`IDList::getNewID`) —
65,535 slots, no reuse detection. The new id is 20 bits of slot + 6 bits of generation, bumped on
every slot reuse and carried in every entity reference, so a client holding a stale (slot,
generation) pair *detects* staleness instead of destroying whichever actor now occupies the slot.
Deferred to Phase 7.

**Player ids are reserved and SHIPPED**, as `id = playernum + 1`; id 0 is the engine's "no NetID"
sentinel. **Needed by Phase 3, not Phase 7** - a psprite record names the player's reserved id, so
both ends compute the inversion through `reservedIDForPlayer` rather than assuming the arithmetic.
The reservation is held by the CURSOR alone: `_firstFreeID` starts past the range and only ever
increments or wraps back to it. Marking the entries not-free as well made `useID`'s collision warning
fire on every spawn, which is the one guard the engine has against this class of bug.

**What it fixes.** Tracker 2552 is a broken invariant - `playeringame[i]` true while `players[i].mo`
is NULL - reached by two paths, and reserved ids close one. All three client spawn handlers DESTROY
whatever actor holds an incoming id; `CLIENT_SpawnThing` guarded against hitting a player body but
`#ifdef _DEBUG` and only for the console player, and `CLIENT_SpawnMissile` had no guard at all - **a
protection that names the failure exactly and is compiled out of every shipping build.** The other
path is untouched: `AActor::Die` destroys an actor with no Death state and has no player exception,
and the client runs `Die()` locally, so a player class whose Death chain is missing or ends in `Stop`
loses its body on every machine at once. GZDoom fixed that in 3.4.0; **Phase 3 must decide what a
player MIRROR does with such a death rather than inherit it.**

**Reservation creates a collision of its own.** The id belongs to the player NUMBER, so on every
respawn it legitimately hands over from corpse to new pawn - and the kill site above fired on all of
them, destroying the corpse's inventory and leaving `FilterCoopRespawnInventory` to transfer from an
empty source, so the player respawned with nothing. **Any handler that treats "id already held" as
grounds for destruction has to learn the difference between a collision and a handover.**

**The handover moves the previous body to a fresh id**, announced as `SVC2_SETTHINGNETID` ahead of the
`SpawnPlayer` that takes the reserved one; a body moved to 0 could not be destroyed or changed online
and stayed on every client after a respawn. An actor's `NetID` field and the id table are separate
state, and `AActor::Destroy` frees by the actor's OWN field, so any move must update both or the
corpse later frees the living player's entry and every command addressed to that player silently finds
nothing. **Voodoo dolls** reach `P_SpawnPlayer` with a real player number, so a doll would claim and
release a live player's id; `SPF_VOODOODOLL` joins the existing `SPF_TEMPPLAYER` for that. Inventory
that travels between maps takes a new id at the load, as the pawn does, since the load reissued its
old one.

**The capture has three exclusions.** Dolls zero their `NetID`. **Spectators** keep their reserved id
while the server strips `MF_SOLID` and sets `RenderStyle` to `STYLE_None` - neither reconstructible by
a mirror, so a captured spectator drew as a body and blocked the player inside `fua_csnap_collide_dist`.
Excluded rather than described, since nothing outside that player needs the pawn; spectate and
unspectate are a leave and an enter. **Owned inventory** is out of the world and reaches the client by
class through the inventory commands, so a pickup is a leave and a drop an enter; a respawning item
stays in the world unowned and is still captured.

**Reservation is not a general fix for id reuse** - it covers 64 of 65,536, and a tracer's id is
reused within seconds (§9.2). The general case needs the generation bits above.

## 4. Snapshot protocol

**Frames.** The server tags each built snapshot with a 32-bit frame number (= gametic). For each
client it retains a ring of the last `snap_history` (default 64 ≈ 1.8 s) frames it *sent to that
client*, storing per-frame: the entity set and each entity's post-quantization field values.
Quantized values are canonical — deltas compare quantized against quantized, so encoder and decoder
agree bit-exactly and there is no float/fixed drift channel.

**Delta encoding.** Each packet says "this is frame N, delta'd against frame R," where R is the
newest frame the client has acked. Encoding walks the union of entity sets of N and R in id order:
entities in both → per-group change bits + changed fields; in N only → full block (enter); in R only
→ tombstone id (leave). If the client's ack is older than the history ring (heavy loss, alt-tab),
the server sends against R = null: a self-contained baseline. **Loss recovery therefore requires no
retransmission of state at all** — the next delta is simply computed against an older acked frame.
This single property removes the entire "was that update reliable? was it dropped? was it
reordered?" bug class that the current `NetCommand` reliable/unreliable split exists to manage for
state.

**Acks are keyed on a per-client DATAGRAM SEQUENCE**, not on the frame number. A frame-keyed ack
folds every datagram sharing that number into `believed`, including ones that never arrived - which
is invisible until a frame spans more than one datagram, and then silently marks lost records as
delivered. The sequence is allocated at commit, one per chunk, never rewound. Clients piggyback the
newest received sequence on every outgoing usercmd packet, costing 4 bytes and no extra packets.

**The delta reference is `sent`, not `believed`** - what the client will hold once everything in
flight arrives, rather than what it has acknowledged. Deltaing against the acked state re-sends every
group for a whole round trip: **~14% of the packet under netem 100 ms + 5%**, and only 1-3% on a LAN,
which is why the LAN figure was never the number. Behind `snap_optimistic`.

The reference is a full `SnapEntityState` array per client, 2.25 MB. Three cheaper designs failed: a
sparse map keyed on "differs from `believed`" has no owner for eviction; an index into `pending`
cannot survive compaction, and a lost older frame poisons a newer record anyway; and packing the
struct would create a third hand-maintained field list beside `DiffMask` and
`StateChecksum`, which have already drifted three ways once (§9.2). **Repair is what keeps the
optimism honest**: a frame the ack history proves lost reverts those entities to `believed`, whole
entity rather than the lost frame's groups, because a newer in-flight record was diffed against a
reference that assumed the lost frame arrived, and then **rebuilds them over what is still in
flight**: `sent` is `believed` with every pending record for the entity merged over in sequence
order, which is what the client will hold once those land. Reverting alone forgot them, so a group
an in-flight record changed that changed back afterwards matched `sent` and was never re-sent, and
the client kept the in-flight value until the group next changed: about 0.14% of settled checksums
under 5% loss, 0.53% with an ack delay of 3. Psprites rebuild the same way from their newest record
in flight; movers need nothing, a reverted seq re-sending the whole body.

**A frame counted lost without proof may have arrived** - older than the ack's 32-frame history, or
behind an ack gap wider than it. Presence is set only on proof, so an enter in such a frame can
reach a client the server believes empty, and if the entity then dies before a later enter is
confirmed, no leave is owed and the client draws it until the map changes. Such ids are marked and,
once out of the world, sent a leave through the ordinary leave path; the mark clears when an enter
or a leave for them folds, or with the world. **A forced re-baseline strands ids the same way**: the
server forgets the world while the client keeps its ghosts, so every id it may hold - present,
marked, leaving, or entered in a frame still pending - is marked again after the forget; a map
change clears both sides and marks nothing. A leave for an id the client lacks is counted as
redundant but still stamps the id, so an enter it overtook is refused when it lands. And the client
refuses every datagram the server can no longer read an ack for, one past its 32-datagram history
as well as one the server declared lost inside it, so a late enter can no longer make an entity
the server holds absent. A new stream begins only at `SnapJoin`, never on a sequence that jumps
backwards, since a datagram held back past the history would reset the client's ack state, and with
it the bits of datagrams just received, which the server would then read as lost. The one other
restart is a guard: `SNAP_ACK_HISTORY` refusals past the history in a row, with nothing accepted,
which only a newest sequence from another stream produces (an old server's datagram landing after a
restarted one's `SnapJoin` on the same map stamp), and which a held-back datagram, refused alone
among datagrams on time, cannot. The server's half of that case: an ack naming a sequence it has not
sent is ignored, or that client's acks would all read as stale until the next map.

**The ack fold resolves frames in sequence order.** A clear history bit is UNKNOWN until
`SNAP_REORDER_WINDOW` (8) datagrams behind, and the fold stops at the first unknown frame, so
`believed` stays the in-order fold of what was delivered - what the client holds. Read as lost, a late
datagram was repaired after the client had applied it. An ack repeating the last sequence is
processed, since it carries the history that resolves a late frame. **Presence follows the same
rule**: an entity back while its leave is in flight is sent enters until one newer than the leave is
folded (a per-client leave marker), since the client cannot apply dirty records to an id it has just
removed.

**The checksum has three outcomes**: verified or DIVERGED; `unsettled`, a newest record behind a
missing datagram the server had not yet repaired when it built it, whose mismatch is that loss; and
`late`, not checked, since it describes a state the client no longer holds. **Settling follows the
server's repair, not a count of datagrams**: each datagram echoes how far behind it the newest ack
the server had processed is (the ack lag), and a record is settled only once that ack is
`SNAP_REORDER_WINDOW` past every datagram missing before it (`ComputeAckSettled`), which is where
the loss was resolved and repaired. Counting 8 datagrams back on the client instead settled records
the server built before the ack proving the loss could reach it: with three datagrams a frame and a
round trip of one to two frames, DIVERGED read about 0.5% of settled records under 5% loss, peaking
just past the window and moving outward with a longer round trip. With the echo and the repair's
rebuild over what is in flight (above), DIVERGED reads 0 of 53,515 settled records under 5% loss, 0
of 38,269 with an ack delay of 3 as well, 0 of 11,396 under 20% loss and 0 of 615 under 30%
(2026-11-09). Heavy loss leaves few records to check, since nearly every one then has a loss within
a round trip behind it and waits; there the server's reference check (`fua_snap_debug_refcheck`),
which tests the invariant itself, `sent` equal to `believed` with the records in flight merged over,
read 0 differing over 5.6 million checks of ids in flight across 2,566 repaired datagrams at 30%.
**A datagram arriving after its loss was declared is refused**: the echo cannot see it, since the
server repaired it as never delivered while the client would hold its values. Once the client has
acked `SNAP_REORDER_WINDOW` past a missing datagram the server will declare it lost, so the client
neither applies it nor ever sets its bit (`ComputeAckTooLate`), and the server counts any bit set
for a datagram it has declared lost ("set after declared lost"), which then reads 0 bar datagrams
declared lost without proof that had arrived. `fua_snap_debug_holdback` reproduces the case;
reordering inside the window, reproduced with Clumsy's out-of-order at 75% (about 30% of datagrams
confirmed out of order at the server, nearly all within their own frame; at most about 3% arriving
from an older frame at the client, worst 1 frame back), reads 0 DIVERGED.

**The header binds each datagram to a map, a reset and the reliable stream.** Beside frame, game tic
and sequence it carries the map stamp (map loads on this server), the reset count (§7.1), the ack
lag (above) and the newest reliable sequence sent. A client accepts only the stamp of the map it
joined, learned from `SnapJoin` on the reliable stream, and before that applies and acks nothing:
the server marks a client spawned on its level authentication, ahead of `MapLoad`, so the next map's
frames can arrive before this one is torn down. The reliable sequence closes a tail loss the gap
test cannot see - with snapshots carrying the world, the reliable stream can stay quiet for seconds
after its last packet is lost - so the client requests anything up to the announced sequence it has
not received.

### 4.1 What stays on the reliable channel

Sounds (with entity-id attachment so they follow ghosts), print/chat/obituaries, HUD messages and
ACS HudMessage, intermission and map change, join/leave/team/frag bookkeeping, and
cvars/serverinfo. This is a small, stable command set — on the order of 30 commands, versus the 186
`::Execute` handlers in cl_main.cpp today. The existing `NetCommand`/reliable-buffer machinery is
kept as-is to carry it.

### 4.2 The local player

The local player is the one entity the client simulates, and the machinery already exists:
`cl_pred.cpp` re-runs `P_PlayerThink` from the last server-acked state through pending usercmds. The
block is what reconciliation compares against, and it is **exactly what the replay consumes as server
truth**: position, velocity, jumpTics, and the client gametic naming the ring slot they belong to.
Read out of `cl_pred.cpp` rather than copied from `MoveLocalPlayer`, which also carries a
lag-detection clock and a spectator branch that are not prediction inputs. Four fields an earlier
draft named are absent, each verified unread by the replay: viewheight, on-ground (derived from
`z == floorz` and `MF2_ONMOBJ`), crouch (saved locally, deliberately not recalculated during replay),
and the weapon timeline. Adding any would be a NEW wire field, not a migration.

**Unquantized, and that is the acceptance criterion rather than a preference.** Reconciliation error
measures 96-100% under ONE MAP UNIT and `QuantPos` rounds to one map unit, so routing the block
through the entity record would make the census read the quantizer, and read it worse by
construction. It carries no server gametic either: the datagram already has one, and two definitions
of one clock is the defect §9.2 names twice.

**It rides a reserved pass ahead of the priority drain and is never rationed**, because a frame
without it leaves the reconciliation ring cursor where it was, and a stalled cursor is the measured
cause of every uncomparable correction. Its gate covers `MoveLocalPlayer` and `SetLocalPlayerJumpTics`
together - they write the same cursor and one is buffered where the other is not - and the authority
bit rides in the RECORD rather than in a client cvar, so whichever machine decides produces the bit.
Spectators are excluded, matching sv_main.cpp's own exclusion: their position is client-owned and a
block would fight it. **The command buffer is deliberately not reproduced** - its only measurable
effect was holding corrections to one per tic, which changed WHEN they landed and never how wrong
they were; that pacing absorbs a beat between the two 35 Hz clocks (§8), not network variance.

**Measured against reconciliation error, which is the bar.** `fua_csnap_predcensus` reports XY error under
one map unit on 96-100% of corrections at either impairment, Z always sub-unit, replay depth 0-1 on a
LAN and 4.5-6.0 under netem. **Quadrupling replay depth barely moves it**, so each predicted tic is
essentially exact and nothing compounds along the chain; the residual correlates with the fight rather
than with depth, so it looks like INTERACTION - being blocked, pushed, stepped on - rather than
movement integration. Against that baseline the block is BETTER than the legacy path at real replay
depth: worst-case XY collapses from 7-12 units to 1-3 and worst Z reaches 0 in three impairment
profiles of four, with zero divergences in shadow mode over ~12,000 comparisons.

#### Psprites

**The local psprite simulation stays.** `P_PlayerThink` calls `P_MovePsprites` once per live tic and
skips it while the prediction loop replays (it sits in `P_PlayerThink`'s
`CLIENT_PREDICT_IsPredicting() == false` block), so the weapon runs ahead of the server unreplayed,
which is what makes firing, refire and weapon switching instant. Only what reconciliation compares
AGAINST moved to the snapshot.

**Remote psprites are NOT deleted, and stream for every player unconditionally.**
`R_DrawPlayerSprites` draws `camera->player->psprites`, so spying shows another player's weapon with
their `ReadyWeapon` and their bob; both enforcement sites for `sv_disallowspying` exempt
`bSpectating`, so in survival the dead watch the last player alive with the flag off in exactly the
case that matters. A subscription is not available - `players[].camera` is client-side with no `CLC_`
carrying it - and would still leave the first frames after every spy switch stale. Streaming is cheap:
a weapon in Ready is one state, so two layers of (`qstate`, `qtics`) delta-code to nothing in steady
state and spike only on fire and switch. The record carries the weapon's class id, so a client
resolves a psprite state without holding the remote player's `ReadyWeapon` at all.

**The stream cannot drive the animation, so both mechanisms stay.** It delivers
correctly - `unresolved` reads 0 across firing, switching and quick-switching, both ends' counters
agree, cost is 0.7-1.3 B/tic/player - but `P_Ticker` runs `P_PlayerThink` for every player but its
own, so the stream has always been a second writer over a machine already producing the right answer,
and every figure above is a counter rather than a picture. **A stream that samples state cannot drive
an animation unless the receiver can advance it**: gating the local advance froze a spied player's
weapon outright, because the stream is CHANGE-TRIGGERED and nothing decrements `psp.tics` between
records. That is the problem §5.5 solved for actors with `qtics` and a local clock, and giving the
receiver the same clock - predicting the weapon layer locally between records - is the ONLY thing that
would let the stream stand alone. It is Phase 6 work with a wire cost, to be revisited only if
something needs it; until then the local machine drives the animation and the stream serves the
players it cannot reach.

**And on the branch that prompted it, the local run is RIGHT and the stream is wrong** (§6.1): the
weapon's ready pose branches on a token a CLIENTSIDE script grants from a replicated userinfo cvar, so
every client resolves it correctly and the server, which never holds the token, is permanently on the
default branch. What the local run cannot do is produce side effects safely - it starts looping sounds
whose stop it may never reach - or run without input, which is why a spied weapon needs the stream at
all. **Three replacements each failed differently**: gating froze the weapon; advancing without
actions lost `sx`/`sy` and the flash layer, which are OUTPUTS of actions rather than of state; and
refreshing on an interval re-asserted the server's wrong branch once a second. So the duplication is
deliberate rather than unresolved: the local run is authoritative for the picture, the stream exists
for players the local run has no input for, and neither can be deleted without the receiver-side clock
above.

**Psprite records are NOT suppressed, and that is a decision.** ~56% of weapon-layer changes are
derivable by the walk appearance uses. A walker cannot predict a trigger pull or a weapon switch, and
unlike a monster's walk cycle there is no ordinary record to correct a wrong verdict - the layer state
travels only this way, so a bad guess is a weapon that stays wrong until the player next changes it.
That is the desync class §1 exists to remove structurally, and the legacy protocol carried an instance
for years (a quick switch to a rocket launcher charging ammo and firing without spawning the rocket).
A few percent of the channel does not buy reopening it; any future argument needs a MULTI-STEP
derivability census, measured from the last state actually sent rather than from the previous tic
(§5.5). Branch events are not needed either: weapon-layer change intervals are 44% at 2-4 tics and 32%
at 5-9, with the 1-tic bucket reading zero in every window, so nothing changes faster than the sample
interval.

**Prediction of hits stays server-authoritative** via the existing lag compensation: `unlagged.cpp`
already rewinds hitscans against stored positions. It gets *simpler*: with one simulator there is
exactly one position history, and the reconciliation ambiguities in the current code (which of
several client positions was "real") disappear.

---

## 5. Interest management and bandwidth

The load problem, stated honestly: slaughter-class maps can have **hundreds to a few thousand
monsters simultaneously visible** (huge open arenas with kilometer sightlines) and thousands more
staged in ambush closets. Distance-based *culling* is therefore wrong for Doom: distance does not
imply invisibility, and a ghost the server never sent is a monster the player can see popping into
existence. The design principle that survives contact with slaughter maps is:

> **Existence is generous; update *rate* is what gets prioritized.**

### 5.1 When does a ghost begin to be drawn? (the existence policy)

A ghost is drawable from the moment its *enter block* arrives in a snapshot, so the mechanism is
entirely a server-side inclusion policy, and that policy is **awake-state, not distance**:

- **At join**, the baseline enumerates the map's entire live population, including the
  5,000-monster closet army, so from that moment the client can draw anything the map contains and
  pop-in is structurally impossible. Enter blocks measure ~26 B raw and compress well, being highly
  self-similar; what it costs in practice is §9.3's join parity figure.
- **Newly spawned entities** (Pain Elemental skulls, ACS spawns) enter on the tic they spawn, at
  high priority — spawn is exactly the moment visibility matters most.
- **Hard existence culling** exists only as an overload valve (`snap_exist_radius`, default
  infinite) with enter/exit hysteresis (exit radius > enter radius)  so boundary entities do
  not thrash, and should not engage on any realistic budget.

**Idle entities are cheap but not free** (§5.4). Dormant Doom monsters are not static: their
Spawn-state `A_Look` loop cycles sprite frames, so a fraction of any dormant population dirties every
tic. That fraction is set by the WAD, not the engine - a two-tic look loop dirties five times as
often as stock's ten, a single-frame Spawn never dirties at all - so the ~100% of a quiet map's
traffic measured on MAP13 generalises no further than MAP13.

It is a scheduling cost rather than a bandwidth one, since the budget discards ~90% of dirty records
before the wire. What it does is put appearance and position in competition for the same frame: every
record cycling a dormant sprite is one not spent on a monster walking toward the player. The answer
is **local animation on MID and FAR mirrors** (§5.5), which suppresses nothing and costs no wire
bytes.

**A frozen animation on a distant sprite is not invisible**, and the cosmetic exemption was built on
the opposite assumption. A client recording measured 97% of entities beyond 4,096 units with
animation more than five seconds stale - permanently frozen - and the effect was visible from ~1,024
units outward; `fua_snap_animref_interval` now bounds it at ~3.3 s. It read as a dead server rather than a
throttled one only once `fua_snap_nolegacy 3` removed the client-side actor that had been ticking
its own state machine and supplying smooth animation for free. **A migration hides its own
regressions until one stack is switched off**, so raising the suppression gate is a prerequisite for
honest evaluation rather than an optimisation milestone - and **do not reason about what a distant
sprite needs; measure it.**

### 5.2 Rate tiering (where the budget actually goes)

Each tic, per client, every *dirty* entity is scored on distance from the view point, view-direction
weighting, is-player / is-projectile / SKULLFLY boosts, recently-spawned and recently-damaged
boosts, and time-since-last-sent aging. Entities are written in score order until the per-client
byte budget (`fua_snap_maxbytes`) is spent.

**Records per frame is the quantity that matters**: at ~9 B a record and the shipped 4,500 B budget,
~400 records/frame against a contested set of ~2,000. §9.1 carries where those records currently go
and what each band costs.

**Age must measure time spent wanting to be sent and losing, not time since last transmission.** The
aging guard was originally tested ahead of everything including the cosmetic test, and a withheld
record accrues age while it is withheld - so it eventually tripped the guard and jumped to the
*highest* band in the system, ahead of players, enters, and the monster in the player's face. The
exemption did not reduce distant-animation traffic so much as delay it and deliver it at maximum
priority. The guard now runs *after* classification, so it only rescues records that competed on
merit and lost. An entity with nothing outstanding is not starving, and stamping it is what makes
the aging-forced diagnostic mean anything.

**Any band merely ranked last on a saturated budget is a band that never sends**, so a
bounded-refresh guarantee requires a reserved share. This is the single most-repeated failure in the
project: it accounts for the aging guard above, for the cosmetic band, for the appearance refresh
band given the mop-up leftovers (§5.1), and for two patches that lifted a suppression and expected
service to follow.

**Refresh rate is governed by contested-set size, not by the ack round-trip.** It is tempting to
think the round trip divides the refresh floor directly; measurement refuted it. A re-send happens
only when the priority rotation returns to an entity before its ack lands, so with contested far
below capacity everything is sent every frame anyway (~33 Hz held across ack delays of 0-8 frames and
0-10% loss), and with contested far above it the rotation period vastly exceeds the ack window (60-73%
of records fresh under the same impairment). Only contested ≈ capacity erodes the fresh fraction, and
smoothly. **Latency and loss are close to free for refresh rate at both ends of the load range**; the
planning quantity is contested-set size against per-frame capacity. Emitted-record rate overstates
useful throughput whenever re-sends occur, so achieved per-entity Hz and fresh percentage are
reported separately.

**Which groups carry the bytes**, on a MAP13 fight: `pos` 47% of records / 46% of group bytes, `appear`
75%/49%, `ang` 17%/5%, `vel` 1%/1%. Appearance and position are ~95% of group bytes and roughly half
each. **Velocity is withheld only for the gravity class**, which derives it (below). The far tier used
to strip it, which was no bandwidth lever - Doom monsters move via `P_TryMove` and report `velx == vely
== 0` - and left a body flung and stopped at range holding its last velocity. Removed for §6's velocity
extrapolation: VELMISC then measures 4-5% of emitted records and 6-9% of group bytes on MAP13, with
records/frame and kB/s inside the baseline.

**Two suppressions carry the scheduler**, and both are record SHRINKS rather than reallocations -
which is why they worked where six redistribution levers measured neutral. A shrink's gain is bounded
by `bytes freed / total budget`, not by the number of records affected.

**Appearance suppression** (`fua_snap_appear_predict`, on) advances `sent.qstate` by the tics
elapsed on the group's own clock and withholds APPEAR when the result matches. Derivability degrades
with staleness: 96-97% under 250 ms, 38-49% past a second. **It is exactly wire-neutral** - ~8% of
payload saved against ~8% of compression lost, APPEAR being the most compressible group in the
packet - so what it buys is records per frame and contested backlog, not bandwidth.

**It is OPEN-LOOP with respect to the mirror.** The server walks from `sent`, what it believes the
client holds; the mirror animates from what it last DREW; nothing closes that loop, and the v15
checksum hashes the state the client will hold rather than the mirror's (§9.2). A divergence persists
until a record the walk cannot derive forces a send. A shared `WalkChain` does not answer this -
it only guarantees both ends walk the same way from the same start. **Measured on the mirror** via
the exempt carve-out: suppression off reads 99-100% at every tier, on reads 91-98% near and mid and
**80-86% FAR against an exempt 100%**. That gap is the price and it is open (§9.3).

**Its reach is narrower than the band figure suggested.** A decision is attempted only while elapsed
stays inside `SNAP_APPEAR_MAX_TICS` (70), and the held population is served at ~115 tics, so **75% of
decisions on it read `unwalkable` before a walk runs**. Suppression does nearly all its work on
entities served often enough to keep elapsed small - which held monsters never are and a player, at a
decision per tic, always is (**93-97% held**). The bound governs the FORCED-SEND rate, not the hold
fraction: swept 70 → 9, holding moves only 96-97% → 84-88%. It stays at 70.

**Position suppression for ballistic missiles** (`fua_snap_missile_predict`, tolerance 16 units)
extrapolates a flagged missile through the same function the client sampler uses and omits POS while
the two agree, bridging at most 14 tics. +14-18 records/frame; the band's rate doubles because a
predicted missile stops being owed. Tolerance swept 8/16/32 and indistinguishable. **All missile
figures are monster-fired projectiles**; the player's own are §9.3's separate population.

**The gravity class is bridged by a law both ends compute**: after n tics from a record,
`z = z0 + n·v0 - g·n(n-1)/2` and `vz = v0 - n·g`, `P_ZMovement`'s order (`n+1`
under `ZACOMPATF_OLD_ZDOOM_ZMOVEMENT`). `g` is level × sector × class-default gravity × 81.92, the
sector read at the record's position (`LawAt`), all of it map, class or replicated level state.
POS, POSZ and VELMISC are withheld together while the predicted position is within tolerance, the
predicted `velz` within one quantum (0.25), and `vx`, `vy`, misc and effects unchanged; otherwise
all three go, so position and velocity share one reference tic. A record without VELMISC then means
"by the law" for this class. Runtime gravity changes, water, landings and bounces leave the law and
fail the check. `BarbatosPoison` fell from 0.72 records per tic alive to 0.18.

**Deep catch-up turns bridging off for both forms.** Elapsed time is counted in frames, so the
prediction falls short by the tics a frame spans: both forms hold almost nothing from `sim` ~6, and
every flagged missile is then sent each frame. Safe, at the cost of the records bridging saved.

**The appearance refresh quota is DERIVED**, from held population and `fua_snap_animref_interval`,
the way `TIER_REFRESH` derives from `SNAPCL_AGE_COSMETIC`; `SNAPCL_ANIMREF_MAX` is a ceiling on it.
It reproduces the fixed 15 it replaced, which is the point - 15 over 1,517 held was already inside
the 3.3 s bound, right only for the population it was chosen against. It must never reach 0, or the
far tier reports zero `bystate` samples and appearance suppression loses its unbiased measurement.

**The interval is DECIDED and stays at 3.3 s.** Faster refresh is free where the budget has surplus
and produces nothing: far-tier `bystate` spans 92.0-93.4% from 3.3 s down to 1.0 s, non-monotonic, and
the within-setting spread exceeds the between-setting spread. **Under saturation it is actively
worse** - mobbed, `cosmetic` collapses from ~116 records/frame to ~36 and far-tier correctness falls
92.9% to 90.4% at 1.7 s, with the gap to the exempt control widening 6.3 to 8.8. The extra records stop
coming from surplus and start displacing records that were not current. **`held` is a DEPENDENT
variable** - serving the band shrinks its own backlog, 1,750 at 3.3 s against 1,096 at 1.0 s - so
anything using it as a denominator has to know that. `SNAPCL_ANIMREF_MAX` (40) never binds.

Also available: **per-client snapshot rate divisors** (`snap_rate` 35/17.5/11.6 Hz) for narrow pipes.

### 5.3 Visibility assist (Phase 2 — the scale milestone)

Doom's BSP yields no cheap PVS and REJECT lumps are unreliable on modern maps, so v1 ships without
sector visibility. But the server already pays for line-of-sight constantly (`P_CheckSight` drives
all monster AI), and an *amortized* LOS sampler — sight-test a rotating subset of awake entities
against each client, cache results ~1 s — is cheap and makes a useful **priority hint**:
on-screen-and-visible entities outrank awake-but-occluded ones. It must remain a hint, never an
existence gate; the model's correctness comes from §5.1.

The general shape of the client budget mirrors the server budget: both are dominated by the
*resident population* (thinkers, collision) rather than the *visible* set, so §5.3's interest
management — shrinking what each client is responsible for — is the structural lever on the client
side too, exactly as it is for the server-side priority pass. The tick-skip fix is the
population-cost fix for the *tic loop*; a collision-scope fix for prediction would be its analogue
for the *player replay*.

### 5.4 Scale limits and overload behaviour

**Server CPU is a separate wall from bandwidth, and it arrives first.** A coop pass with ~2,800
monsters awake pushed the game sim past its tic budget and the unbounded catch-up loop entered a
death spiral: backlogs grew to **1,682 game tics per `SERVER_Tick` call**, ~60 wall-seconds per call,
with no recovery. The snapshot protocol caps bandwidth and reduces CPU - one capture sweep per pump
instead of per-monster command construction - but cannot fix simulation cost.

**The fix is to let game time dilate rather than let the loop chase.** `fua_snap_maxcatchuptics` (default
8) processes at most N tics per pump; a rerun at 2,989 awake pinned sim/pump at the cap, the world
running at ~70% speed and the client surviving a session that had previously timed it out. An
overloaded server degrades to slow motion instead of minute-long grinds.

**The per-client pass is linear in clients, and that sets the ceiling.** Measured at 1, 2 and 4
clients co-located in one fight, so a cost difference cannot be the scene:

```
snapshot ms/tic  =  1.7-2.0 (capture)  +  2.4 x clients        at ~7,200 entities, 4500 B budget
```

**Capture is flat in CLIENTS, not in ENTITIES**, and this document long read it as a constant. It
moved 1.86 → 2.04 ms as a nightmare-difficulty run grew the population 7,348 → 7,618, tracking entity
count and ignoring the budget entirely. So the first term is a function of the map's live population
and keeps growing wherever content respawns.

**~11 clients reaches the 28.6 ms budget and 16 is ~43 ms** - the hard limit of the design as
configured, before any game simulation. Two things degrade before that wall: the far bands starve
(`d3` 32 → 16-20 Hz, `cosmetic` 20 → 0.3) because game time dilates ~35% while each client still
receives its ~430 records, and the event share blocks hard (`EXPIRED unsent` 0 → ~2,000 a window)
because branch OBSERVATIONS scale with clients while the share is a fixed percentage of a per-client
budget - §5.2's fixed-percentage failure at a new scale.

**Three independent exits, none decided.** Smaller records (Phase 4) raise the record count at a
fixed budget, the only one that does not trade the snapping back. Fewer contested entities - §5.3's
LOS sampler and a tighter `fua_snap_cull_dist`, both unbuilt - cut the classification cost AND the budget
needed for a given rate. And **the per-client pass is embarrassingly parallel**: `g_capture` is
read-only during it and every mutable structure is per client, so what blocks threading is the
statistics globals and the file-static scratch arrays, not the algorithm.

**Per-client cost is EMISSION-dominated, not classification-dominated** - the opposite of what this
section assumed. Swept against the pump split at fixed scene:

| budget | records/frame | per-client ms |
|---|---|---|
| 1500 | 133 | 1.28 |
| 3000 | 309 | 1.95 |
| 4500 | 401 | 2.40 |
| 6000 | 406 | 2.51 |

`perclient ~ 0.85 + 0.0039 x records`: classification is a ~0.85 ms floor, about a third of the cost
at the shipped budget, and the rest scales with what is emitted. Halving the budget cuts per-client
time 47%. Contested was HIGHER at 1500 (2,635 against ~2,070) because starvation grows the backlog,
so the cheapest point had the most to classify and still cost least - which refutes the classification
hypothesis directly rather than by fitting a line. **Lowering `fua_snap_maxbytes` is still the wrong
lever**, because it re-creates the starvation multi-packet delivery was built to remove; what changed
is that Phase 4's smaller records buy client count and not merely refresh rate.

**And the snapshot path is not what saturates it.** The pump is split into game simulation, world
capture and the per-client pass, because `sim` is a TIC COUNT and no log could otherwise distinguish
a slow simulation from slow netcode. On MAP13 with ~1,100 hunting:

| | capture | per-client | total | worst tick |
|---|---|---|---|---|
| idle, no client | 0.48 | 0.00 | **0.48 ms** (2%) | 2.9 |
| joined, quiet | 1.57 | 0.84 | **2.41 ms** (8%) | 3.9 |
| full fight | 1.75 | 2.48 | **4.22 ms** (15%) | 6.5 |

**A 6.5 ms worst tick cannot push a 28.6 ms budget over**, so saturation is simulation cost and
`fua_snap_maxcatchuptics` is doing its job. Capture is nearly flat with load while the per-client pass
scales 5x - the shape to remember as later phases add to that pass.

**Stats windows with sim ≫ 1 aggregate multi-tic spans.** The frame builder's per-pump capture
semantic is the right overload behaviour - one frame spanning however many tics were processed - and
every calibration figure in this document comes from sim ≈ 1 windows. Every rate (kB/s, Hz, ms/tic,
the pump's budget share) divides by the window's game tics (`simTics`), not by pumps, or a window at
`sim` 8 reads eight times high. Per-capture figures are labelled `/frame` and stay per
pump; the pump line adds the per-pump and worst-pump cost in ms, since one long pump stalls the loop
once per catch-up batch. **The budget is spent per pump**, and so is the animref quota (population ÷
interval in tics): at `sim` 8 a client gets about an eighth of the updates per game second in every
band, and the animref cycle runs about 8x its target. Open for the overload work; scaling per-pump
work up on a server already behind would deepen the overload. **A mean sim hides the spikes that matter**: `sim` 1.05 over ~175 pumps is one pump of ~10
tics, which moves every entity at once where a uniform 5% slowdown is invisible. The wall gap between
pumps is the direct measurement, and it also sees stalls outside the timed region, which the phase
timers cannot.

**Compression scales with frame size, and the cost of a datagram is a CONSTANT, not a ratio.** Taken
at constant content - one built payload compressed whole and then in 2/3/4 slices cut at record
boundaries - the relationship is linear to within 3 B over four points:

```
compressed(S) ~ 0.734 x S + 61        marginal ratio 1.36x
```

**61 B per datagram** under plain zstd, of which ~10 is the frame header and the rest lost window
context; **40 B** measured live against the `zstddict`, and that gap is the point - a dictionary earns
most on small payloads, so the lump §9.1 measures as a ~3% COST at 1,500 B is a BENEFIT at 490 B.
Either way the constant is a third of a 500 B slice and a twentieth of a 1,500 B one, so few large
datagrams are much cheaper than many small: three chunks cost **+7.6%** at 490 B each against ~2.6%
at 1,500 B for the same records. That is what sizes multi-packet delivery, and why the chunk is the
MTU-safe payload rather than a tunable.

**The budget is denominated in pre-compression bytes, and must be.** `NETWORK_LaunchPacket` compresses
every outgoing datagram below every layer that knows what it is compressing, so `fua_snap_maxbytes`
rations *uncompressed* payload while the constraint it exists to respect - path MTU - applies to the
*compressed* datagram. So the budget can be raised by roughly the compression ratio at no MTU cost,
and **headroom must be sized off the peak ratio, not the mean.** A related trap: `fua_snap_maxbytes` once
defaulted above `sv_maxpacketsize`, so every datagram silently exceeded the configured packet size -
`NetCommand` warns, but `sv_showwarnings` defaults off.

---

### 5.5 Client-side animation

Three quarters of dirty traffic is animation: the actor advanced along its state chain. The question
is how much of it a client running the same state machine can produce unaided, and the answer
depends entirely on how the question is asked.

**A derivability measurement taken on the server measures the server's sampling, not the client's
prediction.** A server-side census comparing each actor's state against its state one tic earlier
reports 99-100% derivable, because a monster advancing one state per tic is trivially chain-adjacent.
That number is real and useless. Records arrive every several tics, so the client's question is not
"did this advance one step" but "**starting from the state I last received, can I reach the state you
are now in**". Only an instrument comparing what the client actually produced against what the wire
subsequently reported answers it.

#### What ships

**`qstate` - which state.** One byte was never enough: resolving a state from `spriteframe` is
ambiguous exactly at branches, because the search walks forward from where the mirror already is and
is therefore biased toward the chain the client is on. Measured, it picked a **different state than
the server's on 36-37% of resyncs**. Sending the state index cut phase error nearly in half - a
phase attached to the wrong state is worse than no phase.

`spriteframe` is gone with it, since a state determines its own sprite. The complication is that
`qstate` is unset whenever an ancestor class owns the state, which is how player pawns work
(`DoomPlayer` inherits from `PlayerPawn`); the fix packs inheritance depth into `qstate`'s top 3 bits
and the offset into the low 13, so no extra byte is spent. Depth caps at 6 because depth 7 at maximum
offset encodes to `0xFFFF`, the not-resolvable sentinel.

**`qtics` - phase.** One byte carrying tics remaining. The client already resolves which state to
draw; only the offset into it was missing, and without it the mirror restarts each state's full
duration on resync and runs permanently late. Excluded from the dirty comparison, since `tics`
decrements every tic and comparing it would dirty the world every frame; it rides with `APPEAR`,
which is exactly when the frame changed.

**Branches as EVENTS, not as a priority band.** Detection works by comparing each actor's state
against the one that would have followed its previous state - it flags pain, death, attack and waking
uniformly at ~6-8/tic for 1,100 monsters, with no hook at any of the ~47 call sites.

**"The state that would have followed" is not `GetNextState`.** `AActor::Tick` chains through
ZERO-DURATION states within one tic, so the capture never observes them and the successor is the
first state after the previous one that takes time. A jump table of `TNT1 A 0` states in front of an
animation is the standard DECORATE idiom for branching on inventory, and every loop of such an
animation was flagged as a branch - once every eight tics for a modded BFG ball, each one an event
carrying a position. Stock content never does this, so it took modded content to surface.

**RE-ENTRY is a branch and needs its own test**, because every other mechanism here keys on state
IDENTITY. A weapon refiring calls `SetState` on the state it is already in: the pointer does not
move, `qstate` does not move, `DiffMask` sends nothing, and the client's resync gate would
discard the record anyway. Detected on the PHASE - `tics` only counts down within a state, so a rise
means re-entry. Two exclusions, each of which was a false positive costing a measurement round: a
state that LOOPS TO ITSELF (Doom's Cacodemon `See`, which made re-entry read 28/tic, five times all
real branches combined), and a slot whose OCCUPANT CHANGED, since ids recycle and a new missile in a
recycled slot shares its predecessor's class and Spawn state.

This is the defect the legacy protocol carried as "still has problems with `A_ReFire`" and worked
around by replicating the attack buttons - the derivation this phase deletes.

Routing a branch to a high-priority *band* made things worse - off-chain misses rose from 5.9% to
8.6-10.2%, and removing the class took them to 4.8% - because the branch arrived promptly while the
RECOVERY records stayed in the distance bands at 0.5-0.9 Hz (§9.2).

As an event: id, tic, `qstate`, `qtics`, position, 14 bytes, on a reserved share emitted ahead of the
priority passes, culled at the entity radius, dropped after a 4-frame deadline (a death's is longer,
below). Position is carried
because 45% of branches happen on entities 10+ tics stale; velocity is not, because 98.6% report
zero. **The tic is what makes it safe** - without it the event overwrites newer state (§9.2).

**Measured neutral**, at 2.5-3.5 kB/s and ~1,000 applications a window: events on scores `localanim`
80-85% against 81-90% off, overlapping given 5 points of scene variance. Kept on because transient
states below the sample interval cannot arrive any other way, and the sprite-frame metric may be too
coarse to see a 2-6 tic flinch.

**An event is an overlay on the drawn sample, never part of the model.** It writes the newest sample
and its `posTic`/`appearTic`; `nextState` stays the record-only state the checksum covers. A later
record that omits position or appearance carries those groups forward from the drawn sample with their
stamps - from the model, a pain state reverted and a position-suppressed missile snapped back.

Delivery is blind copies rather than retry-on-ack. The channel is idle in 98% of frames and then
needs ~900 events at once when a mass branch fires, so a flinch gets one copy and the 4-frame deadline
dropping a burst is correct. The ack history proves a frame lost only once it is `SNAP_REORDER_WINDOW`
(8) frames behind, so a retry on proof lands with the state record's own repair, ~9 tics on: one try
inside a 14-17 tic death, the next after the leave. At 33-37% loss 9 of 102 rockets never showed
their explosion - the death record and its event share a frame, and the one repair was lost too.

**A death gets spaced copies.** Terminal events - `SNAPPRI_TERMINAL` or the corpse bit, the
scheduler's definition, since the corpse bit alone missed every missile - go first, with
`SNAPCL_EVENT_TERMINAL_COPIES` (3) spaced `SNAPCL_EVENT_TERMINAL_SPACING` (3) frames apart under a
deadline of 4 + (copies - 1) x spacing, the last copy at +6. Spaced, because burst loss takes
consecutive frames together; blind, because they need neither the return path nor a short round trip.
Of rocket deaths whose first frame was lost, never recovered: 28% with one copy, 18% with two, 6% with
three - each as independent loss predicts at 31-42% - so 2.3% of rockets against 8.8%, for ~15 bytes
a copy on deaths only. Without loss every copy is caught as a duplicate. An event whose entity's
leave is going out is dropped: the client walks leaves ahead of events, so it would be an orphan.
**A copy must be dropped as a duplicate**: no event sequence is on the wire, and a second application
bumps the resync counter, lifting the hold on a death that has played out. An event matching the
last one the ghost applied (tic and state) is skipped and counted; matching the ghost's newest record
instead missed a reordered copy landing after a newer appearance record.

**An event that changes no drawn field needs its own resync trigger.** The first build delivered
1,125 events a window with zero orphans and moved correctness by nothing: the mirror resync keyed on
the sprite frame changing, and an event carries state and phase but no frame. A per-ghost sequence
counter fixed it. **A signal that arrives perfectly and changes nothing is a wiring failure, and
`sent == applied` does not prove the signal reached what it was for.** The same gate then discarded
re-entry events for the same reason at one remove - they name the state the mirror already draws - so
an event-triggered resync applies its phase regardless of frame agreement.

#### Where it stands

Local animation is **74-80% correct on the NEAR tier** by `bystate`, against 47% before this work,
and refresh rate is not the lever: across five runs spanning 34-55 kB/s and a 4x spread in near-band
Hz, correctness stayed within two points. The near render tier is ~1,450 entities while the band
being fed holds 100-200, so raising that band reaches under a tenth of what the metric measures.

**The residual is not a defect, and a per-entity trace is what established that.** Roughly 11% of
samples sit one to four states either side of the wire with the record's age reading `0t` - and a
Revenant's See chain runs 2 tics per state, so ONE TIC of jitter lands on a neighbouring state. Every
"ahead" miss traces with the mirror at `tics 2` and every "behind" miss at `tics 1`: the mirror is
within a tic of the server in all of them, and the resync samples precisely at the transition.
**The metric's resolution is one state and the error is one tic.** The other 6-8% is `off-chain` with
real ages - an attack chain arriving 81-858 ms after the branch - which is **delivery latency, not
prediction**, and moves only with records per frame.

Three mechanisms were suspected and all measured clean: local animation never drops a tic, `ageTics`
never under-ages, and `ghost_SetStateSafe` places the phase exactly, including a 24-tic age landing
on the right state with the right remainder.

**`localanim` is soft in both directions and `bystate` is the honest metric.** `localanim` compares
sprite frames, and Doom monsters share a frame across pairs of states, so a mirror on the wrong state
drawing the right frame scores as correct while a one-state difference can score as a miss.

**Local animation runs on every render tier** (`fua_csnap_localanim`: 0 off, 1 NEAR, 2 +MID, 3 all;
default 3). It was scoped to NEAR on a cost concern that measurement did not support: MID costs
+1.6% of ghost frame time and FAR +5.4%, or 0.09 ms on a 10-20 ms frame. Correctness by tier is
74-80% NEAR, 60-72% MID, 72-90% FAR, against a frozen mirror - one holding the last wire frame,
which is what MID and FAR did before - scoring 3-6% and under 1%. FAR scores HIGHER than MID because
its entities are mostly dormant and cycling a fixed loop; the mid tier holds the hunting set.

**A final state is not corrected backward.** The mirror runs to now while the wire's state is aged only
to the newest record, so a mirror resting in its final frame could be resynced to the state before it
and replay the transition. No resync, and no event's phase correction, moves a mirror resting in a
final state (`tics -1`, drawing it) that the wire's state leads to, walked up to 64 states; a branch
elsewhere, such as a resurrection, does not lead there and is still corrected. **The hard resync sets
the aged state and does not force the wire's older frame after it** - in a final state nothing redrew
it until the next record. That was the visible gib flicker: a body stopped by a wall sends nothing for
over a second, the hard resync fires, and the first XDeath frame showed between the last two.

**The comparison must be taken after the frame's advance, not before it.** It was originally scored
at the top of the resync block while `ghost_AdvanceAnim` ran at the bottom, so the mirror was judged
holding the previous frame's state - systematically one step short on exactly the frames a record was
parsed on. The same ordering dropped a tic outright on the hard-resync path, which consumed
`animTics` from the per-mirror clock and then skipped the advance. Both were one reorder.

**A tier that is not animating cannot be scored**, because the metric samples on an arriving wire
record. So raising the level grows the sample set and a per-tier split is what makes the levels
comparable at all; the aggregate cannot separate "this tier is bad" from "this tier was never
measured".

#### What this does not solve

Entities whose motion is not derivable - steering projectiles, anything under external force - still
need position at rate. A room with ~1,000 simultaneously visible monsters remains the pathological
case for interest management: the cull cannot help when everything is genuinely visible, so the
design works by making each entity cheaper rather than by choosing fewer of them.

---

## 6. Client: the ghost table and sampler

New client-side subsystem (`entities.cpp/h`): a flat array indexed by entity slot holding
{generation, class id, the two most recent snapshot states, current render state, attached sound
channels}. Renderer integration replaces "walk sector thing lists of real AActors" with "submit ghost
vissprites": the drawer needs position, sprite, frame, translation, alpha, renderstyle — all present
in the block. The touchpoints are `R_ProjectSprite`/vissprite submission; ghosts either feed a
parallel submission path, or — pragmatic phase-1 shim — each ghost owns a hollow client-local
`AActor` whose `Tick()` does nothing and whose fields the sampler writes each frame, reusing the
existing renderer and sector-lighting interaction wholesale and optimized away later.

#### Render-delay interpolation, built and REMOVED

The design assumed it throughout: hold each ghost `renderDelay` behind live, interpolate between the
two bracketing states, get smooth motion at low update rates. That is the standard approach and the
wrong one under a budgeted priority scheduler. The per-entity delay estimator tracks the MEAN arrival
gap and cannot follow the variance the scheduler produces - several updates in consecutive frames,
then nothing for seconds - so the render target overruns the newest sample and the sampler falls out
of bracketing into either holding (freeze, then jump) or extrapolating (a straight line `A_Chase`
invalidates within a few tics). Measured in a 1,100-monster fight: **855 entities extrapolated per
frame against 558 bracketed**, and both failure modes rated visibly worse than stepping.

**The removal rests on measurement and the reason it cannot help is structural.** One recording was
replayed with the sampler toggled seven times, so the comparison was against the same traffic:
interpolation and stepping measure the same across four versions of the glide instrument, and three
facts needed no glide metric at all - `bracketed` was ~1% of active ghosts, median delay was ~42 ms
WORSE with it on, and the visual ratings preferred stepping. Refresh rate sets the SIZE of the
positional error and the sampler only chooses its SHAPE: at 0.7-1.3 Hz a monster genuinely walks
500-700 units between updates, and whether the client snaps or slides across that gap does not change
how wrong it is. **An entity starved of updates SNAPS if it is held and GLIDES if it is interpolated,
and which one you see is a property of the sampler, not of the starvation** - an artefact that went
unattributed for a session because it read as the opposite of the snapping it shares a cause with.

**What ships is: newest received state, plus velocity extrapolation** on two horizons, aged by the
server's clock, **and error smoothing**, which draws between the two newest records without a delay
estimator (both below). The render delay, cadence estimator, history ring and bracketing logic are gone.
A ghost is stale between updates but never fabricated, which is what the legacy protocol did and why
it felt instant. The class's own `Speed` survives only as the bound the replay's glide census
measures against (`SpeedForClass`).

#### Extrapolation

**Two horizons, with different evidence.** The SHORT horizon, up to the next record, needs no proof:
its worst error is one record gap's curvature, and stepping already lags by velocity x gap. The LONG
horizon, bridging withheld records, takes only motion the server has measured as predictable. **Both
are decided by what the record says about motion, never by what the actor is** - a class gate left
tossed items, gibs, knocked-back monsters and mod debris stepping at the record rate.

**Long horizon: the ballistic bits are MEASURED**, under `MF_MISSILE | MF_SKULLFLY`. Set from flags they
were wrong on 54-65% of missile samples; now set after three tics on one heading and cleared on any
deviation (§9.2). BALLISTIC alone is constant velocity: `velz` changing by under 1/16 a tic, which under
no gravity is exactly 0. BALLISTIC | STEERING is constant ACCELERATION - plan heading held and velz's
second difference within a quarter unit - drawn by the law. Both are suppressed (§5.2). STEERING alone
is turning or unproven. Every reader goes through a predicate. **The class is decided over the WIRE
velocity**: a missile moving under the wire's step holds a heading but crosses as zero, so it is
unproven and takes no homing priority; nor does the gravity class, being predictable. Flagged ballistic,
still effects (burning fires, comet trails) counted as frozen at the cap for their whole lives, which
read as detonations arriving late. **The second difference compares this tic's velz change with the
previous tic's**, and storing the change before reading it made it always zero, so any missile straight
in plan earned the gravity class. Fixed, the class carries real gravity: Complex Doom reads mean
0.41-0.47 u/tic/tic, worst 1.00, none refused.

**The long horizon draws the gravity class by the law** (§5.2), from the position's tic, the velocity
carried to it from its own `velZTic`, which moves only when the merge applies VELMISC. The stored `qvz`
stays the record's value, or the checksum reads DIVERGED. **A measured acceleration stays** for the
short horizon and as the law's check (`measured disagreeing`): two velocity samples through the
quantiser, **adopted only when two consecutive readings agree**, with a 2 u/tic/tic bound as backstop.
One reading across a bounce or a landing reported 5.00 against a true 0.75 and moved a projectile
767-852 units over the cap; real gravity over that horizon is legitimately ~115 units, so no bound
separates them and consistency does.

**Short horizon: `extrapvel`.** A non-player ghost with a missile bit that is not ballistic (a
missile not yet proven straight, or a monster's charge) and a nonzero wire velocity is carried
along it for at most `snap_velextrap` (100 ms), with the measured acceleration while velz is
nonzero. **A body (no missile bit) is held instead**: drawn ahead, it was pulled back
whenever its records stopped following its velocity (a knocked-back walker's steps, a dash
re-aimed every tic, a tossed item's landing), and live, a held lunge looked no worse than a carried
one. Both wire-velocity exits run from a ghost's first record - held there, a fast projectile was
drawn at its spawn point until its second record, then a tic's travel on. **It rests on one
property: a record carries every group that changed.** A record without velocity means velocity is
unchanged and current - by the law for the gravity class - which is why velocity is withheld nowhere
else (§5.2); a freshness test that guarded the far tier's stripping also refused unchanged velocity,
and corpses sliding over stairs switched between carried and held, record by record. Two rules
follow:
- **Aged from the newest record** - Z always, and x/y when that record carries them - since every
  record dates the position; aging Z on the x/y clock held a Z-only fall at the cap.
- **A newest record without x/y means they moved under a unit**: they are carried from the last record
  that had them, over the x/y clock, and clamped per axis to its rounding cell (±0.5 u). A slow
  mover runs on to the cell edge, one against a wall stops at most half a unit out. Z is unchanged.

Measured: the wire-velocity share of held-moving ghosts 2.3% vanilla and 0.9% modded to 0%, dropped
items gone from the jump list, 0 DIVERGED under 33-52% loss.

**What it still draws wrong**, all bounded, measured on sliding gibs:
- **The 1-unit grid.** Records are rounded to the nearest unit, so landing on the next record pulls a
  sliding body back over 0.25 units on 27-34% of records, mean 0.5 - rounding's prediction (28%, 0.5)
  at every slide speed. Visible close up in coop; error smoothing is the fix (§9.3).
- **Velocity changed over the x/y age.** Inside the cell the newest velocity is applied over the whole
  x/y age, and friction makes that error backward: 60% of such records, mean 0.42 u, worst 0.75.
  Integrating each record's velocity over its own interval since the last x/y record fixes it without
  new state; held unless it shows.
- **Stops** - a pickup, friction reaching zero, a hit on an actor - overshoot by up to one record gap
  of travel (0.9-5 u measured) before the record pulls the body back. A projectile dying in its first
  tic on a monster is carried through it from its first record (about 150 u for a fast tracer). Walls,
  floors and ceilings are clamped (below).
- **Loss.** A lost record carrying new x/y or velocity leaves the older value until repair, bounded by
  the repair round trip and the cap.

**Ages run from the server's clock** (`fua_csnap_age_servertime`, on). A record's age is measured
from the clock model's date for its game tic (§8), not from its arrival: a packet is stamped when
the client's frame reaches it, so arrival jitter is mostly the client's own frame time, and it
landed as a correction on every record of a fast projectile. A date after now makes the age 0. **A
ballistic ghost dated ahead is drawn between its records.** Held at its newest position it would
jump forward and then stall, so it is drawn where it was at now, on a cubic Hermite curve through
the positions and velocities of the newest record dated at or before now and the next newer one; the
gravity class arcs, and a chord cuts across it. With no earlier record it holds, which covers a
spawn, and two records farther apart than twice the faster one's speed over the gap plus 32 units
are a teleport or respawn and hold too. The path is one the ghost took, so it is not swept against
walls, and it draws only between real records. **Only those two place anything by the server's
clock.** Every ghost's age is dated by it, but stepped walkers carry no velocity, so their position
does not move with it; the age reaches them only through the one-second animation resync, where a
few ms do not matter. A monster carrying wire velocity - a charge, a knockback, a thrust - is
extrapolated, so it is placed by the server's clock. Remote players are not extrapolated and are
smoothed from arrival, deliberately: unlagged rewinds the server by the shooter's ping, assuming the
target as it was when its data reached the client, so drawing an early record later than it arrived
would add a small, systematic offset on exactly the ghosts where aiming matters. Unlagged rewinds
only players and sector planes (`UNLAGGED_Reconcile`), so that exception does not reach monsters.

**Motion without velocity stays stepped.** 84-90% of held-moving ghosts carry no wire velocity -
walkers moving by `P_TryMove`, warps, orbiting effects. Position-derived velocity (the retired
`snap_extrap`) would carry them, and is not built on: a straight line between `A_Chase` steps,
around an orbit or across a teleport is worse than the step.

**An event's position carries its own time.** A branch event writes position and `posTic`, and must
set `posMS` with them, back-dated by its age in the frame (`wallMS - (frameTic - evTic) x 1000/35`,
clamped between the old stamp and arrival). Without it the ballistic exit started from the event's
current position and added the whole withheld stretch again: ~330-unit jumps on a distant suppressed
plasma stream.

**An extrapolated ghost knows no floor, and the wire goes quiet exactly when it matters.** A landed
projectile is no longer dirty, so the client integrated its stale downward velocity for the full cap
and froze below the ground. **A stale position is merely old; a stale VELOCITY keeps being
integrated.** A descending pose from any extrapolating exit (`SnapGhostPose.extrapolated`) is clamped
to the sector's floor PLANE - one `ZatPoint`, not the `P_FindFloorCeiling` scan unaffordable at ~1,000
mirrors a frame, so 3D floors and step-ups are ignored. The POSE is clamped, not the actor, or the
movement test relinks every frame. A rising pose is clamped to the ceiling plane the same way.

**Walls are clamped on the extrapolation's own path.** Both wire-velocity exits put it in the pose:
origin (the last x/y record), Z acceleration, cap. The mirror traces it once per record
(`FPathTraverse`, cached per id on origin, velocity, acceleration and cap) over the exit's cap plus
the box's half-diagonal, and stops the pose at the nearest contact with a line that blocks its class,
at the height the path reaches there. **Contact is `PIT_CheckLine`'s own test swept along the path**:
the square box overlaps the line's bounding box AND straddles the line. The infinite line alone stopped
a shallow path in open air short of a pillar or jamb it passes (90 u back-off for a rocket at 10°).
Blocking is `PIT_CheckLine`'s flags and railing and `P_TryMove`'s opening tests from the class
defaults, since the mirror is `MF_NOCLIP`; the wire's missile bits count only for a non-monster, as a
monster's mean a skull charge. Polyobject lines are seen. Not seen, each only missing a clamp: 3D
floors and 3D midtextures (`P_LineOpening` without an actor), lines the box clips without the centre
crossing them, a low wall cleared at contact that the path falls into while still straddling it
(blocking is tested once, at contact), and a door moving under a cached trace. The x/y cell branch
carries no path; it is already bounded to half a unit. Cost on Complex Doom: ~540 traces per 5 s
against ~850 frames of ~340 mirrors, ghost frame 0.18 ms.

**Losing the bit instantly is deliberate.** Latching it cut class changes in flight from ~2,300 a
window to ~400 and made tracers visibly worse: `A_Tracer` re-aims one tic in four, so they were
extrapolated through the turn rather than stepped through it. Chatter at the threshold is the cheaper
artefact.

#### Error smoothing

**Interpolation and error smoothing are one mechanism.** The census (two clients, 103 fps, 0% loss)
found remote players' records arriving every tic (98.7%) within ±5 ms of the tic clock, 0 jumps -
and drawn in 12-17 unit steps held ~3 frames, because a mirror sets `PrevX = x` and stock
interpolates every actor across the tic. The engine's own interpolation cannot be reused: records
arrive at any point in a tic, and `r_TicFrac` assumes the move happened at its start. So each mirror
carries an offset, `drawn = target + offset`:
- **Fold only when a record or event arrives** (`serverTic` or `eventSeq` changed): the change in the
  target against the old model's prediction (its last target plus its own motion per frame, measured
  between records, so a cap, the cell clamp, a wall or gravity are in it) goes into the offset.
  Folding every frame drew an extrapolated ghost on past its cap.
- **Decay at the actor's own speed over the newest record's tic gap** (`posGapTics`, 1-4). With records
  on time that is linear interpolation between the two newest, one tic behind, as stock. After a lost
  record it holds a tic, covers the 2-tic step at real speed and the next at 2x, back to one tic
  behind; holding the lag instead would drift from what unlagged rewinds.
- **Snap above a threshold**: 32 units per tic of the gap for stepped ghosts (over p99 running, 21;
  bridging a 3-tic gap is the point). Teleports and respawns snap; a short in-room teleport would need
  the dormant wire teleport flag, not a tighter distance.
- **Extrapolated ghosts: a dead-band of the rounding cell.** A rounded record says the body is
  within half a unit of it, so the part of the offset inside that cell (per axis) is retained, not
  drawn back; only the excess decays, and only the excess is tested against the threshold
  (`fua_csnap_smooth_nonballistic`, 1 u; 3 under arrival aging was measured and did not help). It
  governs turning and unproven missiles, bodies being held.
  The retained part holds while the body moves and settles over 200 ms once its wire velocity is 0;
  a ghost stays smoothed until it has settled, so a stop does not jump. Z is retained too, so on a
  slope a body level or rising can be drawn up to half a unit below the floor (the floor clamp acts
  only when descending); if it ever shows, retain x/y only for bodies on the floor. **The wiggle was
  the grid, not friction**: replaying both recordings, friction left the prediction error unchanged
  (median 0.56 u; bodies slide at 1.1-1.5 u/tic median), corrections split 48/52 backward/forward,
  and drawing a half unit back over a tic reverses a body crawling under 1 u/tic. Frames drawn
  moving against the wire velocity outside snaps fell from 1.7-2.0% to 0-0.07% with the dead-band;
  the `smoothing` line counts them (`REVERSED`), snaps apart.
- **Ballistic missiles are not smoothed** (`fua_csnap_smooth_ballistic` 0; the ballistic bit,
  gravity missiles included). Both ends run the same law, so their corrections are small against
  their speed, and easing them only cost accuracy: measured on seven recordings against marks set
  before the blind half, ballistic accuracy improved by 0.12-0.27 u at 0 with no material rise in
  frames drawn moving backwards (the worst +0.079 points, inside the tolerance), where bodies at 0
  drew backwards 14-32% more on three. The gain shows only while the clock model is trusted; under
  arrival aging 0 is neutral. Turning or unproven missiles (the steering bit alone) stay at the body
  threshold of 1: their own mark, frames drawn more than 15 degrees off course, failed at 0 on the
  recordings run at 90 frames a second and up, though not on the tracer-heavy one at 56. The same
  off-course rise shows on ballistic missiles at 90 frames a second and up (none at 56 or fewer), a
  measure that scales with the frame rate rather than with visibility; by eye, live at a normal
  uncapped frame rate, 0 and 1 could not be told apart, so they stay at 0.
- **Angle is not smoothed**: stock picks a sprite's rotation from `angle`, never between tics.
- **Order**: sampler, wall clamp on the model, offset, then the floor and ceiling clamps on the drawn
  position.
- **Reset** on mirror creation (including after a hard resync), a level unload, a rebaseline (stream
  epoch), and a map reset (the mover gate's window, which ends in its replay; nothing is smoothed
  inside it).
- **Scope**: remote players and extrapolated ghosts (`fua_csnap_smooth_entities` 1). Held monsters
  (2) cost little - relinks 10 to 25 a frame, ghost frame +0.05 ms, no frame-rate loss on Sunder
  MAP01 - and glide through their 2-4 tic gaps.
- **Outside the smoothing, the renderer slides.** Stock slides every actor across the tic
  its move happened in (`r_TicFrac` from `PrevX`), so a walker's step glides one tic and holds until
  the next, where a held mirror set `PrevX = x` every frame would jump each step in one frame as its
  record landed, which is not stock's look. So a held ghost outside the smoothed
  population moves only on its first frame of each tic, to its pose, with `PrevX` where it stood,
  and the engine slides it over the tic (`ComputeSmoothSlides`, `ComputeSlideFrame`). Waiting for
  the tic and sliding over it, a held ghost trails its newest record by up to about 2 tics. A far
  mirror, visited one frame in eight, moves on its first visit in a tic, so after a missed tic it
  can step two tics' worth at once, and a fast one then trips the snap below. A mirror never ticks,
  so the ghost frame ends every slide at each tic's start, as the engine's tic does for an actor
  that ticks. A step past 32 u a tic of its gap (a teleport or respawn) is drawn as it lands.
  `nomonsterinterpolation` draws a monster's walking steps (no wire velocity) as they land, as
  stock's `P_Move` does, while a lunge or knockback still slides; a monster knocked back while
  walking slides where stock would draw that tic raw. `cl_capfps` draws every actor at its tic's
  end.
  A walker updating every 2 tics slides one tic and holds one, the mild judder stock has too.
- **"Stale, never fabricated" holds**: interpolation draws only between real records; the `smoothing`
  line's `drawn OUTSIDE its records` counter (the box of the current segment, from the drawn position
  at the last record to that record) must read 0. The spied player is drawn
  through `players[].mo`, moved once per tic after its `PrevX` is saved, so the engine interpolates it.
- **Cost**: 96 bytes per id (6 MB); every smoothed mirror relinks each frame while it carries an offset.
- **Under loss**: two clients watching each other through 1.5-3.8% frame loss drew 0 snaps and 0
  frames outside their records on the remote player; a lost record shows only as the 2x catch-up,
  1-6 times per 5 s.

#### One timeline

**The ghost has ONE timeline**, and four artefacts were the same violation of it: appearance taken
from the newest state while position was interpolated at the delayed target, so a projectile's death
animation played while its position caught up to where it died; presence at entry applied on arrival,
so an explosion appeared ahead of the projectile still flying toward it; local animation running on
the tic clock with a resync rule but no termination rule; and the legacy-actor hide applied from the
render loop rather than the packet clock, so it stopped being applied exactly when the frame rate
collapsed.

**Presence at departure is open, and slight.** A leave is applied on arrival, so a ghost drawn
behind its newest record vanishes before reaching its final drawn position: with smoothing, a remote
player about a tic early. **The teleport bit is dormant**: specified and unimplemented, it exists to
suppress a lerp across a teleport, and smoothing snaps a teleport by distance instead. If a render
delay ever returns, entry and departure must be handled together, or half a solved problem returns.

#### Mirrors and the renderer

**Four mirror defects share one root: a renderer branching on `thing->player`, which a mirror lacks.**
`R_RenderPlayerView` hides the viewer by flagging the CAMERA actor, so spying a mirrored player drew
his body inside his view - `ghost_ExcludedPlayer` is a UNION of local and spied, not a swap, because
`cl_pred` calls `mo->Tick()` and a hidden local player stops moving. `gl_spriteclip` and
`P_CheckPlayerSprite` gate on it while every other term is a class property, and widening the second
would have been wrong twice: it dereferences `actor->player` and it WRITES `actor->sprite`, the field
a mirror's agreement test compares against the wire. Monster mirrors were never affected,
`MF3_ISMONSTER` being a class property - which is why differencing actor FIELDS found mirror and
player identical (§9.2).

**Nothing about skins crosses the wire and nothing should**: the server forwards the NAME and each
client resolves it locally, so **two clients can draw the same player differently and both be right**,
which rules out any instrument comparing mirror appearance across clients.

**Remote player animation was never a wire problem.** Entry records arrive under two tics and a
mirror reaches the server's state unaided on 95-100% of them; the defect was a resync gated on
sprite-and-frame equality, where a pawn's `Spawn` and its first `See` state are both `PLAY A` (§9.2).
Remote players ride `SNAPPRI_PLAYER` into the top band and measure `<4t` 96-100% with 0-2 jumps,
against monsters at 88-95% with 55-296, so a lift there has nothing to gain. **Extrapolation for
players is REJECTED**: the snapping attributed to it was the server advancing 8 tics in one pump
behind a stalled report (§9.2), and no predictor recovers eight tics of another player's input.

**Client-side prediction of the shooter's own projectiles is DECLINED.** The enter leaves on the
SPAWN TIC in 100% of 1,431 cases on LAN and under netem alike, so the residual is one round trip
legacy does not avoid either - `SERVERCOMMANDS_SpawnMissile` fires when the server spawns the missile,
which it cannot do until it processes the usercmd. Snapshot plasma already judges visibly BETTER than
stock 3.3-dev on the same scene, so prediction would put the fork past the parity bar rather than meet
it - and it would be the first mechanism here to draw an entity the server may never send, wrong about
EXISTENCE when the shooter was dead, out of ammo, or `P_CheckMissileSpawn` failed. Revisit only if a
scene below stock's crossover shows a gap.

#### Mirror collision

**Mirrors are solid inside `fua_csnap_collide_dist`** (512), and it is a PREDICTION fix rather than
a gameplay feature. `cl_pred.cpp` replays `P_PlayerThink` against the world the client can see, and
that world contained no monsters - so every tic held against one was a mispredict the server
corrected, seen as the screen bobbing when running into an imp. Invisible on a LAN, where replay
depth ~1.3 gives the correction no distance to travel; apparent at 100 ms, where it reaches ~4.5.
**Free at every radius measured** - 518 solid mirrors at replay depth 4.5 cost less than the timer
resolves. The radius is chosen against record staleness rather than cost, since a stale collision box
blocks the player where a monster WAS.

`PIT_CheckThing` takes a `NETFL_MIRROR` early-out - ZDoom's own prediction short-circuit, sitting
commented out in the same function. Its position is load-bearing in both directions: above the
walk-on-actors block loses standing on mirrors, below `MF6_TOUCHY` re-opens ungated `P_DamageMobj`
calls. Mirrors are also skipped in `P_ChangeSector`, so a moving sector cannot crush what the server
owns.

**A mirror must not block an actor the SERVER positions.** The early-out had no exclusion for the
actor a mirror stands in for, and a hidden legacy actor stays blockmapped, so a player's own mirror
sat in his cell. Nothing local may act on that: the position arrives by command and
`CLIENT_MoveThing` writes it directly, so the only caller that asks permission is `P_CrouchMove` -
which reverts on a failed move when RISING and not when sinking, leaving remote players stuck at half
height. The line is **who owns this actor's position**, not "is this a mirror": mirrors must stay
solid to the one entity the client simulates, which is what the prediction fix above rests on.

Three flag facts, each of which cost a session. **`MF_NOCLIP` on the TARGET disables solidity**, so
clearing `MF_NOBLOCKMAP` alone leaves mirrors blockmapped and passable, reading as "collision does not
work" rather than as a flag error. **`MF_NOBLOCKMAP` also gates `P_ChangeSector`**, so making mirrors
solid silently enrols them in sector crushing. And **`UnlinkFromWorld` is gated on `MF_NOBLOCKMAP`**,
so both directions of the transition must unlink first - setting the flag before unlinking once
stranded every mirror in its spawn cell at 10 ms per replayed tic.

#### The payoff

**What gets deleted from the client**: the ~150 state-mutation command handlers in cl_main.cpp
(SpawnThing/MoveThing/SetThingState/DestroyThing and their Exact/IfChanged variants — including all
three "kill it!" collision-destroy sites from the 2552 work, which become structurally impossible);
client-side ticking of remote actors; client-side actor state machines; the `NETWORK_InClientMode()`
guards across the gameplay tree; the full-update special path. Realistic estimate: net deletion of
15–25k lines once migration completes.
### 6.1 Sounds and the derivation contract

§4.1 assigns sounds to the reliable channel in one clause, and that clause hid this phase's largest
discovery: at suppression level 3, **nothing was audible at all.**

**The problem was not delivery. Nobody was sending them.** Monster sounds are side effects of action
functions - `PlayActiveSound()` in `A_Chase`, the pain sound in `P_DamageMobj`, `A_PlaySound` in
DECORATE - and in the legacy model the client owns a copy of every monster and produces them itself;
`S_Sound`'s `bSoundOnClient` defaults to `false` precisely because the common case needed no
transmission. Of 47 engine call sites, 17 broadcast and **30 rely on the client reproducing them**,
and the DECORATE population is larger. Mirrors deliberately call `SetState(state, nofunction)` - one
that ran action functions would spawn projectiles and deal damage - so at suppression 3 those sounds
have no producer anywhere. The one exception is client-side spawn actions (§6.3).

**This is the contract, and it generalises.** The legacy protocol's implicit rule is *the client
reproduces whatever it can derive locally, and the wire carries only what it cannot.* Every phase of
this migration removes another part of the client's ability to derive, converting a silent local
reproduction into a required transmission nobody has enumerated. Sounds are the first case where the
absence is audible; expect the same of spawned decorations, quakes, particle effects and screen
flashes - found one at a time by their absence unless enumerated deliberately (§10).

**Two are already named.** `P_PlayerThink` runs on the client for every REMOTE player, and its
`P_CalcHeight` produces their `bob` and `viewz`. Most of what else it evaluates there is inert -
sector specials reach a `P_DamageMobj` that returns in client mode, `P_MovePlayer` returns immediately
for a non-console player - but those two fields are real, and `UpdatePlayerExtraData` corrects them
only for a spied player. Phase 7 deletes their producer with the client-mode layer: a spy camera stops
reacting to landings and a spied weapon stops bobbing. Neither is derivable from what ships - `bob`
comes from `player->velx`, accumulated from usercmds that are not replicated, not from the mobj
velocity VELMISC carries.

**At suppression level 3 a client holds no copy of any captured actor.** Clients spawn no ordinary map
thing (`P_SpawnMapThing` returns in client mode unless the class is `ALLOWCLIENTSPAWN` or
`CLIENTSIDEONLY`), and `SpawnThing` and `LevelSpawnThing` are gated, so a legacy command addressed by
NetID finds nothing unless it resolves to the mirror (`CLIENT_CommandAcceptsGhost`). Below level 3 the
copy exists, hidden, and frozen once its motion is gated. `RespawnDoomThing` and `TeleportThing` were
found this way: each silently found nothing, and the client lost the fog it derives from them.

**Client-side effects are made by the client, never captured.** A captured effect draws as a mirror,
doubles any copy the client makes, and restarts its animation as its records land.
`SERVERCOMMANDS_ClientEffect` frees the effect's id (`NETFL_ALLOWCLIENTSPAWN` and `FreeNetID`, as
`P_SpawnPlayer`'s fog always did) and, where the client cannot derive the effect, spawns it
NetID-less at the server's position. Every fog and flash goes through it: item respawn, teleport,
`Thing_Spawn` and `Thing_Move`, `A_Respawn`, nightmare respawn, invasion spots, morph, the rebel
beacon, D'Sparil, the brain spawner, `A_Teleport`'s `FogType` and `PickupFlash`. A NetID-less actor
is client-handled, so the client runs its code pointers itself; a custom flash whose states spawn
actors therefore spawns local copies beside the server's. The Arch-vile's fire follows its target
and stays captured. Cosmetic classes in general are §6.3.

**The contract runs the OTHER way too, and that is easier to miss.** `CLIENT_SpawnPlayer` calls
`G_PlayerReborn`, which calls `GiveDefaultInventory` - the client derives a respawning player's
loadout locally, and it never travels the wire. A whole session was spent probing the server for
inventory it was correctly building and correctly not sending. **Before instrumenting a value, check
which machine produces it**; a derivation the client performs looks identical to a transmission the
server makes until something breaks.

**And in one case the client derives what the server CANNOT KNOW.** Inventory is not replicated, so
content that varies a weapon per player routes around the gap: a CLIENTSIDE script grants a token from
`GetUserCvar`, which IS replicated, and the weapon's state chain branches on it. Every client resolves
that correctly for every player while the server - which never runs a CLIENTSIDE script and never
holds the token - is permanently on the default branch. **So "the server's state is authoritative" is
false for this category**, and a stream reporting it faithfully makes the picture worse: measured with
every psprite record routed, resolved and applied, the weapon visibly wrong until the stream was
switched off. Expect the same of any cosmetic per-player variation, the same inventory gap forcing the
same idiom.

**SOLIDITY was the second case, and the first where the absence is tangible.** A monster drops
`MF_SOLID` in `A_NoBlocking`, an action function a mirror does not run, and `P_KillMobj` is equally
unreachable since a mirror never takes damage - so the first build of near-tier collision left every
killed monster as a permanent invisible hitbox. The wire had carried the answer since v11: `misc` bit
0, written by the capture and read by nobody for four format versions (§5.5's wiring failure).

**One hook, not an audit of 47 call sites.** `S_Sound(AActor*, ...)` is the single function every
actor-attached sound passes through, engine and DECORATE alike, so the fix sits there on the same
`SuppressesLegacy` gate as every other category - §9.2's rule is that categories, not
functions, are what gets gated. Ghost-only entities produce an event; everything else keeps today's
behaviour; nothing double-sends.

**Wire shape.** Sounds reuse the existing `SoundActor` command, which already carries entity id,
channel, name, volume and attenuation. When the client does not yet know the entity the server sends
`SoundPoint` at the actor's position instead - **~60% of sound events during a fight**, because the
reliable and snapshot channels have no ordering relationship: a sound is delivered immediately while
the entity's enter record waits for budget. Unresolved sounds fell from ~70% to under 1%.

**That ordering gap applies to every event type.** Any event referencing an entity can outrun the
entity's existence, so each needs a defined behaviour for "recipient does not know this id yet".
Sounds degrade to a position; deaths, pickups and obituaries may not have so graceful an answer, and
Phase 6 should decide this per event class rather than discovering it per bug.

**Sounds travel by index only against a checked table.** Clients extend the sound table with content
the server has never seen - a skin appends its own sounds, `SNDINFO` is not an authenticated lump,
an `-optfile` sound pack may be absent - so an unchecked index resolves to whatever occupies that
slot locally: a plausible but wrong sound. The server takes its table size N once per map, after
`S_Start`'s rebuild, and after `SnapJoin` sends N and a hash of its first N logical names
(`SVC2_SNAPSOUNDTABLE`); the client hashes its own first N and answers on every snapshot ack, since
the return path has no retransmission. Content loaded after the server's files appends and still
matches; content loaded before shifts the prefix, and that client gets names for the map, as does a
client that has not answered. A matched client gets `SVC2_SNAPSOUND`: flags, NetID or position, the
channel's low byte (`CHAN_LOOP` as a flag, never on a point, since a loop with no actor could not be
stopped), a 2-byte index below N, and volume and attenuation only off their defaults - ~8 B for an
actor sound and ~14 for a point, against ~24 and ~29 by name. It is reliable, so a sound cannot
arrive before the entity it names; a routed cosmetic's NetID-less sound goes as an unreliable point
(§6.3). The index names a logical sound, so `$random` and skin-remapped `*pain` still resolve on the
client. Stock senders (player and weapon sounds, ACS) keep names. A sound no client can play - id 0,
the `{ no sound }` placeholder, or volume 0 - is not broadcast. `fua_snap_soundids` switches it;
`[snap] sound ids` counts sends by index and by name and clients matched, DIFFER and unanswered.
**The same constraint applies to anything else tempted to index into client-extensible content**,
sprites and skin classes included.

**A point sound moves onto its mirror.** Left at the point, it counts against the engine's near
limit (2 copies within 256 units) for its whole length, where stock's copy rides the projectile out
of range in a few tics, so a monster's own attack sound was refused beside launch sounds still
playing at its feet. The client plays a point on the mirror at once if one exists, else at the
point, and moves the channel onto the mirror if it appears while the sound plays
(`S_AttachPointSound`, the reverse of `S_RelinkSound`); a channel already playing on the mirror
keeps its sound. Attached, a projectile's death sound on `CHAN_VOICE` cuts its launch sound, as in
stock. The `point sounds` line counts each outcome. **Under overload it barely reaches a mirror**:
at `sim` 8 most projectiles die before their enter, and about 10% of point sounds find or reach one.
The cure is entering them sooner, where having just made a sound is a candidate input to enter
priority. "Refused" there is any start that got no channel - the near limit, `$singular`, a backend
with no free channel - so the near-limit share wants its own counter at the `S_CheckSoundLimit`
refusal. Limits: a looping sound started while its entity is unknown plays once at a point and never
loops on the mirror; after a map-local `SNDINFO` a later joiner can differ, and keeps names.

**Timing: specified, measured, declined.** Carrying the emitting `serverTic` would let the client
queue an event until that ghost's render clock reached it. With interpolation removed there is no
render delay to be behind, and the queue has a failure mode current behaviour does not: a queued sound
whose ghost stops advancing never plays at all. Sounds play on arrival, and the local player's own
never travel this path (§4.2).

### 6.2 Sound interest management and status

**Sound needs its own cull from the first line of code**, or the reliable channel becomes the new
unbounded per-actor cost - the thing this project exists to remove.

Sound has a cleaner cut available than entity state does: **attenuation defines an audible radius, so
an event outside it is not low-priority, it is inaudible.** It can be dropped outright, which is a
stronger statement than the distance cull can make about entities. **The engine already computes
this** - `S_GetRolloff` returns exactly 0 beyond `MaxDistance`, so the client is receiving sounds it
then discards, and the cull is that same predicate evaluated one machine earlier. Sharing the
function also guarantees the server never culls something the client would have played.

Three details the engine's own code makes visible:

- **`ROLLOFF_Log` has no cutoff** - logarithmic rolloff asymptotes and never reaches zero, so the
  test cannot be "volume == 0". It needs an audibility floor, and where that floor sits is a gameplay
  judgement: too tight and a distant cyberdemon goes silent where players expect to hear it.
- **Attenuation is a property of the sound, not only of the call** (`attenuation *= sfx->Attenuation`),
  so the cull must run *after* sound resolution, which constrains where the hook can sit.
- **`ATTN_NONE` needs no special case** - attenuation 0 means full volume map-wide, so an early exit
  is enough.

**Looping sounds belong in the snapshot record, not the event stream**: they are state, and a client
entering the radius after the loop began has missed its start. `misc` no longer has a spare bit for
it (§3). Its home is a self-describing event type (§9 item 2), which is the shape `SNAPEV_PSPRITE`
already uses to carry state on the event encoding: delta'd against a reference and re-sent while
unacked, borrowing the ENCODING and not the blind-copy machinery.

**Status: built and closed**, with the `SetThing*` gate classification complete (§9.2). The `S_Sound`
choke point on the `SuppressesLegacy` gate, per-client emission with the audibility cull, the
positional fallback, and client-side mirror resolution for `SoundActor`, `SoundActorIfNotPlaying` and
`StopSound` via an allowlist - a blanket fallback would let ungated commands write linkage flags onto
mirrors. Sector sounds, ambient sounds and `SoundPoint` already work, not being attached to a
suppressed entity, and music and UI sounds are unaffected. On MAP13 at ~5,300 live monsters: 736-1,162
events sent per window against 1,218-1,476 culled inaudible (53-91%), ~1.5-3.4 kB/s. At 1, 4 and 8
clients: zero LOST, zero wrong-class.

**The double-play guard is built** (`fua_csnap_nolocalsound`, default on) and reads zero suppressed
at suppression 3 - correct rather than inert, since mirrors have no state machine to produce a
duplicate from. It earns its place only below that level, where the client would produce the sound
*and* receive the event.

**Player sounds stay on the legacy path.** `SuppressesLegacy` returns false for any actor with
`player != NULL`, so weapon fire, pain and death ride the legacy commands. Mirrors run no action
functions, so the 30 of 47 call sites relying on client reproduction would lose their producer the
moment that clause narrows - and the machinery already handles it, since the choke point culls per
client and falls back to a positional sound. **The local player must stay exempt permanently**,
being predicted rather than replicated, so the condition becomes "is this the LOCAL player", not a
deletion. That predicate cannot express it - it takes an actor and no client, and a player body is
local for one client and remote for the other 63 - so it belongs to the per-client loop that gates
`MovePlayer` (`SuppressesLegacyPlayer`), with Phase 6's sound work.

**`StopSound` now has the hook `S_Sound` has**, and it was a real gap rather than symmetry for its
own sake: `A_StopSound` replicates only for `ACustomInventory`, which a weapon is not, so a looping
weapon sound had a replicated START and no stop at all. **A stop is an EVENT and no state record can
carry one** - it sits on a zero-duration state, invisible to any sampler - so the moment a client
stops running the chain that produced it, the sound has no terminator anywhere. Gated on the actor
having a player, and a duplicate stop is harmless since the channel is already silent, which is why
the `ACustomInventory` path is left alone rather than untangled.

### 6.3 Cosmetic actors

At level 3 a mirror runs no actions, so every effect a state spawns - trails, puffs, smoke, debris,
most of a mod's weapon effects - had no producer on the client, while the server's copies cost
NetIDs and enter records. Three mechanisms replace that: classification, events and mirror
spawns. `fua_snap_cosmetic_events`: 0 off, 1 report, 2 act; default 0, test servers run 2.

**Classification.** Per map, each class is `+CLIENTSIDEONLY`, `+NONETID`, inert, or networked with
the first failed test. Inert: a DECORATE class on `AActor`; no play flag (solid, shootable, missile,
pickup, counted, pushable, monster, line activation without `+NOTRIGGER`) or damage; no line special
or `CallACS` in a parameter expression; every action in the states it owns, inherits or reaches is
whitelisted look/sound/move/jump, a spawn of a cosmetic class or a client-side spawn with constant
flags, no telefrag and tid 0, or a CLIENTSIDE script of the map. Native code, computed classes and
the markers a client resolves by NetID (`PointPusher`, `PointPuller`, `TeleportDest` and
descendants) are networked. A class any loaded ACS module's string table names is marked
`ACS-named`. `fua_snap_cosmetic_force` passes a class as inert outright and
`fua_snap_cosmetic_never` takes it off the route; never wins. 1-4 ms a map;
`fua_snap_cosmetics [text]` lists the set.

**Rows.** The verdict and the flags give the route:

| Class | Server copy | Client copy |
|---|---|---|
| `+CLIENTSIDEONLY`, inert | destroyed at the flush | `NETFL_CLIENTSIDEONLY` |
| `+CLIENTSIDEONLY`, not inert, or ACS-named | kept, NetID-less | `NETFL_CLIENTSIDEONLY` |
| inert | kept, NetID-less | `NETFL_CLIENTSIDEONLY` |
| `+NONETID`, not inert | kept, NetID-less | plain NetID 0 |
| a blood spawn whose class is in no row above | kept, NetID-less | plain NetID 0 |

**Events.** At mode 2 a routed spawn - a classed actor spawned by an actor or by ACS, or any blood
spawn - gets no NetID, and each client makes its own copy from `SVC2_SNAPCOSMETIC`. Map spawns stay
networked.
- **Flush** at the copy's first tick, after `PostBeginPlay` and before `Tick`, so the spawner has
  set velocity, angle and state; blood goes at the call (direction, damage, originator NetID).
- **The TID rule**: a copy reaching its first tick with a tid, special or args takes a NetID and
  enters by snapshot instead - unless its spawner's client copy still makes it, or it is `+NONETID`,
  which stock never gave a NetID.
- **A flagged copy acts on the client**: its own action sounds are muted on the server and its
  children get no events, since the client copy makes both.
- **Held for a server script**: rows 2 and 4 and ripper blood are held across their first `Tick`,
  and past it while a non-CLIENTSIDE script with the copy as activator runs; released when it
  finishes or pauses, or at 8 tics. A release carries `SNAP_COSX_ACTED`, so the client sets the
  state without its action. This lets a server script choose a trail per player setting.
- **Wire**: map stamp, count, then per event a flags byte (kind, age 0-7, state, fine velocity,
  extras); generic ~13 B, blood ~11, splatter ~9.
- **Budget, per client**: culled at `fua_snap_cosmetic_dist` (4096), then a bank of
  `fua_snap_cosmetic_bytes` (600) per tic simulated, up to `max(4, tics)` tics saved and capped at a
  datagram's room, nearest first. A pump's overflow is carried one pump, a carried event winning
  ties; overflowing again or passing age 7 drops it.
- **Client**: a copy is ticked forward by its age at its first tick, and children spawned meanwhile
  are owed the rest. Only a repeat of the same `CHAN_AUTO` sound from the same actor in one catch-up
  is held back.
- **Sounds a routed actor makes outside its own actions** (`P_SpawnPuff`'s hit sound) go as snapshot
  point sounds, unreliable and culled.

On Chillax MAP13 with four clients about a quarter of pumps fill the bank and nothing is dropped; on
Sunder MAP11 `SVC_SOUNDPOINT` falls from 0.7 kB/s to 0.

**Map things stay networked.** Map things clients could make (inert, unflagged, no tid, special or
args) number 0 on Chillax MAP13 and Sunder MAP11 and up to half a map's entities on decoration-heavy
content (grass, lights, gibs), but cost 0.0-0.1 records a tic a client after the join, so a list
would save only their enter records at a join. Decided with Phase 6's bulk baseline.

**Mirror spawns.** A client-side spawn - `A_SpawnItemEx` with `SXF_CLIENTSIDE`, or any of the four
spawners on a `+CLIENTSIDEONLY` class - is never made by the server, so a mirror runs exactly those
actions:
- **Gate in `SetState`**: `nofunction`, `g_bSnapMirrorSpawns` up, `this` a `NETFL_MIRROR`, and a
  spawner state whose owner (`StaticFindStateOwner`) has no special-running parameter expression.
  The action's own `NETWORK_ShouldActorNotBeSpawned` lets a client make only client-side spawns, so
  nothing the server makes doubles, and events and mirror spawns never overlap.
- **Per-frame advance only**: the mirror is moved back by velocity × the tics owed for the call and
  the child is owed them, so FAR mirrors (one frame in eight) keep their spacing.
- **Records** are judged against a ring of the last 16 states the mirror entered, kept only for
  classes with a spawner state: terminal hold; not animating (seed the ring); resync (in the ring,
  entered within the state's longest duration plus 4 tics of the record); behind (reachable
  forward); undecidable (reachable from the ring's oldest); otherwise a jump, which runs the
  target's spawn owed the record's age, capped at 8. The aging advance and the hard resync never
  spawn.
- **`P_RemoveThing` and `P_DamageMobj` refuse a mirror**, so a child's `A_RemoveMaster`,
  `A_KillMaster` or `A_DamageMaster` cannot reach it.
- **The missile's target crosses the wire.** `A_CustomMissile` in aim modes 0 and 1 spawns only with
  a target, and a mirror had none. The enter of a missile whose class has a spawner state carries
  its shooter's NetID: `SNAP_CLASS_TARGET`, the top bit of the enter's `classId`, says a 2-byte
  NetID follows, so the record delimits itself and every reader masks the bit. The capture keeps it
  outside `SnapEntityState`, never compared or checksummed; the client resolves it once, mirror
  first (`MirrorForNetID`) and then the real actor, and the pointer lives only on the mirror.

On Complex Doom MAP05, per 5 s: 18-90 jumps against 40-209 resyncs, undecidable 0-1.

---

## 7. Sectors, polyobjects, and ACS

**Far less needs to move than "sectors onto snapshots" suggests**, and what does is not the planes.
The rule is what READS the field. A sector effect whose output reaches only the renderer ships as a
DEFINITION and runs locally on both machines; one a gameplay path reads ships as STATE.

**Lights are already definitions and stay so.** `DFireFlicker::Tick` and its siblings have no client
guard, and every `NETSTATE_SERVER` check in p_lights.cpp sits on a spawn or a state change, never on
the tick. Two RNGs diverge freely because nothing compares them: sector light feeds the renderer and
no gameplay path, which is the condition that makes local simulation correct here and wrong for
actors. **Phase travels only where the cycle is deterministic**: `DPhased::Tick` counts 63 to 0 as a
pure function of its phase, so `DoSectorLightPhased` carries `lPhase`; a flicker has none worth
sending.

**Scrollers and pushers are definitions too, but a client builds none from the map.**
`P_SpawnScrollers` and `P_SpawnPushers` make nothing in client mode, so a client creates them only
from `DoScroller` and `DoPusher`, sent at the join (`SERVER_UpdateMovers`), and ACS's `SetScroller`,
`SetWallScroller` and `AdjustPusher`. `DScroller::Tick` has no client guard and `sc_carry` feeds the
client's own `level.Scrolls`, so the local player's carry is predicted and a remote actor's arrives
as position. A joiner's scrolled texture offset is not sent: cosmetic.

**`DoScroller` carries the accumulated state.** An accelerative scroller's `m_vdx`/`m_vdy` integrate
and never converge from a wrong start, and a controlled one's `m_LastHeight` is a tic behind the
server's plane, since scrollers tick before movers. Velocity is its start value plus `m_dx` times
the control height's change, so it matches the server's whenever the client's control plane does -
which is why this waited for movers.

**A point pusher's source arrives after the pusher.** `DoPusher` precedes any snapshot, so the
constructor's lookup always failed. `DoPusher` now carries the centre and radius, and the client
keeps the source's NetID and looks it up in `Tick` while the pointer is NULL: real actor, then
mirror. Without a source neither end pushes. A source must be a `PointPusher` or `PointPuller`; any
other class holding the id means the source was destroyed and its id reused, so the client drops it,
matching the server's NULL. A map reset moves or hides a source rather than respawning it, so its id
is fixed from the map load.

**A repeated full update replaces them rather than adding to them.** A client re-sends its snapshot
request after 3 s, so a late reply gave two full updates on one level load, and two of every
scroller, pusher and waggle. `BeginSnapshot` destroys all three, and `SERVER_ConnectNewPlayer` sends
it and `UpdateMovers` in one call; a map change loads a fresh level and sends no `BeginSnapshot`.
Costs: a scroller a CLIENTSIDE script made before the snapshot is not re-sent, and a full update
spanning datagrams leaves a tic or two without them. Light and sector-link creation commands
presumably duplicate the same way; unchecked (Phase 6 step 3).

### 7.1 Movers

#### Why capture, not planes

**The client runs the motion; the server sends thinker state at transitions.** On the worst shipped
map (646 perpetual lifts) planes dirty 306-323 a tic against 5.9-6.6 transitions, and against
356-475 records a frame planes are unaffordable where transitions are noise. It is safe where §6
rejected it for monsters: a mover's travel is fully determined by speed, direction and target, where
`A_Chase` re-decides. Local collision is the non-deterministic part, hence three parts: capture
thinker state at transitions, stop the client blocking locally, and replicate blocked-ness as state.

#### The record

**A self-describing event type** (`SNAPEV_MOVER`): class, flags, sector or polyobject index,
instance generation, then a per-class body. The entity id field is zero and the index rides the
body, keeping the second namespace out of the entity one. **A body is the class's `Serialize` list
less what the client cannot use or must not hold.** `DCeiling` and `DFloor` drop `m_Texture` and
`m_NewSpecial`, read only at a terminal `pastdest`; the texture crosses as a name on
`SetSectorFlat`, since an `FTextureID` is a local index (§6.1). `DDoor`'s `m_BotSpot`, a vertex
pointer, crosses as a line end, since vertex numbering differs where a client builds GL nodes.
Elevators and pillars carry both planes. A flag may select a second body shape (`SNAPMVF_STAIR` adds
`DFloor`'s stair timers); flags precede the body, so readers dispatch on class and flags and never
derive a field position from the length byte. Bodies are full only; `SNAPMVF_UPDATE` is reserved for
an update form, which would save nothing on a real map.

**The legacy commands could not be reused.** They are a second, incomplete serialisation written to
announce events, where the fields they omit are derivable - `DoPlat` omits `m_Wait`, `m_Count`,
`m_OldStatus` and `m_Crush`, `DoCeiling` both crusher speeds, and `DMovePolyTo` has no command - and
`UpdateToClient` reused them to describe evolved state, where those fields are not derivable.
`Serialize`, maintained for savegames, is the list a capture is checked against.

#### Capture

**At both ends of `Tick`**, since a head capture alone loses a mover created, blocked and finished
inside one tic; the tail capture is skipped on `OF_EuthanizeMe`. **Countdowns are out of the dirty
compare and the constants they reload from are in**: the live counters ride every record for a
joiner, and a counter the client advances locally is safe provided its start value crosses at the
transition that sets it, which rules out `DWaggle`. **The capture struct is zeroed before filling**,
since the dirty test is a `memcmp` and the bodies carry padding. Capture needs `fua_snap` as
well as `fua_snap_movers`, since only the snapshot tick releases slots.

#### Slots

**A gone slot is held until every started, in-map client has acked its GONE.** Delivery is
guaranteed only while the slot exists, and an early release leaves that client's plane wrong, or its
mover running, until a reset or a rejoin. A client that has never acked holds nothing from the slot
and falls to a backstop of `2 x SNAPCL_MAX_PENDING` frames, so `fua_snap_debug_ackdelay` must stay
well below
128. A full table takes the oldest gone slot.

**Reuse is keyed on the sector plane**, which is what the hazard is about. The server runs one mover
per plane, so a live capture on a plane with a gone slot takes the slot over in place, its seq
continuing so its record supersedes the pending GONE, which would otherwise end the successor when
it lands late or is re-sent. `MoverPlanes` is the one class-to-plane-set map. **The generation
names the instance**: never 0, from one counter that is never reset; a record applies only to an
occupant of its class and generation.

**Two-plane movers.** `DElevator` and `DPillar` drive both planes and have no state machine, so they
record at creation, on a change of blocked-ness and at the GONE. A pillar's planes move
independently, so its body carries a per-plane `blocked` mask. Takeover is by plane set: a gone slot
whose planes the new mover covers is reclaimed; one holding a plane the new mover does not take is
narrowed, its GONE keeping only that plane (`SNAPMVF_NOFLOOR`/`SNAPMVF_NOCEILING`) so a re-send
cannot end the successor. `DSectorEffect::Destroy` clears both plane pointers, so both reset loops
stop both sound channels before destroying either - a stock bug.

#### Blocking

**The client does not block locally.** `MoveFloor`, `MoveCeiling` and `PO_MovePolyobj` consult an
actor set the client does not own: a client measured `crushed` on 459 of 477 tics while the server's
plat rose. Two vetoes are discarded: `P_ChangeSector`'s, ahead of its deep-water block, which is
gated on `nofit`, and `MoveAttached`'s, which gates twelve of `MovePlane`'s `return crushed` sites;
`P_MoveLinkedSectors` can also fail on a floor crossing its own ceiling without consulting either.
The server owns the crush and sends the death.

**Blocked means stalled: `crushed` with the plane unchanged.** A Doom crush moves the plane and
still returns `crushed`, so a Doom crusher is not blocked; a Hexen crush stops; a plat blocks only
as `platToggle`. Blocked-ness is a boolean per mover, emitted on change, and a blocked record writes
the plane, since the stall arrives a round trip late.

**Every outcome ends in a record that writes the plane**: the GONE, an at-rest record or a pillar's
stall, including `MovePlane`'s instant branch, which reverts a blocked last step and returns
`pastdest`. Two remainders are bounded by one step - a `DCeiling` crush-and-raise blocked at its
last bottom step, and a resetting stair blocked at its last step - each converging at the next
clamp; writing the plane on moving transitions would hitch every cycle.

**A plane the client cannot derive is written from the record**: stopped mid-travel (the at-rest
record, never while moving), destroyed mid-travel (the GONE carries the plane and is applied before
the thinker goes), created mid-session (the first record, captured after the server's first move
tic). **Every snapshot-path plane write carries attached geometry** - linked sectors and 3D
midtextures through `MoverMoveAttached` by the same delta - since every correction after the
join's absolute writes is relative, and attached geometry skipped once stays out for good.

#### The client sink

**Ownership.** The client skips an absolute write on what a record owns, since the join's writes
race the records: the plane of a sector whose mover has taken a record (`MoverOwnsPlane`), a plane
linked to an owned control (`LinkControlOwned`, direct controls only), and the mid y offset of a
line attached to an owned plane (`Midtex3dLineOwned`). A skipped write also skips its
`P_MoveLinkedSectors`, which is why the snapshot path's own writes carry attached geometry.

**A record older than one already applied to its plane is refused.** Payloads apply out of order, so
the frame number is checked against a per-sector, per-plane stamp, rebuilt when the stream epoch
moves. **The occupant is checked by class and generation**: a live record evicts another mover on
its plane; a GONE ends another tracked occupant while that class's legacy is gated, a GONE being its
plane's newest state; an untracked occupant is left; a GONE with no local mover still writes the
plane. A new class needs a line in `snapmv_RecordClassOf`, `MoverPlanes`,
`MoverSuppressesLegacy` and `MoverOwnsPlane`.

#### Polyobjects

**One record per polyobject, listing every action on it** (`SNAPMV_POLY`), since the `Polyobj_OR_*`
specials run a second thinker on a busy polyobject. The header carries `StartSpot`, `angle` and a
count; then the live actions in creation order - rotate, move, move-to, slide, swing - each with a
generation and the flags `SPECIAL` (the entry `specialdata` names, so `PO_Busy` and a CLIENTSIDE
`PolyWait` agree with the server), `NOINTERP` and `BLOCKED`. The body is walked from the count and
kinds and refused where the walk disagrees with the length byte. An action records at creation, a
change of blocked-ness and the GONE, a door at each reversal, and a perpetual rotation every 175
tics, staggered by index, since nothing else re-anchors its phase.

**Server.** Each `FPolyObj` lists its actions (`snapActions`) and owns the slot, so takeover is by
construction; a hub return also restores `specialdata`, which nothing serialised. Blocked is a
failed move or rotate, which reverts the whole step. The reset destroys every listed action, ending
override orphans stock left moving.

**Client.** A record-made action runs the server's arithmetic and holds while blocked or once its
countdown runs out; a CLIENTSIDE script's actions are left alone. **Position is written per axis** -
translation for move, move-to and slide, rotation for rotate and swing - on the first record, a new
action on that axis, an axis at rest, the GONE and the rotator resync; never while the axis moves,
which is the stutter.

**Sound.** `PlayPolyobjSound` is broadcast at creation and sent to a joiner from `UpdateToClient`. A
polyobject has one sequence, so an override move replays the start sound and its end silences a
rotation that carries on, as in stock.

**Limits.** An override polyobject with one action moving and another blocked on the same axis is
written only when that axis rests. The server's thrust on the local player is not predicted.

#### Off the capture

**Waggles stay on legacy.** `DWaggle` recomputes its plane from an accumulator every tic and has no
transition to capture; the server sends its full state at creation and join and resyncs every 5 s.
`client_DoWaggle` evicts a tracked occupant, and `client_DestroyWaggle` sets the plane to
`m_OriginalDist`, since the client's waggle runs a network delay behind and the destroy would stop
it a sliver short.

**`DAnimatedDoor` is replicated by its effects**: `SetLineTexture`, `SetSomeLineFlags`, an instant
`SetSectorCeilingPlane`, `StartSectorSequence`, and the join's textures, flags and heights. One rare
race is open: if the predecessor's GONE has not arrived when the door starts, the client skips its
open write as owned and the GONE writes the old height, so the ceiling reads closed until the door's
closing write.

**CLIENTSIDE movers are unsupported.** A client-only action with no record ends on a server destroy
that never comes, as in stock.

#### Gating legacy

**The server stops the legacy mover state.** `MoverServerStops` sits in the 28 `SERVERCOMMANDS_*`
state functions of the seven tracked classes and at the 16 verification sites (`SetSector*Plane`
from the movers' `Tick`s and stops, `SetPolyobjPosition`/`Rotation` from `TickPoly`), gated at the
call because those commands also serve the join and the reset. The gate needs `fua_snap_movers`,
`fua_snap`, `fua_snap_send` and `fua_snap_nolegacy` 3. Kept: the sounds, keyed on the
sector or polyobject (the door's as `SVC2_DOORSOUND`); the waggles; `DAnimatedDoor`'s effects;
`SetSectorFlat`; the join's and the reset's absolute writes and `DestroyAllSectorMovers`. **The lift
stutter went with the legacy writes**: a legacy correction landing on a plane the client's thinker
moved in the same tic drew two displacements as one step.

**`SnapMoverGate { legacyOff, frame }` tells the client**, reliably, since `fua_snap_nolegacy`
syncs only to RCON admins and `fua_snap_movers` not at all: after `BeginSnapshot` and ahead of the
movers on both join paths, and to every client when the predicate changes.
`MoverSuppressesLegacy(cls)` decides the handlers, ownership, eviction on GONE, a GONE with no
local mover and timed doors. **A flip hands the live movers between paths**, a test facility until
Phase 6 deletes the gate: gate-on drops what legacy made and applies each polyobject's newest
record; gate-off drops what records made while the server re-sends the live movers as legacy, and
the client refuses records built before the frame after the flip.

**Timed doors** (`doorCloseIn30`, `doorRaiseIn5Mins`) are spawned by `P_SpawnSpecials` on both
machines, and a client's copy outlives a finished server door: the gate drops them on a first
connection, later maps skip the spawn, and the server respawns them after a map reset, since
`SavedSpecial` misses them.

#### The join and the reset

**Join heights go in link order.** `SetSectorFloorPlane::Execute` propagates through
`P_MoveLinkedSectors`, one level deep, so a sector written before a control that moves it is
displaced for good. `LinkWriteOrder` puts every sector after each control driving it, by longest
path over floor and ceiling links, so each sector's own write lands last; a cycle cannot be ordered
(§9.3). Every driven plane is written, flagged or not, since a link made after its control moved
would otherwise keep that move on a joiner. **Join heights are full precision** (`Fixed`, not
`AproxFixed`): a record sets the plane absolutely but moves attached geometry by its delta, so a
lost fraction stayed in a 3D midtexture.

**Records and the join's absolute writes are unordered**; ownership keeps the older writes off what
a record owns. Residual: a driven sector with motion of its own before the join stays off by it.

**A map reset opens a window in which nothing is owned**, since its absolute writes postdate every
record and the reset destroys the owners. The client's `GAME_ResetMap` opens it and destroys the
client's movers; `DestroyAllSectorMovers`, sent after the server's writes, closes it. Inside it
`SetSector*Plane` skips linked propagation, since the server sends each driven sector its own write.
Every reset reaches the client ahead of the server's writes, except an invasion client past wave 0
at the first countdown.

**Mover records are bound to a reset count**, carried by every snapshot and learned from
`DestroyAllSectorMovers` and `SnapJoin`. A record ahead of the client's count, arriving with the
window open or before the count is known, is held - coalesced per plane and generation, at most
three a plane - and replayed after the destroy or the `SnapJoin`; one behind it is dropped. A window
open across two reports is stuck, and turns ownership and linked propagation off until a reconnect.
**A replayed mover is advanced by its hold**, through its own `Tick`, by the gametics since its
newest record arrived, up to 5 s: replayed late, a perpetual crusher would run behind for good.

**The reset restores a 3D midtexture once**, absolutely, in the line pass; upstream's extra scroll
by `-MoveDistance` is gone.

### 7.2 ACS

ACS runs server-side only (it already largely does); its client-visible outputs are either world state
(covered) or HUD events (reliable channel). CLIENTSIDE scripts remain a local-only layer - **and that
layer can hold state the server cannot, which is not merely a difference but a better answer** (§6.1).
Anything replicating a field a CLIENTSIDE script also writes must decide which wins, and the server is
not automatically right.

---

## 8. Time base and tick synchronization

**The platform tic derivations disagreed, and this tree shows why.** Windows derived tics from
`timeGetTime()` and SDL/Linux from `SDL_GetTicks()` — two independent millisecond-granularity clocks,
against a tic period of 1000/35 = 28.571… ms that has no exact millisecond representation. Truncation
behaviour and timer resolution differ per platform, so a "35 Hz" server on each ran at slightly
different real rates and drifted from wall time in different directions. For netcode that costs
twice: client logic assuming exactly 1/35 s accumulates error against a real server, and clients
generate usercmds on *their* notion of 35 Hz while consuming state produced at the *server's* —
mismatched rates starve or flood the command buffer, seen as periodic warping. The POSIX timers had
two defects of their own: the signal path (`setitimer`/`SIGALRM`) counted tics in a handler, so
expirations that coalesced under load were lost with no catch-up and a busy Linux server ran slower
than 35 Hz; the polled `SDL_GetTicks` fallback busy-waited at 100% CPU.

**Fixed, and shipped.** `i_tictime.{h,cpp}` is the shared implementation; `I_GetTimePolled` and
`I_MSTime` derive from it on both platforms and `timeGetTime()` is off the tic path. Tics come from
dividing the whole elapsed span of a monotonic high-resolution counter (`QueryPerformanceCounter`,
else `clock_gettime(CLOCK_MONOTONIC)`) by its frequency in 64-bit arithmetic, so the rounding error
stays below one tic and the long-run rate is exactly 35.000 tics/s on every platform, immune to timer
granularity, `timeBeginPeriod` and NTP slew. The polled wait sleeps to within 2 ms of the next tic
and spins only for the rest, and `I_SelectTimer` no longer selects the `SIGALRM` counter. It
benefits the legacy path too, which is why it landed in Phase 0. The SDL half (`clock_gettime`, the
same division, the `usleep` wait) has run as well: a Linux dedicated server held 28.571 ms a tic
over 33 minutes and five maps, recovering load spikes of up to 136 ms with catch-up tics and losing
none, and a Linux client beside it, rendering at about 170 frames a second, kept its prediction
depth at a tic or less outside a map change.

**A client clock model dates the ghosts' ages.** It fits server time as a line over the last 128
(game tic → arrival time) pairs, converging on the server's actual tic period whatever it is. With
`fua_csnap_age_servertime` (on) a record's age runs from the model's date for its tic rather than
from its arrival (§6). The client stamps a packet when its frame gets to it, so arrival jitter is
mostly the client's own frame time - about a third of it - plus a few ms from the server. The model
is trusted only after 160 accepted samples since a reset, by which time a burst after a load has
left its 128-sample window, and while the window's mean residual is past `fua_csnap_age_maxjitter`
(50 ms) ages fall back to arrival: on a client at 100 ms a frame and more, the model's dates drew
worse than arrival. Two findings from validating it against recorded traffic:

- **Track the current tic period, not a session average.** A windowed fit follows the regime the
  stream is in, which is what a sampler needs; a stream shifting 29 → 40 ms/tic reports 40.000 and
  the reverse 29.000. Session-wide characterisation is a separate number.
- **Residual-based outlier rejection must be permissive, and asymmetric rejection biases.** An early
  version rejected samples over 250 ms off the fit and read five consecutive rejections as a clock
  discontinuity — so a saturated server, whose pump duration legitimately swings by hundreds of
  milliseconds, was misread as a broken time base and the model never converged. Detect genuine
  discontinuities on *consecutive-sample* anomalies instead (time running backwards, or a multi-second
  gap) and reserve residual filtering for wild samples. Slow pumps produce large positive residuals
  while fast ones cannot produce equally large negative ones, so aggressive rejection skews the
  estimate toward "faster than reality".

**Command pacing feedback — specified, not built.** The server would include in each client's
snapshot a one-byte report of that client's usercmd buffer depth relative to target, and the client
would nudge its local tic interval by a bounded fraction (±0.5%) to hold the buffer there: the
standard Source/QW tick-sync loop. It replaces "both sides run 35 Hz, surely" with an explicit
controlled variable, and it is what would make §5.2's per-client rate divisors safe. Those divisors
should not be used until it exists.

**The cost is now measured, and it is not small.** With the local player's corrections applied on
arrival rather than through upstream's command buffer, **18.7% of them land in a tic that already had
one** - on a link with zero jitter and zero loss. The rate falls as jitter rises (18.7% at 0%, 19.1%
at 5%, 6.2% at 30%), which is the signature of a beat between two free-running 35 Hz clocks that
jitter smears out rather than anything the network did. Upstream's buffer absorbs it by releasing at
most one correction per tick, and `numConsistentArrivals` is that absorber; reconciliation error is
unaffected either way, so what the buffer buys is cadence, not accuracy.

---

## 9. Migration plan (dual stack)

The old and new protocols coexist during development; a connect-time flag selects per-client, so the
game is playable at every phase and A/B comparison is trivial.

**Phase 0 — instrumentation + time base (days). ✅ COMPLETE (July 2026).** Add `fua_snap_stats`:
per-tic counts of live netID actors, *awake vs. idle* split, dirty actors, byte estimates — run
real maps (Complex, slaughter, survival) to get the distributions that validate §5's budget defaults;
the awake/idle ratio on slaughter maps is the single number that proves or breaks §5.1. Land the
unified monotonic timer from §8 (both i_system.cpp files) — it benefits the legacy path immediately
and shrinks the drift the new protocol must tolerate. No protocol change.

**Phase 1 — vertical slice (the de-risk milestone). ✅ COMPLETE (July 2026).** Server:
`capture.cpp` — end-of-tic sweep of `g_ActorNetIDList`, build/delta/quantize/compress, single
hardcoded budget, hook at the end of `SERVER_Tick` (sv_main.cpp). Client: `entities.cpp`
ghost table + hollow-actor render shim + interpolation; snapshot channel drives *remote actors
only*; everything else (players, sectors, HUD, sounds) still rides the legacy protocol. Local player
untouched. All exit criteria met: two clients on a monster-heavy map (independent per-client
streams, flat per-client cost), bytes/s within budget and below the legacy protocol above the
crossover, no legacy MoveThing traffic for monsters at `nolegacy 3`, and correct behaviour under
≈230 ms / 10% injected latency and loss. Three items surfaced during Phase 1 are carried into the
phases below rather than back into it: ghost-only-client **sounds** and **collision**, and the
**MAP13 refresh floor** (a contested-set-size problem, Phase 2).

**Phase 2 — monsters. ✅ COMPLETE (August 2026)** (see §9.1 and §9.3). Multi-packet delivery, the
band scheduler, sounds, client animation, optimistic referencing, appearance and missile suppression,
and near-tier collision are built and measured. 390 records/frame at 96 kB/s against stock's 97 (87-100
after Phase 4's encoding, §9), every
distance band at ~33 Hz, and collision free at every radius tested. Judged by eye against legacy on
the same scene, monsters are on par.

**Phase 3 — players onto snapshots. ✅ COMPLETE (August 2026).** Remote players are ghosts, psprite
state streams for every player (§4.2), and the local player's authoritative block is on the wire with
`cl_pred.cpp` reconciling against it (§4.2 for the block's constraints, §6 for the mirror and
renderer work). Unlagged untouched server-side. Shipped behind `snap_playermirrors`,
`snap_nolegacy_players` and `snap_nolegacy_localplayer`, all on: reserved player ids (§3.1), which
closed the NetID half of tracker 2552 on the way; crouch as a value on the wire; the psprite stream
and its client apply; and `SNAPEV_LOCAL`.

**Gating a command means inheriting its side effects, and two of `MoveLocalPlayer`'s are not
prediction**: `CLIENT_SetLatestServerGametic`, read when building every outgoing usercmd and so what
lag compensation rewinds against, and `g_bClientLagging`, read by the renderer, `d_main` and `p_tick`
and stuck at its last value if nothing writes it. §6.1's derivation contract in the direction where
the server is the producer - **enumerate what a command produces before gating it.**

Nothing is left open. The psprite stream cannot drive animation on its own and the local machine
stays beside it (§4.2); removing either needs receiver-side prediction of the weapon layer, which is
Phase 6 and only if something needs it.

**Phase 4 — record encoding. ✅ COMPLETE (August 2026).** Items 1, 2 and 4 shipped, item 3 dropped.
The encodings are in §3; where the bytes go is §5.2, and the wire figures are §9.1.

| | payload B/rec | wire B/rec |
|---|---|---|
| before | 9.80 | 7.84 |
| 1. sorted-id varint (v21) | 8.25 | 6.27 |
| 4. narrow APPEAR (v22) | **8.03** | **6.15** |
| 2. self-describing event types (v23) | structural | |

**21.6% off the wire, of which item 1 is 20 points.** Item 2 moved no byte and was built for a
property rather than a saving: an unknown type is a hard stop, so before it every new type was a
lockstep version bump across both binaries, and Phase 5's second id space is its first user.
`ZSNAP_VERSION_MIN` stopped tracking `ZSNAP_VERSION` there, for the first time.

**Item 3 - an APPEAR-only short record - is DROPPED**, because items 1 and 4 took two of its three
savings: the id is already one byte and APPEAR is already two. What remains is the mask byte on ~9%
of records, ~1.1% of payload, which item 2's length prefix would spend back. **The extended entity
mask item 2 originally proposed is dropped too**; §3 carries the rule that replaces it, and none of
the three features it was for has a consumer.

**Selection is unchanged by any of them.** The drain charges each record the 2 bytes the fixed id
cost - it cannot know a gap, since emission is not sorted until layout - so the encodings land as a
smaller payload rather than a different frame, which is what makes "records/frame unchanged" a usable
acceptance criterion. If the budget ever binds, the reclaimed bytes are what to spend.

**Phase 5 — movers onto snapshots. ✅ COMPLETE (October 2026).** `DPlat`, `DCeiling`, `DFloor`,
`DDoor`, `DElevator`, `DPillar` and polyobjects are captured on a self-describing type, the client
no longer blocks its own movers, and blocked-ness is replicated (§7.1). Waggles stay on legacy and
`DAnimatedDoor` is replicated by its effects; lights, scrollers and pushers stay definitions, the
scroller and pusher state riding their commands (§7). Under the mover gate the server stops the
legacy mover state and its plane verifications; the handlers stay for a server that does not gate
and go with Phase 6's state commands.

**Acceptance is the measured repro, and it passes for every tracked class**: a client joining
mid-cycle ends with the same plane and phase as the server. It is checked as a three-way
`fua_snap_planedump` diff - server, a client present from the start, the mid-cycle joiner - with the
movers stopped; the dump's `vbo` column is local to each client and outside the diff. Not
`fua_snap_sectorcheck` or any tag-keyed instrument: a linked mover is driven from a control sector
whose tag differs from the sectors it moves, so the dump walks `sectors[]` and names indices. It
reads zero disagreement across joins, loss and reorder, linked sectors, attached 3D midtextures,
resets taken with movers live, stalls, crushes, takeovers, overrides, stops, reset holds and a hub
return.

**Phase 6 — events cleanup.** Reclassify every remaining legacy command as event (keep, on the
slimmed reliable set) or state (kill), the legacy mover handlers, their senders and the mover gate
among them. Delete the full-update special path - a join becomes a reliable handshake plus a
null-reference snapshot - and **join parity lands here** (§9.3): the bulk baseline has no home until
this handshake exists, and the 1.7 s `JOIN FILL` figure is what it has to beat.
**`fua_snap_nolegacy` levels 0-2 are retired**: they were steps to level 3, nothing tests them,
and every change would otherwise carry an untested legacy branch. The server becomes fork-clients
only. **A reference build is tagged first**, the last with every level, as the fixed point for A/B
runs and the fallback the in-build switch was. In order:
1. **Measure.** Legacy bytes per command and channel, counted per recipient in
   `NetCommand::sendCommandToOneClient`, with reliable resends apart, plus what is written outside
   `NetCommand` and the reliable send time. Sounds lead: `SoundActor`, `SoundPoint`, `WeaponSound`
   and `SoundActorIfNotPlaying` are 84-94% of reliable command bytes, each carrying its name as a
   string (~17 of ~24-29 B); then `Earthquake`, sent to every client whatever the tremor radius, and
   `SpawnBlood`. On a simulation-bound server the reliable send is ~2.4 ms a tic, so cutting sounds
   is bandwidth, not the overload.
2. **Cosmetic actors (§6.3), built.** `fua_snap_nonetid_classes` leaves at step 4 or when mode 2
   becomes the default. What can close of the mirror spawns' known limits (§9.3) comes just before
   step 3.
3. **Reclassify in the counter's order.** A state command's sender is deleted outright, not gated.
   First sounds - by index where the tables match (§6.1), then their channel (unreliable, or events
   with a deadline; the §6.1 distance cull and point-sound rule for unknown ids stay) - then
   `Earthquake`, only to clients inside the tremor radius. Each item states its behaviour for an id
   the client does not hold yet. Folded in: the read-only actor command sweep, the `AproxFixed`
   slope normals, duplicated light and sector-link creation at a join.
4. **The join**, with the link-cycle `noPropagate` pass. Deleting the full update, the legacy mover
   handlers and the gate removes the levels' join and mover paths; the remaining `fua_snap` and
   `nolegacy` checks go as dead code in the same step.

**Phase 7 — deletion and hardening.** Remove the legacy state protocol, the client-mode guards
across the gameplay tree, Huffman; widen ids per §3.1 (doing it at the end avoids carrying the old
protocol's assumptions); revisit the cvars and tuning levers, archive flags included (§10.1); new
demo format = recorded snapshot stream + reliable stream, which incidentally yields seekable demos
and free "join any demo at any point." **The decoder is fuzzed already**: malformed snapshots,
cosmetic event streams and recordings must never crash a client, and `fua_snap_fuzz` in the engine
and `snap_fuzz` standalone feed mutated inputs through the real readers (maintainers guide,
`fuzz.md`).

Each phase ends green: playable, measurable, revertible.

---

### 9.1 Measured baseline (Chillax MAP13)

One client on MAP13 (~7,200 entities, ~1,000-1,100 hunting monsters in a fight) unless noted.
**Anything measured before the report drain (§10.1) is not comparable**: the five-second stats block
cost 220-250 ms of console output, which built an 8-tic backlog that `fua_snap_maxcatchuptics` then
processed in one pump, so every distance figure carried one self-inflicted catch-up per window in its
tail. Medians and directions survived it; worst cases did not. Earlier still, anything before
`snapcl_ApplyFrame` was corrected (§9.2) worked from a dirty set roughly half its true size.

**Why the legacy protocol feels instant.** It applies each update the moment it arrives -
`MoveThing` sets position, `SetThingState` sets the frame - with no buffer between receipt and
display. It does not simulate or predict monsters; it simply has zero latency. It is also
event-driven, so a moving actor generates updates continuously and a still one costs nothing, with
no per-frame budget deciding who is served. Below the link's capacity every moving thing is current.

The two fail in opposite directions, structurally: legacy is exact until volume exceeds the link and
then collapses without warning, while the snapshot path rations a fixed record count and degrades
smoothly from the first frame. **Matching legacy below its wall is the fork's bar; not having a wall
is what it buys.**

**Stock measures 97 kB/s** on the MAP13 room, from its own per-class census - 84% of it `DoomImp`,
`Revenant` and `RevenantTracer`. The fork runs the same scene at **39-40 kB/s** at a single-datagram
budget and **87-100 kB/s** at `fua_snap_maxbytes 4500` over three, where it also matches stock's
record rate. The crossover against the snapshot path sits at roughly **540 dirty entities per tic**:
below that legacy is cheaper, above it
snapshot is, and the gap widens without bound. Legacy's reliable channel also retransmits what a
stalling client has not acknowledged, so its recovery is proportional to the backlog;
delta-against-acknowledged has no such term.

#### Where the frame goes

At the shipped `fua_snap_maxbytes 4500` over ~2.8 chunks and a ~9 B record, **356-475
records/frame** across fourteen consecutive combat windows:

| band | records/frame | rate / population |
|---|---|---|
| d3 | 90-193 | 35.0 Hz |
| d2 | 45-132 | 35.0 Hz |
| cosmetic | 44-214 | 32.5-35.0 Hz on 44-222 |
| d1 | 19-45 | 35.0 Hz |
| MISSILE | 11-35 | 35.0 Hz |
| dlast | 5-43 | 34.6-35.0 Hz |
| animref | 14-16 | 0.3 Hz on ~1,690 - appearance floor AND the verification sample (§9.3) |
| d0 | 3-11 | 35.0 Hz |
| enter | 6-9 | 35.0 Hz |
| d4/d5 | 1-14 each | 35.0 Hz |
| top | 0-1 | players and the aging guard |
| refresh | 0-1 | position holds, 2 s bound |

**Position is no longer rationed in this scene; appearance is.** Every distance band runs at the full
35 Hz and the only starved band is `animref`, at its configured 0.3 Hz. The constraint has moved from
the budget to the appearance backlog - and past 4500 the budget stops binding entirely (§9).

Against a contested set of **2,000-2,150** entities, of which **15-20% owe position** and 81-85% owe
appearance.

**Wire, at v23.** ~1,150-1,300 B payload per datagram at 1.30-1.33x, peak compressed ~1,212 B whether
the frame spans three chunks or four - ~190 B of margin under a 1,500 B path MTU with IP/UDP headers.
Snapshot channel 87-100 kB/s, everything else 2-3%. Pump 4.0-4.7 ms/tic, `sim` 1.00 throughout. Zero
lost frames, zero rebaselines, zero decode errors. Before Phase 4 the same scene read ~1,300-1,500 B
at 1.24-1.36x with a peak of 1,322-1,369, and 9.80 B of payload per record against today's 8.03.

Sizing uses the **peak** ratio, not the mean, since MTU safety is a worst-case property. At
`fua_snap_maxbytes 1900` the peak reached **1,540 B**, which with 28 B of IP/UDP exceeds a 1,500 B
path MTU: invisible on a LAN, and over a real path it fragments, losing the whole datagram exactly
when the frame is fullest. Both cvars had to move, because `sv_maxpacketsize` bounds the datagram
while `fua_snap_maxbytes` bounds only the snapshot command inside it. The advisory in the stats
line reports clamp headroom and knows nothing about MTU, so it will happily suggest ~2,000 B.

**Interest management.** The contested set inside a given radius, averaged over combat windows:

| ring | contested | cumulative |
|---|---|---|
| <512 | 17-24 | 24 |
| 512-1k | 71-163 | 187 |
| 1k-2k | 166-441 | 628 |
| 2k-4k | 387-608 | 1,236 |
| 4k-8k | 627-1,010 | 2,246 |
| >8k | 601-1,046 | 3,292 |

53-87% of the contested set reads *in front* of the viewpoint, and 87-91% when facing the swarm
squarely, which is why the
cull tests distance **or** cone rather than both - a facing-only test culls almost nothing. Radius is
monotonic in refresh with no knee, so the limit on tightening it is visual rather than arithmetic -
and on open maps distance does not imply invisibility (§5.1), so it is an operator knob per map
rather than a default to tune against MAP13.

#### Multi-client and impairment

**Cost is linear in clients and the server is not the wall.** Re-measured clean at 2 clients: both
see the same entity to within one record (§9.3), 0 rebaselines, every band still at 35.0 Hz, ~100 kB/s
each and the peak datagram unchanged at 1,310-1,323 B. Earlier, at 1, 4 co-located and 8 scattered
clients on a live fight: wire total ~48 / 193-195 / 377-396 kB/s, i.e. ~48 kB/s each throughout;
records/frame held at 136-142 each; **zero rebaselines**; all clients holding the full world. `sim`
stayed at 1.01-1.10 tics per pump, so the server never entered catch-up. The four co-located clients
are what prove no per-entity server state is *consumed* by the first client's pass rather than read -
they saw the same fight and resolved it identically.

**netem 100 ms + 5% loss passes**, closing the last Phase 1 exit criterion: zero decode errors, zero
rebaselines, zero event orphans, full world retained - including branch events, which ride blind
copies and were the specific risk. The cost is service, not correctness: monsters roughly halve in
rate and double in jump distance, while missiles degrade proportionally because they hold a reserved
share. The mechanism is `pending` going 2 to 6, so `unackedMask` re-sends each group three times as
often - **the re-send tax is a LAN floor, not a typical value.**

#### Encoding measurements

**Bit-packing is worth ~1.15x, not 2.67x.** Measured twice on two populations. A recorded server
capture - the global world diff, before the per-client priority pass rations it - reported 2.67x raw
density. Re-run against the client stream, what one client was actually sent, over 179,457 records:
1.14x absolute, 1.31x delta-coded with per-axis presence, +0.03x for class-default appearance bits.

The gap is the population. A server capture is dominated by dormant monsters cycling animation
frames: small records, highly repetitive, positions that barely move. The prioritised per-client
stream is overwhelmingly *moving* entities with genuinely large position deltas - precisely the
traffic packing helps least, and precisely the traffic that ships. **The right measurement taken on
the wrong population**, an error also made sizing per-axis presence, the loop hint and the
dictionary (below).

**The shipped `zstddict` costs ~3%, measured live and left in place.** A zstd dictionary primes the
compressor's window with representative data so repeated patterns compress from their first
occurrence - it matters most on small payloads, which is why §5.4 called it a requirement. The
shipped lump predates snapshots and is trained on legacy per-actor commands, so its entries rarely
match snapshot records.

Measured on a MAP13 fight by disabling it: compression **1.38-1.42x undicted against 1.34-1.38x
with**, 46.2-47.0 kB/s against 47.5-48.5, peak compressed slightly lower. Consistent in every
window, in the same direction.

An earlier offline test suggested 3.06x against 2.36x - a ~20% effect. It was taken on concatenated
frames of the SERVER capture, which carry more intra-chunk redundancy than a real per-client packet.

Left in place for now: the dictionary applies to every outgoing datagram in `NETWORK_LaunchPacket`,
not only snapshots, and the legacy channel it was trained on is still partly ungated - so skipping
it for snapshot payloads is the correct shape of fix rather than deleting the lump. 3% does not
distort anything at this stage.

**Worth revisiting, for a specific reason.** When this was measured the channel carried one record
shape, self-similar enough that zstd found most of the redundancy unaided - which is why undicted won. Movers,
polyobjects and the local block now share the packet, making it heterogeneous, which is the regime
where priming pays. Two conditions on that: measure on real per-client packets, and remember that a
retrained dictionary is a versioned artifact both ends must hold byte-identically, with the same
deployment discipline as the wire format.

**A payload shrink is not a wire shrink, and the sign is not predictable from the field.** Three
measurements, three answers. Appearance suppression saves ~8% of payload and loses ~8% of compression,
exactly neutral (§5.2). The id varint saves 0.95 B/record of payload and 1.34 on the wire, RAISING the
ratio 1.253 to 1.352. The narrow APPEAR saves 0.21 and keeps 0.12, about 57% of it. The difference is
what the removed bytes WERE: a sorted id interleaved between varying record bodies offers zstd no
repeat and was compressing worse than the packet average, so removing it took filler away and the
ratio rose; `qstate`'s high half is mostly zeros and was already folded, so removing it took
redundancy away and the ratio fell. **Measure the wire, not the payload** - the payload figure
describes the mechanism and says nothing about the result.

**Delta-coded values and per-axis presence are not freely available.** The server deltas against the
last ACKED state while the client's newest is the last RECEIVED one; those diverge whenever records
are in flight, so value-level delta coding would silently decode against the wrong reference.
Group-level presence is safe only because `unackedMask` widens the mask until acked. Closing this is
optimistic referencing (§9.3). **The Z half of per-axis presence needed none of it** and shipped in
v11, because Z is unchanged on 78.6% of position records against *either* reference.

### 9.2 Invariants established by implementation

Constraints proven by building the thing, as distinct from §9.3's open questions. Each cost at least
one wrong turn; the instance is kept only where it is what makes the claim checkable.

#### Instruments

**A correct zero and an inert mechanism look identical.** Work out what a counter's population can
CONTAIN before predicting what it will read. **A counter stuck at zero is a claim about the code, not
the world**, and **a new mechanism needs its counter in the same patch.** `SNAPMVF_BLOCKED` shipped
without one and sat unattributable for a phase: a server that never blocks, a flag that never reaches
the wire, and a flag arriving a tic late all read the same, and four counters spanning capture,
emission, arrival and use separated them in one run. **Two clients under different impairment in one
run is what makes a zero readable** - the impaired one proves the predicate reachable, the clean one
that nothing preempts the mechanism.

**A counter buried in a larger population cannot be read from that population's total.** Movers are
a couple of records against hundreds, so a nonzero repaired-frame count said nothing about the mover
branch; its own counter, reading 0 in windows where no lost frame carried a mover, did.

**A sampling point chosen for a slow mechanism is wrong for a fast one.** A capture at the top of
`Tick` lags its own result by a tick: invisible across a long travel, total for a mover that
finishes inside one.

**A memcmp over a struct compares its PADDING.** Stale padding in the mover bodies read as a change,
~10x the records, and behaved for months only because the compiler reused the stack slot. **Zero a
struct before filling it if anything will compare it whole.**

**A mechanism the dual stack does the job for is untestable, not absent.** The ceiling's at-rest
correction was correct while a legacy plane verification beat it to the plane, and its counter read
a correct zero. **Gate what duplicates the mechanism before concluding anything from its silence.**

**A function with two exits needs a counter on BOTH.** The mover emission pass breaks on its record
ceiling or on the byte budget, and only the ceiling was counted, so "0 frames capped" said nothing
about the pass ending early. **Every visit must land in a named outcome.**

**A rename is the one edit the static checks here cannot catch.** Brace, paren, CRLF and
format-string checks all pass on a file that was never written, and a diff replay confirms the patch
reproduces the tree rather than that the tree holds what was intended. Check that a renamed symbol
is PRESENT, and cross-check every changed declaration against its definition.

**A test's inputs are part of the test.** The position quantizer's self-test checked a one-sided
bound, which fits truncation, against a quantizer that rounds, and passed on Windows only because
MSVC's 15-bit `rand()` never reached the sign bit or a rounding half. The format blocks
draw from a fixed-seed xorshift, so every compiler tests the same inputs, and run both in the
engine (`fua_snap_selftest`, `formattest.cpp`) and without it (the `format_test.cpp` cases and
the `snap_fuzz` target), which also checks that `format.h` needs no engine header beyond
`basictypes.h` and `tables.h`.

**A counter shared by several producers cannot attribute.** One at-rest column could not separate a
plat, which rests at every endpoint, from a ceiling, which rests only when stopped. Split per class.

**Three marginals are not a joint.** A narrow encoding is available only when all its conditions
hold in the SAME record; the census reported depth, offset and phase separately at 100%, 99.9% and
99.5%, and the joint, counted directly, read 99.42%. **Where a decision needs a conjunction, count
the conjunction.**

**A report can encode a design that was never built.** The encoding census charged every chunk
opener a byte that only a rejected variant spends. **A formula derived from a design under
discussion outlives the discussion.**

**A report must print the value the code USED, never recompute it**, and must not enumerate a set by
hand: `dirty groups` printed v10 labels against a v12 tally and hid appearance starvation for two
format versions. Tie columns to the constant with a `sizeof` assertion - **asserting against a
literal is what let the previous form be "fixed" twice by editing the number.**

**A counter that cannot be checked against another measuring the same thing is not yet an
instrument**, and where two disagree, the disagreement is the finding. Both must cover the same
POPULATION, and **a counter whose denominator moves with the scene cannot answer a yes/no
question.**

**Confirm what is actually running before instrumenting it.** A sprite tap in `r_things.cpp` never
fired: that is the software renderer and the build is GL. A comment is not evidence either: one
stated that clients spawn map things from the WAD, and a fix built on it addressed client actors
that do not exist at level 3 (§6.1's "check which machine produces the value"). The same holds for a
fix: a build without it reads as the fix failing until a trace shows it never ran.

**Release on the condition, not on a proxy for it.** The gone sweep tested elapsed gametics while
`moverBelievedSeq` - the condition itself - sat beside it; the proxy shrank eightfold exactly under
catch-up.

**Key a mechanism on what the hazard is about.** Slot reuse was keyed on thinker address while the
hazard was the sector plane; ordering stamps were keyed on the `sectors` pointer while they depend
on the stream, which a restarted server restarts at the same address.

**An exemption justified by one case is wrong for every case it lumps in**, and **ownership must
cover everything the owner moves**: the first-record exemption, argued from a join, also covered
every mover created mid-session, and a plane gate naming only the mover's own plane let late writes
land on its linked sectors and midtextures.

**A gate that discards writes must know which are newer than its owner.** The map reset's writes
postdate every record, so a gate keyed on ownership turned a correct write into a permanent error.

**Check a wire field's width and precision against what its consumer needs.** A sector index sent as
a byte broke on the first map above 256 sectors; a plane height in whole units broke under a 3D
midtexture moved relatively from it.

**A fix gated on a state flag inherits every caller that sets it.** Gating the join propagation on
`CTS_RECEIVINGSNAPSHOT` would have fixed `SERVER_ConnectNewPlayer`'s pass, passed its acceptance
test and left `server_AuthenticateLevel`'s live; the fix lives in the function both share.
**Enumerate the callers of the state, not the callers you reproduced.**

**An instrument aimed by a property its target SHARES cannot see a fault in whatever else shares
it.** Every mover trace selected its sector by TAG, while a linked lift is driven from a control
sector whose tag differs from the sectors it moves, so clean traces described the wrong sectors.
**And a cross-machine dump cannot be aligned by hand while anything is moving**: stop the movers.
Key on IDENTITY and let the outcome select - `fua_snap_planedump` and `fua_csnap_planetrace` walk
`sectors[]`, name indices and are diffed across machines, the only way to see a divergence that is
locally self-consistent, and each linked write is labelled by its CALLER.

**Check what a consumer READS before proposing what corrupts it.** Three mechanisms proposed for a
wrongly drawn plane all measured dead, two of them claims about the renderer made without opening
it; `FFlatVertexBuffer::CheckPlanes` answers both in one read.

**A field diff cannot see a consumer's branch on an object's IDENTITY** - every actor field matched
between a mirror and its player while the renderer asked whether it had a `player_t`. **Place a
comparison where the values are FINAL**, and **select a population by what identifies the thing
measured, not a property it shares** - a tap keyed on `NETFL_MIRROR` recorded whichever mirror drew
last.

**An instrument whose reporting perturbs the system cannot measure it.** The stats block cost
220-250 ms of console output on the game thread, building a backlog that `fua_snap_maxcatchuptics` drained
8 tics at once - everything on screen snapping together once a window, contaminating every
measurement taken before it was found. **Diagnostics on the game thread must be bounded per tic, not
per report.**

**A mechanism that selects its own samples cannot be measured by anything sampling its output.** Once
appearance suppression runs, the only APPEAR records arriving are the ones it could not derive, so
four arrival-sampled instruments read 20-30 points down regardless. Answers in increasing strength:
server-side decision-point counters; a population the mechanism does not reach; **best, CARVE OUT a
slice it is forbidden to touch**, which validates against itself since with the mechanism off both
columns must read equal. **Size the control group's noise from the ENTITIES, not the samples.**

**A derivability figure attaches to a SERVICE RATE, not to the mechanism**: raising `snap_animref_max`
15 → 60 moved both columns ~15 points with nothing about the suppressor changed. The GAP is the
measurement, and **any change to how often a band is served needs its control re-taken in the same
session.**

**Varying a budget varies the MIX, not only the size.** A `fua_snap_maxbytes` sweep meant to price
compression instead moved a fixed-record-count band's share 11.5% → 34%, and APPEAR is the most
compressible group in the packet; slicing one payload at constant content put the real constant 3.6x
higher (§5.4).

**A checksum over the wire state cannot verify a mechanism whose effect is on the MIRROR.** With a
group withheld, sender and receiver hold the same stale state and agree - the checksum read 0
throughout while appearance suppression discarded a correction it was named as the safety net for.

**A mechanism running BESIDE the thing it duplicates is verified by its counters and by nothing
else.** The psprite stream resolved 100% of states, agreed end to end and cost what it was predicted
to - while the client's own `P_MovePsprites` produced the same answer a tic earlier. Switching the
duplicate off is the only test, and it failed immediately. **Until then the figures describe
delivery, not the feature.**

**An instrument must answer the instrument's question, not imitate the code it measures.** Aging a
census's shadow to present "to match what the mirror does" is correct for RENDERING and a time-base
mismatch for COMPARISON.

**Check the instrument can resolve something smaller than the residual before chasing it.** `bystate`
samples on a state change while a Revenant's See chain runs 2 tics per state, so one tic of jitter
scores as a miss; `localanim` shares a frame across state pairs and reported "fine" through a visible
570 ms stutter.

**A census over a cyclic structure must ask where the walk LANDS, not what it passed through** - an
8-state chase loop means any gap over one cycle passes through everything, and the tell was that the
worst-served bucket scored best.

**A null result is only as good as the instrument's reach**: "raising the budget moved nothing" was
measured on a sprite metric over the NEAR tier while the extra records went to `d0`; measured
properly it moved DoomImp gap p50 340 → 59 ms. **And a change that measures as nothing may not have
run** - phase aging was inoperative for months while this document credited it with a null result,
and the `MovePlayer` gate was soaked over 246 windows against a predicate that had no call site at
all. **Report a count of what the mechanism DID, not the state of the switch that arms it**: a switch
that is on and a mechanism that never runs read the same from a log.

**Run-to-run variance on `localanim` is at least 5 points**, so **alternate a toggle rather than
switching it once** and take non-overlapping ranges as the criterion. **Count what a fix did before
crediting it with a change**: one touching nine records cannot move a quarter of a million samples,
and the difference was the scene.

**A falsification criterion set without reasoning through the mechanism is not a criterion.** "Any
nonzero checksum divergence is this feature" was correct and useless: optimistic referencing defers a
lost group's re-send, so transient divergence is the feature working. The bar was the duration.

**When a performance number moves, check whether unrelated subsystems moved with it.** Independent
code paths do not share a factor; the machine does.

**Instruments have lifetimes.** Anything keyed on an entity id is invalidated by the id reuse that
invalidates protocol state - the jump census read 173 u/tic because tracers recycle ids fastest, and
the branch detector later flagged a false re-entry on every missile spawn. Clear on `entering`, not
inside the fresh-ghost branch, which a lost leave tombstone skips.

**A struct member survives the change that stopped populating it, and reads as data** - four
instances. **The inverse is the same defect**: `misc` bit 0 carried `MF_CORPSE` unread from v11, so
near-tier collision spent a session inventing a problem the wire had already solved. **A third form
is a value discarded because a sentinel collides with a legitimate one** - `STYLE_None` means do not
draw and was rewritten to `STYLE_Normal`, so everything the server hid drew; remove the ambiguity
where it is created, not at the consumer. **A fourth is a field whose MEANING changed while its
writers did not follow.** Both directions want a deliberate sweep (§10.1).

**A counter of UNITS says nothing about TIME.** Server saturation was attributed to four subsystems
in turn, none falsifiable from a log, because `sim` counts tics per pump and a slow simulation and
slow netcode read the same; splitting the pump into phases answered it in one run. **Three sessions
of hypotheses cost more than the instrument did**, and **a stack trace beats a fifth hypothesis.**

**A print and its reset must be gated on the same condition** - twice, the second worse because the
gate was UNRELATED: the suppression reports sat inside a block gated on a count of deaths, so a window
without a death printed neither line and cleared neither counter. **A figure taken that way is both
selected on death-bearing windows and summed over the ones that gated out.** And a counter printed on
two lines is read and reset ONCE, before both: reset by the later line, the earlier one reads zero as
soon as the two swap order.

**A histogram's buckets expire with the regime that sized them**, and **four of five buckets reading
zero forever is the correct-zero rule one aggregation level up.** Rebucketing exposed a component
pinned at ~1,000 a window matching no service rate - **a counter that does not move when the scene
does is measuring the clock, not the world** - which traced to a cull path returning without setting
the flags the census keyed on. **Key a census on the OUTCOME, not on flags.**

**Establish which population shows an artefact before attributing it.** A grenade reads close to
legacy from an observer and jitters from the shooter, because the artefact scales with angular size;
four rounds of extrapolator tuning went by before anyone asked who was watching. Same error as sizing
a client-stream property from a server capture (§9.4).

**A RESIDUAL is not a measurement.** `blocks - compared` summed four populations, one of which the
instrument could not see at all. Attribution replaced it: every visit lands in one of six named
outcomes whose sum is printed against the total, so an exit added later without a counter announces
itself. **And the denominator must not be selected on a condition unrelated to what is measured** -
the comparison sat behind three early returns, so tics where prediction did not run vanished into the
residual.

**A zero is ambiguous until something independent says the condition occurred.** An out-of-order
guard reading zero means either the guard works or nothing arrived out of order, and no amount of
re-running the impairment separates them; a counter at the one test that distinguishes a reordered
payload answered it in one run. **Set the falsification criterion on the witness, not on the
mechanism.**

**Three counters in one phase each spanned two populations**, and every one was found by a reading
that looked like a defect: a spectator refusal counted as no-pawn, a chunk counted as a duplicate, a
window's pre-toggle records counted against post-toggle totals. Each was a shared exit or a shared
name. **When two things reach a counter by different routes, they need different counters** - and the
tell is a number that is plausible but an order of magnitude off.

**Remaining rules.** A measurement that cannot distinguish between the fixes under consideration is
not yet an instrument. Report every toggle's state on the line that depends on it *and* a count of
what it did. **Per-entity instruments resolve in minutes what aggregate counters cannot resolve at
all**, and are worth reaching for sooner than feels justified - three defects this phase were
narrowed by four rounds of aggregates and then attributed by one trace.

#### Populations, numerators and denominators

**Numerator and denominator must describe the same population by construction** - five instances.
`contested` as `emitted + deferred − withheld` could go negative; one world-wide refresh rate
averaged a band at 12 Hz with hundreds of entities at zero and reported 0.8 Hz, which was the exit
criterion for three runs. **Where a budget is rationed across classes, report per class.**

**Sizing a rate by one population while the membership is another bounds nothing.** The cosmetic
clock bounds POSITION staleness but its quota was sized by the whole held population, so a configured
2 s delivered 4.3. Only splitting held records by what they OWE fixed it.

**A predicate sized on the unloaded case describes a population the loaded case does not contain.**
Terminal promotion accepted APPEAR-only records because a death carries nothing else - true when the
entity was served recently, false under starvation, so it passed 7-9% of flagged records in exactly
the case it was built for.

**A repeated delivery is not a repeated event.** An enter is re-sent until acked - 0.94x per record
on a LAN and 4.6x under impairment - so a latency census counting every emission measures the re-send
schedule rather than the event. Only the FIRST delivery to each client is a sample, which has to be
tracked per client because the same record goes to all of them.

**Numerator and denominator must also span the same INTERVAL.** The jump census divided displacement
since the last RENDER by the gap between the last two RECORDS, reporting a player at 184 units per
tic; denominated render-to-render it reads 16.7.

#### One definition, not two

**Anything that computes over a field must compute over the value that CROSSES THE WIRE.** `qpitch`
broke this twice: the checksum hashed it pre-encode on one side and post on the other, and the dirty
predicate compared ten quantized bits while eight cross - so every entity with nonzero low bits was
ANG-dirty forever, sending a 6 B record whose bytes never changed. Monsters have pitch 0, which is
why it survived unseen. `WirePitch` is the single definition, applied at the capture and at both
comparisons.

**A predicate the protocol depends on must exist once.** The capture sweep and the per-client pass
held separate copies of "what changed" and drifted three ways, each individually reasonable. Now
`DiffMask`.

**A header that names a type must include the header defining it.** Three headers here named wire
types while including nothing that defined them, compiling only because every existing includer
happened to have `format.h` first. The failure is delayed and lands in an unrelated translation
unit.

**A checksum over unsent fields must cover exactly the fields the dirty predicate compares**; a field
in one set and not the other is a defect whichever way it points (v14). **v15 changes whose state is
hashed**: with a group suppressed the sender's state and the receiver's model differ, so the writer
hashes what the CLIENT will hold.

**Two clocks are needed wherever one group is suppressed, and BOTH ends need both.** POS suppression
broke three things of one shape; APPEAR suppression is its mirror image and needed `appearTic`, since
a stationary firing player produces no record at all while events keep delivering a fresh state - the
record clock ran away and the phase was aged 18-39 tics through a 12-tic attack state on every
correction. **A clock added for one group does not generalise to the next.**

**A counter that identifies a unit of DELIVERY and one that measures elapsed TIME must be separate,
even when they happen to be equal.** The ack was keyed on the frame number, which is also the
scheduling clock - fine until a frame spanned two datagrams. The same number then served as an
elapsed-time clock, but it counts PUMPS, so staleness was understated by the span factor exactly when
it mattered, reporting a player crossing 190 units in "one tic". `gametic` now rides the wire beside
`g_snapFrame`; `serverTic` keeps delivery, ordering, deadlines and the clock model. **And two
counters subtracted from each other must share an origin** - `fua_snap_sound_entergrace` compared
`gametic` against a stamp taken from `g_snapFrame` and was inert either way.

**A one-shot signal must carry its own timestamp, on the clock the RECEIVER compares it against, per
GROUP.** A branch event queued on observation may leave four frames later and wind an entity
BACKWARD; v13 added the tic. But v13 put a FRAME number on the wire while the client compares against
per-group clocks measuring world time, and the guard asked only whether the event was older than the
last record of ANY kind - so a stale position overwrote one the client had correctly extrapolated,
because a predicted missile has its position withheld while other records keep arriving. v18 carries
a GAME tic and guards each group separately against its own clock.

#### Scheduling

**A restructuring that must measure as NOTHING is the only way to attribute the change after it.**
Sorted-id coding landed as three patches - prefix records into the emit list, then estimate/cut/encode,
then the format - and the first two moved no byte. Each hid a breakage that would have been silent
inside a combined patch: `out.Size()` was a running total read in five places in the drain, three of
them live ceilings, and freezing the buffer left them reading a constant that would have mis-sized the
reserve and the missile cap with every counter still plausible; the chunker partitioned `sent.records`
with one forward cursor over ascending offsets, which holds only while layout order equals the order
rows were appended in, so sorted emission would have put records in the wrong chunk's `pending` and
made REPAIR REVERT THE WRONG ENTITIES under loss - correct payload, plausible counters, intermittent
desync; and the emit descriptor was sized from the entity constant while events share the same list
and are longer. **The 15.8% wire drop is attributable only because the two patches before it moved no
number.**

**Insertion order is a scheduling policy whether or not it was chosen as one** - the priority walk
ordered by NetID, so late-spawning projectiles were structurally last. **Any bounded walk over a set
larger than the budget needs a cursor that persists and advances by work done.**

**A guarantee placed at the end of a function is not a guarantee.** The starvation guard sits at the
bottom of the classifier, so any early return silently opts out - and the starvation is invisible
because the counter that would report it is the one bypassed. The branch census sat behind the same
kind of exit and dropped every branch on an entity with nothing else to send, which is why a
stationary firing player's weapon animation never restarted. **Prefer fairness as a property of the
mechanism everything uses over a check everything must reach.**

**A band that outranks the distance bands needs a CEILING, not a reservation**, and a fixed
percentage cannot be right for every load: 23 projectiles need 10%, 92 need 40%. **Every new cap
needs its own meaning** - one added as a fifth to "four caps, four meanings" fell through to the
default and ended the packet, measured as every distance band at 0.0 Hz.

**Promoting one record of a sequence is not promoting the sequence.** The transition class delivered
pain, attack and waking at high priority while the records carrying the actor BACK to its chain
stayed in the distance bands; off-chain misses nearly doubled. Death is the only safe member
precisely because nothing follows it.

**A reservation sized for a saturated budget becomes a CAP when the saturation goes.** **Quotas that
bound an INTERVAL should be derived from population and interval**, not fixed as a record count
correct only for the population it was chosen against.

**A newer fix can make an older one unreachable without removing it.** Terminal promotion required
`tier >= DIST + 2`; a later, correct change clamped flagged records to d1, making it unsatisfiable.
The tier histogram had been saying so by putting every record in one bucket.

**Budget converts to rate and is NOT absorbed by a larger served set.** **Six redistribution levers
measured neutral**, each improving one population by close to what it cost another; what worked was
record SHRINKS and then more budget. **A neutral aggregate can also hide a real transfer.**

#### Removal, side effects and classification

**ZERO-DURATION states are invisible to anything that samples or places state**, because the engine
chains through them within a single tic. Three subsystems have now been caught. Branch detection
compared against `GetNextState()`, so a `TNT1 A 0` jump table - the standard DECORATE idiom for
branching on inventory - flagged every loop of an animation as a branch, once every eight tics for a
modded BFG ball, each one an event carrying a position. And `nofunction` protects only the state you
SET: `CLIENT_SetActorToLastDeathStateFrame` walked only CONTIGUOUS states, so a Death chain ending in
a `Goto` parked a corpse mid-animation and the actor ticked through the remainder itself, running
every action - heard as a player's skinned death scream on a joining client. **A walk that stops at
the first discontinuity does not reach the end of a chain**, and suppressing an action on entry is
meaningless if the actor will immediately leave.

**A dual stack MASKS a defect as readily as it hides a missing feature**, and the masking form is
harder to attribute. That scream is pre-existing at every suppression level; only at 3 does the
`*Thing` traffic stop overwriting the corpse's state each tic before it can animate out. The bug was
never introduced - the cover was removed.

**A mechanism whose correctness rests on a side effect of another breaks silently when that other is
removed**, and surfaces elsewhere, which is why it reads as a regression rather than an omission. The
animation freeze was accidentally the hard resync's once-only guard.

**And sometimes the side effect is the good part - but say what it is a side effect OF.** The mirror's
correction is gated on sprite-and-frame equality while `qstate` names the state exactly; replacing
that with state equality made every metric worse, because the frame test rate-limits a correction
that is itself lossy. That holds for phase jitter WITHIN a chain and no further. **A matching frame is
not agreement when the two states sit on chains that cannot reach each other** - a pawn's `Spawn` and
the first `See` state are both `PLAY A`, so a stop was frame-identical to the walk it interrupted and
was discarded. `ghost_SameChain` overrides the frame test only where frames match, states differ, and
neither chain reaches the other.

**A classification derived from what something IS will be wrong about what it DOES.** The ballistic
bit meant "a missile with no seeker, bounce or hugger flag" and was **wrong on 54% of samples**; a
class test cannot see ACS `SetActorVelocity`. Two caveats on the replacement: **the run-length
histogram measures TURN INTERVALS, not straightness**, and **past straightness predicts future
straightness only for monotonic motion**.

**A classification gated on a flag inherits that flag's lifetime.** The ballistic bits are set only
while the actor holds `MF_MISSILE`, which `P_ExplodeMissile` clears, so every detonation registered
as a classification flip.

**Losing a classification instantly is a feature for anything that turns PERIODICALLY.** Decaying the
run instead, with the bit latched between separate thresholds, cut class changes in flight from
~2,300 a window to ~400 and made tracers visibly worse: they held the bit through their re-aims and
were extrapolated across a turn rather than stepped through it. Chatter at the threshold is the
cheaper artefact.

#### Two shipping desyncs, both silent, both found by a four-bit checksum

v12 hashes the entity's full post-decode state - every field, INCLUDING those the record did not
send, because a record omitting Z is only correct if the client's Z matches too. It read 4-8% where
it must read zero. `qpitch` hashed pre-encode (**a checksum over lossily-encoded fields must hash the
post-decode value**; hashing pre-encode tests the sender against itself), and `snapcl_ApplyFrame`
copied the whole state into `believed` on ANY record, so an APPEAR-only record marked position,
velocity and angle delivered and they were never sent again - shipping, and invisible without a
checksum, because a stale position is indistinguishable from ordinary scheduling error.

Fixing the second **doubled contested, 2,100 to 4,300**, and that doubling was itself a defect: it
left `believed.spriteframe` never assigned while the APPEAR test still compared it. **Two
individually correct changes, a defect at their intersection.**

**The same one-sided error appeared at four layers**, each invisible until the SIGN of the residual
was examined rather than its size.

#### Branch events

Events measured 4-7 points WORSE than no events at all, and the order of checking was the lesson.
Only the first hypothesis was verified in code before being asserted, and it was the right one; of
the other three, one was recorded as refuted by an out-parameter the accessor never assigned, one was
wrong on the timing, and one cost three tuning rounds because the report recomputed a share instead
of printing the emitter's value.

With the tic fix in, events measure **neutral** and are kept on at 2.5-3.5 kB/s, because transient
states below the sample interval cannot arrive any other way.

### 9.3 Open

**The two levers that change the DENOMINATOR are untaken**: §5.3's LOS sampler, and `fua_snap_cull_dist`,
monotonic in refresh with no knee and so an operator knob per map. Phase 4's encoding levers are
shipped or dropped (§9.1); bit-packing is the one left unmeasured.

**The loop hint is the largest unbuilt encoding lever** - 77-82% of arriving APPEAR groups name a
state the client could have derived unaided, measured on the client stream after a server capture had
written it off. Its client half, local animation on MID and FAR, shipped; the predicate is a chain walk from `believed`, so the class-table form stays dead.

**The slope commands are `AproxFixed`.** `SetSectorFloorPlaneSlope` and `SetSectorCeilingPlaneSlope`
send `a`, `b` and `c` as whole units, so a plane normal's components round to 0 or ±1. Reached only
by a runtime slope change; Phase 6.

**A link cycle is still wrong on a joiner**, parked until a map has one (the census reports `CYCLE`,
the join warns). No write order fixes it, since each absolute write propagates into the other. The
fix is a `noPropagate` flag on `SetSectorFloorPlane`/`SetSectorCeilingPlane`, set only by the join
pass: every plane propagation would move already gets its own write there, as in the reset window.
One byte per join write, and a wire change.

**The wire cannot say "not solid" in general.** `SNAPMISC_CORPSE` answers one producer -
`A_NoBlocking` and `P_KillMobj` - while a mirror otherwise takes solidity from the class default.
Spectators were a second producer and are excluded from the capture instead (§3.1); what remains is
content clearing `MF_SOLID` at runtime, e.g. by ACS. **Unmeasured, and not worth building against
until it is**: generalising the bit changes the meaning of a shipped one, which needs a version bump
though no byte moves. The census is cheap - captured actors whose `MF_SOLID` differs from their class
default and are not corpses.

**Appearance suppression's FAR-tier cost is unattributed** (§5.2 carries the numbers). The player
half had a nameable cause and is fixed; the monster half did not move with that fix. It needs no new
instrument - the exempt carve-out measures it every window - and it is the price of a mechanism whose
other justification is a record count taken over a population it largely does not reach.

**Per-axis presence, X/Y half.** The Z half shipped in v11 and is 84% of the total, so the increment
is the smaller remainder.

**Join parity, 1.7 s measured at `fua_snap_maxbytes 4500`** (was 4.3-4.5 at one datagram).
Budget-limited, not CPU-limited: the fill window carries full packets, so ~182 kB of enter blocks is
being rationed through a steady-state budget, which applied to a join burst is a category error. The
fix is a bulk baseline at connect with the player held until it is acknowledged. **It belongs to
Phase 6**, which deletes `SERVER_SendFullUpdate`, the only place a baseline could attach. The `JOIN
FILL` timer stays in the tree as the acceptance test.

**Death copies cut short.** A death event's copies stop when the entity is removed, so a short
visible death removed before +3 has one try under loss; `later copies cut` cannot tell those from
`TNT1` chains except per class.

**Branch detection lags the branch**, in over half of cases by 7-15 tics. Not delivery - `event age
<2t` is ~95% - but sampling: a branch is observed on a pass rather than at the moment it happens, so
a 2-6 tic transient can be partly missed even when the event arrives promptly.


#### Known limits

**Mirror spawns (§6.3).**
- Only the four spawner actions run on a mirror: client-side ACS a state starts, native `Tick`
  effects and remote players' weapon states still have no producer.
- A mirror never runs `A_Jump`, so it follows the default chain; a branch state's spawns come only
  from a record's jump, late, and random tail variants lean to the default branch.
- `A_SetUserVar` never runs on a mirror, so a spawner parameter reading a user variable sees the
  class default.
- A mirror's health is the class default; only client-side ACS could read it. `SNAPMISC_CORPSE` can
  set it to 0 if that ever matters.
- Below ~35 fps eight frames exceed the 8-tic advance clamp, and a FAR mirror loses the tics past
  it, their spawns included.
- A misprediction can double or miss one spawn; the jump and resync counts bound it.
- A missile mirror's target: a reflected missile keeps its first target; a shooter whose NetID is
  reused inside the enter's re-send window gives the trail an unrelated target; a recreated mirror
  keeps none; and a shooter's mirror destroyed while the server's shooter lives leaves the target
  NULL, so later trail spawns are missing. A counter applying the action's own tests reads a handful
  a window against tens of thousands of spawner actions, so the engine fallback (aim opposite the
  caster's velocity) is not built; the counter stays for a mod where it reads higher. A monster
  mirror's `A_CustomMissile` has no target, as stock clients had none.

**A routed copy is told once (§6.3)**, to the clients present and in range; a joiner or a late
arrival misses any still alive, where stock's NetID copy reached both. Complex Doom, Chillax and stock
Doom route nothing that lasts past 5 s while clients are connected; a gore mod with resting gibs would.
The `cosmetic layer 1` line's persistence and alive-at-join counts show it; a mid-fight join is the
check on the first mod reading copies past 30 s. An empty server stops its actors while game time
runs, so copies alive after the last client leaves read as persistent.

**The hold (§6.3).** A script that swaps its activator leaves its copy held to the 8-tic deadline
rather than released when it stops; the hold line's deadline count shows it. A `USER` cvar a server
script reads gives one choice for every player, since there is one server copy - as stock.

**The gravity class (§5.2).** A level or sector gravity change while a missile is bridged reaches the
server's law at once and the client's when the reliable command lands, and the server's check
compares its own prediction with the truth, so the client's drift goes unseen for up to 14 tics. In
plan view a missile turning under `SNAP_STRAIGHT_TOL` (~1° a tic) still bridges as straight, bounded
by the 16 u tolerance; fix it only if a gently homing missile shows the same flick.

**Transient states below the sample interval.** The snapshot samples *state*, so a state shorter than
the refresh interval falls between two samples and is never represented. Stock pain states run 2-6
tics, needing >5.8 Hz for the longest and >17.5 Hz for the shortest. Branch events (§5.5) remove this
for pain, death and attack because they are sent rather than sampled. Sampling alone cannot fix it: a
room of ~1,000 visible monsters would need ~600 records/frame for full pain fidelity.

**Wide maps (§3).** The frame keeps every entity, event and polyobject position right past ±32k, but
mover planes travel as absolute 16.16 values, as ForkUnderA's own sector commands send them: a mover
plane beyond ±32k in z, or a steeply sloped mover far from 0 in x or y (whose plane distance includes
`a*x + b*y`), is out of range. A map wider than 16,777,215 units on an axis clamps at the frame's edge
and says so on the `[snap]` line; a third width is a byte count away, without a format change.

**The read-only-actor-argument family.** Resolving `SpawnBlood`'s originator against a mirror fixed a
whole command family, on the reasoning that the originator is only read. The same argument covers
every command whose actor argument is read-only and whose subject is cosmetic. Worth enumerating
across the command set rather than extending the allowlist as each absence is noticed.

### 9.4 The recorders

Two taps, answering different questions. The server's `.zsnap` captures the global world diff before
the per-client priority pass rations it - "what was available to send". `.csnap` is the client-side
tap: payloads with arrival times, a per-frame marker with the render clock, the player pose and the
sampler cvars. **Every artefact now worth chasing lives in the difference between them.**

**The same error has a second form inside one log.** Item 4 was sized at 8-10% of payload by taking
APPEAR's 81-87% share from the OWED groups report - the contested set, what the scheduler is behind on
- rather than the 15-22% it holds in the EMITTED report. APPEAR dominates the backlog because
appearance suppression holds it, and is a minority of what ships because the scheduler serves position
first; it measured ~2%. Two lines of the same log, one population error.

Using the wrong one is a recurring error (§9.1): bit-packing, per-axis presence, the loop hint and
the dictionary were each sized from a server capture.

**The two fields §9.4 recorded as missing landed at v9.** A delta carries the GAME tic beside the
frame number - a frame counts pumps, so a replay measuring staleness in frames read short exactly
where the server was catching up, and the wire's event clock moved to game tics at v18 for the same
reason. The header carries the recording player's number, which is not derivable: the pose comes from
`pCam`, so under a chasecam matching a ghost to it identifies the SPIED player. The version check
precedes the new header read, since a v8 file has no such field and reading one would consume the
first record's kind byte. The decoder also reports game tics against wall time, which is the only way
a recording shows that the server was dilating while it was made.

**Two fields in `.csnap` were nearly omitted and are what make a replay meaningful.** The **player
pose**, because the render tier is a function of camera distance and the tier decides how much work
a mirror gets, so the sprite drawn depends on where the player stood - taken from `pCam` rather than
`players[].mo`, since a chasecam changes which actor that is. And the **cvar block**, because the
sampler reads a dozen settings and a replay under different values silently reproduces a different
pose; recorded by name, not position, so adding one later cannot shift the meaning of the others.

The harness feeds the real decoder and the real sampler rather than reimplementing either: a harness
that disagrees with the game is worse than none. It applies the recording's settings, segments on every cvar change so an A/B needs
no hand-split files, and restores the console afterwards so a replay cannot turn the next
measurement into a different experiment. A `name=value` argument overrides a recorded integer or
boolean setting, so a new recording, which carries every tuning setting, can still be compared both
ways.

**Lessons, each of which cost a build.**

*A client tap has no baseline privilege.* The server recorder forces one; a wire tap sees only what
arrives, so a recording started mid-session is deltas against a table the replay never had. Every
dirty record for an unknown id is then correctly rejected as redundant and the replay reproduces
almost nothing - measured as 5 entities of 6,626. `ExportBaseline` serialises the present ghost
table through the same encoder as live traffic.

*Aggregate reporting cannot answer an A/B.* The first harness averaged both halves of a mid-stream
toggle and answered neither. Segmenting on cvar changes makes alternating a setting every few
seconds the right protocol: a single switch left 132 entities active in both windows, against 1,153
when alternating.

*A format version must be enforced, not documented.* The offline decoder had no version check and
silently misparsed a v12 recording as v11 - garbage class ids and 50,000-unit jumps that looked like
data. **A reader that cannot tell it is reading the wrong format will report confidently on
nonsense.**

*And a second copy of the layout will rot even where the first is maintained.* The two containers
differ only in framing, so the record walk was transcribed twice; `csnap2.py` was kept current with
every version and `zsnap_info.py` was not, ending seventeen versions behind with no version check to
say so. Both now call one walk, and `--verify` checks the ID ENCODING as well as the widths - a
reader can agree about every field length and still split the bytes differently. **A field list that
exists twice is maintained once.**

*A parse that only fails on rare input fails silently for months.* `CSNAP_RESET` writes a timestamp
before its reason length; the decoder skipped straight to the length. LAN captures have exactly one
reset, at teardown, so the misparse had nothing left to corrupt and went unnoticed until a session
with a mid-stream reset truncated a 4.5 MB recording to 5.5 seconds - while still reporting plausible
figures for the part it did read.

*A harness that inherits the session measures the session.* The entity layer's injected hooks were
whatever the game had installed: the gravity law and the animates answer from the first drawn frame,
the mover and local sinks from client startup, the psprite sink with the law. So a replay read
differently at the title screen, offline and after a connection, and wrote recorded movers into the
loaded map. The replay now installs its own law and animates answer (from the recorded
`fua_csnap_localanim`), runs with the three sinks NULL, and restores all five; it refuses in client
mode, since it clears the ghost table a connection or demo is using. The law draws against the loaded
level, so `.csnap` v16 names each map (`CSNAP_MAP`, name and `MapData::GetChecksum`) and the law runs
only where it matches.

**The glide metric failed four times and was abandoned.** The goal was to measure motion drawn faster
than an entity can move, and every version failed on its threshold: a per-frame bound counted
`P_TryMove`'s discrete steps as glide and produced a confident 5.6x; a windowed one without margin
sat above even a 2.5x glide, `Speed` being units per MOVE while `A_Chase` moves once per state; one
bound could not serve missiles at 10.02 u/tic against Speed 10 and walkers at 2.53 against 8; and
with calibrated bounds it still discriminated nothing, because **`DoomImp` sustains 59-113% of
`Speed` while `Revenant` sustains 20-41%** - floating monsters move by velocity through
`P_XYMovement` like missiles and carry no wire bit saying so. The samplers measured the same across
all four versions. **A plausibility bound is content calibration, not arithmetic.** What survives is
the per-class rate table, which is real content knowledge independent of any bound.

**What replaced it needs no bound at all**: per-class update-interval and displacement percentiles,
and jump attribution by the entity's own gap since its previous record. Distributions cannot be wrong
the way a threshold can. That is what established the jumps are staleness (70-90% follow a gap over
500 ms) and, later, that appearance was frozen beyond 4,096 units - **a measurement over intervals
between consecutive records could not have shown the second, because an entity receiving nothing
produces no intervals and drops out of the histogram entirely.**

Known limits: `SampleGhost` is the sampler, but local animation runs above it in
`GhostFrame`, so the harness reproduces position and sampled appearance, not the
locally-animated sprite; closing that means moving the animation step behind a callable seam. And it
draws through the smoothing as a mirror does, but without the mirror's wall, floor and ceiling
clamps.

## 10. Risks and mitigations

§9.2 holds constraints already proven by implementation. What follows are risks that remain open -
things that could still go wrong rather than things that already did.

**Bandwidth on pathological maps.** The tiered-rate system degrades gracefully by construction -
awake far entities lose refresh rate before near ones lose correctness - but the degradation has a
floor set by record size, now 8.0 B of payload and 6.2 B on the wire. A map denser than MAP13 will
hit it first; multi-packet delivery raised the ration threefold, so the floor binds three times
later. **Path MTU is not the binding constraint**: the chunker splits at `sv_maxpacketsize` however
many chunks result, so the peak compressed datagram does not grow with chunk count (§9.1).

**Content varies more than any single wad can show.** Every figure in this document comes from
MAP13 or a Chillax derivative. DECORATE sets state durations freely, so the dormant dirty rate a
wad produces spans from zero (a single-frame `Spawn`) to several times stock's; class populations,
projectile speeds and effect actors vary the same way. Anything justified from one wad - the
appearance share, the missile share, the cosmetic hold - is calibration rather than a constant, and
should be exposed as an operator setting rather than baked in. `fua_snap_cull_dist` is the sharpest
case: on open maps distance does not imply invisibility (§5.1), so a value tuned against a corridor
map is wrong there.

**The dual stack hides its own regressions.** While both protocols run, the legacy path silently
covers for gaps in the snapshot path, so a missing snapshot feature looks like a working one until
suppression is raised. §6.1's derivation contract means some of those revelations will be *absent*
behaviour rather than broken behaviour - harder to notice. Mitigation: raise suppression early and
deliberately on a test server rather than late and accidentally on a real one.

**Reliable-channel growth.** Moving sounds, and later deaths and pickups, onto the reliable channel
reintroduces per-actor traffic in a place with no budget. §6.2's audible-radius cull is a day-one
requirement rather than an optimisation; without it the first slaughter fight makes the reliable
channel the bottleneck. Currently 2.0-2.4 kB/s against the snapshot channel's 48, with the cull
dropping 53-91%.

**Sound attenuation must agree across the wire.** The audibility cull evaluates a curve that compat
flags can change (`ZACOMPATF_ORIGINALSOUNDCURVE`, `COMPATF_SECTORSOUNDS`). If server and client
disagree, sounds vanish rather than error. Verify the replication rather than assume it.

**Classification errs networked, and content can still fool it.** An actor a mod drives by TID or
class from ACS, or one whose effect matters on the server, is protected by the TID rule, the ACS
scan and the override lists (§6.3). Not by a class named only in a runtime string, nor an instance
given a tid after its event: those lose their server identity at mode 2 until listed `never`.

### 10.1 Cvar and dead-code triage

Every cvar, command and report line was given a verdict, and the verdicts are carried out. What the
triage settled and what stays open:

**Kept, against the obvious reading.** The psprite counters read all zeros and their question is
answered, which is normally grounds for deletion - but the local weapon machine is staying, so the
stream is permanently a mechanism running beside the thing it duplicates and is verifiable by nothing
else (§9.2). `bad class`/`bad state` are also the standing check on the `(depth, offset)` encoding, a
hand-maintained correspondence between two files.

**Kept, as the only check on a class of bug.** `fua_snap_debug_holdbackgap`, the one way to land a
datagram past the ack history, and the presence logs with `tools/snapshot/presence_diff.py`, the one
check that sees a mirror the server holds absent, which the checksum cannot by construction, since
the server sends such a ghost nothing. Both cost nothing when off.

**Waiting on the work they were built for.** The gravity-class counters stay while that class is new,
since they are the only thing distinguishing "never fires" from "fires and does nothing" - and they
have already earned it twice, once by reading `worst 5.00` against a true 0.75 and once by showing
254 plain-ballistic updates against 8 gravity, which is why a clamp gated on the wrong class did
nothing. The law's counters join them: the server's `gravity class` held, sent on position and on
velocity, and the client's `by the law`, `no law`, `measured disagreeing`.

**Constants, not cvars.** 27 settled values: optimistic referencing (the `believed` reference
and the `unackedMask` rebuild are gone; repair stays), the event carry, the band floors, the cone
radius, the terminal promotion cap and gate, the death-event copies, the animref ceiling (40; never
0, or the far tier reports no samples and appearance suppression loses its only unbiased
measurement), the two nolegacy player gates, the client's mover apply and non-block, the sampler's
velocity exit (100 ms), the animation resync (1000 ms), the hide-legacy and player-mirror switches,
and `snap_extrap` 0 with the ms extrapolation path it selected. `SNAP_BALLISTIC_MAX_TICS` is bound
to `CLENT_MISSILE_EXTRAP_MAX_MS` at compile time, and an archived `fua_csnap_missile_extrap` below
the bridge is raised on read. The three tunes tested off their defaults now default to the tested
value.

**Removed.** Diagnostics whose question closed: the capture wire-cost compression, the chunk
size override, the overdraw probe, the Phase 5 thinker census and mover load, the plat and plane-draw
traces, `snap_monstersnear`, the first ghost harness (`snap_ghosttest`, `snap_ghostplay`,
`snap_ghoststop`; `fua_csnap_analyze` replaced it), and the report lines `appear probe`,
`shadow walk`, `stateidx`, the qstate marginals, `ptickphase` with its stock-file timers,
`missile leave age`, `re-entry classes`, `cone exempt` and `terminal detail`.

**Decisions outstanding.** The `effects` counter's two sides still describe different populations
while the comment between them claims otherwise.

**A sweep in two directions, neither done.** A field written every capture and read nowhere is live
work producing nothing; a field read but no longer written reads as data. Both have cost sessions
here (§9.2), both are mechanical to look for, and neither has been looked for deliberately.

**The mover cvar.** `fua_snap_movers` gates the server's capture, emission and mover gate; §7.1's
acceptance has run for every class, so it is an operating parameter, archived and on by default,
with `fua_snap` still the master switch.

**Plane instruments, all diagnostic and unarchived.** `fua_csnap_planetrace` (a sector index, or -1
for every write) is CLIENT ONLY: its linked-sector hooks fire every tic a mover moves, so a print
per write on the server's game thread would build a catch-up backlog, and set on a server it warns
once. `fua_snap_linkcensus` is the server half and reports the link graph at each join. Set both
before connecting, since the writes they measure arrive during the join pass. `fua_csnap_planetrace`
is noisy - most traced writes on a running lift carry a delta of zero - and wants a filter.

**Every log carries its configuration.** `[snap] cvars off default` lists each snapshot cvar not at
its default, with the default: the server at its first snapshot tick, a client at the first delta of
each connection. The ini's placeholders for names this build no longer has (`CVAR_AUTO`) are skipped.

**Current state.** Defaults are the measured operating point: `fua_snap_maxbytes` 4500,
`sv_maxpacketsize` 1550, `fua_snap_cull_dist` 4096, `fua_csnap_collide`/`fua_csnap_collide_dist`
on/512, `fua_csnap_localanim` 3, `fua_snap_missile_predict` 16, `fua_snap_missile_share` 20,
`fua_snap_appear_predict` on, `fua_snap_animref_interval` 115 tics, `fua_csnap_age_servertime` on
and `fua_csnap_age_maxjitter` 50 ms (both archived),
`fua_csnap_smooth_step`/`_extrap`/`_missile`/`_pop` 32/1/0/1 (unarchived; archive flags are
revisited in Phase 7), `fua_snap_soundids` on (unarchived, the A/B switch),
`fua_snap_cosmetic_events` 0 (2 on test servers), `fua_snap_cosmetic_dist` 4096,
`fua_snap_cosmetic_bytes` 600 (all three unarchived). Diagnostics off: `fua_csnap_predcensus`,
`fua_csnap_recmirror`, `fua_snap_wirecensus`, `fua_csnap_loopcensus`, and on the client
`fua_csnap_soundtrace "<text>"` (one line per sound command whose name contains it: path, actor
resolution, whether it is playing afterwards). Reproducers, off: `fua_snap_debug_ackdelay N` holds a
client's acks N frames, the deterministic window for the presence race, and
`fua_snap_debug_resetsplit N` delays a reset's `DestroyAllSectorMovers` N tics, holding every window
open.

**One archive flag still breaks the rule below**: `fua_snap_event_copies` is an EXPERIMENT carrying
`CVAR_ARCHIVE`. The rest are operating parameters, where archiving is correct.

**Archive rules, in three cases.** An EXPERIMENT must not be `CVAR_ARCHIVE`: a toggle surviving a
restart turns a null result into a false one. An OPERATING PARAMETER must be, because a silent revert
invalidates every comparison made afterwards. And a parameter meaningful only against a compile-time
constant - `fua_snap_refresh_max` against `SNAPCL_AGE_COSMETIC` - must not be, because an archived
value can outlive the constant it was sized against and a cap larger than its quota never clamps and
never warns. A removed archived cvar is moved by the engine into the ini's
`[Doom.UnknownConsoleVariables]` section, silently.

**A diagnostic that lives outside the config file is a manual step that gets forgotten.**
`fua_snap_wirecensus` is the before/after instrument for every encoding item and costs less than the pump
timer resolves (measured across a mid-run toggle: capture and per-client both unchanged). It was
typed at the console for half of one baseline run and never for the other, so the encoding figures
existed for one run and half its windows. Put an experiment's instrument in the server config, not in
the operator's memory.

**A server-side cvar is settable from a client console and does nothing.** Both ends are one binary,
so the client registers every server setting, autocompletes it, accepts a set and echoes a plausible
value while the server's copy never moves - FOUR runs lost to it, each reading as a null result for a
mechanism that had never been toggled. The startup line puts each end's own state in its own log;
beside it, a line reports what the code USED - the local block's count of records that actually
carried the authority bit - which is what separates "never toggled" from "toggled and inert". Client-only
cvars and commands carry the `fua_csnap_` prefix: `fua_csnap_` is read on the client only,
`fua_snap_` on the server or both, and the master switch is `fua_snap` itself.

**Diagnostics can cost more than they report, and the cost is the OUTPUT.** The capture's world-delta
compression measures ~0.1 ms/tic and runs only while recording; the report's own printing cost
220-250 ms on the game thread against a 28.6 ms tic. It is now queued through `snap_Say` and drained
one line per tic - 0.5-0.7 ms, no pump gap over one tic - and a report due while the previous is
still draining is skipped and counted, since one that silently stopped covering whole windows would
look like a quiet server. **Anything printing from inside the tic loop goes through that drain.**

**The apparatus wants one gate.** A single `fua_snap_debug` server cvar, rather than a toggle per report,
would keep it available without shipping it, make "is this build reporting?" one question, and give
the client-console guard above somewhere to live.

---

## Appendix A — key existing assets and their fates

| Asset | Location | Fate |
|---|---|---|
| Actor registry | `g_ActorNetIDList`, p_mobj.cpp / actor.h | **DONE** as the snapshot sweep set; id widening (§3.1) deferred to Phase 7 |
| Player prediction | cl_pred.cpp | kept; **DONE** - retargeted to the authoritative block (§4.2) |
| Lag compensation | unlagged.cpp | kept; simplified (single position history) |
| zstd contexts + dictionary | network.cpp, `network_InitZstd` | kept; the shipped `zstddict` predates snapshots (§9.1) |
| Reliable channel | network/netcommand.* | kept for events; state use removed for monsters |
| Full update | SERVER_SendFullUpdate, sv_main.cpp | deleted Phase 6 (= null-ref snapshot); sends `SnapJoin` (§4) |
| Join sector pass | SERVER_UpdateSectors, sv_main.cpp | kept; **DONE** - link order, every driven plane (§7.1) |
| 186 client command handlers | cl_main.cpp | ~150 state handlers deleted by Phase 6; ~30 event handlers kept |
| Client-mode guards in gameplay code | p_enemy/p_mobj/p_interaction/p_user/g_* | deleted Phase 6 |
| `PIT_CheckThing` / `P_ChangeSector` / `DMover::MoveAttached` | p_map.cpp, dsectoreffect.cpp | kept; mirror early-outs (§6) and the client's discarded vetoes (§7.1) |
| `DPlat`, `DCeiling`, `DFloor`, `DDoor`, `DElevator`, `DPillar`, polyobjects | p_plats.cpp, p_ceiling.cpp, p_floor.cpp, p_doors.cpp, p_pillar.cpp, po_man.cpp | **DONE** (§7.1) |
| `DWaggle`, `DAnimatedDoor` | p_floor.cpp, p_doors.cpp | kept on legacy commands, which survive the deletion (§7.1) |
| `DScroller` / `DPusher` | p_spec.cpp | definitions, their state riding `DoScroller` and `DoPusher` (§7) |
| Legacy mover handlers (`client_DoPlat` and the rest) | cl_main.cpp | gated by `SnapMoverGate` (§7.1); deleted Phase 6 |
| Mover sounds (`PlayPlatSound` and the rest) | sv_commands.cpp | kept as events, keyed on the sector or polyobject (§7.1) |
| `SetSectorFloorPlane` / `SetSectorCeilingPlane` / `SetLineTextureOffset` | cl_main.cpp | kept; full precision, skipped where the snapshot path owns the plane (§7.1) |
| Map reset | `GAME_ResetMap`, g_game.cpp | kept; opens the reset window (§7.1) |
| Huffman | network/huffman | retired with legacy protocol (Phase 7, not yet done) |
| Platform tic timers | win32/i_system.cpp, posix/sdl/i_system.cpp | **DONE** - `i_tictime.{h,cpp}` on both platforms (§8) |
| Bots | bots.cpp etc. | unaffected (server-side) |

## Appendix B — the .zsnap fixture format

Recorded by `fua_snap_record <name>` / `fua_snap_stoprecord` on the server (capture.cpp); this
turns real server traffic into replayable test streams for the client decoder, ghost table, and
clock model — including deliberately time-skewed playback. All integers little-endian.

```
header:  "ZSNP" | u16 version(=34) | u16 ticrate | u32 flags (bit0: zstd in use) | u32 reserved
         | from v33 the map's frame: i32 originX | i32 originY | i32 originZ
         | u8 widthX | u8 widthY | u8 widthZ (2, or from v34 3: signed relative to the origin)
record:  u32 gametic | u32 wallMS | u16 spanTics | u32 rawSize | u32 storedSize
         + storedSize payload bytes
payload: sequence of entity records:
         u16 id | u8 mask | [u16 classId — present ONLY when the id is entering;
         top bit SNAP_CLASS_TARGET: a u16 shooter NetID follows]
         | group fields per mask
         mask 0 = leave tombstone
         groups: POS=1 (4 B: x,y; a byte more an axis on a wide one) | ANG=2 (3 B)
         | POSZ=4 (2 B; 3 wide) | APPEAR=8 (2 or 4 B)
         | VELMISC=16 (9 B: vel, misc u16, effects) | ENTER=32 | EVENT=64 | RENDER=128 (4 B)
```

Properties the decoder relies on: `storedSize == rawSize` means the payload is stored raw, smaller
means one zstd frame; `rawSize == 0` records carry timing only; the first record after
`fua_snap_record` is a forced **baseline** (every live entity as an enter block) so the stream is
self-contained; a map change clears the capture's presence without writing leaves, so the next map's
baseline would read as duplicate enters to a presence-tracking reader: the recorder stops at a map
change, one recording, one map;
`wallMS` per record carries real server timing for clock-model development; `spanTics > 1` marks
captures made under saturation.
**Enter vs. dirty is distinguished by presence tracking, not by mask** — a dirty record may
legitimately carry the all-groups mask; only entering records carry a classId.

**The version must be enforced by any reader, not merely recorded** - up to the minimum, which since
v23 is not the current version. Group sizes and mask bit
assignments have changed at v9, v11 and v12; v14, v15 and v18 each changed what a field MEANS without
moving a byte; v19 and v20 added and then grew the local block; v21 made the entity id a varint gap
from the previous record in the payload; v22 gave APPEAR two widths; v23 reserved event types 16-31 as
self-describing and is the first version a lower one still decodes; v24 added the first of them,
SNAPEV_MOVER, v25 a second mover class - an older reader reads 0 from `MoverBodyBytes` and SKIPS
on the length byte rather than guessing, which is v23's property one level in - and v26 a third,
`SNAPMV_FLOOR`, the first with two body shapes; v27 added `SNAPMV_DOOR`, v28 a generation byte
in the mover envelope, re-encoding the door's botSpot, v29 `SNAPMV_ELEVATOR`, the first two-plane
class, v30 `SNAPMV_PILLAR`, v31 `SNAPMV_POLY`, one variable-length record per polyobject, retiring
`SNAPMVF_POLY`, v32 the missile's shooter behind `SNAP_CLASS_TARGET`, v33 the map's position frame
in the header, leaving the payload as v32's (a reader takes the header's size from the version, and
an older file is read in the narrow frame at origin 0), and v34 the wide axis, a byte more on every
position on it, a narrow frame's payload unchanged. A version tracks the stream's
meaning, not only its shape, and what each one meant is written where the constant lives. A decoder built for one and fed another does not
fail,
it misparses - garbage class ids and 50,000-unit jumps that look like data (§9.4) - so offline
tooling should refuse a mismatch rather than warn.

Neither recorder is affected by the datagram sequence of §4: the `.zsnap` captures the world diff
below the per-client pass, and the `.csnap` records the payload with the frame number it already
carried. `SVC2_SNAPSHOT`'s header grew the game tic, the sequence, the map stamp, the reset count,
the ack lag and the newest reliable sequence (§4), and has no version handshake, so server and
client must be built together.

`.csnap` is the client-side counterpart (§9.4), currently `CSNAP_VERSION` 18, and carries payloads
with arrival times, a per-frame marker with the render clock and player pose (64-bit from v17), the
sampler cvar block, and map records carrying the map's frame (from v17, wide from v18).

**Both offline readers share one record walk** (`snap_payload.py`), because the two containers differ
only in framing. They did not, and the cost was exactly what §9.4 predicts: `zsnap_info.py` sat at a
v3 layout - POS 9 bytes, APPEAR 6, MISC 1, no EVENT or RENDER bit - against a v20 stream for
seventeen format versions, with no version check to say so. Fed a current payload it invented an
entity out of the middle of a branch event, lost a leave, and reported the result as two protocol
faults. `--verify` now checks the ID ENCODING against `format.h` as well as the group widths and
mask bits, since a reader with the right group table and the wrong id encoding passes every other
check and misparses every record after the first; a version mismatch is refused rather than warned
about. The walk takes the map's frame from its container, so a wide recording reads through the same
code and `--verify` checks the frame's widths too.
