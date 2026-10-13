# features/levelprep

A loaded level readied for either renderer, moved from GL's `gl/data/gl_setup.cpp` and
`gl/data/gl_data.cpp`. Both renderers walk and draw what it builds, so it ran inside
`gl_PreprocessLevel` before, and a launch without GL still needed GL's file to set a level up.

`PrepareLevel` builds, once a level is loaded:

- map sections (`SetMapSections`), which the walks are confined to;
- each sector's subsectors and the "hacked" flag spread across subsectors that render another sector;
- transparent door hacks (`PrepareTransparentDoors`);
- each vertex's sectors and height list (`InitVertexData`), which seamless walls split at;
- each sidedef's segs in order along it (`PrepareSegs`);
- each sector's vertices, the stacked portals (`gl_InitPortals`) and MAPINFO's `gl_renderer` options
  (`InitGLRMapinfoData`, `features/rendersettings/mapoptions.cpp`).

Once per game start it also sets the `SetGlobalFogParameter` special and applies the IWAD's
`sprofs` sprite offsets (`AdjustSpriteOffsets`). `CleanLevel` frees what it built, the portals and the
level's light actors. `listmapsections` and `dumpgeometry` print what it made.

GL keeps its own part of a level, its wall cache and vertex buffer (`gl_PreprocessLevel` and
`gl_CleanLevelData` in `gl/data/gl_data.cpp`).

## In-engine hooks

- `features/frame/levelrenderer.cpp` `PreprocessLevel`: `PrepareLevel`, then GL's `gl_PreprocessLevel`,
  then the backend's mesh. `CleanLevelData`: the backend's state, GL's, then `CleanLevel`.
