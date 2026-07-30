# mapinfo-damagetype

Ports the top-level `DamageType` MAPINFO block from UZDoom.

- **Keyword:** `DamageType <name> { factor <f>; noarmor; replacefactor }`
- **Class:** PORTABLE (real behavior)
- **Upstream:** uzdoom@9e2830a3d

## Behavior

Defines global properties for a named damage type, stored into the engine's
existing `GlobalDamageDefinitions` table (which the damage code already
consults via `DamageTypeDefinition::Get` / `ApplyMobjDamageFactor` /
`IgnoreArmor`):

- `factor <float>` — the default damage multiplier for this type (a `0`
  factor implies immunity, so it also sets `ReplaceFactor`).
- `replacefactor` — the type's factor replaces rather than multiplies.
- `noarmor` — armor does not mitigate this damage type.

## Code hooks (in-place)

- `src/zandronum/src/g_level.h` — `FMapInfoParser::ParseDamageDefinition()` decl.
- `src/zandronum/src/g_mapinfo.cpp`
  - `ParseDamageDefinition()` fills a `DamageTypeDefinition` and calls
    `Apply(name)` (stores into `GlobalDamageDefinitions`).
  - `ParseMapInfo()` dispatches the top-level `damagetype` keyword.
