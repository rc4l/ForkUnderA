# localpred: the local player's block and its census

Covers: src/zandronum/src/features/net-snapshot/localpred.h, src/zandronum/src/features/net-snapshot/localpred.cpp

## Responsibility

The local player's own state, which no mirror can carry: the sink the apply layer writes a local
block through, the shadow ring that block is kept in, the comparison against what the legacy
position command wrote at the same tick, and the reconciliation and arrival censuses with their
report. The block's codec and its place in a payload are `format.h`'s and `entities.cpp`'s
([entities.md](entities.md)); everything here is one call from `cl_pred.cpp` or `cl_main.cpp`.

The source tree kept this in stock `cl_pred.cpp`. It is a module file here, so the stock prediction
code holds the calls and none of the bookkeeping. The design: [The local
player](design.md#42-the-local-player).

## The block, applied or compared

A block carrying `SNAPLOCAL_AUTHORITATIVE` is applied by `CLIENT_ApplyLocalBlock`, which stays in
`cl_main.cpp` beside the legacy command's own steps and returns the outcome this file counts. It
runs those steps in their order, the two clocks first, including the two that are not prediction --
`CLIENT_SetLatestServerGametic`, which lag compensation rewinds against, and `g_bClientLagging` --
and never the command buffer.

Without the bit the block is a shadow: stored by client tic and compared, when the replay is about
to read that slot, against what the legacy position command wrote. Agreement shows the block carries
the right values, not that it is enough for the replay, which is why the two modes read apart on the
report. A block addressed to any id but the console player's reserved one is dropped and counted as
wrong-id.

In the authoritative mode the comparison is not printed at all: the block wrote the ring, so
comparing it against the ring compares it with itself and the zero that comes out means nothing --
indistinguishable on the page from the verified zero the shadow mode produces.

## What it keeps, and what the census only counts

The shadow ring, the slot stamps and the legacy marks are kept whatever the settings say, since they
are how a later tic is read at all: `LocalPredStamp` records which tick a slot now holds, so a slot
reused 70 tics later is not read as the tick it once held, and `LocalPredLegacyWrote` marks the
slots the legacy command wrote, which is what tells a block that found no legacy write from one that
disagreed with it. Only the counters are gated on `fua_csnap_predcensus`, so turning the census on
mid-session costs nothing it cannot measure.

The prediction depth figures the client's own report prints are taken ahead of that gate, since
`[snap] predict` is part of the 5-second report and not of this census.

## The report

With `fua_csnap_predcensus` set to an interval in seconds:
- `[snap] reconcile`: the corrections, their XY error by size (<1 u, <4, <16, <64, 64+ with the
  worst), the Z tail, the replay depth beside them, and the ones that could not be compared -- an
  error figure without a depth cannot be compared across pings, so they print together.
- `[snap] arrival`: the gaps between periodic corrections, by tics since the previous one, counting
  every one that lands whether or not its slot was predictable. While the legacy command carries the
  corrections, `cl_buffercommands` buffers them, so the wire rate and the applied rate differ.
- `[snap] local block`: the mode, then in AUTHORITATIVE the blocks and outcomes (applied, no-pawn,
  out-of-order, dead, spectator, wrong-id, no-sink, unknown), whose parts must add up to the blocks
  or the line says `** UNACCOUNTED **`; in shadow the blocks, the slots the legacy command also
  wrote, the differences by field and the worst XY and Z. Divergence must read 0, since both sides
  read the same server fields on the same tic. A mode change inside a window is named.
- `[snap] local attrib`: every visit in one named outcome -- compared, idle, repeat, absent, stale,
  no-legacy -- adding up to the visits, so a missing outcome shows as a total that does not add up
  rather than as a quiet omission.

## Public functions

Each is one call from the stock prediction code:
- `LocalPredInstall`: installs the sink the apply layer writes a block through. With none installed
  the apply layer counts `g_localNoSink` and draws nothing.
- `LocalPredReset`: cleared with the prediction ring beside it, so a slot's stamp and its shadow
  never outlive the ring they describe.
- `LocalPredLegacyWrote`: the legacy command has just written this slot.
- `LocalPredError`: the reconciliation error -- where the server's position landed against what the
  ring predicted for the same tick, both in map units as the ring holds them.
- `LocalPredArrival`: a periodic correction arrived, called from the two places one lands, so the
  stock setter keeps its own signature.
- `LocalPredVisit`: the tic the replay is about to read, with the ring's position, velocity and jump
  tics, so every tic the cursor names lands in one named outcome; `bIdle` says prediction will not
  run this tic.
- `LocalPredSample`: the replay's depth for this tic, and the report when its interval is up. The
  client's `[snap] predict` counters are taken here too.
- `LocalPredStamp`: this slot now holds this tick.

## Settings

Client only, unarchived: `fua_csnap_predcensus`, the report interval in seconds; 0 off.

## Rules that must hold

- **One visit, one outcome.** Every tic the replay's cursor names is counted exactly once, and the
  attribution line adds up or says it does not.
- **A slot is read by its stamp, never by its index alone.** The ring is 70 tics deep and its slots
  are reused.
- **The shadow is kept whatever the census says.** The comparison needs it the moment the replay
  reaches that slot, which is before anyone decides to look.
- **The authoritative mode prints no comparison.** A zero taken from the ring the block itself wrote
  would read exactly like a verified one.
- **The stock setter keeps its signature.** A periodic correction announces itself through
  `LocalPredArrival` from the two places one lands, rather than by a flag threaded through the
  engine's own call.

## Engine hooks

Each site carries a `[Kaori-Chani]` line.
- `cl_pred.cpp`: `LocalPredReset` with the ring's clear, `LocalPredInstall` in
  `CLIENT_PREDICT_Construct`, `LocalPredLegacyWrote` where the legacy command writes a slot,
  `LocalPredError` and `LocalPredVisit` in `CLIENT_PREDICT_SetPosition` and
  `CLIENT_PREDICT_PlayerPredict`, `LocalPredSample` with the replay's depth, and `LocalPredStamp`
  where a slot takes a tick. The same file installs the mover sinks ([movers.md](movers.md)) and
  carries the two prediction brackets the client census times ([ghostactor.md](ghostactor.md)).
- `cl_main.cpp`: `LocalPredArrival` in `CLIENT_ApplyLocalBlock` and in
  `ServerCommands::MoveLocalPlayer::Execute`, the snapshot block's and the legacy command's
  correction. `CLIENT_ApplyLocalBlock` itself stays in `cl_main.cpp`, and this file's sink calls it.

## Tests

None in the self-tests: the block needs a server and a predicting client. Live, with
`fua_csnap_predcensus` on, `[snap] local block` reads 0 no-sink and, in shadow mode, 0 divergence,
and `[snap] local attrib` adds up. The block's codec is covered by `format_test.cpp`
([format.md](format.md)), and a recording carries every applied block, so a replay reads what the
client was told ([csnap_replay.md](csnap_replay.md)).
