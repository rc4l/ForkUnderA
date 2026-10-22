# features/dynres

Dynamic resolution: while the GPU cannot finish a frame inside its budget, the 3D scene is drawn
below full scale and painted up over the frame; once there is room it climbs back to full scale.
The HUD, menus, console and automap are drawn after it at full resolution and stay sharp.

## How it behaves

- On by default (`vid_fua_dynres`), for the Vulkan and WebGPU backends, desktop and browser alike;
  GL draws at full scale. Video options holds it, the target frame rate and the lowest scale.
- **The budget** (`ComputeFrameBudgetMs`) is the target frame rate (`vid_fua_dynres_target`, 0 for
  the display's refresh), never above what a cap allows: vsync's refresh (always in a browser,
  which holds every frame to its own pace), `vid_maxfps` and `cl_capfps` (`features/frame-limit`),
  so a cap it cannot beat is no reason to blur. Untimed (a browser), target 0 aims at 60 fps
  (`kUntimedHz`), since every step there is a trial and a high-refresh display's rate is often
  beyond the browser's CPU alone, and a refresh read from the frames under 60 Hz is taken for 60,
  since a page that starts in a fight reads its slow frames as a slow display.
- **It judges the GPU's time**, from the backend's duration queries (`gpu/frame_ms`), never the
  time between frames: a frame waiting on a cap is long and cheap, and one the CPU holds is not
  helped by fewer pixels.
- **A browser has no GPU timings**: standard WebGPU writes timestamps only at a pass's edges, and
  Diligent's duration queries need them inside passes (the queue's `onSubmittedWorkDone` was tried
  and reads the browser's polling, some 3 ms in Chrome and 16 ms in Firefox with the GPU idle), so
  it judges the frames' mean over each window of `vid_fua_dynres_window` seconds (0.15 by default,
  "Reaction time" in the browser's video options) and every step is a trial the next window bears out:
  - a window whose frames average over 1.2 budgets steps down, whatever the CPU's share, since
    Firefox lets several frames through and then holds one for the GPU at the animation frame, or
    waits on the GPU inside its own calls; the step is kept only if the next window's frames, from
    a quarter second after it, are a twentieth shorter, and followed at once by the next while they
    stay slow, since a step kept for nothing is soon undone by a step up;
  - a step down undone waits a time that doubles, from two seconds up to sixteen, before the next,
    unless the frames grow a quarter heavier than when it was undone, which is another load;
  - below full scale it steps up after two windows in a row under 0.8 budgets, or after three seconds at a scale kept
    whatever the frames, since frames paced to a 60 Hz display never show room, and a step down
    tried and undone meanwhile does not restart that wait; a step
    up that makes slow frames longer by a tenth is undone and that wait doubles, up to sixteen
    times, until frames a tenth lighter than before it forget the wait;
  - it reads no frame while the game is paused, a menu or the console is up or the window has no
    focus (`ComputeFrameJudged`), since a paused game draws once a tic and a browser slows a page
    out of focus; it holds the scale and lets 30 frames pass on the way back.
- A browser's wait for its next animation frame counts as present time, so its CPU time is the
  frame's own work. Its refresh is the median of its last 120 frame intervals, held at the fastest
  whole window yet (`ComputeFrameRefreshHz`) since no run of frames the GPU holds back, however
  long, is a slower display, and unknown above 1000 Hz, where a browser drawing for no display runs; the low
  tenth it once took read Firefox's jitter at 480 Hz as 540.
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
- `posix/sdl/i_system.cpp` `I_WindowIsActive` answers from SDL's keyboard focus, which a browser's
  focus and blur move, for `ComputeFrameJudged`.

## Telemetry and testing

- `frame/dynres_scale`, `frame/dynres_budget_ms` and `frame/dynres_changes`; `fua_dynres_report`
  prints the scale, the budget and the refresh it came from, and what holds it.
- `fua_dynres_log 1` prints each step and why (`ComputeDynresWhy`); `fua_dg_dynres_force` pins a scale. fuactl's driven launches start with `vid_fua_dynres 0` so a
  measurement or a comparison is never made at a scale the load chose.
- Scenarios `dynresgrid` (the light grid at half scale), `dynreshud` (the status bar and border
  pixel for pixel, the view window), `dynresmirror`, `dynressky`, `dynresstack` and
  `decalresize`'s half-scale step.
