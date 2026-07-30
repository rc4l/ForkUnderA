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

## Attack extent (HitRadius / HitHeight)

The [projectile-pass] `HitRadius`/`HitHeight` properties (`projectilepassradius`/
`projectilepassheight`) are runtime-adjustable the same way:

- **ACS**: `SetActorProperty(tid, APROP_HitRadius/APROP_HitHeight, v)` (+ get/check).
- **DECORATE**: `A_SetHitSize(hitradius, hitheight)` — `-1` keeps the current value, `0`
  clears it so the extent falls back to the physical radius/height.
- `AActor::SetHitSize` relinks, because the blockmap link radius is
  `MAX(radius, GetAttackRadius())` (`p_maputl.cpp`). Unlike the physical size, the attack
  extent is **server-authoritative** and not networked — matching the existing property.

**ACS ids:** `APROP_HitRadius = 50`, `APROP_HitHeight = 51`. Ids 42–49 are left unused to
match UZDoom/GZDoom (Friction … WaterDepth), which ZandroX does not implement yet, so ACS
written against UZDoom's constants stays valid. `APROP_Height`/`APROP_Radius` (35/36) already
match UZDoom. (These fork-specific constants aren't in stock `acc`; use the numeric id or a
local `#define` in ACS.)

## In-engine hooks (edits to existing files, not part of this folder)

- `p_map.cpp` — `AActor::SetSize` (relink-safe resize + player `FullHeight`) and
  `AActor::SetHitSize` (relink-safe attack-extent change).
- `thingdef/thingdef_codeptr.cpp` — `A_SetSize` (uses both helpers) and `A_SetHitSize`.
- `p_acs.h` / `p_acs.cpp` — `APROP_Radius`/`APROP_Height` and `APROP_HitRadius`/
  `APROP_HitHeight` get/set/check.
- `sv_commands.{cpp,h}`, `sv_main.cpp`, `cl_main.cpp`, `network.h`, `network_enums.h`,
  `protocolspec/spec.things.txt` — the `SVC2_SETTHINGSIZE` command + late-join full update
  (physical size only; the attack extent is server-authoritative).
- `d_player.h` / `p_user.cpp` / `version.h` — per-pawn `FullHeight` (+ savegame field / `SAVEVER`).

## Provenance

`A_SetSize` is ported from GZDoom/UZDoom — see the codepointer comment in
`thingdef_codeptr.cpp` for the upstream link.
