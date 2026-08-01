**DECORATE expression function**

`int OverlayID ()`

# OverlayID

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Behaviour follows GZDoom's DECORATE
> `OverlayID`.
>
> **Single-player only** in practice, since the [layers](Psprite_layers.md) it identifies are
> not replicated — see [Psprite layers](Psprite_layers.md#availability).

## Usage

Returns the id of the psprite [layer](Psprite_layers.md) whose state is currently executing.

This is what lets an overlay's states refer to **themselves** without hardcoding their own layer
number. An overlay sequence written with `OverlayID()` keeps working when the layer it is placed
on changes, and the same sequence can be reused on several layers at once — which is impossible
with a literal id, since every copy would fight over the same layer.

Unlike the rest of the family this is an **expression function**, not an action function: it
returns a value and is used inside an argument or a condition, never called on its own as a
state action.

Every function in the `A_Overlay` family also accepts a `layer` argument of `0` to mean the same
thing, so `A_OverlayAlpha(0, 0.5)` and `A_OverlayAlpha(OverlayID(), 0.5)` are equivalent. Prefer
`OverlayID()` where it fits — `0` reads as a layer number and invites the reader to go looking
for layer zero.

## Return value

The current layer's id, or **`0` when nothing is executing on a layer** — which is what happens
when the expression is evaluated from an actor state rather than a psprite state. `0` is not a
valid layer, so passing the result straight into an `A_Overlay` function in that situation is
harmless: the call does nothing.

Inside the weapon's own states it returns `1` (`PSP_WEAPON`); inside a flash state, `1000`
(`PSP_FLASH`).

## Examples

One overlay sequence reused on two layers at once. Both copies run the same states, and
`OverlayID()` resolves to `10` in one and `11` in the other, so each configures and fades only
itself.

```
ACTOR TwinGlowGun : Pistol
{
  States
  {
  Fire:
    PISG A 4
    PISG B 6 A_FirePistol
    PISG B 0 A_Overlay(10, "Glow")
    PISG B 0 A_Overlay(11, "Glow")
    PISG C 4
    PISG B 5 A_ReFire
    Goto Ready

  Glow:
    GLOW A 0 A_OverlayFlags(OverlayID(), PSPF_ALPHA|PSPF_RENDERSTYLE, true)
    GLOW A 0 A_OverlayRenderStyle(OverlayID(), STYLE_Add)
    GLOW A 1 A_OverlayAlpha(OverlayID(), 0.6)
    GLOW A 1 A_OverlayAlpha(OverlayID(), 0.3)
    Stop
  }
}
```

Only their offsets need to differ, and that can be handled by the caller before or after the
sequence starts.

Used in a condition, to let a sequence behave differently depending on where it was placed —
here, an overlay that only draws its backdrop when it is behind the weapon:

```
  Glow:
    GLOW A 0 A_JumpIf(OverlayID() < PSP_WEAPON, "Backdrop")
    GLOW A 1
    Stop
  Backdrop:
    BACK A 1
    Stop
```

An overlay ending its own state chain, which removes the layer:

```
    GLOW A 1 A_Overlay(OverlayID())
```

## See also

- [Psprite layers](Psprite_layers.md) — layer ids and what `0` means as an argument
- [A_Overlay](A_Overlay.md) — create the layer this identifies
- [A_OverlayFlags](A_OverlayFlags.md), [A_OverlayOffset](A_OverlayOffset.md),
  [A_OverlayScale](A_OverlayScale.md), [A_OverlayAlpha](A_OverlayAlpha.md),
  [A_OverlayRenderStyle](A_OverlayRenderStyle.md), [A_ClearOverlays](A_ClearOverlays.md)
