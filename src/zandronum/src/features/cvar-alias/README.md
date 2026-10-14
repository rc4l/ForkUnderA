# cvar-alias

A cvar known by a second name as well as its own: setting, reading, `toggle`, binds, menus, config lines
and mods' `GetCVar` all reach the one cvar, which keeps its own name in what the console prints and in the
config it saves. The table is `computation/cvaralias_compute.cpp`, one line per alias.

- `fua_vid_fps` is `vid_fps`.
- GL's settings that both renderers read are renamed from `gl_` to `r_`, and their old names are aliases:
  the dynamic lights (`r_lights`, `r_lights_size`, ...), light mode, fog, weapon light and night vision,
  sprite clipping, billboards, fuzz and particle styles, models, mirrors and sky boxes, seamless walls,
  and texture filtering, hires textures and hqresize. A config written by an older build migrates on its
  first save, since the old names are read through the aliases and the new ones written; an older build
  does not know the new names.

`r_` because these settings describe the picture, not the API that draws it: GL (Legacy) and Vulkan both
obey them, as they already obey Doom's own `r_` settings here (`r_particles`, `r_mirror_recursions`). `dg_` and `fua_dg_` name the Vulkan backend's implementation (Diligent) and stay
for its own switches; settings only GL reads keep `gl_` (`gl_wallmesh`, `gl_texture_format`,
`gl_render_*`, `gl_vid_multisample`, ...). The software renderer's look-alikes (`r_skyboxes`,
`r_drawmirrors`, `r_fogboundary`) are its own and unrelated.

## In-engine hooks

- `c_cvars.cpp` `FindCVar` and `FindCVarSub`: a name no cvar has is looked up in the alias table
  (`ComputeCVarAliasTarget`) and answers with its target. The config reader (`FGameConfigFile::ReadCVars`)
  finds cvars through `FindCVar`, which is how old configs migrate.
