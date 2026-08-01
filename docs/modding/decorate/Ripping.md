# Ripping

A **ripper** is a projectile that tears through what it hits instead of exploding on it —
`+RIPPER` on the projectile, `+DONTRIP` on anything that should be immune.

In stock Zandronum that is the entire system. Ripping is binary: a projectile either rips
forever or does not rip at all, and there are **no actor properties correlated with ripping**.
Anything more nuanced had to be faked from outside — granting `PowerProtection` in a monster's
Pain state to blunt a ripper, or running an ACS script that watched a projectile and capped its
damage before it despawned.

ZandroX makes ripping authorable: budgets that limit how long a ripper keeps going, damage
falloff on repeat hits, a tier system for which rippers can pierce which victims, and a `Rip:`
state that fires when a rip lands.

## Availability

> **ZandroX only.** None of this exists in Zandronum 3.2. The tier system
> (`RipperLevel` / `RipLevelMin` / `RipLevelMax`) is ported from GZDoom/UZDoom; the budgets,
> falloff and `Rip:` state are original.
>
> **Works in netgames**, and costs no extra bandwidth — see [Netplay](#netplay).

## Everything is opt-in

Every budget defaults to `0`, which means **unlimited**, and `RipperDamageFactor` defaults to
`1.0`, meaning no falloff. A ripper that sets none of them resolves to "rip normally, forever"
and behaves exactly as it did before this feature existed. Existing content is untouched.

## The three budgets

A ripper can be limited three independent ways. All are authored on the **projectile**.

| Property | Limits |
|---|---|
| [`RipperCount`](RipperCount.md) | Rip hits against any **one** victim. |
| [`RipperMaxCount`](RipperMaxCount.md) | Rip hits over the projectile's **whole life**, across all victims. |
| [`RipperMaxDamage`](RipperMaxDamage.md) | Cumulative **damage actually dealt** by ripping. |

Rip damage lands **once per tic** for as long as the projectile is inside a victim, so these
budgets are counted in tics of contact, not in distinct enemies touched. A fast projectile can
land several rips in one tic.

### What happens when a budget runs out

**All three detonate on the bite that spends them, not on the next contact.** The hit lands in
full, then the projectile explodes in the same instant. `RipperCount 3` with
[`+RIPEXPLODEONLIMIT`](RIPEXPLODEONLIMIT.md) is "pierce three times, then boom" — there is never
a fourth contact.

The damage that crosses a `RipperMaxDamage` cap is **not** clamped to the remaining budget: the
last hit deals its full amount and the cap is simply exceeded.

The two budgets differ in what a *spent* projectile does:

- A spent **`RipperMaxDamage`** always detonates. That is the property's whole contract.
- A spent **count** budget (`RipperCount` or `RipperMaxCount`) makes the projectile pass through
  inertly — no damage, blood, sound, poison or push — while still hurting everyone else. Add
  [`+RIPEXPLODEONLIMIT`](RIPEXPLODEONLIMIT.md) to make it detonate instead.

## Tiered ripping

Instead of the all-or-nothing `+DONTRIP`, a victim can define a window of rip power it resists.

- [`RipperLevel`](RipperLevel.md) on the projectile is its rip tier.
- [`RipLevelMin`](RipLevelMin.md) and [`RipLevelMax`](RipLevelMax.md) on the victim bound which
  tiers may pierce it. Either bound is disabled by `0`.

A projectile outside the victim's window **cannot rip it and explodes on it instead**, behaving
exactly as though that victim had `+DONTRIP`. This is the ported GZDoom/UZDoom behaviour.

The three tier values are the only ripper numbers that are **not** clamped non-negative, so
negative tiers are legal and can be used as a band below the default `0`.

## Readable state

Five ripper values can be read in DECORATE expressions, so a projectile can branch on its own
progress:

| Variable | Meaning |
|---|---|
| `RipperLevel` | This actor's rip tier. |
| `RipLevelMin` | This actor's lower resistance bound. |
| `RipLevelMax` | This actor's upper resistance bound. |
| `RipperDamageDone` | Damage this projectile has dealt by ripping so far. |
| `RipperHitsDone` | Rip hits this projectile has landed so far, across all victims. |

`RipperDamageDone` and `RipperHitsDone` are runtime counters with no matching property — they are
read-only. [`A_ResetRipCounters`](A_ResetRipCounters.md) is what clears them.

```
    SPIK A 1 A_JumpIf(RipperHitsDone > 5, "Weaken")
```

> Note that `RipperDamageDone` only advances on the server, so a client's copy reads `0`. Do not
> branch on it in a way that must look identical on both sides.

## The `Rip:` state

With [`+USERIPSTATE`](USERIPSTATE.md), a projectile enters its [`Rip:`](Rip.md) state after
ripping, mirroring `+USEBOUNCESTATE` and `Bounce:`.

## Netplay

**Steady-state bandwidth cost: zero.** None of the ripper state is networked.

The counters are booked by the same rule on both sides with no RNG involved, so they agree as
long as the client's simulation sees the same contacts. They can drift when it does not, but
**the drift is cosmetic**: every point of rip damage comes from the server, because the damage
call in the rip path is already server-gated. A client whose counter runs ahead simply stops
drawing blood and playing the rip sound early; one that runs behind draws an extra hit. Neither
changes gameplay state.

Clients never self-detonate on `RipperMaxDamage`; they learn about it from the existing missile-
explode message, which is also what corrects a client that briefly kept a spent projectile
flying.

The [three tier setters](A_SetRipperLevel.md) are inputs to that same server-authoritative
decision and are likewise not broadcast.

## Limits and rounding

- `RipperMaxDamage`, `RipperCount`, `RipperMaxCount` and `RipperDamageFactor` are clamped
  **non-negative** when parsed — a negative budget has no meaning, and `0` already spells
  "unlimited".
- `RipperDamageFactor` is additionally clamped to a maximum of `128.0` before compounding.
- Scaled rip damage saturates at `4194304` rather than overflowing, and floors at `0` once
  falloff decays it past a whole point of damage.

## Known limitations

- **A ripper overlapping two actors can damage each more than once per move.** Upstream widened
  its "last ripped" tracking to a map to prevent this; doing so here would put a container
  constructor on `P_TryMove`, the hottest path in the engine, so it is deliberately not done.
  `RipperCount` and `RipperMaxCount` bound the overshoot anyway.
- **A spent ripper that strikes a `+REFLECTIVE` actor is reflected, not detonated** — the
  pre-existing blocked-missile ordering tries reflection before exploding.
- **No ACS access.** There are no `APROP_` ids for any of these fields.

## Implementation notes

The decision logic is in
[`features/ripper/computation/`](../../../src/zandronum/src/features/ripper/computation/) as
pure helpers, with the engine side in `p_map.cpp`'s rip path. The feature's
[README](../../../src/zandronum/src/features/ripper/README.md) covers the per-victim ledger,
the deferred `Rip:` state change, and why a spent budget detonates by reporting the victim as
solid rather than exploding directly.

Introduced in [#144](https://github.com/rc4l/ZandroX/pull/144).

## See also

**Properties** — [RipperMaxDamage](RipperMaxDamage.md) ·
[RipperCount](RipperCount.md) · [RipperMaxCount](RipperMaxCount.md) ·
[RipperDamageFactor](RipperDamageFactor.md) · [RipperLevel](RipperLevel.md) ·
[RipLevelMin](RipLevelMin.md) · [RipLevelMax](RipLevelMax.md) · [RipSound](RipSound.md)

**Flags** — [+NORIPSOUND](NORIPSOUND.md) · [+RIPEXPLODEONLIMIT](RIPEXPLODEONLIMIT.md) ·
[+RIPPERNOPAIN](RIPPERNOPAIN.md) · [+RIPSOUNDNORESTART](RIPSOUNDNORESTART.md) ·
[+USERIPSTATE](USERIPSTATE.md)

**States** — [Rip](Rip.md)

**Functions** — [A_SetRipperLevel](A_SetRipperLevel.md) · [A_SetRipMin](A_SetRipMin.md) ·
[A_SetRipMax](A_SetRipMax.md) · [A_ResetRipCounters](A_ResetRipCounters.md)
