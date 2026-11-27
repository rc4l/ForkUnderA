# features/acs-traces

ACS line traces: ZScript's `LineTrace` and `LineTracer.Trace` on our fixed-point `Trace()`, kept in tracers
a script reads at its own pace, `fua_` functions 24050-24061. Modder
reference: [`docs/modding/acs/Line_traces.md`](../../../../../docs/modding/acs/Line_traces.md).

- `fua_LineTrace` is UZDoom's `P_LineTrace`: `ComputeLineTraceMasks` builds its actor and line masks from
  the `TRF_` flags, `ComputeLineTraceStart` its start, and `ComputeLineTraceDecision` is its
  `CheckLineTrace`. `TRF_BLOCKSELF` leaves out `MF8_BLOCKASPLAYER`, which we don't have; portal counting is
  dropped. Actors are hit at their attack extents unless `TRF_FUA_PHYSICALSIZE` asks for `TRACE_NoPassWidth`.
- A trace passes through a client's snapshot mirror (`NETFL_MIRROR`), which only draws the actor scripts
  read, so no hit is ever a mirror.
- A hit is turned into ACS values once (`ComputeTraceHit`): its actor as a handle, lines and sectors as
  indices plus their first ID or tag, positions cut to ACS fixed point, the texture as a name.
- `fua_Trace`'s rules stand in for `LineTracer.TraceCallback`: `ComputeRuleAction` picks the first rule
  matching a hit's kind (`ComputeHitKind`) and filter, `ComputeRuleRecord` keeps stops and records, and
  `ComputeRuleStatus` tells `Trace()` what to do, a floor or ceiling always ending the trace. `FQ_`
  filters run through the queries' `ActorFilters`, `TQ_` ones through `ComputeLineFilterMatch` and the
  hit's texture. `Trace()` ends on a floor or ceiling without calling back, so that hit is decided after
  it returns, unless the rules already saw it.
- A `TRACE_Callback` rule runs the tracer's callback, an ACS function reference, through
  `DLevelScript::RunFunctionNow`: a script started at the function's address that ends at the
  function's own return (`RunScript` ends a run on a return with no caller), reading its caller's
  tracers (`FuaTracers`). One that waits is unlinked and its hit stops the trace; no trace starts while a
  callback runs; an actor a callback removed is passed by. `ComputeCallbackAction` turns the returned
  value into an action.
- A tracer lives on its script (`DLevelScript::fua_tracers`), freed and saved with it; a script holds up
  to 16, the 17th `fua_TracerCreate` returning 0 with one warning a map. Ids come from the query ids'
  serial (`acsqueries::NextObjectId`), so a tracer's and a query's never meet.
- Scripts save their tracers from SAVEVER 4541, their rules from 4542 and their callback from 4543, as one run of ints (`ComputeFlattenTracers`) with texture
  names saved as names through the queries' `SerializeNamedRun`; a damaged run loads as no tracers.

## Hooks

- `p_acs.h`: `DLevelScript::fua_tracers`, and the callback run's `fua_tracerOwner`, `fua_entryFunction`
  and `fua_returned`.
- `p_acs.cpp`: the twelve functions, `DLevelScript::Serialize`, `RunFunctionNow` and `IsCallableFunction`,
  and in `RunScript` the entry function's arrays and the return that ends a callback's run.
- `g_level.cpp`: `EnterLevel`, so the full-script warning shows once a map.
- `version.h`: SAVEVER 4541, 4542 and 4543.
- `features/acs-argcount`: each function's minimum argument count.

## Tests

- `computation/traces_compute_test.cpp`: the `TRF_` masks and `TRF_BLOCKSELF` per shooter, angles,
  direction and start offsets, the pass-or-stop decision, wall parts and textures, hits of every kind as
  ACS values, the 16-tracer and 64-hit limits, and saved tracers with damaged inputs; rule kinds, actions
  and filters, which filter fits which kind, first-match decisions, recording and aborting, planes ending
  the trace, 32 rules a tracer, and rules saved with their names and loaded from runs without them.
- `fuactl scenarios acstraces`: a wall with every `TR_` property, a `BLOCKHITSCAN` line, a step's upper and
  lower parts, a row of imps through `TRF_THRUACTORS` and `TRF_THRUSPECIES`, a solid pillar, a 3D floor,
  Boom and 3D-floor water, the sky with and without `TRF_NOSKY`, `fua_LineInfo` and `fua_SectorInfo`,
  16 tracers and one warning, another script's and a freed id, and two tracers kept across a savegame;
  then `fua_Trace` rules recording, stopping, continuing, aborting and filtering by tid, class, handle,
  line flag, special and texture, an ignored actor, `TRF_FUA_ALLLINES`, floors, the sky with and without
  `TRF_NOSKY`, refused rules and the 32-rule cap, and a tracer's rules kept across the savegame.
  Callbacks from `fixtures/acstraces/tracecb.bcs` (compiled by zt-bcc, loaded as a library): a piercing
  shot reading each hit, a context naming the action, an invalid action, a callback that waits (written as
  assembly, since zt-bcc refuses `Delay` in a function), a refused nested trace, a cleared callback, one
  removing an actor ahead, and one kept across the savegame. It fails on a build without the functions,
  with the tracers' `SerializeScript` taken out, and with the rules or the callback left out of the save.
- `fuactl scenarios netacstraces`: every step before that savegame, the rules' and the first callbacks'
  too (`tracecb.bcs`' CLIENTSIDE copies 1030 and 1032), as CLIENTSIDE copies on the player's client, an
  observer and a late joiner, then on the server, each tracing its own copy of the map and its actors
  with its own tracer ids; the callback that removes an imp runs last, on every machine. The server runs
  the snapshot netcode at `fua_snap_nolegacy 2`.
