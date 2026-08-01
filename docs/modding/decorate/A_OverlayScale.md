**[Inventory](https://zdoom.org/wiki/Classes:Inventory)**

`void A_OverlayScale (int layer, float scalex = 1, float scaley = 0, int flags = 0)`

# A_OverlayScale

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Behaviour follows GZDoom's DECORATE
> `A_OverlayScale`.
>
> **Single-player only.** Overlay layers are not replicated — see
> [Psprite layers](Psprite_layers.md#availability).

## Usage

Resizes a [layer](Psprite_layers.md). `1.0` is the sprite's authored size; `2.0` is double,
`0.5` is half. Scaling is applied about the sprite's own origin, and it scales the layer's
offset along with its pixels, so a scaled overlay stays anchored where it was rather than
drifting.

Negative scales are accepted and flip the sprite along that axis. For a horizontal flip that
does not also invert the offset, prefer `PSPF_FLIP` — see
[`A_OverlayFlags`](A_OverlayFlags.md).

Scale persists on the layer until changed; it is not reset by a state change.

### Parameters

- `int layer`

  The layer to resize. `0` means the layer whose state is currently executing — see
  [OverlayID](OverlayID.md). Does nothing if the layer does not exist.

- `float scalex`

  Horizontal scale factor. Default `1`.

- `float scaley`

  Vertical scale factor. **A value of `0` means "square": copy `scalex`.** This is a
  convenience, not a degenerate case — `A_OverlayScale(5, 2.0)` doubles the layer in both
  directions, which is what you almost always want. There is consequently no way to scale a
  layer to zero height; use [`A_OverlayAlpha`](A_OverlayAlpha.md) or remove the layer to hide
  it.

  The substitution happens before `WOF_ADD` is applied, so
  `A_OverlayScale(5, 0.1, 0, WOF_ADD)` adds `0.1` to **both** axes.

- `int flags`

  Multiple flags can be combined with `|`. The same `WOF_` flags as
  [`A_OverlayOffset`](A_OverlayOffset.md), applied to the scale axes:

  - **WOF_KEEPX** — leave the horizontal scale untouched; `scalex` is ignored.

  - **WOF_KEEPY** — leave the vertical scale untouched; `scaley` is ignored.

  - **WOF_ADD** — add to the current scale instead of replacing it.

  - **WOF_INTERPOLATE** — accepted, but scale is not interpolated between tics; only the offset
    is. Passing it here affects the layer's *offset* interpolation state, so avoid it on scale
    calls unless that is what you want.

  Note that `WOF_KEEPY` is applied **after** the square-scale substitution, so
  `A_OverlayScale(5, 2.0, 0, WOF_KEEPY)` sets the horizontal scale to `2.0` and leaves the
  vertical one alone, rather than making it square.

## Examples

A muzzle flash that punches in — starting at 150% and shrinking back to normal over three tics —
on its own layer so the weapon sprite underneath is untouched.

```
ACTOR PunchFlashGun : Pistol
{
  States
  {
  Fire:
    PISG A 4
    PISG B 6 A_FirePistol
    PISG B 0 A_Overlay(1001, "BigFlash")
    PISG C 4
    PISG B 5 A_ReFire
    Goto Ready

  BigFlash:
    GLOW A 1 Bright A_OverlayScale(OverlayID(), 1.5)
    GLOW A 1 Bright A_OverlayScale(OverlayID(), 1.25)
    GLOW A 1 Bright A_OverlayScale(OverlayID(), 1.0)
    Stop
  }
}
```

Each call passes `scaley` as its default `0`, so all three are square scales — the flash shrinks
evenly rather than squashing.

A layer that stretches horizontally only, growing 10% per tic while keeping its height:

```
    GLOW A 1 A_OverlayScale(OverlayID(), 0.1, 0, WOF_ADD|WOF_KEEPY)
```

## See also

- [Psprite layers](Psprite_layers.md) — the layer model
- [A_OverlayOffset](A_OverlayOffset.md) — the same `WOF_` flags, applied to position
- [A_OverlayFlags](A_OverlayFlags.md) — `PSPF_FLIP` and `PSPF_MIRROR` for mirroring
- [A_Overlay](A_Overlay.md), [A_ClearOverlays](A_ClearOverlays.md),
  [A_OverlayAlpha](A_OverlayAlpha.md), [A_OverlayRenderStyle](A_OverlayRenderStyle.md),
  [OverlayID](OverlayID.md)
