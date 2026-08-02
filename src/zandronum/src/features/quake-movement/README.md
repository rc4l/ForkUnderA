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
2. ⬜ **Quake core.** `QFriction`/`QAcceleration`/`QCrouchWalkFactor`, `P_MovePlayer_Quake`, the
   `P_XYMovement`/`P_ZMovement` friction branches, velocity cap.
3. ⬜ **Jump rework.** `CheckJump`: the second-jump state machine, `+WALLJUMP`/`+WALLJUMPV2`,
   `+DOUBLETAPJUMP`, `+EDGEJUMP`, `+ELEVATORJUMP`, `+GROUNDSECONDJUMP`, `+ABSOLUTESECONDJUMP`,
   `+USER4JUMP`, plus their prediction state.
4. ⬜ **Traversal.** Crouch slide, wall climb, air wall run, their looping sounds, and local-player
   client-side effect actors.
5. ⬜ **Speed tiers.** Four-entry `Player.ForwardMove`/`SideMove`/`FootstepsEnabled`,
   `Player.CrouchScale`, `Player.CrouchChangeSpeed`, footsteps, and the `BT_SPEED` change.

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
