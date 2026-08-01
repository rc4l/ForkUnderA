# ZandroX modding reference

Reference documentation for everything ZandroX adds to, or changes in, the modding surface it
inherits from Zandronum — DECORATE, ACS, and MAPINFO.

This is the **modder-facing** half of the documentation. It is deliberately separate from the
engineering notes in [`src/zandronum/src/features/`](../../src/zandronum/src/features/), which
explain how a feature is implemented and what it cost; those are linked from each page under
*Implementation notes* when you need to go deeper.

## How to read a page

Pages follow the [ZDoom wiki](https://zdoom.org/wiki/)'s layout, because that is the reference
Doom modders already know:

| Element | Meaning |
|---|---|
| **Bold class name** at the top | The class the function is a member of. It can only be called from that class's states (or a subclass's). |
| Signature line | Return type first, then the name, then parameters as `type name = default`. |
| **Availability** block | Whether the entry exists in stock Zandronum, and any restriction on where it works. **Read this first** — several ZandroX additions are single-player only. |
| *Usage* | What it does, what existed before it, and what it costs. |
| *Parameters* | One entry per parameter. Flag constants are listed under the parameter that takes them. |
| *Examples* | A complete, loadable DECORATE actor, with prose explaining why it is built that way. |

Anything marked **ZandroX only** does not exist in Zandronum 3.2 or in any other port unless the
page says otherwise. A mod that uses it will not load elsewhere.

## Categories

### DECORATE — psprite layers

The `A_Overlay` family, restored from GZDoom in
[#44](https://github.com/rc4l/ZandroX/pull/44). Lets a weapon draw more than the two stock
player-sprite layers.

- [Psprite layers](decorate/Psprite_layers.md) — the layer model these functions operate on. **Start here.**
- [A_Overlay](decorate/A_Overlay.md) — create, replace, or remove a layer
- [A_ClearOverlays](decorate/A_ClearOverlays.md) — remove a range of layers
- [A_OverlayFlags](decorate/A_OverlayFlags.md) — set or clear a layer's `PSPF_` flags
- [A_OverlayOffset](decorate/A_OverlayOffset.md) — move a layer
- [A_OverlayScale](decorate/A_OverlayScale.md) — resize a layer
- [A_OverlayAlpha](decorate/A_OverlayAlpha.md) — set a layer's translucency
- [A_OverlayRenderStyle](decorate/A_OverlayRenderStyle.md) — set a layer's render style
- [OverlayID](decorate/OverlayID.md) — expression function; the calling layer's id

Deliberately **not** ported from GZDoom: `A_OverlayPivot`, `A_OverlayRotate` and
`A_OverlayVertexOffset` (with their `PSPF_PIVOTPERCENT` flag). This renderer draws psprites in an
anisotropic pixel space, so faithful rotation needs its own work. A GZDoom mod that calls them
will fail to load.

### DECORATE — ripping

Authorable ripper behaviour ([#144](https://github.com/rc4l/ZandroX/pull/144)): budgets, damage
falloff, a tier system, and a state that fires on each rip. Previously ripping was binary —
`+RIPPER` or nothing — with no properties attached to it at all.

- [Ripping](decorate/Ripping.md) — the rip system: budgets, tiers, detonation rules, netplay.
  **Start here.**

*Properties* — [RipperMaxDamage](decorate/RipperMaxDamage.md) ·
[RipperCount](decorate/RipperCount.md) · [RipperMaxCount](decorate/RipperMaxCount.md) ·
[RipperDamageFactor](decorate/RipperDamageFactor.md) · [RipperLevel](decorate/RipperLevel.md) ·
[RipLevelMin](decorate/RipLevelMin.md) · [RipLevelMax](decorate/RipLevelMax.md) ·
[RipSound](decorate/RipSound.md)

*Flags* — [+NORIPSOUND](decorate/NORIPSOUND.md) ·
[+RIPEXPLODEONLIMIT](decorate/RIPEXPLODEONLIMIT.md) ·
[+RIPPERNOPAIN](decorate/RIPPERNOPAIN.md) ·
[+RIPSOUNDNORESTART](decorate/RIPSOUNDNORESTART.md) ·
[+USERIPSTATE](decorate/USERIPSTATE.md)

*States* — [Rip](decorate/Rip.md)

*Functions* — [A_SetRipperLevel](decorate/A_SetRipperLevel.md) ·
[A_SetRipMin](decorate/A_SetRipMin.md) · [A_SetRipMax](decorate/A_SetRipMax.md) ·
[A_ResetRipCounters](decorate/A_ResetRipCounters.md)

### DECORATE — actor properties

Attack hitboxes independent of the collision box
([#62](https://github.com/rc4l/ZandroX/pull/62)), so an actor can be shot at a different extent
than it moves at.

- [Attack hitbox](decorate/Attack_hitbox.md) — which attack paths honour it, and the widening
  and narrowing rules. **Start here.**
- [HitRadius](decorate/HitRadius.md) — horizontal attack extent
- [HitHeight](decorate/HitHeight.md) — vertical attack extent (alias: `ProjectilePassHeight`)

### DECORATE — runtime size

Changing an actor's size after spawn ([#104](https://github.com/rc4l/ZandroX/pull/104)), rather
than only at class-definition time.

- [A_SetSize](decorate/A_SetSize.md) — change the physical collision box, optionally reverting
  if the actor no longer fits
- [A_SetHitSize](decorate/A_SetHitSize.md) — change the attack hitbox only

### ACS

- [Actor size properties](acs/Actor_size_properties.md) — `APROP_Radius`, `APROP_Height`,
  `APROP_HitRadius`, `APROP_HitHeight`
  ([#104](https://github.com/rc4l/ZandroX/pull/104))

### DECORATE — state jumps

- [A_JumpIfInput](decorate/A_JumpIfInput.md) — jump when a player is holding given buttons
  ([#56](https://github.com/rc4l/ZandroX/pull/56)). Puts input tests in the state sequence
  instead of a client-side ACS script. Unlike the overlay family, this one works in netgames.

### Not yet written

Merged features whose reference pages are still to be written, roughly in the order they are
planned:

| Area | Feature | PR |
|---|---|---|
| DECORATE | `A_NoiseAlert`, `A_StartSound`, `A_FireProjectile` | [#102](https://github.com/rc4l/ZandroX/pull/102) |
| DECORATE | `A_SpawnProjectile` | [#98](https://github.com/rc4l/ZandroX/pull/98) |
| DECORATE | `A_CheckRange` gains `two_dimension` | [#103](https://github.com/rc4l/ZandroX/pull/103) |
| DECORATE | MBF21 flags and boss flags, `+FULLVOLSEE` | [#84](https://github.com/rc4l/ZandroX/pull/84), [#101](https://github.com/rc4l/ZandroX/pull/101), [#97](https://github.com/rc4l/ZandroX/pull/97) |
| DECORATE | PSX / Doom 64 render-style flags | [#116](https://github.com/rc4l/ZandroX/pull/116) |
| MAPINFO | Full MAPINFO / ZMAPINFO / GAMEINFO parity | [#105](https://github.com/rc4l/ZandroX/pull/105) |
