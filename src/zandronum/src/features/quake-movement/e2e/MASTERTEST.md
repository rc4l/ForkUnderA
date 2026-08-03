# quake-movement master E2E

The whole feature, exercised in one place against a **real dedicated server with real connected
clients** — because every bug this port has shipped lived in the glue between individually-correct,
unit-tested pieces, and none of them was visible in single player.

Run on 2026-08-03 against `1009896` + the fixes below.

## Why it is shaped like this

**Server-authoritative readback.** Every number below is `dumpactor p<N>` **on the server**, or an
ACS trace running server-side. A client's own view of itself is prediction; it is only used where
the point is to compare the two.

**A generated map, not an IWAD map.** `build_mvmaster.py` emits `mvmaster.wad` (UDMF, nodes built at
load). Every test position is a stated constant. This exists because the feature already produced a
false positive on an IWAD map: a pawn wedged in blocked space holds full velocity while its position
never changes, which is indistinguishable from a working air wall run in a `dumpactor` dump. Traces
here log position every tic, so "it moved" is checked, never assumed.

**One class per behaviour** (`mvmaster_decorate.txt`). A pawn carrying several movement flags cannot
tell you which flag produced an observed jump. `MAPINFO` *replaces* the player class list, so
`DoomPlayer` is not selectable — the old trap where a map change silently reverted the pawn now
reverts to `MvDoomCtl`, a name `dumpactor` reports.

**A per-tic ACS sampler** (`script 20`). An MCP round trip is tens of tics wide, so nothing tic-exact
can be sampled by polling from outside. The instrument has to live on the authoritative side.

## Reproducing

```
python build_mvmaster.py                 # -> mvmaster.wad
python build_mvmaster.py --control -o mvcontrol.wad
```

Server: `-host 4 -file mvmaster.wad +map MVTEST +sv_cheats 1 +cooperative 1 +sv_maxclientsperip 4`
Clients: `-connect 127.0.0.1 -file mvmaster.wad +playerclass <MvClass>`, then `join`.

Control channel from a client: `warp x y` (`CLC_WARPCHEAT`, server does the teleport), `mcp_look 0
<yaw>` (absolute angle travels in `CLC_CLIENTMOVE`), `give` (`CLC_GIVECHEAT`), `puke -20 <tics>
<tag>` (negative = `ACS_ALWAYS`, so several players can trace at once).

---

## 1. Property and flag surface — `MvAll`

`MvAll` sets **every** new property to a value distinct from both its default and its neighbours',
and all thirteen `MV_*` flags. Read back identically on server and client:

| | authored | read back |
|---|---|---|
| `mvFlags` | all 13 | `00001fff` |
| `MvType` | 1 | 1 |
| Ground/air accel, friction, CPM, cap | 11.5 / 7.5 / 0.75 / 13.5 / 0.25 / 9.5 | all exact |
| Jump set | `JumpZ 9.5 JumpXY 3.25 JumpDelay 21 SJZ 11.5 SJXY 4.25 SJDelay 22 SJAmount 3 Tap 13` | all exact |
| Crouch slide | `11.25 / 1.75 / 65 / 2.75 / 13` | all exact |
| Wall climb | `6.5 / 1.875 / 66 / 2.25 / 14` | all exact |
| Air wall run | `67 / 2.5 / 10.5` | all exact |
| `CrouchScale`, `CrouchChangeSpeed` | 0.25, 0.25 | exact |
| `FootstepsEnabled` | `1,0,1,0` (inverted vs default) | `1 0 1 0` |
| `ForwardMove` / `SideMove` | `.125 .25 .5 .75` / `.375 .625 .875 1.125` | exact |
| `EffectActor` ×3 | `MvDust` | `MvDust` ×3 |

Live state initialises per-property, not from a shared constant: `slide -65 / climb +66 / wallrun
+67` — each counter seeded from **its own** cap, and the slide meter seeded **negative** (the
documented lockout). A copy-paste bug would show three identical numbers.

Defaults are equally pinned by `MvDoomCtl`: `qtier 1000 1000 1000 1000` proves `Player.ForwardMove
1,1` still mirrors into all four tiers, and `qcrouch 500 / 83` are `FRACUNIT/2` and `FRACUNIT/12`.

## 2. The wire

