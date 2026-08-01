**[Inventory](https://zdoom.org/wiki/Classes:Inventory)**

`void A_Overlay (int layer, state st = "", bool nooverride = false)`

# A_Overlay

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Behaviour follows GZDoom's DECORATE
> `A_Overlay`.
>
> **Single-player only.** Overlay layers are not replicated — see
> [Psprite layers](Psprite_layers.md#availability).
>
> **Returns nothing.** GZDoom's `A_Overlay` returns a `bool` reporting whether the overlay was
> created, which can be tested in an `if`/`else`. ZandroX's returns no value, so `nooverride`
> can suppress the overlay but cannot tell you that it did.

## Usage

Places a psprite on the given [layer](Psprite_layers.md), starting it at state `st`, creating
the layer if it does not exist yet. This is the function that makes every other one in the
family useful: nothing else creates a layer.

Before this, a weapon had exactly two sprites available to it — itself and its flash — so an
extra hand, a scope, a shell ejecting, or a second gun had to be animated into the weapon's own
sprite sheet, frame by frame, multiplying the art for every combination.

The overlay runs its own state sequence, independently of the weapon's. When that sequence
reaches `Stop`, the layer removes itself.

Calling `A_Overlay` on a layer that already exists **replaces** its state, restarting it at
`st` — unless `nooverride` is set.

Only the player's psprites are affected, so this does nothing when called from an actor with no
player attached.

### Parameters

- `int layer`

  The layer id to place the sprite on. Any integer, though ids `2` and up or `-2` and down are
  recommended to stay clear of the reserved ones; see
  [Psprite layers](Psprite_layers.md#layer-ids) for those and for what the sign and magnitude
  mean for draw order.

  `0` means **the layer whose state is currently executing**, letting an overlay replace its own
  state chain. If nothing is executing on a layer, the call does nothing.

- `state st`

  The state to start the layer at. Defaults to none — and calling `A_Overlay` with **no state
  removes the layer**, which is the explicit counterpart to letting it run to `Stop`.

- `bool nooverride`

  When `true`, do nothing if the layer already exists **and** currently has a state. Use it to
  start an overlay that must not be restarted while it is still playing — a reload animation
  triggered from a `A_WeaponReady` loop, for example. Default is `false`.

  A layer that exists but has already ended its state chain does not count as occupied, so
  `nooverride` will still start it.

## Examples

A pistol that ejects a spent casing on a layer of its own. The casing animation is 6 tics long
and plays out on layer `5`, in front of the weapon (id `1`) but behind the flash (id `1000`),
while the weapon's own states carry straight on to the refire check — the two run at the same
time, which is the whole point.

`Stop` at the end of the `Casing` sequence is what removes layer `5`; without it the layer would
linger on its last frame forever.

```
ACTOR CasingPistol : Pistol
{
  States
  {
  Fire:
    PISG A 4
    PISG B 6 A_FirePistol
    PISG B 0 A_Overlay(5, "Casing")
    PISG C 4
    PISG B 5 A_ReFire
    Goto Ready

  Casing:
    CASE ABC 2
    Stop

  Flash:
    PISF A 7 Bright A_Light1
    Goto LightDone
  }
}
```

A scope overlay that must not restart while it is already showing. Layer `-2` draws *behind* the
weapon, so the scope body sits under the gun. `nooverride` means holding the alt-fire button
down re-enters this state every tic without ever resetting the scope's fade-in.

```
  AltFire:
    PISG A 1 A_Overlay(-2, "Scope", true)
    PISG A 1 A_ReFire
    Goto Ready

  Scope:
    SCOP BCDE 2
    SCOP E -1
    Stop
```

Note the `SCOP E -1`: an infinite duration keeps the layer alive until something removes it —
here, the weapon being switched away, or an explicit
[`A_ClearOverlays`](A_ClearOverlays.md).

## See also

- [Psprite layers](Psprite_layers.md) — the layer model, draw order, and lifetime rules
- [A_ClearOverlays](A_ClearOverlays.md) — remove layers by range
- [OverlayID](OverlayID.md) — the calling layer's id
- [A_OverlayFlags](A_OverlayFlags.md), [A_OverlayOffset](A_OverlayOffset.md),
  [A_OverlayScale](A_OverlayScale.md), [A_OverlayAlpha](A_OverlayAlpha.md),
  [A_OverlayRenderStyle](A_OverlayRenderStyle.md)
