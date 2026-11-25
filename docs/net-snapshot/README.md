# The snapshot netcode: maintainers guide

This directory documents the snapshot/delta netcode in `src/zandronum/src/features/net-snapshot`,
which replaces Zandronum's event-driven actor replication: one page per source file or close group
of them (a header and its `.cpp` share a page), and the design doc, [design.md](design.md), for why
things are the way they are. The pages say what each file does, what it holds, what it offers, the
rules it keeps, where the engine calls it, and what tests it. They link to the design doc for
reasons rather than repeating them.

The module's own [README](../../src/zandronum/src/features/net-snapshot/README.md) is the shorter
door into the same code: what the feature owns, its hooks, where it writes, and the checks that are
not a page's own.

## How it fits together

**The server, once per pump** (`ServerTick`, called from `SERVER_Tick`):

1. Capture: every actor with a NetID is quantized into a `SnapEntityState`, compared with the last
   capture (`DiffMask`), and its dirty groups noted. Movers and polyobjects capture their thinker
   state at transitions; player psprites, the local-player block and the property slots are captured
   beside it.
2. Per client: each dirty entity is scored (distance, view direction, class, age) and written in
   priority order until the byte budget (`fua_snap_maxbytes`) is spent, delta'd against what that
   client will hold once its in-flight datagrams land. Branch events, psprites, property sets, the
   local block and mover records ride the same payload.
3. Emit: the payload is cut into MTU-safe chunks, each a datagram (`SVC2_SNAPSHOT`) that decodes on
   its own. Acks come back on every usercmd; a datagram proven lost is repaired by reverting what it
   carried to what the client is known to hold, so the next records re-send it.

**The client, per datagram and per frame:**

1. Decode: each datagram is checked against the joined map and applied to the ghost table, group by
   group, last writer by frame. Movers, the local block, psprites and property sets go to their
   sinks.
2. Sample: each frame, every ghost is drawn at its newest state, extrapolated along its velocity
   where its motion is predictable, aged by the server's clock, and smoothed between records.
3. Mirror: each ghost owns a client-local `AActor`, the mirror, which the renderer, sound and the
   player's collision see. Mirrors run their animation locally and no gameplay code, apart from
   client-side spawn actions. The ghost frame runs inside `FillNow`, so the copy the renderer draws
   holds the mirrors that frame made.

**Beside the snapshot payload**: sounds (`SVC2_SNAPSOUND`, reliable, except a point sound for an
actor with no NetID, which is unreliable), the movers' own sounds by sector (`SVC2_DOORSOUND`,
`SVC2_FLOORSOUND`, `SVC2_MOVERSOUND`), cosmetic spawn events (`SVC2_SNAPCOSMETIC`, unreliable,
appended to the datagram carrying the last chunk when it fits), and the join handshake and the mover
gate (reliable).

**Names.** The public functions live in one namespace, `zx::netsnapshot`, and the pages name them
without it; types, macros and constants stay outside it. Module files call them by their bare names,
through a `using` directive below their includes, and stock files in full. Settings carry the
`fua_snap_` prefix on the server and `fua_csnap_` on the client.

## Pages

The format layer:
- [format.md](format.md): the wire format, quantizers, records, events and file framing.
- [stream.md](stream.md): the `.zsnap` reader.
- [selftest.md](selftest.md): `fua_snap_verify` and `fua_snap_selftest`.
- [formattest.md](formattest.md): where the codec is tested, and the two offline targets.
- [i_tictime.md](i_tictime.md): the tic time base.

The server:
- [capture.md](capture.md): the snapshot tick, legacy suppression, mover capture and sounds,
  cosmetic events, snapshot sounds, the property slots, the recorder and the report.
- [clienttrack.md](clienttrack.md): the per-client pass: acks and repair, the bands, and the delta
  build.

Movers and the client's commands:
- [movers.md](movers.md): the mover classes' capture and apply, the client's sink, hold and reset
  window, ownership, the questions the thinkers ask, the join's link order, scrollers and pushers.
- [polyobj.md](polyobj.md): polyobjects as one record each, captured and reconciled.
- [apply.md](apply.md): the client's snapshot datagram and ack, the mover sounds, the sound table
  and a snapshot sound.

The client's entities:
- [entities.md](entities.md): the ghost table, the decoder, the sampler and the clock model.
- [ghostactor.md](ghostactor.md): the mirrors: the per-frame sync, local animation, the hidden
  legacy copy, mirror spawns, cosmetic events, property sets and late point sounds.
- [localpred.md](localpred.md): the local player's block, its shadow comparison and the prediction
  census.

The recording and test tools:
- [record.md](record.md): the client recording, `.csnap`, and its reader.
- [csnap_replay.md](csnap_replay.md): `fua_csnap_analyze`, a recording through the real decoder and
  sampler.
- [fuzz.md](fuzz.md): decoder fuzzing, in the engine and standalone.
- [tools.md](tools.md): the Python readers and the anchor comparison.

## Keeping the guide true

Each page opens with a **Covers:** line naming its source files. For those files a page must name
every public function, every setting and command, every stock file that calls in, and (for the ghost
table) every installer hook and the files that install them. In the other direction, every name a
page writes in backticks must exist in the code, every link into the design doc must name one of its
headings, and a retired or renamed setting may appear only on a line that says it is.

So: **a patch that changes a module file updates its page in the same patch.** A family of functions
may be listed as a prefix and a trailing star, in backticks, followed by its count in parentheses;
the count makes a new member fail the check until its line is touched, and the line says what the
family does.

Module-owned files are those opening with the 2026 credit header, and every one must be covered by a
page: a new file is not documented until a page's **Covers:** line claims it.

Pages describe the code as it is: no history, no measurements (those belong to the design doc), and
a link to the design doc by heading for anything that needs a reason. `design.md` is the source
tree's own document, ported with its headings and its wrapping untouched so every page's anchors
keep pointing where they point; the pages themselves wrap at 100 columns.

Everything the guide and the code rely on lives in the repo. No page, no module-owned file and no
`[Kaori-Chani]` block may point at a note kept outside it. A finding that matters goes into the
design doc or its page, in a line or two, and is cited by section.