| path | evidence |
|---|---|
| `mvFlags` → `SetThingFlags(FLAGSET_MVFLAGS)` | `give MvGiveSlideOff` → `A_ChangeFlag` runs server-side → `00001fff` → `00001ffd` on **both** ends |
| `MvType` → `SetThingProperty` | `SetActorProperty(APROP_MvType, 0)` → `mvtype 0` on **both** ends |
| `APROP_MvType` get / `CheckActorProperty` | `getmvtype 1 is0 0 is1 1` |
| `APROP_MvType` range check | `asked 7 → readback 0`, `asked -3 → readback 0` — refused, not clamped |
| `CheckFlag` (a path that is not `dumpactor`) | all 13 flags read `1` |
| `tools/protocol-snapshot.py --check` | unchanged, **192 commands** |
| pre-friction velocity on the wire | `vel 13.141` but `qsrvvel 14169` on the server; `qsrvvel 0` on the client and `0` for a Doom pawn |

## 3. `MvType 0` is unchanged — measured across two builds

`ZandroX-dev-871d7c7f` is a packaged build with **no `features/quake-movement` directory at all**.
Same map (`mvcontrol.wad`, stock `DoomPlayer`), same inputs, two engines:

| recipe | pre-feature `871d7c7f` | this branch |
|---|---|---|
| 30-tic run → rest | **x = 716.1** | **x = 716.1** |
| run + jump → rest | **x = 1716.5** | **x = 1716.5** |

Re-run after the three fixes below, since two of them touch shared code paths. Still exact.

## 4. Quake core

| | server-side |
|---|---|
| terminal speed | **13.141** sustained t=10→45 |
| displacement per tic at terminal | **14.170** = `Q_MAX_GROUND_SPEED` |
| acceleration per tic | **4.048** = `GroundAcceleration × maxSpeed / TICRATE` |
| come to rest | velocity reaches exactly **0**, not an asymptote |
| `VelocityCap 4.0` | displacement pinned to exactly **4.000**/tic |

The gap between the 13.141 the actor *carries* and the 14.170 it *moves* is the friction ordering:
the pawn moves on pre-friction velocity, friction then damps the stored value, and acceleration
restores it before the next move. That is the same quantity the server must put on the wire, and
`qsrvvel` shows it doing so.

**Client/server convergence.** 30-tic run, then friction to a full stop — a resting position is
time-independent, so MCP latency cannot distort it:

| | x | y | z | vel |
|---|---|---|---|---|
| server | 689.0 | 384.0 | 0.0 | 0.000 |
| client | 689.0 | 384.0 | 0.0 | 0.000 |

Identical to printed precision. Double-applied friction would put the client visibly short.

## 5. Move tiers

Server-side ticcmd, all four reachable:

| input | `crouchfactor` | ticcmd `fwd` | expected | tier |
|---|---|---|---|---|
| forward | 100 | 12800 | `0x32 × 256` | run |
| forward + speed | 100 | 6400 | `0x19 × 256` | walk |
| forward + crouch | 50 | 6400 | `0x19 × 256` | crouch-run |
| forward + speed + crouch | 50 | 3328 | `0x0D × 256` | crouch-walk |

Walk and crouch-run share a `normforwardmove` entry, so `fwd` alone cannot separate them —
`crouchfactor` does.

## 6. Jumps

| case | result |
|---|---|
| hold jump throughout | **no** second jump — the release check holds |
| press → release → press | velz spikes **−5 → +9.0**, apex **81** vs the 36 single-jump apex |
| second jump Z semantics | velz is **set** to `SecondJumpZ`, not added — a floor |
| third press, `SecondJumpAmount 1` | nothing; allowance spent |
| `SecondJumpAmount -1` | **third** jump fires — the unlimited tri-state |
| `+GROUNDSECONDJUMP` | on the ground: `state 1` with the flag, `state 0` without. Same class otherwise |
| `+WALLJUMP` at a wall | fires — apex **88** |
| `+WALLJUMP` in the open | **does not** fire — apex **36**. Same class, same input, only the wall differs |
| `Player.JumpDelay` | ground phase between held-jump bounces: **20 → 21 tics, 45 → 46 tics** |

## 7. Traversal

| move | result |
|---|---|
| crouch slide | **7.085**/tic sustained (exactly half full speed) vs **3.542** crouched-not-sliding; meter drains +70 → 0; effect counter advances |
| crouch slide, crouched *after* landing | meter stays **−70**, no slide — one tic upright on the ground re-locks it, exactly the documented sign flip |
| wall climb | z rises **5.000**/tic = `WallClimbSpeed`; `climbing 1`; reached z 456 |
| air wall run | `wallrunning 1`, `onground 0`, upright, `speed2d 13.141`, **position advancing 13.1/tic** |

