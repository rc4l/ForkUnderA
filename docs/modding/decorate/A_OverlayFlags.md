**[Inventory](https://zdoom.org/wiki/Classes:Inventory)**

`void A_OverlayFlags (int layer, int flags, bool set)`

# A_OverlayFlags

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Behaviour follows GZDoom's DECORATE
> `A_OverlayFlags`.
>
> **Single-player only.** Overlay layers are not replicated — see
> [Psprite layers](Psprite_layers.md#availability).

## Usage

Sets or clears `PSPF_` flags on a [layer](Psprite_layers.md). These flags control how closely an
overlay is tied to the weapon that created it.

A new overlay starts with **`PSPF_ADDWEAPON | PSPF_ADDBOB` already set**, so out of the box it
moves and bobs with the gun. Every other flag starts off: the overlay is not sped up by the
weapon speed modifiers, and it is drawn with the weapon's alpha and render style rather than its
own.

Flags persist on the layer until changed. They are not reset when the layer's state changes, so
setting them once on the layer's first frame is the normal pattern.

The five reserved layers ignore `PSPF_` flags entirely. Setting flags on `PSP_WEAPON` or
`PSP_FLASH` is accepted and does nothing — that is deliberate, and it is why existing weapons
were unaffected by this feature.

> **Differs from GZDoom.** There, `PSP_WEAPON` accepts flag changes like any other layer.
> A GZDoom mod that configures the main weapon layer through `A_OverlayFlags` will load here
> but that call will have no effect.

### Parameters

- `int layer`

  The layer to change. `0` means the layer whose state is currently executing — see
  [OverlayID](OverlayID.md). Does nothing if the layer does not exist.

- `int flags`

  Flags to set or clear. Multiple flags can be combined with `|`. The following are available:

  - **PSPF_ADDWEAPON** — *(on by default)* the layer's offset is added to the **weapon layer's**
    offset, so the overlay moves with the gun as the gun's own states reposition it. Clear it
    and the overlay stays where [`A_OverlayOffset`](A_OverlayOffset.md) put it, independent of
    what the weapon is doing.

  - **PSPF_ADDBOB** — *(on by default)* the weapon **bob** is added to the layer's offset, so
    the overlay sways with the player's movement. Clear it for an overlay that should stay
    pinned to the screen — a HUD panel or a reticle — while the gun bobs underneath it.

  - **PSPF_POWDOUBLE** — the layer's state durations are halved while the player has
    `PowerDoubleFiringSpeed` (or the Skulltag double-firing-speed effect). Without it, a
    speed-boosted weapon would animate at double rate while its overlays crawled along at normal
    speed and fell out of sync.

  - **PSPF_CVARFAST** — the layer obeys the `sv_fastweapons` server variable, the same way the
    weapon layer does. Without it, overlays keep their authored timing regardless of the CVar.

  - **PSPF_ALPHA** — the layer is drawn with its own alpha, as set by
    [`A_OverlayAlpha`](A_OverlayAlpha.md), instead of inheriting the weapon's. Without this
    flag, `A_OverlayAlpha` records a value that is never used.

  - **PSPF_FORCEALPHA** — in GZDoom, applies the layer's alpha even under a render style that
    enforces its own, and does not require `PSPF_ALPHA` alongside it. **In ZandroX this is
    currently identical to `PSPF_ALPHA`** — the renderer applies the layer alpha whenever either
    flag is set, and neither can override a style that enforces its own alpha. The flag is
    accepted so GZDoom-authored code ports across unchanged; treat the two as one flag for now.

  - **PSPF_RENDERSTYLE** — the layer is drawn with its own render style, as set by
    [`A_OverlayRenderStyle`](A_OverlayRenderStyle.md), instead of inheriting the weapon's. Has
    no effect unless `A_OverlayRenderStyle` has actually been called on the layer.

  - **PSPF_FORCESTYLE** — in GZDoom, exempts the layer from powerups that impose a render style
    on everything the player is drawing — a Blursphere, which otherwise makes the weapon and all
    its overlays fuzzy along with the player. **In ZandroX this is currently identical to
    `PSPF_RENDERSTYLE`** and grants no such exemption.

  - **PSPF_FLIP** — mirrors the layer's **pixels** horizontally, without moving it. Use it to
    reuse a right-hand sprite as a left hand.

  - **PSPF_MIRROR** — reflects the layer's **position** about the centre of the view, keeping
    the pixels' orientation. Combine with `PSPF_FLIP` for a properly handed mirror image: one
    moves the sprite to the other side, the other flips what it depicts.

  - **PSPF_PLAYERTRANSLATED** — draws the layer with the player's colour translation, the same
    one applied to their body sprite. For overlays that depict the player themselves — a gloved
    hand, an arm — so they match the player's team or colour choice.

  - **PSPF_INTERPOLATE** — smooths the layer's movement between tics rather than snapping it to
    each new offset, so motion driven by [`A_OverlayOffset`](A_OverlayOffset.md) is fluid at
    frame rates above 35 fps. Note that an `A_OverlayOffset` call with neither `WOF_ADD` nor
    `WOF_INTERPOLATE` turns interpolation off for that tic, so a deliberate instant jump still
    reads as instant.

  **Not available in ZandroX:** GZDoom's `PSPF_PIVOTPERCENT` has no counterpart here, because
  the `A_OverlayPivot` / `A_OverlayRotate` / `A_OverlayVertexOffset` functions it configures were
  deliberately left out of the port — this renderer draws psprites in an anisotropic pixel space,
  so faithful rotation needs work of its own.

- `bool set`

  `true` sets the given flags, `false` clears them. Flags not named in `flags` are left alone
  either way.

## Examples

A muzzle-glow overlay that behaves like part of the weapon: it follows the gun's offset and bob,
speeds up with the player's power-ups, and draws additively at half alpha over the top of the
weapon.

The whole configuration is done on the layer's first frame, with a `0`-tic state so it costs no
time. `OverlayID()` is used instead of hardcoding `1001` so the sequence keeps working if the
layer id changes. `PSPF_ADDWEAPON` and `PSPF_ADDBOB` are already on by default and are named
here only to make the intent explicit.

```
ACTOR GlowGun : Pistol
{
  States
  {
  Fire:
    PISG A 4
    PISG B 6 A_FirePistol
    PISG B 0 A_Overlay(1001, "Glow")
    PISG C 4
    PISG B 5 A_ReFire
    Goto Ready

  Glow:
    GLOW A 0 A_OverlayFlags(OverlayID(), PSPF_ADDWEAPON|PSPF_ADDBOB|PSPF_POWDOUBLE|PSPF_ALPHA|PSPF_RENDERSTYLE, true)
    GLOW A 0 A_OverlayRenderStyle(OverlayID(), STYLE_Add)
    GLOW A 0 A_OverlayAlpha(OverlayID(), 0.5)
    GLOW ABC 2 Bright
    Stop
  }
}
```

Layer `1001` puts the glow in front of the flash. `PSPF_ALPHA` and `PSPF_RENDERSTYLE` are what
make the following two calls take effect — without them the glow would silently draw with the
weapon's own opaque style.

A left hand built from the existing right-hand sprite, drawn behind the weapon:

```
  Ready:
    PISG A 1 A_Overlay(-2, "LeftHand", true)
    PISG A 1 A_WeaponReady
    Loop

  LeftHand:
    HAND A 0 A_OverlayFlags(OverlayID(), PSPF_FLIP|PSPF_MIRROR|PSPF_PLAYERTRANSLATED, true)
    HAND A -1
    Stop
```

`PSPF_MIRROR` moves it to the other side of the screen, `PSPF_FLIP` turns the sprite around so
the thumb is on the correct side, and `PSPF_PLAYERTRANSLATED` colours the glove to match the
player.

## See also

- [Psprite layers](Psprite_layers.md) — what a layer carries, and what it inherits by default
- [A_Overlay](A_Overlay.md) — create the layer these flags apply to
- [A_OverlayAlpha](A_OverlayAlpha.md) — needs `PSPF_ALPHA`
- [A_OverlayRenderStyle](A_OverlayRenderStyle.md) — needs `PSPF_RENDERSTYLE`
- [A_OverlayOffset](A_OverlayOffset.md), [A_OverlayScale](A_OverlayScale.md),
  [A_ClearOverlays](A_ClearOverlays.md), [OverlayID](OverlayID.md)
