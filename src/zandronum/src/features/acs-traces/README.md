# features/acs-traces

ACS line traces: ZScript's `LineTrace` on our fixed-point `Trace()`, kept in tracers a script reads at its
own pace, `fua_` functions 24050-24061 (24053, 24054 and 24056 come with the tracer rules). Modder
reference: [`docs/modding/acs/Line_traces.md`](../../../../../docs/modding/acs/Line_traces.md).

- `fua_LineTrace` is UZDoom's `P_LineTrace`: `ComputeLineTraceMasks` builds its actor and line masks from
  the `TRF_` flags, `ComputeLineTraceStart` its start, and `ComputeLineTraceDecision` is its
  `CheckLineTrace`. `TRF_BLOCKSELF` leaves out `MF8_BLOCKASPLAYER`, which we don't have; portal counting is
  dropped. Actors are hit at their attack extents unless `TRF_FUA_PHYSICALSIZE` asks for `TRACE_NoPassWidth`.
- A hit is turned into ACS values once (`ComputeTraceHit`): its actor as a handle, lines and sectors as
  indices plus their first ID or tag, positions cut to ACS fixed point, the texture as a name.
- A tracer lives on its script (`DLevelScript::fua_tracers`), freed and saved with it; a script holds up
  to 16, the 17th `fua_TracerCreate` returning 0 with one warning a map. Ids come from the query ids'
  serial (`acsqueries::NextObjectId`), so a tracer's and a query's never meet.
- Scripts save their tracers from SAVEVER 4541, as one run of ints (`ComputeFlattenTracers`) with texture
  names saved as names through the queries' `SerializeNamedRun`; a damaged run loads as no tracers.

## Hooks

- `p_acs.h`: `DLevelScript::fua_tracers`.
- `p_acs.cpp`: the nine functions and `DLevelScript::Serialize`.
- `g_level.cpp`: `EnterLevel`, so the full-script warning shows once a map.
- `version.h`: SAVEVER 4541.
- `features/acs-argcount`: each function's minimum argument count.

## Tests

- `computation/traces_compute_test.cpp`: the `TRF_` masks and `TRF_BLOCKSELF` per shooter, angles,
  direction and start offsets, the pass-or-stop decision, wall parts and textures, hits of every kind as
  ACS values, the 16-tracer and 64-hit limits, and saved tracers with damaged inputs.
- `fuactl scenarios acstraces`: a wall with every `TR_` property, a `BLOCKHITSCAN` line, a step's upper and
  lower parts, a row of imps through `TRF_THRUACTORS` and `TRF_THRUSPECIES`, a solid pillar, a 3D floor,
  Boom and 3D-floor water, the sky with and without `TRF_NOSKY`, `fua_LineInfo` and `fua_SectorInfo`,
  16 tracers and one warning, another script's and a freed id, and two tracers kept across a savegame.
  It fails on a build without the functions, and with the tracers' `SerializeScript` taken out.
