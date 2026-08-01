# fov-interp

Smooth, frame-rate-independent FOV changes. **Provenance:** q-zandronum@d2475b676 +
q-zandronum@390ea5ac2 · **Class:** ADAPTED (their two-place duplication collapsed into one tested
unit; their 32-bit `FixedMul` tic-frac handling replaced for fixed64).

## The problem

A player's FOV moves toward its target one **tic** at a time (`P_PlayerThink`), driven by the
`fov` CCMD, a weapon's `FOVScale`, or `A_SetFOV`. The renderer then draws whatever
`FieldOfView` happened to be set at the last `R_SetFOV` call. Two consequences:

1. The step is quantised to 35Hz, so at 60/144/240Hz a zoom visibly **staircases**.
2. `FieldOfView` is an *integer fineangle* and `R_SetFOV` sets `setsizeneeded` on every change —
   so it cannot simply be called per frame to smooth things out.

## The fix

Render the **sub-tic position of the step the simulation is about to take**. The renderer asks
this feature for the FOV to draw with; the answer is `player->FOV + fraction_of_next_step`, which
converges exactly onto the simulated value as `r_TicFrac` reaches 1 — so the tic boundary is
invisible instead of being a jump.

Interpolation stops while the simulation is stopped (paused, client demo paused, menu open in a
non-client game); the view then holds precisely at the simulated FOV rather than drifting toward
a target the sim is not moving toward. That guard is q-zandronum@390ea5ac2.

## The pure decision unit (the replaceable core)

`computation/fovinterp_compute.{h,cpp}` (+ `_test.cpp`, 100% coverage) holds all of the
arithmetic and is header-pure — no engine, GL or CVAR includes:

- `FovTargetForWeapon(desiredFov, alive, hasReadyWeapon, weaponFovScale)` — the weapon-scaled
  target (magnitude of `FOVScale`; `0` means "no adjustment", not "collapse to zero")
- `FovStepTic(currentFov, targetFov, changeSpeed)` — one tic of the simulation's movement
- `FovRenderDelta(currentFov, targetFov, changeSpeed, ticFrac, interpolate)` — the sub-tic offset,
  defined as `ticFrac` × the very next `FovStepTic`, so **render and sim can never disagree**
- `FovClamp(fov)` — `R_SetFOV`'s 5..170 range, which the interpolated path bypasses
- `FovChangeSpeedClamp(speed)` — floor of 1 degree/tic

**Why it is isolated:** the renderer staircase (`docs/renderer-staircase.md`) replays
`gl/scene/gl_scene.cpp` verbatim from upstream. Tween logic living in that file would have to be
hand-merged on every future flight. Here the flight only has to re-apply **one call**.

## In-place engine edits (enumerate every one — features/README.md law)

- `src/gl/scene/gl_scene.cpp`
  - `#include "features/fov-interp/fovinterp.h"`.
  - `FGLRenderer::RenderView` — the `RenderViewpoint(...)` call passes
    `FOV_InterpolatedForFrame(player)` instead of `FieldOfView * 360.0f / FINEANGLES`. **One
    line**; marked `[rc4l] fov-interp`.
- `src/p_user.cpp`
  - `#include "features/fov-interp/computation/fovinterp_compute.h"`.
  - New `CUSTOM_CVAR (Float, cl_fovchangespeed, 7.0f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)`, clamped
    at 1 — 7.0 is the constant ZDoom hardcoded here, so the default *is* the historic behaviour.
  - `P_PlayerThink`'s "[RH] Zoom the player's FOV" block now calls `FovTargetForWeapon` +
    `FovStepTic` instead of an inline copy of the same arithmetic. Behaviour at the default
    `cl_fovchangespeed` is identical to before.
- `src/c_cmds.cpp`
  - `CCMD (fov)` **removed** — replaced by the CVAR in `fovcvar.cpp`, which carries its permission
    check and its DEM path. `fov 90` still works from the console, now as a CVAR set. A comment
    marks the spot.
- `src/d_player.h`
  - `player_t::lastFOVChangeTic` — gametic of the last accepted change, for the cooldown.
    Client-local, not serialized, never read by the server.
- `wadsrc/static/menudef.txt`
  - `VideoOptions`: `Field of View` and `FOV change speed` sliders.
