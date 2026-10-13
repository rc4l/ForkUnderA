# features/rendersettings

The level's render settings that GL and the backend both read. They were GL's globals, so every backend
module that lit a surface or walked a view included GL's headers to reach them. They live here now, and
GL's headers include this module.

- `colormap.h`: `FColormap`, a surface's light and fade colour as both renderers light it, the special
  colormap numbers, and `gl_fixedcolormap`, the colormap a powerup or a script fixes the view in.
- `rendersettings.h`: `glset` (MAPINFO's light mode, coloured sprite lighting, bright fog, pixel stretch),
  the fog densities, the outside fog's colour, black fog's distance table and the sky's fog strength
  (`gl_SetFogParams`), the view area (`area_t`, `in_area`) and map sections (`currentmapsection`), the
  frame clock (`gl_frameMS`), the weapon flash's extra light (`getExtraLight`, `gl_weaponlight`), and the
  light helpers `gl_ClampLight`, `gl_isBlack` and `gl_isWhite`.
- `RenderShadingEnv(inSkybox)`: what the light model (`levelmesh/computation/shading_compute`) reads besides
  the surface, with the server's forced video defaults applied as GL applies them. GL's `gl_ShadingEnv` is
  this with GL's skybox flag; the backend asks it directly.
- The settings' cvars: `gl_light_ambient`, `gl_distfog` (which rebuilds the distance table), `gl_fogmode` and
  `gl_weaponlight`.

The names are GL's, kept so GL's code reads them unchanged.

## In-engine hooks

- `gl/renderer/gl_colormap.h`, `gl/data/gl_data.h`, `gl/renderer/gl_lightdata.h`, `gl/renderer/gl_renderer.h`:
  include this module in place of the declarations they held.
- `gl/renderer/gl_lightdata.cpp` `gl_ShadingEnv`: `RenderShadingEnv(GLPortal::inskybox)`.
- `gl/data/gl_data.cpp`: still parses MAPINFO's `gl_renderer` options into `glset` at level start, and
  `features/frame/levelrenderer.cpp` `SetFogParams` still calls `gl_SetFogParams`.
