# features/crosshair — Crosshair Options

Options → HUD Options → **Crosshair Options**. One home for every crosshair setting, and the place where
anything drawn at or next to the crosshair is configured.

The stock crosshair rows moved here from HUD Options unchanged: `crosshair`, `crosshairforce`,
`crosshairgrow`, `crosshaircolor`, `crosshairhealth` and `crosshairscale`. `crosshairscale` is now labelled
"Scale crosshair with resolution" so it doesn't read like the size slider.

Zandronum's "Identify player level" and "Identify monster level" (`cl_identifytarget`, `cl_identifymonsters`),
which had to be cycled through, are replaced here by a list of on/off Target info toggles. The monster drop-item
list and the player's skin name were dropped. Values saved under the old cvars are ignored; the new defaults
match the old ones.

Client-side display only: nothing here touches aiming, savegames or demos. The one network-facing piece is a
server flag, which rides in `zadmflags` and so reaches clients through the existing flag sync.

## Files

| file | role |
|------|------|
| `crosshair.h/.cpp` | Engine glue: the CVARs, `GetRect`, the view trace and view sweep, and the per-frame draw of the text beside the crosshair. |
| `computation/crosshair_compute.{h,cpp,_test.cpp}` | Pure math for the crosshair: the clamps and `ComputeCrosshairRect`. 100% unit coverage. |
| `computation/targetinfo_compute.{h,cpp,_test.cpp}` | Pure logic for the text: which identify lines a target gets, scale, anchor, stacking, fade, and the actor-name and health rules. 100% unit coverage. |
| `computation/viewsweep_compute.{h,cpp,_test.cpp}` | Pure logic for the view sweep: the ray-box entry and exit, which actors it looks at, their kind, and the nearest hit. 100% unit coverage. |
| `computation/telemeter_compute.{h,cpp,_test.cpp}` | Pure logic for the telemeter: when it runs, which kinds it measures, the sweep's range, the body-to-body gap and the `384 u` text. 100% unit coverage. |

## Cvars

The client cvars are all `CVAR_ARCHIVE | CVAR_GLOBALCONFIG` and clamped on set.

- `cl_fua_crosshair_scale` (float 0.25–4, default 1) multiplies the crosshair's size, on top of `crosshairscale` and `crosshairgrow`.
- `cl_fua_crosshair_x` and `cl_fua_crosshair_y` (float −50 to 50, default 0) move the crosshair by a percentage of the 3D view from its centre. Positive is right or down, and ±50 reaches the view's edge.
- Target info toggles (bool): `cl_fua_identify_players` (default on) and `cl_fua_identify_monsters` (off) pick what is identified, showing its name. `cl_fua_identify_health`, `_weapon` and `_class` (off) add those lines for allies only (health also for friendly monsters). `cl_fua_identify_allyenemy` (on) adds the Ally / Enemy label. `cl_fua_identify_ghost` (off) marks `+GHOST` monsters, Heretic's ghosts that some attacks pass through. `ComputeIdentifyLines` applies them.
- `cl_fua_identify_position` (0 bottom of screen, the default; 1 next to the crosshair) sets where Zandronum's identify text goes.
- `cl_fua_identify_actorname` (0 off, the default; 1 DECORATE class; 2 tag) adds a line naming the item, decoration, projectile or corpse in the crosshair. Living monsters and players are left to the identify rows, so a name never shows twice and turning Identify monsters off hides theirs.
- `cl_fua_xhairinfo_textsize` (float 0.5–4, default 1) sizes the text beside the crosshair as a multiple of the HUD text scale; the gap, line spacing and offsets grow with it. The bottom-of-screen message keeps the stock HUD message size.
- `cl_fua_telemeter` (bool, default off) shows how far away the thing in the crosshair is. `cl_fua_telemeter_decorations`, `_monsters`, `_players` and `_projectiles` (default on) pick what it measures; the rest, and items, are looked past.
- `cl_fua_xhairinfo_{name,info,actor,telemeter}_{x,y}` (int −200 to 200, default 0) nudge each line from its stacked place, in HUD-text pixels.
- `sv_fua_noidentifyactors` is a server flag: `zadmflags` bit 29, `ZADF_FUA_NO_IDENTIFY_ACTORS`. It hides the actor-name line for everyone. `sv_noidentifytarget` hides it too, because servers that set that flag (Whodunit, GvH and others) would otherwise leak what it protects.

## How it is drawn

- `ComputeCrosshairRect` runs the stock size maths on the same 64-bit fixed-point values and with the same `Fixed64Mul` as `FixedMul`. At scale 1 and offset 0 it is bit-identical to the old inline code, and a test pins that against a copy of the old formula.
- The offsets are relative to the 3D view, not the screen. With the status bar showing (`screenblocks` 10), the centre is the middle of the area above the bar, as it always was.
- The offset moves the reticle only. Shots and every scan still use the view's centre.

### Text beside the crosshair

