# features/dynlights

The dynamic light actors as game data, which both renderers light by. They were GL's (`gl/dynlights/`),
so every module that lit a sprite or a wall included GL's headers to see a light.

- `dynlight.h`, `a_dynlight.cpp`: `ADynamicLight` and the Vavoom lights (point, pulse, flicker, sector and
  colour lights, as GLDEFS and DECORATE spawn them), what each touches (`FLightNode` lists on sidedefs and
  subsectors, rebuilt as the light moves), and `listlights`. Moved from `gl/dynlights/`, apart from three
  vectors GL's flats once cached in each light and no longer read, and two changes to relinking, which was
  half of a Sunder MAP14 tic:
  - A pulsing light links at its largest radius, as flickering ones did and later GZDoom does, so it relinks
    only when it moves; it had relinked every tic its size changed (18 lights touching 77 subsectors each a
    tic there). Both renderers light by the current radius (`GetRadius`) and stop at it, so a pulse lights
    what it did, except a surface its larger flood reaches round a corner.
  - A relink finds a light's existing node for a target in an index (`computation/nodeindex_compute`), not
    by walking the light's list for every target it touches, which made a relink of n targets n*n.
  The tic there went from 1.09 to 0.53 ms.
- A light keeps its subsector when `UpdateLocation` finds it at the very point it last looked it up for
  (`computation/lightspot_compute`), instead of a BSP lookup: 81% of ~2,000 updates a tic on Complex Doom's crowd.
- A moving light keeps the surfaces its last relink found until it has moved more than `fua_light_relink_slack`
  map units (default 16, `0` relinks every move) from where that relink put it (`computation/relinkslack_compute`);
  its position and so its shading still follow it every tic. The surfaces decide which a light may touch (GL's walls
  and flats, the backend's light links), and a few units of movement change them at most at the faint edge of its
  reach: recorded on both renderers in an open room and through a doorway, no visible difference even at 64.
  On Complex Doom's crowd, 27% fewer relinks, `lights/link` 1.98 -> 1.57 ms a tic; `lights/relinks_kept` counts them.
- `computation/shouldlight_compute`: UZDoom's `ShouldLightActor`, which of a light's GLDEFS flags (`dontlightself`,
  `dontlightothers`, `dontlightactors`) let it light a given actor or particle; `features/levelmesh/pointlight` asks it
  for sprites, models and particles, never for decals, which UZDoom lights as their wall.
- `cycler.h`, `cycler.cpp`: `FCycler`, the value a pulsing light (and a GL texture shader) cycles through,
  moved from `gl/utility/`.

- The renderer's point lights, which read the render copy's lights, are `features/levelmesh/pointlight`, so this
  game-side feature never includes the copy that reads it.

The light settings both renderers read are `features/gldefs/lightsettings.cpp`, since changing one remakes the
attached lights there; `dynlight.h` declares them.

GLDEFS' light definitions and their attaching to actors are `features/gldefs`. GL keeps what is GL's: the
light data it uploads (`FDynLightData`, `gl_GetLight`, `gl_lightbuffer`) and `gl_SetDynSpriteLight`. The backend reads the lights'
links itself (`features/hwrender/diligent/dglights.cpp`, `features/sprites/spritebuild.cpp`).

## In-engine hooks

- `gl/dynlights/gl_dynlight.h`: includes `dynlight.h` in place of the declarations it held.
- `gl/shaders/gl_texshader.h`: includes `cycler.h`.
- `g_game.cpp`, `p_setup.cpp`: the light actors, through `dynlight.h`.
- `CMakeLists.txt`: `a_dynlight.cpp` uses `IMPLEMENT_CLASS`, so it is listed in `add_executable` before
  `zzautozend.cpp`.
