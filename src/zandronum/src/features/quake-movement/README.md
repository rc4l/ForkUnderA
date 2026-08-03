# quake-movement

Quake-style player physics as an **opt-in, per-pawn** movement model. **Provenance:**
q-zandronum@397272811e4f71b168f1949d21369d3e91a7146c · **Class:** ADAPTED.

A pawn selects its model with `Player.MvType`:

| value | model |
|---|---|
| `0` (default) | stock Doom physics — **unchanged by this feature** |
| `1` | Quake friction/acceleration |

Everything else here (`+CROUCHSLIDE`, `+WALLJUMP`, `Player.AirAcceleration`, …) is inert unless the
pawn also sets `Player.MvType 1`. A WAD that never mentions `MvType` plays exactly as it did before.

## Why it's built this way

Q-Zandronum ships Quake movement *and* a rewritten netcode — full-precision player updates every tic,
32-bit NetIDs, clientside prediction of DECORATE/ACS functions, an unlagged overhaul. **None of that
is ported.** The two are separable: the movement math is local simulation that rides on
`CLC_CLIENTMOVE` and `MovePlayer` exactly as they already exist. See the *Netcode* section for the
per-item accounting.

## Staging

This feature lands in five reviewable stages. **Stage 1 is complete**; the rest are pending.

1. ✅ **Scaffolding, behaviour-neutral.** `Player.MvType`, the `P_MovePlayer_Doom` split, the
   `mvFlags` word and its replication, property/APROP plumbing. Every default is `0`, so the engine
   behaves identically to before this feature existed.
2. ✅ **Quake core.** `QFriction`/`QAcceleration`/`QCrouchWalkFactor`, `MovePlayerQuake`, the
   `P_XYMovement` friction branch, CPM air control, velocity cap.
3. ✅ **Jump rework.** `CheckJumpQuake`: the second-jump state machine, `+WALLJUMP`/`+WALLJUMPV2`,
   `+DOUBLETAPJUMP`, `+EDGEJUMP`, `+GROUNDSECONDJUMP`, `+ABSOLUTESECONDJUMP`, `+USER4JUMP`, plus
   their prediction state. **`+ELEVATORJUMP` is deferred** — see below.
4. ✅ **Traversal.** Crouch slide, wall climb, air wall run, their looping sounds, and local-player
   client-side effect actors. `+ELEVATORJUMP` landed here too.
5. ✅ **Speed tiers.** Four-entry `Player.ForwardMove`/`SideMove`/`FootstepsEnabled`,
   `Player.CrouchScale`, `Player.CrouchChangeSpeed`, footsteps. (The `BT_SPEED` change was pulled
   forward into the stage 1-3 bug fixes, because `CrouchWalkFactor` could not be correct without it.)

**The port is complete.** `+ELEVATORJUMP` landed with stage 4.

## The `P_MovePlayer` split (stage 1)

`P_MovePlayer` kept its preamble — the client-mode guard, the free-chasecam angle handling, turn
ticks, the dead check, the end-level-delay `memset`, and the `onground` computation — and now ends in
a dispatch. The stock movement body moved into `P_MovePlayer_Doom` **byte-for-byte**; the only edit is
that `spectatormove` is computed in the new function instead of the caller, since the jump block is
its sole consumer. That was verified by diffing the extracted span against the pre-change file.

Spectators will always take the Doom path once stage 2 adds the branch: spectator movement is a
free-fly camera, not simulated physics, and Q-Zandronum makes the same exception.

## `mvFlags` — a fourth flag word

Q-Zandronum puts these in its own `flags8`, which **collides** with ours: ZandroX's `flags8` is
MBF21's word (bit values fixed by that spec) and `flags9` is the ripper set. So the movement flags get
their own `mvFlags` word on `AActor`, which is also what Q-Zandronum does — meaning a mod's
`+CROUCHSLIDE` means the same thing in both engines.

Defined in `actor.h` (`MV_*`), registered in `thingdef_data.cpp`, serialized in `AActor::Serialize`
under `SaveVersion >= 4513`.

## Stage 2: the physics core

`computation/qphysics_compute.{h,cpp}` holds every arithmetic decision and is engine-free: no actor,
no `player_t`, no CVAR, **no `fixed_t`**. It is float end to end, because Quake movement genuinely is
— Q-Zandronum runs it in `FVector3` and only converts at the boundary. Keeping the conversion out of
the compute unit is what makes it safe under our 48.16 `fixed_t`: `quakemove.cpp` does every
fixed↔float crossing explicitly, so none of the deleted `Fixed`×`float` operators is reachable.

