---
name: fua-capture-spot
description: Record the user's live vantage in ForkUnderA — position, aim, map, wad set and renderer cvars — as a named, reusable spot. Use when they say "capture this", "save where I'm standing", "look at what I'm looking at", or point at something on screen.
---

# Capture the user's vantage

When the user points at something on screen, the thing to preserve is not four numbers — it is
everything that has to be true for that picture to come back. Record it once, by name, and every later
capture and measurement can be aimed at the same place.

Run from `tools/fuactl` as `node src/cli.mjs …`. You need the port and token of the instance the user
is looking at: `.play-session` (a two-line `KEY=VALUE` file) holds the last launch's, or ask.

## Capture it

```bash
cd tools/fuactl
P=$(grep -oE '^PORT=[0-9]+' .play-session | cut -d= -f2)
T=$(grep -oE '^TOKEN=[a-f0-9]+' .play-session | cut -d= -f2)
node src/cli.mjs here --port "$P" --token "$T" \
  --save <short-kebab-name> --note "what is on screen and why it matters"
```

That writes one record into `tools/fuactl/spots.json` holding the camera at full precision
(`x, y, z, angle, pitch`), the `map`, the `loaded` wad set — IWAD plus every PWAD **with its md5**,
read from the engine's own `wads` command rather than from what the launch intended — and the
`renderer` cvars that change the picture (`fua_vulkan`, `fua_dg_standalone`, `fua_dg_embed`,
`fua_dg_skyportal`, `gl_wallmesh`).

**Capture before touching anything.** Do not warp, unpause, toggle a cvar or rebuild first: the
vantage is whatever they are looking at right now, and every one of those actions can change it.

To queue a problem for fixing rather than just save the view, `queue add "<description>" --port P`
saves the spot and the description together (`report-queue`).

**Name it for what it shows**, not for the bug you think it is — `skyportal-city`, not `skyfix2`. The
note is what makes it findable in six weeks; write the symptom, not the diagnosis.

## Use it

```bash
node src/cli.mjs shot <tag> --spot <name> --port <P> --token <T>
```

`--spot` replays the exact camera, so a pair taken today is comparable with one taken before the
change. `goto --spot <name>` stands there without capturing, and `ab <cvar> <a> <b> --spot <name>`
compares a render switch there inside one paused frame. Before believing a comparison, check the
spot's `loaded` and `renderer` against the instance you are shooting in — a matching camera on a
different wad set is a different picture wearing the same name.

## Traps that invalidate a spot silently

- **A pwad map needs its wads.** Without them the map does not exist, the engine sits in the IWAD's
  MAP01, and it renders perfectly — so every number taken there looks valid and is worthless. Relaunch
  with the same set the spot's `loaded` lists, via `--preset <id>` or `--file`; see `fua-side-by-side`.
- **Never shoot from their instance.** Launch your own `--lock` instance on your own port with the
  same wads, set `fua_dg_standalone 0`, and shoot `--spot <name>` there; leave theirs alone to look at.
- **Close other engine windows first.** A GL capture is read back out of the window, and under another
  window it comes back black or scrambled — which looks like the renderer, not the tool.
