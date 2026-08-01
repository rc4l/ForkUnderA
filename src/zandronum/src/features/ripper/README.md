# ripper

DECORATE-authorable ripper behaviour. Before this, a ripper was binary — `+RIPPER` on the
projectile, `+DONTRIP` on the victim, `+NOBOSSRIP` for one niche case — and there were **no**
actor properties correlated with ripping at all. Anything beyond "rips / doesn't rip" had to be
faked from outside the rip system: granting `PowerProtection` in a monster's Pain state to blunt
a ripper, or ACS watching a projectile to cap its damage before it despawned.

Everything here is opt-in. All budgets default to `0` (= unlimited) and `RipperDamageFactor`
defaults to `1.0`, so a ripper that sets none of them resolves to "rip normally, forever" and
behaves exactly as it did before this feature existed.

## Properties

| Property | Side | Default | Meaning |
|---|---|---|---|
| `RipperMaxDamage <int>` | projectile | `0` | Cumulative **damage actually dealt** by ripping — post-armor / DamageFactor / resistance, i.e. what `P_DamageMobj` returns. On reaching it the projectile is forced into its Death state. |
| `RipperCount <int>` | projectile | `0` | Max rip hits against **any one victim**. Once spent, the projectile passes through that victim inertly — no damage, blood, sound, poison or push — while still hurting everyone else. |
| `RipperMaxCount <int>` | projectile | `0` | Max rip hits over the projectile's **whole life**, across all victims. |
| `RipperDamageFactor <float>` | projectile | `1.0` | Compounded per repeat hit on the same victim: hit *n* deals `Damage * factor^(n-1)`. |
| `RipperLevel <int>` | projectile | `0` | Rip power tier. |
| `RipLevelMin <int>` | victim | `0` | Rippers below this tier can't rip this actor; they explode on it instead. `0` disables the bound. |
| `RipLevelMax <int>` | victim | `0` | Rippers above this tier can't rip it. `0` disables the bound. |
| `RipSound <sound>` | projectile | *(none)* | The field already existed for MBF21's DeHackEd "Rip sound"; this makes it authorable in DECORATE. Empty = `misc/ripslop`. |

## Flags (`flags9`)

| Flag | Side | Meaning |
|---|---|---|
| `+NORIPSOUND` | projectile | Rips are silent. `RipSound` can replace the sound but never remove it. |
| `+RIPEXPLODEONLIMIT` | projectile | A spent `RipperCount`/`RipperMaxCount` detonates the projectile instead of ghosting it through. |
| `+RIPPERNOPAIN` | projectile | Rips never induce pain, but the terminal explosion still can. `+PAINLESS` already covers pain for *all* damage from a projectile including its explosion; this is the rip-only half, and it is what replaces the `PowerProtection`-in-Pain-state hack. |
| `+RIPSOUNDNORESTART` | projectile | Let the rip sound finish instead of restarting it every tic. Opt-in, so the vanilla machine-gunned squelch is untouched. Required for any `RipSound` longer than ~1/35 s. |
| `+USERIPSTATE` | projectile | Enter the actor's `Rip:` state after ripping, mirroring `+USEBOUNCESTATE` / `Bounce:`. Harmless if no `Rip:` state exists. |

### `Rip:` — the state

```
Actor Shredder
{
  +RIPPER
  +USERIPSTATE
  States
  {
  Spawn:
    SPIK A 1
    Loop
  Rip:
    SPIK B 1 A_SpawnItemEx("Sparks")
    Goto Spawn
  }
}
```

Entered **once per tic**, matching the cadence of rip damage — not once per victim and not once
per movement sub-step. A `FastProjectile` resets its per-move rip memo on every sub-step and so
can land several rips in a tic, but still enters `Rip:` only once.

The state change is **deferred to the mover** rather than applied in `PIT_CheckThing`.
`SetState` can run an action function and can end the state chain, and `AActor::Destroy` unlinks
the actor from the blockmap — doing that from inside the blockmap iterator that is mid-walk is
the same hazard the `RIP_EXPLODE` path avoids. `PIT_CheckThing` therefore only raises
`FCheckPosition::RipStatePending`, and `P_XYMovement` / `AFastProjectile::Tick` apply it after
the move completes. A `Rip:` state that immediately destroys the projectile is consequently
safe (covered by a test). Every missile-explode path returns before that point, so a projectile
that detonated during the move never has its Death state overwritten.

Like `Bounce:`, the state change is not broadcast — it follows the same client-side convention
as `+USEBOUNCESTATE`.

## When budgets detonate

**All three budgets detonate on the bite that spends them, not on the next contact.** The hit
lands in full, then the projectile explodes in the same instant — `RipperCount 3` with
`+RIPEXPLODEONLIMIT` is "pierce 3 times, then boom", never a 4th contact. The damage that
crosses a `RipperMaxDamage` cap is not clamped to the remaining budget.