The two properties that describe a *velocity* (`AirAcceleration`, `VelocityCap`) stay `fixed_t` so
they read in map units like every other speed property; the pure acceleration/friction coefficients
are plain floats.

**Verified in-engine** (MAP01, `+forward` from rest, `cl_run 1`, class read back from `dumpactor`):

| | 15 tics | terminal speed |
|---|---|---|
| `MvType 0` (Doom, running) | 112.83 | ~7.5 u/tic |
| `MvType 1` (Quake) | **189.2** | **13.141 u/tic** |
| `MvType 1` + `VelocityCap 4.0` | 60.00 | exactly 4.000 u/tic |

13.141 is the model's own prediction: acceleration grants
`GroundAcceleration × maxSpeed / TICRATE` per tic until the remaining headroom shrinks below the
friction drop, i.e. equilibrium at `maxSpeed − friction²/TICRATE` = `14.17 − 1.03`. Matching the
closed form to three decimals is the strongest evidence available that both halves are wired right.

Friction also reaches a genuine stop, not an asymptotic creep: after input is released the pawn
decelerates and then holds one x position across 40 further tics. `MvType 0` is unaffected — a
20-tic run lands on x=452.325, the same as the pre-feature build.

⚠️ **Harness traps that produced wrong numbers here more than once.** Verify the class and `cl_run`
in the same breath as every sample:
- `map MAP01` silently reverts the player class to `DoomPlayer`, so a "Quake" sample after a map
  change is really Doom.
- `cl_run` is archived and can differ between runs, so a class comparison can quietly become a
  walk-vs-run comparison.
- **ZDoom's `Printf` renders some ordinary float values as `-NaN`.** `%.4f` output is not evidence.
  Print `(float)(int)(x * 1000)` instead; integer-valued floats format reliably. Two separate wrong
  diagnoses here came from believing a raw `%f`.
- `set_pause` + `step` cannot deliver a rising button edge: ticcmds keep building while paused, so
  `oldbuttons` absorbs the press. Drive jump inputs with console `wait` on a running engine.

### Three bugs this feature shipped with, and what they teach

All three were found by auditing stages 1–3 before starting stage 4, and all three were invisible to
the unit tests because each lived in the *glue* between tested pieces.

1. **`BT_SPEED` meant the wrong thing.** The engine sets that bit from the raw `+speed` key, while
   the effective run state is `Button_Speed.bDown ^ cl_run`. The Quake move tier is picked from the
   bit **on the server**, which cannot see `cl_run` — so with the default `cl_run 1` and no `+speed`
   held, a running player looked like a walking one and the pawn accelerated toward half speed.
   Fixed in `G_BuildTiccmd`, gated on `MvType` so stock pawns keep the vanilla meaning exactly.
2. **The friction curve overflowed.** `QFloorFrictionForFriction` raised the floor-friction ratio to
   the 16th power unbounded. In the engine build that reached infinity, making one tic's friction
   drop exceed any achievable speed — so **velocity was zeroed every single tic** and Quake ground
   movement was completely broken, while the identical call returned 1.0 under the test build. Now
   the neutral floor short-circuits to exactly 1.0 and the result is clamped to ±16x.
3. **Jump velocities truncated to whole map units.** The velocity helpers took `int` map units, so
   `Player.JumpZ 8.5` became 8. They now work in raw fixed-point units.

The lesson worth keeping: the compute units were all individually correct and green the whole time.
Every one of these lived where a tested function meets engine state, which is exactly the seam the
in-engine MCP pass exists to cover.

### Deliberate divergences

- **`wasJustThrustedZ` is not carried over.** Q-Zandronum skips ground acceleration for one tic after
  a `ThrustThingZ`, via a flag that only exists alongside their thrust-prediction rework. We did not
  take that rework, so the guard has nothing to hang off. Visible difference: a Z-thrust does not
  suppress ground acceleration on its landing tic.
- **`QCrouchWalkFactor` distinguishes only walk vs run.** Q-Zandronum derives it from four
  `ForwardMove`/`SideMove` tiers; ZandroX has two until stage 5 adds the rest.
- **No cached `SpeedFactor` on the actor.** Q-Zandronum caches the powerup multiplier so their
  prediction can replay it; we recompute it each tic, because a cached copy with nothing to sync it
  would just be a second source of truth.

