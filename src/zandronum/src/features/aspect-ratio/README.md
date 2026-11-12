# features/aspect-ratio

The aspect-ratio buckets the video mode lists and `vid_aspect` still name: 0 4:3, 1 16:9, 2 16:10,
3 17:10, 4 5:4, 6 21:9 (5 is a redundant 17:10, never returned). The 3D view, the 2D virtual screen,
the borders and the status bar take the true ratio instead (`ActiveRatio` and the `Aspect*` functions
in `v_video.cpp`, from UZDoom's continuous-aspect commits), so a bucket decides only what a mode is
called, which modes the menu lists, and what `vid_aspect` forces.

`computation/ratiobucket_compute` holds the rules: `ComputeRatioBucket` (the bucket whose ratio is
nearest a size's, zandronum@a48630d214dc), `ComputeForcedRatioBucket` (`vid_aspect` 1 to 6 and
`vid_nowidescreen`), `ComputeRatioIsWidescreen`, and the video mode menu's `ComputeMenuRatioFilter`. 21:9 is Zandronum 3.3's (zandronum@bab77c3aab00); forcing it gives the
3D view a 21:9 lens through `ActiveRatio`'s table.

## In-engine hooks

- `v_video.cpp`: `ActiveFakeRatio` and `CheckRatio`.
- `win32/hardware.cpp`, `posix/sdl/hardware.cpp`: `vid_listmodes` names bucket 6 and colours the
  widescreen buckets.
- `menu/videomenu.cpp`: `menu_screenratios` and the modes `BuildModesList` lists.
- `wadsrc/static/menudef.txt`: 21:9 in `ForceRatios`, `Ratios` and `RatiosTFT`.
- `CMakeLists.txt`: `ratiobucket_compute.cpp` in the engine's sources.
