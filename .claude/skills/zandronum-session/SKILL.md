---
name: zandronum-session
description: Launch and drive a bridge-enabled ForkUnderA dev engine with the fuactl CLI. Use when the user wants to start, run, test, profile, or screenshot the engine — loading a map, measuring frametimes or GPU pass times, comparing GL against Vulkan, reading the UI, or stepping the sim deterministically.
---

# Drive the engine with `fuactl`

`fuactl` (`tools/fuactl/`) is the only supported way to launch and drive a dev engine. It supervises
the instance, allocates the port and auth token, isolates the config, and exposes every bridge RPC.
**Do not launch the engine binary by hand and do not use the old `zandronum` MCP server** — both are
superseded, and the hand-rolled launch wrote to the user's real config file (clobbering their mouse
settings on exit).

Run it as `node src/cli.mjs <command>` from `tools/fuactl` — Node 18+, no `npm install` needed. It is
the same on every OS; the few platform differences are called out where they matter.

## Preconditions

1. **A bridge-enabled build must be staged** where fuactl looks: `dist-windows/forkundera.exe`,
   `build/ForkUnderA.app/Contents/MacOS/forkundera`, or `dist-linux/forkundera`.
   `FUACTL_ENGINE=/abs/path` runs any other binary. Build and stage with the `zandronum-rebuild` skill.

2. **Game data is found from the binary's folder**, which is the engine's working directory; GL
   captures land in its `sweep/`.

## Launch

```bash
cd tools/fuactl
nohup node src/cli.mjs play --map MAP01 --lock --port 7799 --res 1280x720 > play.log 2>&1 &
node src/cli.mjs ready --port 7799 --timeout 120   # returns once it answers
cat .play-session-7799                             # PORT= and TOKEN= of this launch
```

Name the port everywhere: the bare `.play-session` belongs to whichever launch went last, which may be the
user's. `play` refuses a port an engine still answers on (quit it, or launch with `--restart`), so a second
launch can no longer wipe the first one's session file. `quit --port P` stops an instance whose launcher
died and left no token, as `reap --kill` stops every such orphan; never stop engines by process name.

- `play` is the launch to use: Vulkan alone as players run it, freelook, no monsters, cheats verified,
  and the renderers it really got printed (`ok renderers: ...`). `--lock` keeps the engine off the user's
  mouse and keyboard — always on your own launches, never on one the user plays.
- Pick a port of your own (7799, 7796, ...), never the one the user's instance is on.
- For a netgame, `netplay` (see `netplay-testing`) launches a locked server and clients on loopback and
  `netplay --for-user` leaves the first client's input to the user while the others stay locked;
  `netplay --quit` stops the whole set by its tokens.
- Every launch binds its own UDP game port, 11000 + its bridge port mod 4000 (7799 binds 14799), unless it hosts
  or passes `-port`. A client binds 10667 by default, so a Zandronum client run beside fuactl's no longer shares
  a socket with them.
- `--file a.pk3,b.wad` for pwads, `--iwad` for another IWAD (an absolute path works), `--gl` for GL
  alone, `--gl-beside` for the backend beside GL in one process, which only the GL/Vulkan comparisons
  need (`shot` pairs, `fua_*_check`, the live `fua_vulkan 0/1` toggle) and players never run, `--side-by-side`
  for that with its own window (see `fua-side-by-side`), `--webgpu`, `--rt` for ray-traced
  mirrors, which must be on when the scene is uploaded (or set `fua_dg_rtmirrors 1` and run
  `fua_diligent_scene`).
- A `--lock` launch has the focus only when Windows lets it take it, so it launches with its world running
  (`i_pauseinbackground 0`) and presenting (`fua_render_in_background 1`) either way; to test what an
  unfocused window does, set the latter to 0 and `window --activate 0` (`harness-windows-have-focus`).
- A `--lock` launch also starts with dynamic resolution off (`vid_fua_dynres 0`), so its frames are drawn at
  full scale whatever the GPU's load; set it to 1 to see what a player gets.
