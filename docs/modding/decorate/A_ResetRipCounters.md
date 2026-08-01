**[Actor](https://zdoom.org/wiki/Classes:Actor)**

`void A_ResetRipCounters ()`

# A_ResetRipCounters

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2.

## Usage

Refills this projectile's [rip budgets](Ripping.md#the-three-budgets): clears the damage and hit
totals, and forgets every victim it has already ripped.

That third part is the one that matters most. Forgetting the victim ledger means
[`RipperCount`](RipperCount.md) starts over **on every enemy**, so a projectile that had already
used up its allowance against something can bite into it again. It also resets
[`RipperDamageFactor`](RipperDamageFactor.md)'s falloff, since falloff is counted from the same
per-victim hit tally — a decayed ripper goes back to full damage.

Concretely, it clears:

| | |
|---|---|
| `RipperDamageDone` | back to `0`, refilling [`RipperMaxDamage`](RipperMaxDamage.md) |
| `RipperHitsDone` | back to `0`, refilling [`RipperMaxCount`](RipperMaxCount.md) |
| the per-victim ledger | emptied, refilling [`RipperCount`](RipperCount.md) against everyone |

This lets a ripper "recharge" from its own state machine rather than needing ACS — a projectile
that regains its bite on a timer, on picking up speed, or when it enters a new area.

Takes no parameters, and does nothing meaningful on an actor that is not a ripper.

## Examples

A projectile that recharges every second of flight, so it keeps piercing for as long as it
survives but is still limited moment to moment:

```
ACTOR RechargingLance
{
    Radius 8
    Height 8
    Speed 35
    Damage 12
    Projectile
    +RIPPER
    RipperCount 3
    RipperDamageFactor 0.5
    States
    {
    Spawn:
        LANC A 35 Bright
        LANC A 0 Bright A_ResetRipCounters
        Loop
    Death:
        LANC BC 4 Bright
        Stop
    }
}
```

Recharging from the [`Rip:`](Rip.md) state instead, conditionally — here the projectile gets a
second wind once, on reaching a damage threshold. Note that `RipperDamageDone` only advances on
the server, so in a netgame a client's copy of this projectile never takes the branch and its
visuals will differ; use `RipperHitsDone` instead if that matters, since it is booked on both
sides:

```
    Rip:
        LANC B 1 A_JumpIf(RipperDamageDone > 200, "SecondWind")
        Goto Spawn
    SecondWind:
        LANC D 2 Bright A_ResetRipCounters
        Goto Spawn
```

Note that a recharge loop with no other limit makes a ripper effectively unbounded — pair it
with a lifetime on the projectile, or with a condition that can only fire so often.

## See also

- [Ripping](Ripping.md) — the budget model and the readable counters
- [RipperCount](RipperCount.md), [RipperMaxCount](RipperMaxCount.md),
  [RipperMaxDamage](RipperMaxDamage.md) — the budgets this refills
- [RipperDamageFactor](RipperDamageFactor.md) — falloff, which this also resets
- [Rip](Rip.md) — a natural place to call this from