`RIP_EXPLODE` is implemented by **returning "blocked" from `PIT_CheckThing`**, so the victim
reads as solid and the ordinary blocked-missile machinery detonates the projectile — in
`P_XYMovement`'s `explode:` branch, or `AFastProjectile::Tick`'s. This is deliberately *not*
`P_ExplodeMissile` called from the rip path: entering a Death state can `Destroy` the projectile,
and doing that inside the blockmap iterator would free the actor mid-walk.

## Files

`computation/ripper_compute.{h,cpp}` holds the **pure, engine-free** decision logic so it is
unit-testable without linking the game (`ripper_compute_test.cpp`):

- `ComputeRipLevelAllows` — the tiered-ripping window.
- `ComputeRipOutcome` — the pre-hit decision: `RIP_DAMAGE` / `RIP_INERT` / `RIP_EXPLODE`.
- `ComputeRipSpendsProjectile` — the post-hit decision: did this bite spend a budget?
- `ComputeScaledRipDamage` — compounded falloff, saturating at `RIP_DAMAGE_CAP` instead of
  overflowing, flooring at 0 once it decays past a whole point of damage.
- `ComputeNeedsVictimLedger` — whether the projectile needs a per-victim ledger at all.

`*_compute.cpp` is picked up by a glob in both the engine and test CMake, so this folder needs
no CMake edit.

## The per-victim ledger

`RipperCount` and `RipperDamageFactor` need to know how many times *this* projectile has already
ripped *that* actor, so the projectile carries `TArray<FRipVictim> RipVictims` (`actor.h`).

- **Only grown when a budget actually reads it** (`ComputeNeedsVictimLedger`). A plain `+RIPPER`
  projectile keeps an empty `TArray` and never allocates.
- `AActor::RipHitsOn` doubles as the ledger's garbage collector: entries whose `TObjPtr` has gone
  NULL are dropped during the lookup scan, so a long-lived ripper can't accumulate dead slots.
- Capped at `RIP_MAX_VICTIMS` (128) with oldest-entry eviction. A projectile that has ripped 128
  distinct actors is pathological; forgetting the first only refills that actor's budget.
- Marked for GC in `AActor::PropagateMark` — **there are two definitions of that function**
  (`gl/dynlights/a_dynlight.cpp` and `sdl/glstubs.cpp` for non-GL builds); both mark the ledger.
- Like the pre-existing `dynamiclights` `TArray` member, it relies on class defaults always
  holding an empty array, because `AActor`'s copy ctor / `operator=` / `skip_super` are raw
  `memcpy`. Defaults never rip, so this holds.

## Netplay

**Steady-state bandwidth cost: zero.** None of the ripper state is networked.

`RipperCount` / `RipperMaxCount` / `RipperHitsDone` are booked by the same rule on both sides —
no RNG is involved — so they agree as long as the client's simulation of the projectile sees the
same contacts. They can drift when it doesn't, but **the drift is cosmetic**: every point of rip
damage comes from the server, because the `P_DamageMobj` call in the rip path is already
server-gated. A client whose counter runs ahead simply stops drawing blood and playing the rip
sound early; one that runs behind draws an extra hit. Neither changes gameplay state.

`RipperDamageDone` therefore only advances on the server. Clients never self-detonate on
`RipperMaxDamage` and instead learn about it from `P_ExplodeMissile`'s existing
`SERVERCOMMANDS_MissileExplode`, which is also what corrects a client that briefly kept a spent
projectile flying.

`A_SetRipperLevel` / `A_SetRipMin` / `A_SetRipMax` are inputs to that same server-authoritative
decision and are not broadcast, matching the `HitRadius`/`HitHeight` precedent in
[`features/actorresize`](../actorresize/README.md).

The one thing that *can* cost bytes is the `flags9` word, and only when a mod changes it at
runtime: 8 bytes per `A_ChangeFlag` (`SetThingFlags` = SVC byte + Short netID + Byte flagset +
Long flags). The late-join full update gates each word on `!= GetDefault()`, which is false for
a DECORATE-authored `flags9`, so it adds no traffic there.

## In-engine hooks (edits to existing files, not part of this folder)

- `actor.h` — the `MF9_*` enum, `DWORD flags9`, the ripper fields, `FRipVictim`, the
  `RipHitsOn` / `RecordRipHit` / `ResetRipCounters` declarations, and `RipSound`'s type.
- `namedef.h` — `NAME_Rip`, appended (never inserted: namedef indices go over the wire).
- `p_local.h` — `FCheckPosition::RipStatePending`, the `+USERIPSTATE` deferral carrier.
- `p_mobj.cpp` (`P_XYMovement`) and `g_shared/a_fastprojectile.cpp` (`Tick`) — the two movers
  that apply the deferred `Rip:` state once the move is out of the blockmap iterator.
