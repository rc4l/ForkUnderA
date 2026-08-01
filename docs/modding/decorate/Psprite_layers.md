# Psprite layers

A **psprite** ("player sprite") is one of the sprites drawn in front of the player's view — the
weapon in their hands, its muzzle flash, the Strife targeter reticles. Each psprite lives on a
numbered **layer**.

Stock Zandronum has exactly five psprites, in a fixed array: the weapon, its flash, and three
targeter slots. There is no way for a mod to add a sixth. ZandroX replaces that array with a
container addressed by **layer id**, so a weapon can create as many layers as it wants at
whatever ids it likes, and the [`A_Overlay` family](#see-also) manipulates them.

## Availability

> **ZandroX only.** Neither the layer model nor the `A_Overlay` family exists in Zandronum 3.2.
>
> **Single-player only.** Overlay layers are **not replicated to clients**. In a netgame, an
> overlay a weapon creates is visible only to the player running that code, and only if they are
> the host of a local game. Do not build multiplayer content on overlays. The two reserved
> layers below (weapon and flash) are networked exactly as they always were, so ordinary weapons
> are unaffected.

## Layer ids

A layer id is any `int`. Five ids are reserved by the engine:

| Constant | Value | Layer |
|---|---|---|
| `PSP_WEAPON` | `1` | The ready weapon's own sprite. |
| `PSP_FLASH` | `1000` | The weapon's muzzle flash (what `A_GunFlash` sets). |
| — | `2147483645` | Strife targeter, centre. |
| — | `2147483646` | Strife targeter, left. |
| — | `2147483647` | Strife targeter, right. The highest layer number there is. |

These values match GZDoom's, so a layer number copied out of a GZDoom mod lands in the same
place relative to the weapon.

**Use ids `2` and up, or `-2` and down.** This is GZDoom's recommendation and it applies here
too: it keeps clear of the reserved ids, including `-1`, which GZDoom reserves as
`PSP_STRIFEHANDS` for Strife's burning-hands effect. ZandroX does **not** reserve `-1` — its
`A_ItBurnsItBurns` drives the weapon layer directly — so `-1` works in this engine, but a mod
that uses it will misbehave if it is ever run under GZDoom.

Layer `0` is not a real layer. Passing `0` as the `layer` argument of any `A_Overlay` function
means **"the layer whose state is currently executing"** — see
[OverlayID](OverlayID.md). Outside a psprite state action there is no such layer, and the call
does nothing.

## Draw order

Layers are drawn in **ascending id order**, so a higher id draws *in front of* a lower one:

- An overlay at id `2`..`999` draws **in front of the weapon** but **behind the flash**.
- An overlay at id `1001` or above draws **in front of the flash**.
- A **negative** id draws **behind the weapon** — useful for a left hand, a shield, or a
  backdrop the weapon sits on top of.

The three targeter layers are drawn in a separate later pass and always sit in front of
everything else.

## Layer lifetime

A layer is created by [`A_Overlay`](A_Overlay.md) and disappears on any of these:

- **Its state chain ends.** An overlay whose state sequence reaches `Stop` removes itself. This
  is the normal way to end an overlay, and it is the difference between an overlay and a
  reserved layer: the weapon and flash layers persist with no state.
- **[`A_Overlay`](A_Overlay.md) is called on it with no state**, which removes it explicitly.
- **[`A_ClearOverlays`](A_ClearOverlays.md)** removes it as part of a range.
- **The weapon goes away.** Switching weapons, dropping the weapon, and dying all clear every
  overlay, so an overlay can never outlive the weapon that created it. Overlays are cleared
  *before* the incoming weapon's Select state runs, so a weapon may create overlays from Select.

  > **Differs from GZDoom.** There, only overlays created *by a weapon* are cleared on switch;
  > ones created by a `CustomInventory` item persist until the item goes away. ZandroX clears
  > **every** overlay on weapon switch regardless of what created it.

## Where these functions can be called from

The `A_Overlay` family is defined on `Inventory`, so it can be called from the states of a
weapon or any other inventory item, including `CustomInventory`. It does nothing when the
calling actor has no player attached.

> **Differs from GZDoom.** There, the player actor itself can also call these functions.
> In ZandroX they are `Inventory`-scoped only.

## What a layer carries

Beyond the sprite and frame every psprite has always had, each layer owns:

| Property | Set by | Default |
|---|---|---|
| Offset (`x`, `y`) | [`A_OverlayOffset`](A_OverlayOffset.md) | `(0, 0)` |
| Scale (`x`, `y`) | [`A_OverlayScale`](A_OverlayScale.md) | `(1, 1)` |
| Alpha | [`A_OverlayAlpha`](A_OverlayAlpha.md) | `1.0`, but unused — the weapon's alpha is drawn |
| Render style | [`A_OverlayRenderStyle`](A_OverlayRenderStyle.md) | unset — the weapon's style is drawn |
| `PSPF_` flags | [`A_OverlayFlags`](A_OverlayFlags.md) | `PSPF_ADDWEAPON \| PSPF_ADDBOB` |

A new overlay therefore **follows the weapon by default**: `PSPF_ADDWEAPON` and `PSPF_ADDBOB`
are already set, so it moves with the gun and sways with the player's bob, sitting at the
weapon's position until [`A_OverlayOffset`](A_OverlayOffset.md) moves it. This matches GZDoom.
Clear those two flags if you want an overlay pinned to the screen instead.

Everything else is opt-in and starts off: the overlay is **not** sped up by `sv_fastweapons` or
`PowerDoubleFiringSpeed`, and it is drawn with the weapon's alpha and render style rather than
its own. Each is enabled with a `PSPF_` flag.

The reserved weapon and flash layers ignore the flags entirely and behave exactly as they always
have, which is why adding this feature changed nothing about existing weapons.

## Implementation notes

The layer container, the reserved-id remapping, and the per-layer render state are in
[`p_pspr.cpp`](../../../src/zandronum/src/p_pspr.cpp) and
[`p_pspr.h`](../../../src/zandronum/src/p_pspr.h); the drawing pass is in
[`gl/scene/gl_weapon.cpp`](../../../src/zandronum/src/gl/scene/gl_weapon.cpp). The branch-heavy
decisions are extracted into
[`computation/psprite_overlay_compute.cpp`](../../../src/zandronum/src/computation/psprite_overlay_compute.cpp)
with colocated tests.

Introduced in [#44](https://github.com/rc4l/ZandroX/pull/44).

## See also

| The `A_Overlay` family | |
|---|---|
| [A_Overlay](A_Overlay.md) | [A_OverlayFlags](A_OverlayFlags.md) |
| [A_ClearOverlays](A_ClearOverlays.md) | [A_OverlayOffset](A_OverlayOffset.md) |
| [OverlayID](OverlayID.md) | [A_OverlayScale](A_OverlayScale.md) |
| | [A_OverlayAlpha](A_OverlayAlpha.md) |
| | [A_OverlayRenderStyle](A_OverlayRenderStyle.md) |