## Stage 3: the second-jump system

`computation/qjump_compute.{h,cpp}` holds the state machine (`SJ_NOT_AVAILABLE` → `SJ_AVAILABLE` →
`SJ_READY`) and the arming/trigger predicates; `quakemove.cpp` supplies the wall traces, velocities
and sounds. The behaviours worth knowing:

- **The jump button must be released before the second jump arms.** Without that check one long
  press spends both jumps on consecutive tics and the double jump reads as "sometimes doesn't work".
  A dedicated trigger (`+DOUBLETAPJUMP` / `+USER4JUMP`) bypasses it, since the button is then
  irrelevant.
- **`SecondJumpAmount` is tri-state:** `0` disables, a positive count limits, and **negative means
  unlimited** — which is why the arming test is `!= 0` and the decrement is guarded by `> 0`.
- **`+USER4JUMP` takes the jump button away as a trigger.** That asymmetry is Q-Zandronum's and is
  preserved deliberately.
- **The second jump's Z is a floor, not an addition** — using it while already rising faster must
  not slow you down.
- **A wall jump that finds no wall stays armed** rather than being consumed, so the player can still
  spend it once they reach a surface.

**Verified in-engine** (`SecondJumpAmount 1`, `SecondJumpZ 10`, `JumpDelay 20` so the second-jump
branch is distinguishable by leaving `jumpTics` positive):

| | result |
|---|---|
| Hold jump 12 tics | apex ~z 30, no second jump — the release check holds |
| Press → release → press | `remaining` 1→**0**, `jumpTics` **18** (positive ⇒ second-jump branch), velz rising, z **60** vs the ~36 single-jump apex |
| A third press, same airtime | nothing: state never re-arms, velz keeps falling — the allowance is spent |

⚠️ **Harness note.** The rising edge (`buttons & BT_JUMP` and not `oldbuttons & BT_JUMP`) cannot be
driven through `set_pause` + `step`: ticcmds keep being built while the engine sits paused, so
`oldbuttons` absorbs the press and the edge is gone before the stepped tic runs. Drive jump inputs
with console `wait` sequences on a *running* engine instead.

### `+ELEVATORJUMP`

`elevatorjump.cpp` reads the signed vertical rate of whatever surface is carrying the player and adds
it to the jump, so a rising lift launches you instead of eating the launch. Only a **rising** surface
contributes — a descending lift must not subtract, or jumping off one would be worse than jumping off
solid ground.

Which surface counts is not simply "the sector you are in": standing on a 3D floor means being
carried by that floor's *model* sector, via its **ceiling** mover. `DPlat`, `DFloor`, `DElevator` and
`DPillar` each report their rate differently, so each is read on its own terms and a mover at rest
contributes nothing.

This needed three accessors that did not exist — `DPlat::GetSpeed`, `DElevator::GetSpeed` and
`DElevator::GetDirection` (each class already had the matching *setter*). Q-Zandronum also caches a
per-tic `movingSpeed` on `secplane_t` for their prediction rework; we read the thinker live instead,
since sector movers exist on clients too and we did not take that rework.

## Stage 4: traversal

`computation/qtraversal_compute.{h,cpp}` holds the charge bookkeeping. The crouch-slide meter is the
part worth understanding: **one signed counter carries two states.**

| value | meaning |
|---|---|
| `> 0` | usable charge, in tics of slide remaining |
| `< 0` | locked out; the magnitude is what has been banked while standing |

Standing upright on the ground does not merely stop regeneration — it *flips* usable charge negative,
and only going airborne flips it back. That is the rule that makes crouch slide a move you set up by
jumping rather than something you can spam from a standstill. Getting either sign flip wrong yields a
slide that silently refuses to start, or one that never runs out.

Wall climb and air wall run use the plain model instead: regenerate toward the cap, spend a tic per
tic, bottom out at zero.

**Verified in-engine:**

- **Crouch slide**, full lifecycle: spawns locked out from standing (−70) → going airborne releases
  it (+70) → landing crouched sets `sliding 1` and drains the meter → it hits 0 and the slide ends,
  after carrying the player ~867 units.
- **Wall climb**: holding jump at a wall sets `climbing 1` and raises z by exactly 5 units/tic
  (`WallClimbSpeed 5.0`) while spending charge; releasing jump clears the flag and the meter regrows.
