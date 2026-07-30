# actorresize

Runtime radius/height resizing for actors — changeable at any time, not just at class
definition. Reachable from **ACS** (`SetActorProperty(tid, APROP_Radius/APROP_Height, v)`)
and **DECORATE** (`A_SetSize(newradius, newheight, testpos)`), and synchronized in
client/server play (`SVC2_SETTHINGSIZE`).

`actorresize.h` holds the **pure, engine-free** bits so they can be unit-tested
(`actorresize_test.cpp`) without linking the engine:

- `ComputeResolvedDimension` — resolves `A_SetSize`'s `-1` "keep the current value" sentinel.
- `ComputeSizeDelta` / `SizeDelta` — which dimensions differ (mapped to `ACTORSIZE_RADIUS` /
  `ACTORSIZE_HEIGHT` for the broadcast; `Any()` gates whether anything is sent).

The resize itself — `AActor::SetSize` (unlink → set radius/height → relink, with the
optional `testpos` revert, plus the player `FullHeight` update) — stays in the engine
(`p_map.cpp`) because it is inseparable from the world/blockmap linking; it is verified
end-to-end via the MCP rather than in a unit test.

## In-engine hooks (edits to existing files, not part of this folder)

- `p_map.cpp` — `AActor::SetSize` (the relink-safe resize + player `FullHeight`).
- `thingdef/thingdef_codeptr.cpp` — `A_SetSize` codepointer (uses both helpers here).
- `p_acs.cpp` — `APROP_Radius` / `APROP_Height` setter cases.
- `sv_commands.{cpp,h}`, `sv_main.cpp`, `cl_main.cpp`, `network.h`, `network_enums.h`,
  `protocolspec/spec.things.txt` — the `SVC2_SETTHINGSIZE` command + late-join full update.
- `d_player.h` / `p_user.cpp` / `version.h` — per-pawn `FullHeight` (+ savegame field / `SAVEVER`).

## Provenance

`A_SetSize` is ported from GZDoom/UZDoom — see the codepointer comment in
`thingdef_codeptr.cpp` for the upstream link.