- `HUD_DrawTargetName` builds the same text as before, split into the name and the lines below it. At the bottom of the screen it posts the same `'PNAM'` message, byte for byte. Next to the crosshair, the four lines (name, details, actor name, telemeter) are drawn every frame by `DrawBesideCrosshair`. All but the telemeter linger 2 s and fade over 0.35 s, timed in `gametic` like the message. The strings are cached, never an actor pointer, so a target that dies or despawns can't dangle.
- Stacking: the lines start just right of the crosshair's drawn rectangle, with the first line centred on it. They follow the crosshair's offset and size. Hidden lines take no rows, and each line's X/Y offsets apply on top. A new line is one more row in `g_Beside`.
- Text scale follows the HUD text, like chat, times the text size slider: the canvas is the screen divided by `SCREENHEIGHT / HUD_GetHeight() * cl_fua_xhairinfo_textsize`. It is drawn with `DTA_KeepRatio`, so virtual-to-real is a plain scale and the real-pixel crosshair converts exactly.
- The actor name and the telemeter share one **view sweep**, not the engine trace. `Trace` skips actors with no flags (Doom's candlesticks and dead things) and walks the blockmap, which projectiles are never linked into. The sweep:
  - traces walls, floors and ceilings only (`TraceFromView` with mask 0), to find where the view stops;
  - walks every sector's actor list, and keeps each actor whose attack box the view line enters before that point (`ComputeRayBoxEntry`). A box around the eye doesn't count. A widened attack box also needs the identify trace's line-of-sight guard;
  - looks past anything the renderer wouldn't draw (sprite 0, `MF_NOSECTOR`, `RF_INVISIBLE`, an invisible render style or alpha, or not visible to this player), so invisible bridges and hidden actors are never named or measured;
  - looks past actors outside the blockmap unless they are projectiles, which keeps bullet puffs, blood, teleport fog and the icons over players' heads out. A few flat floor decorations (Doom's ColonGibs, SmallBloodPool and BrainStem) are lost with them;
  - looks past the viewer's own projectiles, which fly down the crosshair;
  - runs at most once a tic (`ComputeSweepDue`), because aim only changes once a tic.
- The actor name takes the nearest hit within 2048 units. An actor with `STFL_DONTIDENTIFYTARGET` shows nothing. So does a living monster or player (`ComputeActorNameCoversTarget`): the line is dropped at once rather than naming what is behind it. Corpses lose `MF_SHOOTABLE`, which the identify scan needs, so the actor name covers them.

### Telemeter

- It shows the gap in map units along the view line, as `384 u`. The gap runs from where the line leaves the viewer's own collision box to where it enters the target's (`ComputeRayBoxExit`, `ComputeTelemeterGap`), so touching reads `0 u`. Both boxes are the actors' live radius and height, which covers mods that resize the player and crouching. If a mod widens the target's shootable box and the line only crosses that, the gap is measured to that box instead. It measures the nearest hit within 8192 units, as far as hitscan weapons reach, whose kind is switched on: player (any player body), monster (corpses included), projectile, then decoration. Items are never measured.
- It shows only while the crosshair is on something: no linger, no fade.
- On `sv_noidentifytarget` servers the kind toggles are ignored and everything is measured, so the toggles can't tell a disguised player from a real prop.
- The health in the details line is only shown when this machine is actually sent it. Offline it always is. Online, a player's health is only shown when `SERVER_IsPlayerAllowedToKnowHealth` allows it, and a friendly monster's never is, because the server doesn't sync monster health.

## In-place anchors (the only edits outside this feature)

1. `g_shared/shared_sbar.cpp`: an include, and in `DBaseStatusBar::DrawCrosshair` the inline `size`/`w`/`h`
   maths and the centre point are replaced by `zx::crosshair::GetRect (CrosshairImage, CrosshairSize)`. Every
   gate before it is untouched. The alternative HUD draws through the same function, so it gets the sliders too.
2. `g_shared/st_hud.cpp`: `HUD_ScanForTarget` calls `zx::crosshair::TraceFromView` with its old `MF_SHOOTABLE`
   mask and 2048-unit range (`kIdentifyRange`). `HUD_DrawTargetName` is split into `HUD_CanIdentifyFrom` (the shared checks), `HUD_BuildTargetText`
   (now driven by the Target info toggles through `ComputeIdentifyLines`, filling a `TargetText`) and the draw. The
   health line also checks `HUD_IsTargetHealthKnown`. The `cl_identifytarget`/`cl_identifymonsters` cvars and their
   level enums are gone.
3. `doomdef.h` / `d_main.cpp`: the `ZADF_FUA_NO_IDENTIFY_ACTORS` bit and its `sv_fua_noidentifyactors` flag cvar.
   `features/server-browser/computation/flaghelp_compute.cpp` gives it a tooltip.
4. `CMakeLists.txt`: `features/crosshair/crosshair.cpp` is in the zdoom source list. It has no `IMPLEMENT_CLASS`,
   so its position doesn't matter. The `*_compute.cpp` files are picked up by the existing glob.
5. `wadsrc/static/menudef.txt`: the `Crosshair Options` row in `HUDOptions`, the `FUACrosshairOptions` menu (with
   its Telemeter rows) and its two value lists, and the "Allow actor identification" server option. `wadsrc/static/menudef.za`: the two
   identify rows and their `ZA_Identify*Levels` value lists are removed.
