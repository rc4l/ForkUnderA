**[Inventory](https://zdoom.org/wiki/Classes:Inventory)**

`void A_OverlayOffset (int layer = 1, float wx = 0, float wy = 32, int flags = 0)`

# A_OverlayOffset

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Behaviour follows GZDoom's DECORATE
> `A_OverlayOffset`.
>
> **Single-player only.** Overlay layers are not replicated — see
> [Psprite layers](Psprite_layers.md#availability).

## Usage

Moves a [layer](Psprite_layers.md) to, or by, the given offset.

The offset is in the same coordinate space as the `offset` state keyword every weapon already
uses: `(0, 32)` is the resting position of a weapon on screen, `wx` grows to the right and `wy`
grows **downward**. That is why `wy` defaults to `32` rather than `0` — the parameter defaults
are the neutral weapon position, not the top-left corner.

A newly created overlay starts at `(0, 0)`, and because `PSPF_ADDWEAPON` is on by default that
offset is **relative to the weapon**: `(0, 0)` puts the overlay exactly on the gun, and positive
`wy` pushes it below the gun. Clear `PSPF_ADDWEAPON` (see [`A_OverlayFlags`](A_OverlayFlags.md))
if you want the offset to be an absolute screen position instead, in which case `(0, 32)` is the
neutral spot.

It differs from the `offset` state keyword in three ways that matter:

- It can move a layer **without changing its frame**, so an overlay slides continuously while
  holding one sprite.
- Its offsets are **floats**, not integers, and are not clamped to the screen — a layer can be
  moved right off the edge of the view and back.
- It is an action function, so its arguments can be **DECORATE expressions** rather than
  literals.

By default the move **snaps**: the layer jumps to the new offset for that tic. `WOF_INTERPOLATE`
smooths it instead, and `WOF_ADD` (which is inherently continuous) also interpolates.

An offset set here is the layer's *own* offset. Whether the weapon's offset and bob are added on
top is controlled by `PSPF_ADDWEAPON` and `PSPF_ADDBOB` — see
[`A_OverlayFlags`](A_OverlayFlags.md).

### Parameters

- `int layer`

  The layer to move. Defaults to `1` (`PSP_WEAPON`, the weapon itself). `0` means the layer
  whose state is currently executing — see [OverlayID](OverlayID.md). Does nothing if the layer
  does not exist.

- `float wx`
- `float wy`

  The horizontal and vertical offset. Interpreted as an absolute position by default, or as an
  amount to add to the current one with `WOF_ADD`. `wy` is measured downward from the top of the
  view, so a **larger** `wy` sits **lower** on screen.

- `int flags`

  Multiple flags can be combined with `|`. The following are available:

  - **WOF_KEEPX** — leave the horizontal offset untouched; `wx` is ignored. Lets you drive one
    axis without having to know the other's current value.

  - **WOF_KEEPY** — the same for the vertical offset; `wy` is ignored.

  - **WOF_ADD** — add `wx`/`wy` to the layer's current offset instead of replacing it. Also
    turns on interpolation for the move, since an incremental move is nearly always animation.

  - **WOF_INTERPOLATE** — smooth this move between tics instead of snapping to it. Only needed
    for an absolute move; `WOF_ADD` already interpolates.

  Setting both `WOF_KEEPX` and `WOF_KEEPY` makes the call a no-op apart from its effect on
  interpolation.

## Examples

A scope overlay that slides up into view over four tics and stays there. Each state adds `-8` to
the vertical offset, so the scope rises 32 units in total; `WOF_ADD` makes the motion smooth at
high frame rates without any extra flag.

```
ACTOR ScopeRifle : Pistol
{
  States
  {
  AltFire:
    PISG A 1 A_Overlay(20, "ScopeIn", true)
    PISG A 1 A_ReFire
    Goto Ready

  ScopeIn:
    SCOP A 0 A_OverlayOffset(OverlayID(), 0, 64)
    SCOP A 1 A_OverlayOffset(OverlayID(), 0, -8, WOF_ADD)
    SCOP A 1 A_OverlayOffset(OverlayID(), 0, -8, WOF_ADD)
    SCOP A 1 A_OverlayOffset(OverlayID(), 0, -8, WOF_ADD)
    SCOP A 1 A_OverlayOffset(OverlayID(), 0, -8, WOF_ADD)
    SCOP A -1
    Stop
  }
}
```

The first, `0`-tic call places the scope 64 units below the gun so there is somewhere to rise
from — the overlay's offset is relative to the weapon, since `PSPF_ADDWEAPON` is on by default.
Note it does not use `WOF_ADD`, so it snaps; correct here, because the layer has only just been
created and there is no previous position worth interpolating from.

A sway that only affects the horizontal axis, leaving whatever vertical position the rest of the
weapon's code has established:

```
    SCOP A 1 A_OverlayOffset(OverlayID(), 2, 0, WOF_ADD|WOF_KEEPY)
    SCOP A 1 A_OverlayOffset(OverlayID(), -2, 0, WOF_ADD|WOF_KEEPY)
```

## See also

- [Psprite layers](Psprite_layers.md) — the layer model
- [A_OverlayScale](A_OverlayScale.md) — the same `WOF_` flags, applied to scale
- [A_OverlayFlags](A_OverlayFlags.md) — `PSPF_ADDWEAPON`, `PSPF_ADDBOB`, `PSPF_INTERPOLATE`
- [A_Overlay](A_Overlay.md), [A_ClearOverlays](A_ClearOverlays.md),
  [A_OverlayAlpha](A_OverlayAlpha.md), [A_OverlayRenderStyle](A_OverlayRenderStyle.md),
  [OverlayID](OverlayID.md)
