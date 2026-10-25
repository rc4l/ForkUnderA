# A_StartSound

DECORATE-callable port of GZDoom's `A_StartSound`, the modern successor to `A_PlaySound`.

- **Upstream origin:** `uzdoom@7bfbf612d9d8197c36bb77ab171005bce521a514`, `actor.zs`:
  `A_StartSound(sound whattoplay, int slot = CHAN_BODY, int flags = 0, double volume = 1.0,
  double attenuation = ATTN_NORM, double pitch = 0.0, double startTime = 0.0)`.
- **Why:** Eviternity II calls `A_StartSound` by name in DECORATE (barrel explosions, glass, the
  gib-ditcher weapon).

## Flags (uzdoom `EChanFlags`)

| Flag | Value | Behavior here |
|------|-------|---------------|
| `CHANF_OVERLAP` | 8192 | play beside what the slot already holds, on that slot (`features/sound-overlap`) |
| `CHANF_NOSTOP`  | 4096 | do not start while the actor already plays this sound on the slot, as uzdoom checks; decided in `S_Sound`, by each client online |
| `CHANF_LOOP`    | 256  | loop, with the same server looping-channel bookkeeping as `A_PlaySound` |
| `CHANF_LISTENERZ`, `CHANF_MAYBE_LOCAL`, `CHANF_UI`, `CHANF_NOPAUSE` | 8, 16, 32, 64 | passed on with the sound, as `A_PlaySound`'s channel flags are |
| `CHANF_LOOPING` | 4352 | `CHANF_LOOP\|CHANF_NOSTOP` |

`pitch` is honoured through the networked-sound-pitch path (see the sound-pitch feature).
`startTime` has no backend equivalent in this engine and is accepted for signature parity only.

## In-engine hooks

- C++: `DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_StartSound)` in `thingdef/thingdef_codeptr.cpp`
  (mirrors `A_PlaySound`'s loop/non-loop + server-replication model).
- DECORATE decl: `action native A_StartSound(...)` in `wadsrc/static/actors/actor.txt`.
- Constants: `CHANF_*` enum in `wadsrc/static/actors/constants.txt`.

## Verify

`a_startsound_conformance.txt` exercises overlap, explicit channel, NOSTOP, and looping call shapes.
Pack into a pk3 and load; a dropped decl/param resurfaces as a parse error (see README run steps in
`features/a_noisealert`).