## 8. Elevator jump

Same platform, driven raise at 4 units/tic, jump velz after one gravity tic:

| pawn | floor | jump velz |
|---|---|---|
| `MvElevator` (`+ELEVATORJUMP`) | stationary | 7.0 |
| `MvElevator` | **rising** | **11.0** |
| `MvElevatorOff` (no flag) | stationary | 7.0 |
| `MvElevatorOff` | **rising** | 7.0 |

`JumpZ 8 + lift 4 − 1` = 11. The rising surface contributes only with the flag.

---

## Bugs this test found

All three were invisible to the 82 green unit tests, and all three lived where a tested function
meets engine state it does not own.

1. **`Player.CrouchScale` below 0.5 was unreachable.** `P_CrouchMove` clamps to `CrouchScale`, but
   its *caller* still gated on a hardcoded `FRACUNIT/2`, so a pawn authoring a deeper crouch stopped
   descending at half height and never reached its own depth. Measured `crouchfactor 50` against an
   authored `CrouchScale 0.25`; now 25.
2. **`Player.JumpDelay` was dead for the main jump.** Two causes, both in shared code the feature
   inherited. `P_ZMovement`'s landing handler hardcodes `jumpTics = 7` — Q-Zandronum **deletes** that
   block, we kept it — and it runs before `CheckJumpQuake` sees the grounded tic, overwriting the
   negative sentinel the re-arm keys off. Separately the ticker counts `jumpTics` down through the
   whole airtime, so it was past the `-18` sentinel before landing. `JumpDelay` 20 and 45 both
   produced an identical 8-tic ground phase; now 21 and 46. Both fixes are MvType-gated.
3. **The spectator reset covered two of four move tiers.** `PLAYER_SetDefaultSpectatorValues` exists
   to make spectator speed independent of the player class; this feature extended the tiers from two
   to four and did not extend the reset.

Plus a tooling gap that blocked the entire server-side half: **a `-host` server emitted nothing to
the MCP bridge**, because `PrintString` returns early in the server branch before the tee. Every
driven command timed out. Fixed in `c_console.cpp`.

## Not covered here

Behaviour **not** exercised in-engine. Each one's flag or property is proven to parse, store and
replicate (§1, §2) — what is missing is confirmation of what it *does*:

- `+CPMAIRCONTROL` air control, `+WALLJUMPV2` push direction, `+DOUBLETAPJUMP` dash,
  `+EDGEJUMP`, `+USER4JUMP`, `+ABSOLUTESECONDJUMP`.
- `Player.JumpXY` / `SecondJumpXY` horizontal thrust.
- Footstep sounds and effect-actor *spawning* (the per-move effect counters were observed
  advancing, which is indirect).
- The bandwidth measurement (`stat nettraffic`, `MvType 0` vs `1` steady state). The accounting is
  argued in the feature README and the protocol snapshot is unchanged, but no byte counts were taken.

A double-tap dash in particular resisted console driving — the tap pattern needs a rising edge
inside `DoubleTapMaxTics` and the console's `wait` granularity kept missing it.

## Harness traps found here

Added to the list the feature README already keeps:

- **Clients connect as spectators.** A spectating pawn has `ForwardMove1/2` forced to `1.0`, which
  reads exactly like a broken `Player.ForwardMove`. `join` first, and check for an inventory in the
  dump — a spectator has none.
- **`sv_maxclientsperip` defaults to 2**, so the third client from `127.0.0.1` is silently refused
  with "Too many connections from your IP" and has no pawn at all.
- **The server console's `wait` does not count gametics** the way a client's does. Anything
  tic-exact must be timed by the ACS tracer or by a client-side `wait`, never by the server console.
- **A perpetual plat wedges on a sector whose only neighbour is the room floor**: it snaps to the
  one height it can find and stalls, and the stalled thinker makes every later `Floor_*`/`Plat_*` on
  that tag a silent no-op. Drive test platforms explicitly.
- **`FixedMul(x, 1000.0)` overflows for map coordinates** and returns noise that still looks like a
  coordinate. Split the fixed value instead (`Milli()` in `mvmaster.acs`).
- **The ACS `Delay(1)` sampler occasionally logs twice in one tic.** Steady-state cadence over many
  tics is trustworthy; a single-tic delta is not.
