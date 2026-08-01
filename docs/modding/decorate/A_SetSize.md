**[Actor](https://zdoom.org/wiki/Classes:Actor)**

`bool A_SetSize (float newradius = -1, float newheight = -1, bool testpos = false)`

# A_SetSize

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Ported from GZDoom/UZDoom.
>
> **Works in netgames.** The server decides the resize and broadcasts it, and late joiners
> receive the current size — see [Netplay](#netplay).

## Usage

Changes the actor's physical `Radius` and `Height` at runtime.

Before this, an actor's collision box was fixed at class-definition time: the only way to have a
monster that swells mid-animation, a boss that shrinks as it loses phases, or a crouching enemy
was to spawn a differently-sized replacement actor and transfer its state across.

This changes the **movement** box — what the actor bumps into, what it fits through, and what
can walk into it. To change how large an actor is to *shoot at* without touching its collision,
use [`A_SetHitSize`](A_SetHitSize.md) instead.

The actor is unlinked from the world, resized, and relinked, because both the blockmap links and
the touching-sector list depend on its radius. That happens for you; there is nothing to clean
up afterwards.

### Parameters

- `float newradius`
- `float newheight`

  The new radius and height. **A negative value keeps the current dimension**, which is what the
  `-1` defaults mean — `A_SetSize(-1, 96)` changes only the height.

  Like the `Radius` property, radius is a half-width measured from the actor's centre.

- `bool testpos`

  When `true`, the actor's new size is tested against its current position, and the whole change
  is **reverted** if it no longer fits. Use it for any growth that could happen in a corridor,
  a doorway, or under a low ceiling.

  Default is `false`, which applies the new size unconditionally. An actor that grows into
  geometry that way is stuck: it is inside something solid, and ordinary movement will not free
  it.

## Return value

`true` if the new size was applied, `false` if `testpos` was set and the actor did not fit. With
`testpos` false the result is always `true`.

> **Watch out in inventory state chains.** This result is the one a `CustomInventory` uses to
> decide whether its Pickup or Use succeeded. An `A_SetSize(..., testpos: true)` that fails
> inside a Pickup state will **abort the pickup**, leaving the item on the floor. If you do not
> want that coupling, do the resize somewhere other than a pickup chain.

## Netplay

The server owns the decision and broadcasts the new size to every client, so an actor that grows
is the same size for everyone. Late joiners receive the current sizes of any actor that is not
at its class default as part of their full update.

One exception: a player's **own** pawn applies the change locally as well as receiving it, so
that client-side movement prediction runs against the size the player will actually have rather
than lagging a round-trip behind it.

## Players and crouching

A player's height is re-derived every tic from their standing height times their crouch factor.
Resizing a player therefore updates that standing height too, so the new size survives the next
tic instead of being silently overwritten. Crouching continues to work normally afterwards, now
scaled against the new height.

## Examples

A monster that swells when it enters its enraged state, testing the fit so it cannot wedge
itself into the corridor it was chasing you down. If the growth fails, the jump sends it back to
its ordinary attack instead.

```
ACTOR SwellingBrute : HellKnight
{
    Radius 24
    Height 64
    States
    {
    Pain:
        BOS2 H 2
        BOS2 H 0 A_JumpIf(health < 200, "Enrage")
        BOS2 H 2 A_Pain
        Goto See

    Enrage:
        BOS2 H 4 A_SetSize(40, 96, true)
        BOS2 H 8 A_SetTranslucent(1.0)
        Goto See
    }
}
```

Shrinking on death so the corpse does not block a doorway the player needs to walk through.
Height only, leaving the radius alone:

```
    Death:
        BOS2 M 8 A_SetSize(-1, 16)
        BOS2 NO 8
        BOS2 P -1
        Stop
```

Restoring an actor to its class default is a matter of naming the original numbers — there is no
"reset" sentinel:

```
    BOS2 A 0 A_SetSize(24, 64)
```

## See also

- [A_SetHitSize](A_SetHitSize.md) — change the attack extent instead of the collision box
- [Actor size properties in ACS](../acs/Actor_size_properties.md) — the same change from ACS,
  with `APROP_Radius` and `APROP_Height`
- [Attack hitbox](Attack_hitbox.md) — how the collision box and the attack box differ
