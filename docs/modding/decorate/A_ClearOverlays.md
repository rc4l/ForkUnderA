**[Inventory](https://zdoom.org/wiki/Classes:Inventory)**

`void A_ClearOverlays (int start = 0, int stop = 0, bool safety = true)`

# A_ClearOverlays

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Behaviour follows GZDoom's DECORATE
> `A_ClearOverlays`.
>
> **Single-player only.** Overlay layers are not replicated — see
> [Psprite layers](Psprite_layers.md#availability).
>
> **Returns nothing.** GZDoom's `A_ClearOverlays` returns the number of active layers it
> cleared. ZandroX's returns no value.

## Usage

Removes every [layer](Psprite_layers.md) whose id falls in the inclusive range
`[start, stop]`. With both arguments left at `0`, removes **all** of them.

An overlay normally cleans itself up by running its state chain to `Stop`. `A_ClearOverlays` is
for the cases where it cannot: tearing down several layers at once, or cancelling an overlay
that is sitting on an infinite-duration frame waiting to be told to stop.

You rarely need this on weapon switch or death — the engine already clears every overlay when
the weapon is lowered, dropped, or the player dies, so overlays cannot outlive their weapon.

The range is over layer **ids**, not positions, so `A_ClearOverlays(1, 999)` means "every layer
between the weapon and the flash" regardless of how many exist.

Removal is immediate. It is safe to call on the layer currently executing — including from that
layer's own state — because the engine re-checks the layer after the action returns and stops
processing it if it is gone.

### Parameters

- `int start`
- `int stop`

  The inclusive range of layer ids to remove. When **both** are `0`, every layer is removed
  instead — this is a special case, not a range, so `A_ClearOverlays()` is the "clear
  everything" form.

  Note that a range of `0, 0` therefore cannot mean "just layer 0"; layer `0` is not a real
  layer anyway.

- `bool safety`

  When `true` (the default), the five **reserved** layers are never removed no matter what the
  range covers — the weapon, its flash, and the three targeters survive.

  Setting it to `false` lifts that protection. **Use extreme caution.** Wiping the weapon layer
  leaves no active layer for the weapon to switch away with, so the player can be left holding
  nothing and unable to change weapons out of it. Only do it if that is genuinely what you want.

## Examples

A weapon that builds a three-part HUD out of overlays while it is ready, and tears the whole set
down in one call when it starts firing. The overlays sit at ids `10`, `11` and `12`, so the
range `10, 12` catches exactly them and nothing else.

```
ACTOR OverlayHUDGun : Pistol
{
  States
  {
  Ready:
    PISG A 1 A_Overlay(10, "PanelLeft",  true)
    PISG A 0 A_Overlay(11, "PanelMid",   true)
    PISG A 0 A_Overlay(12, "PanelRight", true)
    PISG A 1 A_WeaponReady
    Loop

  Fire:
    PISG A 0 A_ClearOverlays(10, 12)
    PISG B 6 A_FirePistol
    PISG C 4
    PISG B 5 A_ReFire
    Goto Ready

  PanelLeft:
    HUDL A -1
    Stop
  PanelMid:
    HUDM A -1
    Stop
  PanelRight:
    HUDR A -1
    Stop
  }
}
```

Each panel parks on `-1` (infinite duration) and so would never remove itself; `A_ClearOverlays`
is what ends them. The `nooverride` argument on each `A_Overlay` keeps the `Ready` loop from
restarting them every tic.

To drop everything the weapon has put up without listing ranges:

```
    PISG A 0 A_ClearOverlays
```

## See also

- [Psprite layers](Psprite_layers.md) — layer ids, and when the engine clears layers for you
- [A_Overlay](A_Overlay.md) — create a layer, or remove one by calling it with no state
- [A_OverlayFlags](A_OverlayFlags.md), [A_OverlayOffset](A_OverlayOffset.md),
  [A_OverlayScale](A_OverlayScale.md), [A_OverlayAlpha](A_OverlayAlpha.md),
  [A_OverlayRenderStyle](A_OverlayRenderStyle.md), [OverlayID](OverlayID.md)
