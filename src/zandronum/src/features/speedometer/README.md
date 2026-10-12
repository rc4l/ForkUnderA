# features/speedometer — on-screen speed readout

Shows the displayed player's speed as `583 u/s` (map units per second), in the chat font (`SmallFont`),
at a position the player picks with two sliders. Horizontal (ground) speed by default; one toggle adds
vertical speed for full 3D speed.

Client-side display only: it reads the actor's replicated velocity and changes nothing, so it has no
network, savegame or demo impact. A dedicated server never reaches `D_Display`, so it never runs there.

## Files

| file | role |
|------|------|
| `speedometer.h/.cpp` | Engine glue: the CVARs and `zx::speedometer::Draw`, which reads `mo->velx/vely/velz` and draws with `HUD_DrawText`. |
| `computation/speedometer_compute.{h,cpp,_test.cpp}` | Pure math: speed from velocity, the rounded readout value, percent and colour clamping, and the text position. 100% unit coverage. |

## Cvars

All `CVAR_ARCHIVE | CVAR_GLOBALCONFIG`, menu: Options → FUA Options → Speedometer.

- `cl_fua_speedometer` (bool, default off) — show the readout.
- `cl_fua_speedometer_vertical` (bool, default off) — include vertical speed.
- `cl_fua_speedometer_color` (int, default `CR_WHITE`) — text colour, the stock `TextColors` list; out-of-range values draw white.
- `cl_fua_speedometer_x` / `cl_fua_speedometer_y` (float 0–100, default 50 / 60) — position as a percentage of the screen.

## How it is placed and drawn

- The readout sits in a fixed box sized for `0000 u/s`, placed at the slider percentage of the space the box
  leaves free, so 0 and 100 put it flush against an edge and it can never leave the screen.
- The text is right-aligned in that box, so the `u/s` suffix holds still while the chat font's proportional
  digits change width.
- It draws through `HUD_DrawText`, so it scales exactly like chat (`con_scaletext`, `con_virtualwidth/height`).
  With text scaling off, 0 and 100 are the screen edges. With it on, they are the edges of the same area chat
  and the fullscreen HUD text use, which on a 1920×1080 window is a centred 4:3 region, not the whole screen.

## In-place anchors (the only edits outside this feature)

1. `d_main.cpp` — include, plus in `D_Display`'s `if ( viewactive )` block: the display player passed to
   `HUD_Render` is hoisted into `ulDisplayPlayer`, and `zx::speedometer::Draw( ulDisplayPlayer )` follows
   `HUD_Render`. That spot is `GS_LEVEL` only, skipped on the full-screen automap, drawn under the console
   and menu, and follows a spectated player the same way Zandronum's HUD does.
2. `CMakeLists.txt` — `features/speedometer/speedometer.cpp` in the zdoom source list (no `IMPLEMENT_CLASS`,
   so ordering is irrelevant). The `*_compute.cpp` is picked up by the existing glob.
3. `wadsrc/static/menudef.txt` — the `Speedometer` row in `FUAOptions` and the `FUASpeedometerOptions` menu.

## Known trade-offs

- Your own speed is exact: client prediction has already advanced `mo->vel*` to the current tic by draw time.
- When spectating another player, their velocity comes from the server's `MovePlayer` updates
  (`cl_main.cpp`, `MovePlayer::Execute`): an axis the server did not send reads 0, and the value goes stale
  while that player is out of view, so the reading can be briefly off.
- Hidden when the HUD is hidden (`screenblocks` 12), like the rest of Zandronum's HUD text.
