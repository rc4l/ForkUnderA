# framephase

Where a frame's time goes, phase by phase, so a frame that ran past a refresh can be taken apart: the sim,
the visibility walk, the re-bake of what changed, the actor sweep, the backend's scene draw (with its
upload and light-list parts inside it), the present, the main loop's input reads, and the whole of
`D_Display` with its page flip, so the time a frame spends outside every phase is the frame less sim,
input and display. Modules mark their work with a `Scope`; the main
loop closes each frame with `FrameBoundary`; `fua_viewtrace` records the last frame's phases with each
sample and reports what its slow frames spent more on (`computation/framephase_compute`).

## In-engine hooks

- `d_main.cpp` `D_DoomLoop`: `FrameBoundary` at the top of each iteration, the sim phase around the tics,
  input around `I_StartTic` and display around `D_Display`.
- `d_main.cpp` `D_Display`: input around its `NetUpdate` and flip around `screen->Update`.
- `features/frame/standaloneframe.cpp` `StandaloneScene`: walk, rebake and actors.
- `features/hwrender/diligent/dgscene.cpp`: backend around `DrawSceneOnce`, upload and lights in
  `PrepareFrame`, present around `swap->Present` and the frame pacing wait.
