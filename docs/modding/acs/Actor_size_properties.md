# Actor size properties in ACS

Four [`SetActorProperty`](https://zdoom.org/wiki/SetActorProperty) /
[`GetActorProperty`](https://zdoom.org/wiki/GetActorProperty) /
[`CheckActorProperty`](https://zdoom.org/wiki/CheckActorProperty) properties that change an
actor's size at runtime — its physical collision box, and its
[attack hitbox](../decorate/Attack_hitbox.md).

| Property | Id | Meaning |
|---|---|---|
| `APROP_Height` | 35 | Physical height (collision). |
| `APROP_Radius` | 36 | Physical radius (collision), as a half-width. |
| `APROP_HitRadius` | 50 | Attack-hitbox radius. `0` = fall back to `APROP_Radius`. |
| `APROP_HitHeight` | 51 | Attack-hitbox height. `0` = fall back to `APROP_Height`. |

## Availability

> **ZandroX.** `APROP_Height` and `APROP_Radius` were already **readable** in Zandronum;
> **writing** them is new here. `APROP_HitRadius` and `APROP_HitHeight` are entirely new.
>
> `APROP_Height` and `APROP_Radius` keep the ids UZDoom/GZDoom use. Ids **42–49 are deliberately
> left unused** to match upstream's properties that ZandroX does not implement yet, so ACS
> written against UZDoom's constants stays valid here.

## Values are fixed-point

**All four take and return fixed-point map units, not integers** — the same convention as
`APROP_ScaleX` and `APROP_MeleeRange`. Write `32.0`, not `32`:

```c
SetActorProperty(tid, APROP_Radius, 32.0);   // correct — 32 map units
SetActorProperty(tid, APROP_Radius, 32);     // wrong — a hair over 0.0004 units
```

The integer form does not error. It sets a radius so small the actor is effectively a point,
which is a confusing bug to chase.

`GetActorProperty` returns the same fixed-point form, so it round-trips.

## The two fork-specific constants need defining

`APROP_HitRadius` and `APROP_HitHeight` are ZandroX additions and are **not** in stock `acc`.
Either use the numeric id, or define them yourself:

```c
#define APROP_HitRadius  50
#define APROP_HitHeight  51
```

## Behaviour

**Setting a size relinks the actor.** The blockmap links and touching-sector list depend on the
radius, so both are rebuilt for you. For a player, the standing height used by the per-tic
crouch calculation is updated too, so the new height is not overwritten on the next tic.

**Setting the same value is a no-op.** Each property checks whether the value actually differs
before doing any work, so polling a script that writes an unchanged size costs nothing.

**There is no position test.** This is the one real difference from
[`A_SetSize`](../decorate/A_SetSize.md), which offers a `testpos` argument that reverts a resize
the actor no longer fits into. ACS has no equivalent: enlarging an actor that is standing in a
doorway will leave it **stuck inside the geometry**, and ordinary movement will not free it.
Check the space yourself before growing something, or do the resize from DECORATE where
`testpos` is available.

**Physical size is replicated; the attack hitbox is not.** `APROP_Radius` and `APROP_Height`
changes are broadcast to clients, and late joiners get the current values. `APROP_HitRadius` and
`APROP_HitHeight` are server-authoritative and deliberately not sent, so clients keep the class
default — damage stays correct because the server decides hits, but anything a client draws from
the attack box (notably crosshair target identification) will disagree. See
[`A_SetHitSize`](../decorate/A_SetHitSize.md#netplay) for the full picture.

## Examples

A script that inflates a boss into a larger target as its health drops, without touching its
collision box, so the arena still plays the same:

```c
#define APROP_HitRadius  50
#define APROP_HitHeight  51

script "BossPhase" (int tid, int phase)
{
    if (phase >= 2)
    {
        SetActorProperty(tid, APROP_HitRadius, 72.0);
        SetActorProperty(tid, APROP_HitHeight, 96.0);
    }
    else
    {
        // 0 clears the override, so the hitbox tracks the physical box again.
        SetActorProperty(tid, APROP_HitRadius, 0);
        SetActorProperty(tid, APROP_HitHeight, 0);
    }
}
```

Growing an actor's real collision box, checking the room first because ACS will not check it for
you. `CheckActorProperty` reads back the value that was actually applied:

```c
script "Embiggen" (int tid)
{
    SetActorProperty(tid, APROP_Radius, 48.0);
    SetActorProperty(tid, APROP_Height, 96.0);

    if (!CheckActorProperty(tid, APROP_Radius, 48.0))
        Log(s:"resize did not take");
}
```

Reading a size back and halving it — note the fixed-point arithmetic:

```c
    int r = GetActorProperty(tid, APROP_Radius);
    SetActorProperty(tid, APROP_Radius, r / 2);
```

## See also

- [A_SetSize](../decorate/A_SetSize.md) — the DECORATE equivalent, with a fit test
- [A_SetHitSize](../decorate/A_SetHitSize.md) — the DECORATE attack-hitbox setter
- [HitRadius](../decorate/HitRadius.md), [HitHeight](../decorate/HitHeight.md) — the
  class-default properties
- [Attack hitbox](../decorate/Attack_hitbox.md) — which attack paths honour the hitbox
