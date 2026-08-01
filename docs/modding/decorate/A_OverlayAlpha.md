**[Inventory](https://zdoom.org/wiki/Classes:Inventory)**

`void A_OverlayAlpha (int layer, float alpha)`

# A_OverlayAlpha

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Behaviour follows GZDoom's DECORATE
> `A_OverlayAlpha`.
>
> **Single-player only.** Overlay layers are not replicated — see
> [Psprite layers](Psprite_layers.md#availability).

## Usage

Sets a [layer](Psprite_layers.md)'s translucency, from `0.0` (invisible) to `1.0` (opaque).

**The layer must have `PSPF_ALPHA` set for this to have any effect.** By default a layer is
drawn with the weapon's alpha, and a value stored here is simply never read. Set the flag with
[`A_OverlayFlags`](A_OverlayFlags.md) first — this is the single most common reason an
`A_OverlayAlpha` call appears to do nothing.

Whether the alpha is actually *visible* also depends on the layer's render style: an opaque
style such as `STYLE_Normal` ignores alpha entirely. Pair this with
[`A_OverlayRenderStyle`](A_OverlayRenderStyle.md) and a translucent style
(`STYLE_Translucent`, `STYLE_Add`, …) when you want a fade.

Alpha persists on the layer until changed; it is not reset by a state change.

### Parameters

- `int layer`

  The layer to change. `0` means the layer whose state is currently executing — see
  [OverlayID](OverlayID.md). Does nothing if the layer does not exist.

- `float alpha`

  The alpha value. **Clamped to the range `0.0`–`1.0`**; values outside it are silently brought
  into range rather than rejected, so a fade loop that overshoots is harmless.

## Examples

A hit-marker overlay that fades out over eight tics. `PSPF_ALPHA` and `PSPF_RENDERSTYLE` are set
on the first frame, and the style is switched to `STYLE_Translucent` so alpha means something.

```
ACTOR MarkerGun : Pistol
{
  States
  {
  Fire:
    PISG A 4
    PISG B 6 A_FirePistol
    PISG B 0 A_Overlay(1001, "HitMarker")
    PISG C 4
    PISG B 5 A_ReFire
    Goto Ready

  HitMarker:
    MARK A 0 A_OverlayFlags(OverlayID(), PSPF_ALPHA|PSPF_RENDERSTYLE, true)
    MARK A 0 A_OverlayRenderStyle(OverlayID(), STYLE_Translucent)
    MARK A 1 A_OverlayAlpha(OverlayID(), 1.0)
    MARK A 1 A_OverlayAlpha(OverlayID(), 0.75)
    MARK A 1 A_OverlayAlpha(OverlayID(), 0.5)
    MARK A 1 A_OverlayAlpha(OverlayID(), 0.25)
    Stop
  }
}
```

Dropping either of the first two lines gives a marker that appears at full opacity and vanishes
after four tics — the alpha calls still run, they are just not consulted.

## See also

- [A_OverlayFlags](A_OverlayFlags.md) — `PSPF_ALPHA`, which this function needs
- [A_OverlayRenderStyle](A_OverlayRenderStyle.md) — the style that decides whether alpha is used
- [Psprite layers](Psprite_layers.md) — the layer model
- [A_Overlay](A_Overlay.md), [A_ClearOverlays](A_ClearOverlays.md),
  [A_OverlayOffset](A_OverlayOffset.md), [A_OverlayScale](A_OverlayScale.md),
  [OverlayID](OverlayID.md)