- `src/CMakeLists.txt` — `features/fov-interp/fovinterp.cpp` and `fovcvar.cpp` added before
  `zzautozend.cpp`.

## Gates

- **fixed64**: `r_TicFrac` is 48.16. The glue crosses to float as `int64 → double → float`
  (`FIXED2DBL`, then a cast), per the `fixed64-widening` skill. Q-Zandronum's original ran
  `FixedMul(r_TicFrac, fovDiff)` with a **float** second operand — already loose at 32 bits and
  outright wrong once `fixed_t` is 64-bit. This is the one place the port deliberately diverges.
- **netcode**: **no new wire format.** The interpolation half is render-side only and touches no
  actor state, movement, spawning, AI, RNG or sound. The CVAR half reuses the *existing*
  `DEM_FOV` / `DEM_MYFOV` messages the `fov` CCMD already sent — same bytes, same handlers in
  `d_net.cpp`, only the trigger moved — so no `SERVERCOMMANDS_*` command and no byte-exact
  fixture is introduced. `fov_change_cooldown_tics` reaches clients through the standard
  `CVAR_SERVERINFO` machinery (`SERVER_SettingChanged`, as `sv_aircontrol` does), and
  `lastFOVChangeTic` is client-local and never serialized. Server-side FOV enforcement
  (`sv_nofov` / `DF_NO_FOV`) is untouched and still governs what a client may ask for; this
  feature changes how smoothly the view gets there and who may ask, never what the server allows.
  Note Q-Zandronum went the *opposite* way in 65e0aad7f ("Remove fov enforcement by server") —
  that commit and its follow-up d3cb7f70e are deliberately **not** ported.
- **ZScript**: none. No VM surface anywhere near this.

## The slider, and why `fov` became a CVAR

A menu slider can only bind to a CVAR, and FOV was a CCMD. Q-Zandronum solved that by turning it
into `CUSTOM_CVAR(Int, fov, ...)` (65e0aad7f) — but the same commit *deleted* the server's ability
to lock FOV. We take the CVAR and keep the lock:

- `fovcvar.cpp` holds `CUSTOM_CVAR(Float, fov, 90, CVAR_ARCHIVE)`. It still emits **DEM_FOV /
  DEM_MYFOV**, exactly as the old CCMD did, so demos, prediction and the server see FOV changes
  through the existing path — nothing new goes on the wire.
- `sv_nofov` / `DF_NO_FOV` still decides who may change it: under a lock only the arbitrator can,
  and their change applies to everyone (DEM_FOV). Non-arbitrators get the same refusal message
  the CCMD printed.
- A refused write is rolled back with `ForceSet`, so the slider and the config file can never
  display an FOV the player was not granted.
- Ours is a **Float** CVAR (Q-Zandronum's is Int) because `DesiredFOV` is a float and the clamp
  is applied through the tested `FovRequestClamp`.

`fov_change_cooldown_tics` (`CVAR_SERVERINFO`, 0 = off, ported from Q-Zandronum) is a server-set
minimum gap between a client's FOV changes — the softer anti-peek measure they introduced *instead
of* the lock. Here it **stacks with** the lock. The arbitrator is exempt while a lock is in force
(administrative change, not peeking), and offline play is never rate-limited.

Both rules live in `computation/fovrequest_compute.{h,cpp}` (100% covered) so they are testable
instead of buried in a CVAR callback: `FovRequestDecision()` returns
`FOV_SET_MINE` / `FOV_SET_EVERYONE` / `FOV_DENIED_LOCKED` / `FOV_DENIED_COOLDOWN`, and the callback
does as it is told.

Menu entries (`wadsrc/static/menudef.txt`, `VideoOptions`): **Field of View** (5–179) and **FOV
change speed** (1–100). The server-side `Allow FOV` option is untouched and still present under
Gameplay Options — Q-Zandronum removed theirs.

## Not included

- **Keeping FOV across respawn** (q-zandronum@5d751aff) — independent, still open.
- q-zandronum@65e0aad7f's removal of `DF_NO_FOV`, and its follow-up d3cb7f70e ("Don't send client
  FOV to the server"), which only makes sense once the lock is gone.