- **Effect actors**: `MvDust` observed spawning at the player during both moves.
- **Air wall run**: engages. On **MAP02**, running along the 344-unit wall whose face sits at
  x=940, the pawn was genuinely airborne (`onground 0`, `velz 4.00`), had travelled 157.7 units
  along the wall at 13.141 u/tic, was upright, and reported `wallrunning 1`. It reads 0 again once
  grounded.

  Finding that spot took map introspection rather than guesswork: parse `LINEDEFS`/`VERTEXES` for a
  long one-sided line, take its right-side normal `(dy, -dx)/len`, and stand 20 units off the face
  (inside the 24-unit trace) facing along it. **Verify the pawn actually moves before trusting a
  result** — several computed spots in MAP01 put it in blocked space where it held full velocity
  while its position never changed, which looks exactly like a working wall run in a `dumpactor`
  dump and is not one. Also run god mode: MAP02's monsters killed the pawn mid-test.

Wall climbing does not hard-stop when the meter empties: the airborne branch regenerates faster than
the climb spends, so an exhausted climb alternates rather than ending. That oscillation is inherited —
Q-Zandronum puts the regen in the branch you only reach while *not* climbing — so climbing is
rate-limited rather than capped.

**The three `is*` state flags are cleared on every path that could skip their writer.** `UpdateAirWallRun`
only runs on the airborne branch, so without an explicit clear in the ground branch `isAirWallRunning`
stayed true for as long as the player stood there — ACS and SBARINFO would report a wall run that had
ended tics ago. The wall-climb and water/fly paths return early, so they clear the other two for the
same reason.

## Stage 5: move tiers, crouch tuning and footsteps

`Player.ForwardMove`/`SideMove` go from two entries to four — walk, run, crouch-walk, crouch-run —
and `Player.FootstepsEnabled` is indexed the same way. **That order is DECORATE API**: a mod writes
`Player.ForwardMove 1, 1, 0.5, 0.7` positionally, so `QTIER_*` cannot be rearranged. Omitted entries
mirror the ones before them, so `Player.ForwardMove 1` still means exactly what it always did.

**The tier selection is gated on `MvType`.** A stock pawn keeps its two tiers, because its crouch
slowdown already comes from `crouchfactor` in `P_MovePlayer_Doom` — applying a crouch *tier* on top
would slow it twice. Only a Quake pawn can reach entries 3 and 4.

`Player.CrouchScale` and `Player.CrouchChangeSpeed` replace the hardcoded `FRACUNIT/2` and
`CROUCHSPEED` in `P_CrouchMove`. These are **not** MvType-gated, and do not need to be: their
defaults are those exact constants, so a class that does not override them behaves identically. The
crouch threshold used by crouch slide and air wall run now derives from the pawn's own `CrouchScale`
(midpoint between standing and its authored depth) instead of the fixed 3/4 stage 4 used, so a
shallow-crouching class registers as crouched at its own midpoint.

Footsteps are local-player only, for the same reason as the traversal loops: a step per stride per
player is exactly the recurring traffic this port refuses to add. They need speed ≥ 3× the pawn's
`Speed`, which is what stops a player nudging a wall from machine-gunning footsteps.

**Verified in-engine** (`cl_run 1`, class read back each sample):

| tier | ticcmd `fwd` | terminal speed |
|---|---|---|
| run | 12800 | 13.141 |
| crouch-run | 6400 | 5.870 |
| crouch-walk | 3328 | 2.514 |
| walk | 6400 | — |

Each `fwd` is exactly its `normforwardmove` entry × 256, so the tier really is being selected in
`G_BuildTiccmd`. Footsteps respected their per-tier flags: no dust while walking (tier 0 disabled,
and above the speed gate so the flag was what stopped it), dust while running.

**`MvType 0` is untouched**: the same 15-tic run still covers 112.829 units, the pre-stage-5 figure.

### Why the sounds and dust are local-player only

Zandronum has `SERVERCOMMANDS_SoundActor` but **no matching stop-sound command**, so a looping slide
sound started remotely could never be ended — every other player would hear it forever. Broadcasting
the effect actors would likewise be recurring per-transition traffic, which is exactly what this port
refuses to add. So both are client-side and local-player only: you hear and see your own traversal,
not other players'. Lifting that needs a stop-sound command to pair with the start.

## Netcode — what this costs

**No new `SERVERCOMMANDS_*`, no new `CLC_*`, no change to `CLC_CLIENTMOVE`.** The move command's
bitmask is a single byte and all 8 bits are already spoken for (`0x80` is
`CLIENT_UPDATE_BUTTONS_LONG`, `network.h`). A new bit is neither available nor needed — Quake movement
is derived from `forwardmove`/`sidemove`/`buttons` and `mo->angle`, all of which already travel.

