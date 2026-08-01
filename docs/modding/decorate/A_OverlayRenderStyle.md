**[Inventory](https://zdoom.org/wiki/Classes:Inventory)**

`void A_OverlayRenderStyle (int layer, int style)`

# A_OverlayRenderStyle

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Behaviour follows GZDoom's DECORATE
> `A_OverlayRenderStyle`.
>
> **Single-player only.** Overlay layers are not replicated — see
> [Psprite layers](Psprite_layers.md#availability).

## Usage

Sets the render style a [layer](Psprite_layers.md) is drawn with, independently of the weapon's.

**The layer must have `PSPF_RENDERSTYLE` set for this to have any effect** — see
[`A_OverlayFlags`](A_OverlayFlags.md). By default a layer inherits the weapon's style. Unlike
[`A_OverlayAlpha`](A_OverlayAlpha.md), the relationship runs both ways: the flag is also
inert until this function has been called at least once on the layer, because a layer with no
style recorded falls back to the weapon's.

Styles that blend (`STYLE_Translucent`, `STYLE_Add`, `STYLE_Subtract`, …) use the layer's alpha,
so pair this with `PSPF_ALPHA` and `A_OverlayAlpha` to control how strong the effect is.

The style persists on the layer until changed; it is not reset by a state change.

### Parameters

- `int layer`

  The layer to change. `0` means the layer whose state is currently executing — see
  [OverlayID](OverlayID.md). Does nothing if the layer does not exist.

- `int style`

  One of the following constants. **An out-of-range value is ignored** and the layer keeps its
  previous style, rather than erroring or falling back to a default.

  | Constant | Value | Effect |
  |---|---|---|
  | `STYLE_None` | 0 | Not drawn at all. |
  | `STYLE_Normal` | 1 | Fully opaque. Ignores alpha. |
  | `STYLE_Fuzzy` | 2 | The Spectre "fuzz" effect. |
  | `STYLE_SoulTrans` | 3 | Translucent at the `transsouls` CVar's level. |
  | `STYLE_OptFuzzy` | 4 | Fuzz or translucency, per the user's fuzz preference. |
  | `STYLE_Stencil` | 5 | Drawn as a solid silhouette in the fill colour. |
  | `STYLE_Translucent` | 6 | Blended by the layer's alpha. The usual choice for a fade. |
  | `STYLE_Add` | 7 | Additive. The usual choice for a glow or muzzle flash. |
  | `STYLE_Shaded` | 8 | Uses the sprite's red channel as an alpha mask. |
  | `STYLE_TranslucentStencil` | 9 | Stencil, blended by alpha. |
  | `STYLE_Shadow` | 10 | Flat dark silhouette at fixed low opacity; ignores the layer's alpha. |
  | `STYLE_Subtract` | 11 | Subtractive. |

  > **Careful:** these are the **DECORATE** `STYLE_` values. Above `STYLE_Stencil` they diverge
  > from the numerically different `APROP_RenderStyle` values used by ACS. A mod that defines
  > its own `STYLE_Translucent` as the ACS value `64` will get the engine's `6` here and render
  > differently. ZandroX reports a duplicate global constant as a console warning naming both
  > values instead of refusing to load the mod
  > ([#100](https://github.com/rc4l/ZandroX/pull/100)), so this shows up in the console rather
  > than silently.

## Examples

An additive heat-haze layer over the weapon, fading in as the weapon is fired repeatedly. The
first two calls are the required setup: the flag, then the style.

```
ACTOR HeatGun : Pistol
{
  States
  {
  Fire:
    PISG A 4
    PISG B 6 A_FirePistol
    PISG B 0 A_Overlay(2, "Heat", true)
    PISG C 4
    PISG B 5 A_ReFire
    Goto Ready

  Heat:
    HEAT A 0 A_OverlayFlags(OverlayID(), PSPF_RENDERSTYLE|PSPF_ALPHA|PSPF_ADDWEAPON|PSPF_ADDBOB, true)
    HEAT A 0 A_OverlayRenderStyle(OverlayID(), STYLE_Add)
    HEAT A 0 A_OverlayAlpha(OverlayID(), 0.3)
    HEAT AB 3
    Goto Heat+3
  }
}
```

Layer `2` sits just in front of the weapon and behind the flash, and `PSPF_ADDWEAPON|PSPF_ADDBOB`
keeps the haze locked to the gun as it moves.

To make a layer disappear without removing it — keeping its state chain running so it can come
back later:

```
    HEAT A 0 A_OverlayRenderStyle(OverlayID(), STYLE_None)
```

## See also

- [A_OverlayFlags](A_OverlayFlags.md) — `PSPF_RENDERSTYLE`, which this function needs
- [A_OverlayAlpha](A_OverlayAlpha.md) — the alpha the blending styles use
- [Psprite layers](Psprite_layers.md) — the layer model
- [A_Overlay](A_Overlay.md), [A_ClearOverlays](A_ClearOverlays.md),
  [A_OverlayOffset](A_OverlayOffset.md), [A_OverlayScale](A_OverlayScale.md),
  [OverlayID](OverlayID.md)
