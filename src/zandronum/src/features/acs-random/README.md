# features/acs-random

Named random streams for ACS, `fua_` functions 24070-24072. Modder reference:
[`docs/modding/acs/Random_streams.md`](../../../../../docs/modding/acs/Random_streams.md).

- Each stream is an engine `FRandom` keyed `fua.acs.play:<name>`, or `fua.acs.client:<name>` for a
  CLIENTSIDE script (`ComputeStreamKey`), so no name a mod gives can be the engine's, DECORATE's or the
  other namespace's. A key whose CRC another stream already has is refused with one warning.
- A stream is made on first use and seeded from the current `rngseed`, as `FRandom::StaticClearRandom`
  seeds every stream at a level's start, so a recording and its playback make the same sequence.
- Draws take `GenRand32()`, never the 0-255 `operator()`, which `compat_oldrandom` routes to `P_Random`.
- Savegames name every stream in chunk `fRnd`, read before `FRandom::StaticReadRNGState`, which only
  restores streams that exist; a freshly started engine would otherwise start a saved stream over.
- A script's namespace is taken when it starts (`DLevelScript::fua_clientside`), as `activeBehavior` later
  follows it into a library's functions, and found again by its number on load.

## Hooks

- `p_acs.cpp`: the three functions, `fua_clientside` set in the script's constructor and on load.
- `p_acs.h`: `DLevelScript::fua_clientside`.
- `g_game.cpp`: the chunk written beside the RNG state and read before it.
- `features/acs-argcount`: each function's minimum argument count.

## Tests

- `computation/random_compute_test.cpp`: keys and their namespaces, both ranges at their ends and their
  widest, and the savegame chunk with damaged inputs.
- `fuactl scenarios acsrandom`: a seeded stream repeating, a CLIENTSIDE script's draws leaving the gameplay
  stream alone, named draws never moving `Random()` (with `compat_oldrandom` on too), fixed draws in range,
  a save loaded by a freshly started engine, and a demo played back. It fails with the chunk's read taken
  out, with draws taken from `pr_acs`, and with clientside scripts in the gameplay namespace.
- `fuactl scenarios netacsrandom`: a seeded clientside stream giving the player's client, an observer and a
  late joiner the same numbers, and the server's seeded stream repeating.
