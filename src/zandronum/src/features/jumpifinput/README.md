# jumpifinput

A new DECORATE action function, **`A_JumpIfInput`**, that jumps to a state when a player is
pressing given input buttons. It removes the need for client-sided ACS (`GetPlayerInput` +
manual state redirection) to react to input mid-state — e.g. branching out of a weapon's `Fire`
loop when the player taps altfire.

```
action native A_JumpIfInput(int keys, state label, int flags = 0, int owner = AAPTR_DEFAULT);
```
- `keys`  — OR-mask of `BT_*` button constants (`BT_ATTACK | BT_ALTATTACK`). Combine with `|`.
- `label` — jump target: a quoted state name, or a bare integer offset (`0`/`"None"` = no jump).
- `flags` — `JIF_ALL` (require all listed buttons, not just any), `JIF_EDGE` (only the tic the
  input is newly satisfied), `JIF_NOT` (invert). Default `0` = any button, while held.
- `owner` — `AAPTR_*` selector picking whose input to read (default = `self`'s player).

## Files (this folder)
- `computation/jumpifinput_compute.{h,cpp}` — pure, engine-free decision logic
  (`ComputeInputMatch`, `ComputeShouldSendFullButtons`) plus the `JIF_*` flag enum.
- `computation/jumpifinput_compute_test.cpp` — GoogleTest, 100% branch coverage.

The `*_compute.cpp` is auto-linked into the engine (and the `*_compute_test.cpp` into the test
binary) by the `GLOB_RECURSE` rules in `src/zandronum/src/CMakeLists.txt` and
`tests/CMakeLists.txt` — no CMake edit needed.

## Required in-engine hooks (the parts that can't live in this folder)
- **`src/thingdef/thingdef_codeptr.cpp`** — `#include` the compute header and add the
  `DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_JumpIfInput)` codeptr next to the `A_JumpIf*` family.
  Reads `owner->player->cmd.ucmd.buttons` / `oldbuttons`, follows the `A_Jump` netcode pattern
  (server-authoritative: early-return in client mode unless `NETFL_CLIENTSIDEONLY`), and jumps
  via `ACTION_JUMP(jump, CLIENTUPDATE_FRAME)` so `DoJump` replicates the psprite/actor state.
- **`wadsrc/static/actors/constants.txt`** — `BT_*` button constants (mirroring `buttoncode_t`
  in `src/d_event.h`) and `JIF_*` flags, so they're usable as DECORATE tokens.
- **`src/d_event.h`** — cross-reference comment pointing at `constants.txt` (values unchanged).
- **`wadsrc/static/actors/actor.txt`** — the `action native A_JumpIfInput(...)` prototype.
- **`src/cl_commands.cpp`** (`clientcommand_WriteMoveCommandToBuffer`) — send the full 32-bit
  button set (`CLIENT_UPDATE_BUTTONS_LONG`) whenever a script/user button above the low gameplay
  byte is pressed (via `ComputeShouldSendFullButtons`), so server-authoritative `A_JumpIfInput`
  can see user buttons. The server already reads long-vs-byte off that bit.

## Known limitations
- **Latency:** server-authoritative, so the local player's jump lags by ~round-trip. Client
  prediction for `consoleplayer` (as real fire/altfire feels) is a possible future enhancement.
- **Overlay layers:** `DoJump` currently replicates only the reserved weapon/flash psprite
  layers; a jump issued from an overlay layer is single-player-only for now (pre-existing on this
  branch, not introduced here).

## Verification
- Unit tests: `tests/coverage.sh --auto` (picks up `jumpifinput_compute_test.cpp`).
- In-engine: build, then via the Zandronum MCP load a weapon whose `Fire` loop calls
  `A_JumpIfInput(BT_ALTATTACK, "AltFire")`; hold fire, tap altfire, confirm the branch. Exercise
  `JIF_EDGE`, `JIF_ALL` (`BT_ATTACK|BT_ALTATTACK`), `JIF_NOT`, and a `+user1`/`BT_USER1` jump.