Two pieces of state can change at runtime, and both reuse commands that already exist:

- **`mvFlags`** → `FLAGSET_MVFLAGS`, a new value **appended** to the `FlagSet` enum (it goes out as a
  Byte, so a new entry may only ever go on the end — the same rule the `FLAGSET_FLAGS8`/`FLAGS9`
  comment records). Emitted only when a mod calls `A_ChangeFlag` on an `MV_*` flag.
- **`MvType`** → the existing generic `SERVERCOMMANDS_SetThingProperty`, sent only when the value
  actually changed, mirroring how `APROP_JumpZ` already behaves.

Both are authored from DECORATE and the engine never writes them, so
`SERVERCOMMANDS_UpdateThingFlagsNotAtDefaults` compares equal and sends nothing on a late join unless
a mod genuinely changed one. **A mod that doesn't touch them pays zero bytes.**

### The one place stage 2 touches the send path

Quake friction runs **after** the move, inside `P_XYMovement`. The server therefore moves, frictions,
and only then builds `SERVERCOMMANDS_MovePlayer` — but the client damps the value it receives too, so
handing it the post-friction velocity would apply friction twice and the player would visibly
under-run their own prediction.

So the server caches the pre-friction velocity in `player_t::ServerXYZVel` and `MovePlayer` sends
that instead, **for Quake-movement pawns only**. Same three fields, same byte count, same command id
— only the value differs. It is resolved *before* the `PLAYER_SENDVELX/Y/Z` flags are computed,
because those decide whether a component travels at all: deriving them from the post-friction
velocity could clear a component the pre-friction value still has.

`ServerXYZVel` is server-side, per-tic, never serialized and never read on a client.

`APROP_MvType` is range-checked on **both** ends. The server is trusted, but a value outside the enum
would put local prediction on a movement model that doesn't exist, so the client rejects it too.

## In-place engine edits (enumerate every one — features/README.md law)

- `src/actor.h` — the `MV_*` enum and the `mvFlags` field on `AActor`.
- `src/d_player.h` — the `MVTYPE_DOOM`/`MVTYPE_QUAKE` enum and `APlayerPawn::MvType`. Named `MVTYPE_`
  rather than Q-Zandronum's `MV_` so they don't read as members of the `MV_*` *flag* word.
- `src/p_user.cpp` — the `P_MovePlayer_Doom` extraction and the dispatch; `MvType` in
  `APlayerPawn::Serialize`.
- `src/p_mobj.cpp` — `mvFlags` in `AActor::Serialize`.
- `src/thingdef/thingdef_data.cpp` — the 13 `DEFINE_FLAG(MV, …)` entries.
- `src/thingdef/thingdef_properties.cpp` — `Player.MvType`. An out-of-range value is a parse error
  rather than a silent fallthrough to Quake movement.
- `src/p_acs.{h,cpp}` — `APROP_MvType` (get, set, and the `CheckActorProperty` integer list).
- `src/network.h` — `FLAGSET_MVFLAGS`, appended.
- `src/sv_commands.cpp` — `mvFlags` in `SERVERCOMMANDS_SetThingFlags`,
  `SERVERCOMMANDS_UpdateThingFlagsNotAtDefaults` and `SERVERCOMMANDS_SetThingProperty`.
- `src/cl_main.cpp` — the receiving cases in `client_SetThingFlags` and `SetThingProperty::Execute`.
- `src/thingdef/thingdef_codeptr.cpp` — the `A_ChangeFlag` flag-word → flagset mapping.
- `src/version.h` — `SAVEVER` 4513.
- `wadsrc/static/actors/shared/player.txt` — `Player.MvType 0`.

## Gates

- **fixed64**: nothing in stage 1 does arithmetic. Stage 2 onward must, because Q-Zandronum's Quake
  math is `float`/`FVector3` layered on **32-bit** `fixed_t` while ours is `zx::Fixed` (48.16, strong
  type, float operators SFINAE-deleted). Read `.claude/skills/fixed64-widening` before stage 2.
- **netcode**: see above. No wire-format change; `tools/protocol-snapshot.py --check` is unaffected
  because `FlagSet` is a hand-written enum, not a `protocolspec` declaration — which is exactly why
  the golden byte test below is the only thing guarding it.
- **ZScript**: none.