- `--preset ID [--variant V]` loads a `catalogue/<ID>/addon.json` wad set from the download store
  (`--store DIR`, else `FUACTL_PWADS`, else the engine's own `pwads/` folder). A missing file is named;
  fetch it with `fua_download <file>` in the engine console.
- `.play-session` is written by whichever launch went last. Keep each instance's token yourself.
- It stays up until stopped. Quit by port: `quit --port P [--token T]`, which waits for the port to close. Never `reap --kill` or
  `reap --all` — the registry is shared with other sessions — and never kill by process name
  (`Stop-Process -Name`, `pkill`), which takes the user's instance and other harnesses with it.
- `fua_dg_standalone` matters only beside GL: it defaults on and decides whether GL also draws, so set
  it to 0 there for a GL/Vulkan pair. The Vulkan picture is derived the same way at either value, and a
  side-by-side launch or any `fua_*_check 1` keeps GL drawing by itself. A launch without GL (the
  default) has none, and `shot` there gives Vulkan's half alone and says why.
- GL beside the backend is a comparison rig, not a way to play: its Vulkan window is presented only by
  `LiveFrame`, so a wipe never reaches the screen there (`features/hwrender/README.md`). Reproduce a
  player's report without GL first.

## Drive

Every command takes `--port P --token T`; `--port P` alone takes the token from `.play-session-P`, which
`play` writes, and with neither it uses the last launch's `.play-session`.

| goal | command |
|---|---|
| console command, with its output | `ui exec "<ccmd>"` |
| any RPC | `rpc <cmd> '<jsonArgs>'` — e.g. `rpc player.camera`, `rpc state.actors '{"limit":5000}'` |
| what stands near a point, or ahead of the player | `actors [--near x,y] [--radius R] [--ahead \| --toward YAW] [--cone DEG] [--class REGEX]` |
| a template scenario's map at one of its vantages, with console commands run there | `probe scenarios/x.mjs [--vantage V] [--side S] "<ccmd>" ...` |
| where the camera is; save it as a spot | `here [--save NAME --note TEXT]` (see `fua-capture-spot`) |
| stand at a saved spot | `goto --spot NAME` |
| does a render cvar change the picture | `ab <cvar> <a> <b> --spot NAME [--box x0,y0,x1,y1]` |
| matched GL/Vulkan pair, sim frozen | `shot <tag> --spot NAME` or `--at x,y,z --face yaw,pitch`; `--same-frame` takes both from one engine frame (an ANIMDEFS texture animates on the clock even paused); a launch with no GL frame (Vulkan alone, or GL beside at `fua_dg_standalone 1`) gives Vulkan's half alone and says why |
| the engine's windows as Windows sees them, and what one really shows | `window [--shot gl\|vk --out FILE]`: minimized, not responding, position; the shot comes from the compositor, not GL's own buffer |
| what the crosshair is on, and the mesh piece drawing it | `look --at x,y,z --face yaw,pitch`; `--pixel x,y --size WxH` aims through a capture's pixel, from the live camera when no `--spot`/`--at` is given |
| what a linedef is, or every line near a point | `line <index>` or `line --at X,Y --radius R` |
| a map's sectors, lines, specials, things and ACS source, from its files in any format | `mapdata <MAP> --preset ID [--sector N] [--line N] [--special N] [--tag N] [--thing TYPE] [--scripts REGEX]` (9080 is a SkyViewpoint) |
| does a region move over time (warp, scroller), Vulkan and GL | `motion --spot NAME --box x0,y0,x1,y1 [--interval MS]` |
| compare two captures | `png --diff a b`, `png --boxdiff a b x0,y0,x1,y1 [block]`, `png --diffimg a b out` |
| a box's brightness and channel means, several captures | `png --range a b ... [x0 y0 x1 y1]` |
| lines wearing a texture (any tier, by prefix) | `lines --texture NAME` |
| frametime distribution (p50/p95/p99) | `capture [--frames N]`; it says so when the median sits on a display refresh, where presentation and not the frame sets the pace |
| judder, freezes or "choppy" the player sees: is Windows showing the frames at all | `present [--seconds N] [--port P]` first: PresentMon as administrator (a UAC prompt the user approves), each app's frames handed over and shown a second and the compositor's own updates, which run near the refresh rate on a healthy desktop while anything moves; `--port` records the instance's frames alongside, second by second. The 2026-10-13 MAP14 judder was the compositor updating 4 times a second for every app, cured by a restart, after a day spent on engine frame times |
| the view stepping back or lurching while walking (rubber-banding) | `walkcheck --port P [--runs N] [--spot S]`: W held as a player holds it, each run traced, exit 1 on any back step or lurch |
| a fault tied to alt-tab, without moving anything on the desktop | `window --activate 0\|1 --port P`: the instance told it was switched away or back |
| a recording's frames a second and what its slow seconds spent on | `recap NAME` (or `record ... --recap`); slow frames at a steady beat are a timer, which `sample --stacks` names |
| the game thread: traces, seams, HUD and copy held under both schedules | `fixture check\|seamcheck\|hudcheck\|copycheck\|pictures` (the `game-thread` skill) |
| every frame of a running instance: length, phases, mouse input, focus | `trace --port P [--seconds N]`: frame lengths apart from tic waits, what the slow frames spent more on, where it waited paused or unfocused, mouse messages and gaps; the CSV is kept |
| what reaches the SCREEN of one window, by its pixels | `screen --port P [--seconds N] [--orbit DEG]`: new images shown and stalls a second from the compositor's output (DXGI Desktop Duplication), the aim orbiting via `fua_debug_orbit`, with the engine's frames lined up for how old each image was when shown. It reads the compositor, so a stalling one reads as the game's judder (check `present`), and the window must be on top. vid_fps counts frames drawn, not shown |
| frame rate under GL alone, the backend beside GL, and the backend alone | `fps --map M [--iwad W] [--file ...] [--res WxH] [--modes gl,beside,vulkan] [--engine PATH]`: one locked launch per mode in turn, warmed, three rounds, and a shot of what each window shows |
| what a setting costs | `abperf "<setting>" "<setting>" ... [--rounds N] [--spot S \| --at --face]`: p50/p99 under each console setting inside one session, the others' cvars put back before each, judged against the first within each round; separate launches drift 30% |
| what a frame costs at a view (Vulkan) | `costs --port P [--spot S \| --at --face]`: frame p50/p99, GPU and submit time (`fua_diligent_bench`), the world passes and phases, the walk's clipping, batches and sprites; the step-0 table of `docs/hwrender-performance-scope.md` is these columns |
| any timing, count or memory reading the engine keeps, on any platform | `stats [PREFIX]`, `hitches`, `record`, `flight`, `memsnap`/`memdiff`, `soak`, `perfgate` (`features/telemetry`; the `perf-telemetry` skill says which to reach for) |
| GPU ms per pass | `stats gpu/` on the backend (duration queries); `gl-timers` for GL (check `renderer-info` first; no timer queries reads as zeros) |
| where the main thread's time goes | `sample --port P [--seconds N] [--stacks] [--table]`: on Windows the engine samples itself; `--stacks` adds each function's inclusive share, which is how a frame waiting in `AcquireNextImage` or the walk's share shows; `--table` prints it as text; `--thread game` samples the thread running tics instead |
| pause / step the sim | `rpc sim.pause` · `rpc sim.step '{"tics":1}'` |
| a fault that shows only mid-transition (a crouch, a lift) | `steps <tag> --hold +crouch [--count N] [--spot NAME]`: a GL/Vulkan pair and the eye height each tic |

Checks against GL, each deriving beside it every frame and comparing bit for bit, with a `*_report` that
prints what differed: `fua_vis_check 1` / `fua_vis_report` (the walks, the sky, portal views, and the
player walk's own time), `fua_sprite_check 1` / `fua_sprite_report` (sprites and models),
`fua_psprite_check 1` / `fua_psprite_report` (the weapon), `fua_shading_check 1` / `fua_shading_report`.
Any check on keeps GL drawing (`ComputeFrameRoute`), so a frame time taken with one on includes GL's.

Console diagnostics worth knowing (run them with `ui exec`): `fua_mesh_blank` (empties the level mesh, so whatever still draws the world is not reading it), `fua_shading_check 1` then `fua_shading_report` (every surface the light model shades, compared bit for bit against GL's own `gl_SetColor`/`gl_SetFog`; read the count from the engine log), `fua_gl_viewstate` (the next main view's GL viewport, framebuffer, vertex array and GL errors before and after the world; a window showing 2D but no world), `fua_2d_list` (the 2D draws covering a quarter of the screen or more), `fua_dg_blendorder [z]` (the translucent
pass in draw order with plane layers, or every draw spanning height z), `fua_models` (model draws and
which walk drew them), `gl_portalinfo` (the portals GL ran last frame, a stacked view named by its plane
and displacement), `fua_line_mesh <line>` (what the bake holds for a line), `fua_sector_at x y [x y ...]`
(the subsector, sectors and plane heights under each point, for walking a ray past where `look`'s trace
stops at a sky), `fua_dg_dynstats` (world passes last frame by kind and CPU time, and why a patch rebuilt
the scene). `fua_dg_planemirrors 0` turns the floor reflections off (one is rendered per reflective plane), as
`fua_dg_stackportals 0` does the stacked views. A template scenario's own map at one of its vantages, with
console commands run there, is `probe scenarios/<name>.mjs [--side S] [<command> ...]`.

`png --boxdiff` compares in 16-pixel block means: the renderers frame the world a fraction of a pixel
apart, which reads ~13 per pixel on a bright tiled floor while both are right.

## Captures lie under these conditions

- **Another engine window on screen.** GL (alone or beside) reads its picture back from its window, and
  where another window covers it the pixels are undefined. `shot` warns when more than one engine is running.
- **Vulkan on, some skybox maps.** GL captures on the horizon template's MAP02 came back stale, black
  or scrambled run to run in old and new builds alike; take GL references from a `--gl` launch there.
- **A minimized engine window.** GL reads back blank; `shot` asks `fua_window_restore` and tries again.
- **A pwad map without its wads.** The engine sits in the IWAD's MAP01 and measures perfectly.
- **A paused sim** does not rebuild dynamic geometry or lights.
- **Animated lights between captures.** Two shots of a flickering map differ by ~0.4 with nothing
  changed; `ab` captures both values inside ONE pause and reports the repeat's diff as the noise floor.
- Before blaming a change for an oddity, reproduce it on the OLD binary under identical conditions.

## Regression scenarios

`scenarios/*.mjs` drive live engines and assert. To isolate a feature on a generated map and make it a
GL-vs-Vulkan regression test, use the `template-maps` skill (`src/udmf.mjs` + `src/template.mjs`;
`scenarios/horizon.mjs` is the worked example).

## Where things are

| what | path |
|---|---|
| CLI | `tools/fuactl/src/cli.mjs` (`--help` lists every command) |
| saved vantages | `tools/fuactl/spots.json` |
| addon wad sets for `--preset` | `catalogue/<id>/addon.json` |
| full RPC reference | `src/zandronum/src/features/mcp-bridge/README.md` |
| engine side | `src/zandronum/src/mcp_*.{h,cpp}` |
