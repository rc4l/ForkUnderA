**[Actor](https://zdoom.org/wiki/Classes:Actor)**

`void A_SetHitSize (float hitradius = -1, float hitheight = -1)`

# A_SetHitSize

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2 or GZDoom.
>
> **Server-authoritative and not replicated.** Clients keep the class default — read
> [Netplay](#netplay) before using this in multiplayer content.

## Usage

Changes the actor's [attack hitbox](Attack_hitbox.md) — its
[`HitRadius`](HitRadius.md) and [`HitHeight`](HitHeight.md) — at runtime.

This is the counterpart to [`A_SetSize`](A_SetSize.md), and the difference is the whole point:
`A_SetSize` changes what the actor collides with, this changes only what can hit it. A boss can
become a bigger target while it is vulnerable and a smaller one while it is shielded, without
ever changing what it bumps into or fits through.

Because the attack extent is not the movement radius, **widening it can never get the actor
stuck**, and there is consequently no position test and no failure case. The actor is relinked
to the world so attack sweeps can reach the new extent, but movement is untouched.

### Parameters

- `float hitradius`
- `float hitheight`

  The new attack extents. Three input ranges behave differently, and the distinction matters:

  | Value | Effect |
  |---|---|
  | Negative | **Keep** the current value. This is what the `-1` defaults mean. |
  | `0` | **Clear** the override, so the extent falls back to the physical `Radius` / `Height`. |
  | Positive | Use this as the attack extent. |

  So `A_SetHitSize(0, -1)` clears the horizontal override and leaves the vertical one alone,
  and `A_SetHitSize(0, 0)` returns the actor to being hit exactly at its collision box.

  Radius is a half-width from the actor's centre, matching `Radius`.

  See [Attack hitbox](Attack_hitbox.md) for what a widened or narrowed box actually changes —
  in particular the line-of-sight guard that applies once the box exceeds the physical one.

## Netplay

**This is the sharp edge of the function.** The attack extent is decided by the server and is
deliberately *not* broadcast, matching the `HitRadius` / `HitHeight` properties it sets. Damage
is therefore correct for everyone, because the server is what decides whether an attack lands.

But a client's copy of the actor keeps the **class default** extent. Anything a client draws or
computes from the attack box will disagree with the server after a runtime change — most
visibly the HUD crosshair's target identification, which is client-side. A player can be aiming
at a widened boss, see no target highlight, and still hit it.

If you need clients to agree, do not change the extent at runtime; author it with the
[`HitRadius`](HitRadius.md) / [`HitHeight`](HitHeight.md) properties, which are class defaults
and identical on both sides.

There is one exception, for debugging only: while `sv_debugexplosions` and `sv_cheats` are both
enabled, the server does replicate attack-extent changes so the
[hitbox overlay](https://github.com/rc4l/ZandroX/pull/108) can draw a truthful box on a client.
Ordinary play never sends them.

## Examples

A boss that is a large target while attacking and a small one while shielded, so players are
rewarded for hitting it during its windows. Its collision box never changes, so its movement
through the arena is identical throughout.

```
ACTOR PhaseBoss : BaronOfHell
{
    Radius 24
    Height 64
    HitRadius 24      // starts matching its physical size
    States
    {
    Missile:
        BOSS EF 6 A_FaceTarget
        BOSS F 0 A_SetHitSize(72, 96)      // wide open while it attacks
        BOSS G 12 A_BruisAttack
        BOSS F 0 A_SetHitSize(0, 0)        // back to the collision box
        Goto See

    Pain:
        BOSS H 2
        BOSS H 2 A_Pain
        Goto See
    }
}
```

Note `A_SetHitSize(0, 0)` rather than `A_SetHitSize(24, 64)`: clearing the override is not the
same as setting it to the current physical size. Clearing means the extent keeps tracking
`Radius`/`Height` afterwards — including a later [`A_SetSize`](A_SetSize.md) — where a hardcoded
`24, 64` would stay put and quietly stop matching.

Narrowing only the vertical extent, so shots pass over a crouching enemy while its horizontal
extent stays as authored:

```
    BOSS A 0 A_SetHitSize(-1, 24)
```

## See also

- [Attack hitbox](Attack_hitbox.md) — what the attack box is and which attack paths honour it
- [HitRadius](HitRadius.md), [HitHeight](HitHeight.md) — the class-default properties this
  overrides at runtime
- [A_SetSize](A_SetSize.md) — change the physical collision box instead
- [Actor size properties in ACS](../acs/Actor_size_properties.md) — `APROP_HitRadius` and
  `APROP_HitHeight`
