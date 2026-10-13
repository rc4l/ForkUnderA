# features/dynlights

The dynamic light actors as game data, which both renderers light by. They were GL's (`gl/dynlights/`),
so every module that lit a sprite or a wall included GL's headers to see a light.

- `dynlight.h`, `a_dynlight.cpp`: `ADynamicLight` and the Vavoom lights (point, pulse, flicker, sector and
  colour lights, as GLDEFS and DECORATE spawn them), what each touches (`FLightNode` lists on sidedefs and
  subsectors, rebuilt as the light moves), and `listlights`. Moved unchanged from `gl/dynlights/`, apart from
  three vectors GL's flats once cached in each light and no longer read.
- `cycler.h`, `cycler.cpp`: `FCycler`, the value a pulsing light (and a GL texture shader) cycles through,
  moved from `gl/utility/`.

GL keeps what is GL's: GLDEFS' parsing (`gl/dynlights/gl_dynlight.cpp`), the light data it uploads
(`FDynLightData`, `gl_GetLight`, `gl_lightbuffer`) and `gl_SetDynSpriteLight`. The backend reads the lights'
links itself (`features/hwrender/diligent/dglights.cpp`, `features/sprites/spritebuild.cpp`).

## In-engine hooks

- `gl/dynlights/gl_dynlight.h`: includes `dynlight.h` in place of the declarations it held.
- `gl/shaders/gl_texshader.h`: includes `cycler.h`.
- `g_game.cpp`, `p_setup.cpp`: the light actors, through `gl/dynlights/gl_dynlight.h`.
- `CMakeLists.txt`: `a_dynlight.cpp` uses `IMPLEMENT_CLASS`, so it is listed in `add_executable` before
  `zzautozend.cpp`.
