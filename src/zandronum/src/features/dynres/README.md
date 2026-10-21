# features/dynres

Dynamic resolution: while the GPU cannot finish a frame inside its budget, the 3D scene is drawn
below full scale and painted up over the frame; once there is room it climbs back to full scale.
The HUD, menus, console and automap are drawn after it at full resolution and stay sharp.

## How it behaves

- On by default (`vid_fua_dynres`), for the Vulkan and WebGPU backends, desktop and browser alike;
  GL draws at full scale. Video options holds it, the target frame rate and the lowest scale.
- **The budget** (`ComputeFrameBudgetMs`) is the target frame rate (`vid_fua_dynres_target`, 0 for
  the display's refresh), never above what a cap allows: vsync's refresh, `vid_maxfps` and
  `cl_capfps` (`features/frame-limit`), so a cap it cannot beat is no reason to blur.
- **It judges the GPU's time**, from the backend's duration queries (`gpu/frame_ms`), never the
  time between frames: a frame waiting on a cap is long and cheap, and one the CPU holds is not
  helped by fewer pixels.
- **A browser has no GPU timings**: standard WebGPU writes timestamps only at a pass's edges, and
  Diligent's duration queries need them inside passes (the queue's `onSubmittedWorkDone` was tried
  and reads the browser's polling, some 3 ms in Chrome and 16 ms in Firefox with the GPU idle). There
  it steps down only after frames in a row that were late while the CPU worked well under them
  (`ComputeFrameWaitedOnGpu`), late meaning over one and a half budgets since a frame paced to a
  display misses a whole refresh or none. It keeps the step only if the frames then shorten by a
  tenth or are no longer late; a step that does not is undone, and steps down wait a time that
  doubles, from two seconds up to sixteen, with each one undone. It steps up after 120 frames whose
  average was not late, a wait that doubles, up to sixteen times, each time a step up is soon undone.
- A browser's wait for its next animation frame counts as present time, so its CPU time is the
  frame's own work. Its refresh is the low tenth of its last 120 frame intervals, held at the fastest
  of its last eight such windows (`ComputeFrameRefreshHz`) since frames the GPU holds back are no
  slower display, and unknown above 1000 Hz, where a browser drawing for no display runs.
- **It steps** (`ComputeDynresStep`) down once the GPU's average passes 85% of the budget and is
  what holds the frame, and up once the next step's predicted cost fits under 70% for 30 frames,
  ignoring 8 frames after each step while the timings still describe the old scale. The steps are
  eighths when the upscale is linear and 3/4, 1/2 and 1/4 when it is by point, never under
  `vid_fua_dynres_min`.
- **The upscale follows the texture filter** (`r_texture_filter`): by point for its nearest modes,
  the fresh-config default among them, and linear for the others.
- A new level starts it over at full scale, letting 30 frames of uploads pass first; stereo 3D
  holds full scale.

## How it draws

Every screen-sized pass of a frame draws into the top left `SceneWidth` by `SceneHeight` of its
frame-sized targets, which are made once, so a step changes a viewport and nothing is allocated or
compiled. The player's view draws into the scene's own colour target, and `UpscaleScene` paints it
over the frame before the 2D. Pictures of the screen composited pixel for pixel (mirrors, the sky
portal, stacked views, sky looks) therefore still land where they are read, and a camera texture
keeps its own size. At full scale every pass draws into the frame as before.

## In-engine hooks

- `features/hwrender/diligent/dgtarget.cpp`: `BeginSceneFrame` asks `ScaleForFrame` once a frame,
  `SceneRTV`, `SceneWidth`/`SceneHeight`, the scene target and `UpscaleScene`.
- `features/hwrender/diligent/dgscene.cpp`: the player's view draws into `SceneRTV` at the scene's
  size; `BeginPassTargets` and every later rebinding set the pass's viewport (`ApplyPassViewport`),
  since binding resets it; `ViewPassTargets` gives a screen view the scene's size.
- `dgdecals.cpp` reads the depth and normals at their own texels; `dgviews.cpp` flips a mirror's
  picture across the pass's width; `dgskylooks.cpp`, `dgskyportal.cpp` and `dglines.cpp` draw in
  the scene's pixels.
- `dggputimer.cpp` keeps each pass's last time (`LastGpuPassMs`) and times the frame for this
  feature with telemetry off.
- `wadsrc/static/menudef.txt`: Video options' frame rate limit, dynamic resolution, target frame
  rate and lowest resolution.
- `displayrefresh.cpp` (Windows, SDL) and `displayrefresh_cocoa.mm` (macOS) ask the display its
  refresh; a browser cannot, so `dynres.cpp` reads it from its frames.
- `features/web/webdriver.cpp` counts a browser's wait for an animation frame as the present phase.

## Telemetry and testing

- `frame/dynres_scale`, `frame/dynres_budget_ms` and `frame/dynres_changes`; `fua_dynres_report`
  prints the scale, the budget and the refresh it came from, and what holds it.
- `fua_dg_dynres_force` pins a scale. fuactl's driven launches start with `vid_fua_dynres 0` so a
  measurement or a comparison is never made at a scale the load chose.
- Scenarios `dynresgrid` (the light grid at half scale), `dynreshud` (the status bar and border
  pixel for pixel, the view window), `dynresmirror`, `dynressky`, `dynresstack` and
  `decalresize`'s half-scale step.
