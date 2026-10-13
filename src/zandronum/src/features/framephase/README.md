# framephase

Where a frame's time goes, phase by phase, so a frame that ran past a refresh can be taken apart: the sim,
the visibility walk, the re-bake of what changed, the actor sweep, the backend's scene draw (with its
upload and light-list parts inside it) and the present. Modules mark their work with a `Scope`; the main
loop closes each frame with `FrameBoundary`; `fua_viewtrace` records the last frame's phases with each
sample and reports what its slow frames spent more on (`computation/framephase_compute`).

## In-engine hooks

- `d_main.cpp` `D_DoomLoop`: `FrameBoundary` at the top of each iteration and the sim phase around the tics.
- `features/frame/standaloneframe.cpp` `StandaloneScene`: walk, rebake and actors.
- `features/hwrender/diligent/dgscene.cpp`: backend around `DrawSceneOnce`, upload and lights in
  `PrepareFrame`, present around `swap->Present` and the frame pacing wait.
