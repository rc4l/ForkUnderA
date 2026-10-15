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
- `cycler.h`, `cycler.cpp`: `FCycler`, the value a pulsing light (and a GL texture shader) cycles through,
  moved from `gl/utility/`.

- `pointlight.h`/`.cpp`: the dynamic light on a point or an actor as GL's `gl_SetDynSpriteLight` sums it
  (`PointLight`, `ActorLight`, `ActorDynamicLight`, `PointDynamicLight`), for sprites, particles, the weapon
  and decals; moved from `features/sprites/spritebuild.cpp`, so a decal reads it without the sprites.

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