- `p_map.cpp` — the rip path in `PIT_CheckThing`, plus the level check in the PASSMOBJ/bridge
  gate and in `P_BounceActor` so a level-rejected ripper is consistently "not a ripper" there too.
- `p_mobj.cpp` — the ledger accessors, the `FRipVictim` archiver, and the guarded serialization.
- `p_local.h` / `p_interaction.cpp` — `DMG_NO_PAIN` and the three pain gates that honour it.
- `thingdef/thingdef_data.cpp`, `thingdef_properties.cpp`, `thingdef_expression.cpp`,
  `thingdef_codeptr.cpp` — the flags, properties, expression variables and codepointers.
- `wadsrc/static/actors/actor.txt` — the `RipperDamageFactor 1.0` base default, the `native`
  variable declarations, and the codepointer prototypes.
- `network.h`, `sv_commands.cpp`, `cl_main.cpp` — `FLAGSET_FLAGS9` (and `FLAGSET_FLAGS8`, which
  never existed, so `A_ChangeFlag` on an MBF21 flag silently desynced clients until now).
- `gl/dynlights/a_dynlight.cpp`, `sdl/glstubs.cpp` — GC marking, both copies.
- `version.h` — `SAVEVER` 4511.

## RipSound was dead on arrival before this feature

Exposing `RipSound` to DECORATE surfaced a pre-existing bug: it was declared `FSoundID`, while
every other actor sound field is `FSoundIDNoInit`. `PClass::CreateNew` memcpy's the class
defaults into the new object and **then** runs the constructor, and `FSoundID`'s constructor
sets `ID = 0` — so the authored value was wiped on every single spawn. `FSoundIDNoInit` exists
precisely to skip that initialisation.

The field is therefore now `FSoundIDNoInit` (`actor.h`). This also un-breaks MBF21's DeHackEd
`Rip sound = N`, which could never have worked either.

### Two more things that make a rip sound inaudible

**The default is Doom-silent.** `misc/ripslop` is defined only inside the `$ifheretic` and
`$ifhexen` blocks of `wadsrc/static/sndinfo.txt` — ripping is a Raven mechanic. Under a Doom
IWAD the name resolves to 0 and the fallback plays nothing at all. Doom-based rippers must set
an explicit `RipSound`. (Left as-is deliberately: defining it for Doom would put a squelch on
every existing Doom `+RIPPER` projectile, which are silent today.) Note also that a *typo* is
indistinguishable from this — `S_FindSound` returns 0 for an unknown name and this code reads 0
as "use the default", so a misspelled `RipSound` degrades silently rather than erroring.

**The sound retriggers once per tic** for as long as the projectile is inside a victim, and
`S_StartSound` explicitly stops the channel before restarting it (there is no `CHAN_NOSTOP` in
this engine). That is what makes the short vanilla squelch read as continuous shredding, but it
chops anything longer than ~1/35 s down to its first few milliseconds. `+RIPSOUNDNORESTART`
gates the call on `S_IsActorPlayingSomething(..., CHAN_BODY, ripSnd)` so the sound runs to
completion. It is opt-in precisely so existing content keeps the machine-gun behaviour.

The guard is inert on a server (no sound channels exist there, so it never suppresses), which is
correct — servers do not play sound anyway.

## Known limitations

- **`FCheckPosition::LastRipped` is still a single slot.** UZDoom widened it to a
  `TMap<AActor*, bool>` so a ripper overlapping two actors can't ping-pong between them and
  damage each more than once per move. Porting that would put a `TMap` ctor/dtor on every
  `P_TryMove` — the hottest path in the engine — so it is deliberately not done here.
  `RipperCount` / `RipperMaxCount` now bound the overshoot anyway.
- **A spent ripper that strikes a `+REFLECTIVE` actor is reflected, not detonated.** That is the
  pre-existing blocked-missile ordering in `P_XYMovement`, which tries reflection before
  `explode:`; the budget only decides that the projectile is blocked.
- **No ACS `APROP_` ids** for the new fields. Straightforward follow-up if wanted — ids ≥ 52,
  continuing the `actorresize` numbering.

## Provenance

Tiered ripping is ported from GZDoom/UZDoom — `ComputeRipLevelAllows` carries the upstream
commit link in `ripper_compute.cpp`, and `A_SetRipperLevel`/`A_SetRipMin`/`A_SetRipMax` carry
theirs in `thingdef_codeptr.cpp`. `DMG_NO_PAIN` keeps upstream's numeric value (1024), with
128/256/512 reserved for the upstream damage flags ZandroX does not have yet.
