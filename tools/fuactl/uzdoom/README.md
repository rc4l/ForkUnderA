# Checking against UZDoom

These harnesses run the same generated pk3 in a UZDoom build and in ours and compare what each shows. UZDoom is
`FUACTL_UZDOOM`, else `UZDoom-build/Release/uzdoom.exe` beside the repository, built out of tree from a UZDoom
checkout with Visual Studio's cmake (`cmake --build <UZDoom-build> --config Release --parallel 8`). Ours is
`FUACTL_ENGINE`, else the staged build. Run them from `tools/fuactl`.

- `src/uzdoom.mjs` runs UZDoom without activating its window, muted, windowed, on a fresh config in the run's folder:
  `runUzdoom` returns its log, `uzdoomScreenshot` its picture of a map. `fuactl uzdoom --file a.pk3 --map MAP01 --chain "..."`
  does the same from the command line.
- `shots.mjs` takes each engine's picture of a map; ours through `fuactl launch` on a port of its own, quit by port.
- A scenario can run in UZDoom too, as `node scenarios/acsparity.mjs --uzdoom` (on the ACS branches) judges UZDoom's ACS answers.

Four traps the module handles:
- UZDoom's `wait` only delays the rest of its own command string, so `+wait 70 +screenshot +quit` quits at once; the
  chain is one string, `+wait 70;screenshot;wait 35;quit`.
- `+logfile` takes a bare name and writes `log-<name>.txt` in the working directory, so UZDoom runs in the output
  folder; a path makes it print "Could not start log" and write nothing.
- UZDoom stops its game while its window isn't active (`i_pauseinbackground`), and the launcher never activates it,
  so a run stalled whenever Windows called it inactive; every run sets `+i_pauseinbackground 0`.
- A stale `LIB` (a removed MSVC folder) fails PowerShell's Add-Type, so the launcher clears it.

Never puke a script that crashes UZDoom (acsparity's script 2, `DamageActor` on a missing tid): its crash dialog
lands on the user's screen.

# OBJ models

`node uzdoom/objparity.mjs <out.pk3> [--shots]` writes an OBJ axes model (+X STARTAN2, +Y COMPBLUE, +Z BROWN1, hub
FLAT23) and the `objmodel` scenario's prism, seen from a player start (MAP01 south, MAP02 north-east); `--shots` takes
both engines' pictures of each map, to compare by eye. `CULL=1` adds FORCECULLBACKFACES, which shows whether the two
engines wind triangles the same way.

Result for the OBJ port (ab069f5d): orientation, v flip and winding match; the only differences are UZDoom's own
bugs the port fixes (faces before the first usemtl shift its surfaces; faces past four corners are cut).

# GLDEFS light parity

`node uzdoom/lightparity.mjs [--port 7875] [--only MAPxx]` builds one black map per light property, screenshots each in
UZDoom and in the staged build, and compares each lit spot's added brightness over its map's reference spot (exit 0
within 15%, or 0.1 near zero). Spots are the brightest 50 pixels of a picture zone, since UZDoom screenshots at its
own window size and field of view. Add a `CASES` entry per new light property, and run it on the old build first.

| Map | Property | UZDoom | ours | build before |
| --- | --- | --- | --- | --- |
| MAP01 | intensity 0.5 / 1 / 2 on a wall | 0.52 / 1 / 1.95 | 0.50 / 1 / 2.00 | 1 / 1 / 1 |
| MAP02 | dontlightactors on a carried light, sprite brightness | 0.06 | 0.00 | 1.00 |
| MAP03 | dontlightothers, a bare sprite 64 units from the carrier | 0.02 | 0.00 | 0.51 |
| MAP21 | additive 1 white light against a plain one, 0.5 grey wall (added after the texture) | 0.42 | 0.43 | 0.18 |
| MAP22 | red subtractive dip against a white one, lit grey room (`dip` measure: length - colour) | 0.91 | 0.91 | 0.32 |

# Model parity

`node uzdoom/modelparity.mjs [--port 7874] [--only MAP01]` (needs `src/quakemodel.mjs`, from PR #425
on) builds an MD3 map (MAP01) and an MD2 map (MAP02), each with keyframe 0, keyframe 1 and an actor blending
between them, in a full-bright grey room. An ENTER script gives the player a time freezer 17 tics in, so both engines
hold the same blend (both drop the tic fraction while frozen); the blending actor starts on a one-tic state, since a map
thing's first state starts at a random tic the two engines' RNGs need not agree on. Each block is found as a run of
skin-coloured columns between the top eighth (our PAUSE banner) and the status bar, and compared by width over height,
lean, and the blend its height shows (32 + 16t over 32). Launches are shared with `lightparity.mjs` in
`shots.mjs`.

| Map | UZDoom | ours (GPU model path, #425) |
| --- | --- | --- |
| MAP01 MD3, keyframe 1 lean / height ratio | -0.447 / 1.492 | -0.446 / 1.505 |
| MAP01 MD3, blend 17 tics into 35 | 0.475 | 0.484 |
| MAP02 MD2, keyframe 1 lean / blend | -0.447 / 0.475 | -0.446 / 0.484 |
