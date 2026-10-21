# features/hwrender

The Diligent (Vulkan) backend that draws the level mesh beside or over GL's window, and the pure
`computation/` helpers it is built from. `diligent/` holds the backend itself; `backendonly.h` is the
Diligent-free interface the engine and the other features call it through.

The player's view is framed in the view window as GL's `SetViewport` frames it (a status bar raises it,
a smaller `screenblocks` shrinks it): `computation/backendview_compute`'s `ComputeViewWindowViewport` is
GL's viewport, and `ComputeNdcWindow`/`ComputeApplyNdcWindow` fold it into the projection `BuildMVP`
makes, so the target stays whole and every pass that reads `gl_FragCoord` against the MVP stays in step.
Scenario `viewwindow`.

Its shape is the view's lens, `features/viewsetup`'s `CurrentView()` (`fov`, `ratio`, `fovRatio`), as GL's
`SetProjection` takes it, never the frame's own width over height: `ComputeCameraLens` picks it unless a
camera texture or the save picture overrides it. A forced aspect ratio (`vid_aspect`) is then stretched
across the window as GL stretches it, and the walks' frustum, seeded from the same ratio, covers the
picture; framed from the frame's shape, the screen's edges past the walk drew sky.

## A launch without GL (the default)

A launch draws with the backend alone, with no GL context at all (every client build), unless the
player chose GL: `vid_backend` (the video mode menu's Renderer, "Vulkan (Recommended)" or "GL (Legacy)", read
at startup), with `-vulkan` and `-gl` overriding it for one launch (`computation/backendchoice_compute`).
`ComputeVidBackendFor` keeps it to what the build can honour: a browser's build, whose only renderer is WebGPU,
always reads WebGPU, and `vid_backend_choosable` is false there, which greys the menu's Renderer out. The
value the renderer was chosen from is kept in `vid_backend_launched`, and the menu's `RestartOption` row says beside
its value that the game must restart once the two differ (`menu/restartnote_compute`). When
Vulkan was chosen and its device does not start, the launch says why on the console and draws with GL instead
(`BackendDeviceStarts`). `I_CreateRenderer`
installs `features/frame`'s `FBackendRenderer` (`features/frame/backendrenderer.cpp`), whose `RenderView` is
`RenderPlayerView` and whose level, light and fake-flat work is shared with GL. Each platform's video keeps the
window and modes but makes no GL context (`Win32GLVideo` sets no pixel format, SDL's window is made without
`SDL_WINDOW_OPENGL`, Cocoa's content view is a CAMetalLayer), and makes `DiligentFrameBuffer` (`backendvideo.cpp`,
over that platform's frame buffer): the 2D draws
recorded through `features/draw2d`, the palette and gamma from `features/display`, `screenshot` read back from
the frame, and every frame presented from its `Update` by `PresentBackendOnly`, with the world only under
a frame that drew a view of it (the title, menus and console otherwise). The launch turns `fua_vulkan`,
`fua_dg_standalone` and `fua_dg_embed` on, and puts the config's values back
before it is written. An unfocused window is not presented to, as GL's is not, but its level still sets up.
`backendonly.h` is the Diligent-free interface between them. `fuactl play` launches one unless asked for GL,
`dumprenderer` says `renderer=vulkan` in one, and every scenario's Vulkan side runs as one (scenario `nogl`). Its
frames are the instant replay's own stream, the menus' included (`PresentFrame` records every presented frame).
With `fua_wipe_trace 1`, `PresentFrame` also reads each frame back for `features/wipe`'s trace, which
`fua_wipe_report` turns into each wipe's frames and how far each sits from the pictures before and after
(scenario `wipe`).

## The backend beside GL (for comparing the two)

A GL launch with `fua_vulkan` on (`fuactl play --gl-beside`, or `--side-by-side` for its own window) draws the
backend's frame into a window over GL's, or beside it, so the two can be compared in one process: `shot` pairs,
the `fua_*_check` reports and the live `fua_vulkan 0/1` toggle need it. It is not the game players run. GL
owns the frame loop there, and `LiveFrame` presents the backend's window only at the end of a normal frame,
so a wipe, which `D_Display` runs in its own loop through GL's `WipeDo`, never reaches it (2 of 2 wipes in
scenario `wipe` when moved beside GL), and the frames an intermission draws nothing on, once Use is pressed,
show the previous map's world under them, since `LiveFrame` draws the world whatever the frame drew. A fault
seen only beside GL belongs to this rig; reproduce a player's report without GL first.

Its frame is drawn into an offscreen target at the screen's size (`diligent/dgtarget.cpp`) and drawn up to the
window, linearly filtered, at present, as GL's scale buffer is, so `vid_scalefactor` and the scale modes render
at the virtual size; screenshots and the replay read that target. It keeps the last frame for the screen wipes
(`diligent/dgwipe.cpp`): the old screen copied at `WipeStartScreen`, this frame drawn and copied at
`WipeEndScreen` with its 2D dropped, and each tic's melt, crossfade or burn (`features/wipe`) drawn under the
console and menus. The save picture is the player's last view drawn at the picture's size through the camera
texture path (`RenderSavePicture`), with the screen's tint applied on the CPU (`ComputeApplyScreenFill`). A level
settles before its first view (`SettleLevelScene`), since the map bakes it whole, so that view and a wipe's new
screen are drawn rather than black. Each wipe step is a frame of its own and starts its 2D afresh (`D_WipeStep`);
the console and menu piled up under one generation before, and a browser refused the frames drawn past the 2D's
vertices (`ComputeRebuild2DVertices` now builds them for every quad recorded).

On WebGPU the backend frees its own GPU memory: each texture and buffer it makes (`CreateDeviceTexture`,
`CreateDeviceBuffer`) and the swapchain's own two are kept by handle (`DeviceApi::keep`,
`computation/keptobjects_compute`) and destroyed after the first present once nothing holds them
(`FreeReleasedObjects`). A browser frees a released one only when its garbage collector runs, and a page resized a
few times ran out of GPU memory first. Vulkan frees them itself. Textures' handles and buffers' are kept apart: a
browser numbers each kind from one, and a buffer sharing a kept texture's number went unkept and waited on the
collector (queue #48). Diligent's WebGPU device keeps every upload page it ever made, so the scene buffer is kept
while the level fits it and its big writes go in upload-page-sized pieces (`kUploadPageBytes`, `dgupload.cpp`).
Every page one frame needs stays in that pool, and a level goes up in one frame, as does a rebuild of it, so
`SceneUpload` and `RefreshMovedGeometry` flush the context each time `kUploadFlushBytes` have gone while they run
(`UploadFlushing`, `ComputeUploadFlushDue`): Sunder MAP11 kept 48 MB of pages a load and 96 MB after a save was
loaded and flown around, and Firefox ran out of GPU memory.
A material and translation are found in the texture cache and the slot table by `computation/materialindex_compute`,
where a walk over thousands of Complex Doom sprite frames, for every slot of every binding refill, took 11% of a
fight's frame.
`features/telemetry` reads what the page holds by label (`gpu/page/`) beside the backend's own (`gpu/mem/tag/`).
Every start, a `wad_reload` restart's included, calls `ForgetBackendTextures` (from `features/frame/framedriver.cpp`), so the caches keyed on the old textures and materials go before new ones can reuse their addresses.

At exit the backend shuts down before the static destructors (`ShutdownBackend`, an `atterm` in
`diligent/dgwindow.cpp`): the swapchain goes while its window is still there, the frame fence while the console
its destructor reports to is still there, and Diligent's messages stop reaching the console. A quit crashed when
the fence's message reached a console that had already been freed (scenario `quitclean`).

Every 2D draw is recorded through `features/draw2d`'s `Record2DCanvas` into its frame's capture: the
backend's own frame buffer draws its 2D with it, and in a launch with GL the frame buffer `features/sidebyside`
makes records GL's 2D with it beside GL drawing it, so GL's renderer records nothing itself.

A decal glued to a wall is drawn with GL's polygon offset, `-1` times the slope and `-128` resolvable steps
(`RasterizerDesc.SlopeScaledDepthBias` and `DepthBias`). The two were swapped before, so a decal seen square
on had no slope term to lift it and fought its wall (scenario `walldecal`). The sky's fade layer is drawn over
the dome in the fog `features/visibility` derives, and not over a sky box, as GL's `RenderBox` draws none
(scenario `skyfog`; its pipeline was refused every frame before, for want of its shaders).

A frame signals a fence as it is presented, and one more than `fua_dg_frames_ahead` (2) ahead of the GPU waits
for it (`computation/framepacing_compute`). `vid_vsync` picks the present interval, FIFO when on and mailbox
when off, and the swapchain has three images: with Diligent's default two, mailbox had none free to draw into
until the next vblank and Sunder MAP10 ran at the monitor's refresh interval instead of 1.0 ms. The
benchmark's unpresented frames are submitted and finished as Present does both; it finished none, so on wdiex02
Diligent's dynamic heap ran out inside `fua_diligent_bench` and the engine died.

The translucent pass sorts by `ComputeTranslucentSortKey`, the order `ComputeDrawsBefore` states packed into
one 128-bit number per draw (tested to agree with the rule on every pair of 576 draws), so the sort compares
integers rather than walking the rule.

`vid_fps` gives each renderer its own frame time (`rendererrate.cpp`): the frame is split at `LiveFrame`, GL's
part before it and the backend's after. Its fps is the frames drawn in the last whole second, which a limit or a
browser's refresh holds, never what the frame time would allow.

The sky is three files: the level's dome or box and the fade (`diligent/dgsky.cpp`), the skybox portal and
the surfaces painted from it by screen position (`dgskyportal.cpp`), and other skies' pictures
(`dgskylooks.cpp`), sharing `dgsky_internal.h`. Line mirrors are `dgviews.cpp`; reflective planes and
stacked views, drawn into pooled targets, are `dgviewtargets.cpp`.

Every pass `DrawWorld` draws binds its own targets through `BeginPassTargets` (`dgscene.cpp`): its colour,
depth and normals, and its size, which the scene constants and the light grid take. A mirror, a reflective plane,
a stacked view, the skybox and a camera texture each have a G-buffer of their own, so the decal pass marks what
that view drew; it read the player's view's depth and normals before, and drew no floor mark in any other view.

A frame's sprites, models and decals are laid out on the main thread (`computation/dynlayout_compute`, each
slot found there, since finding one may make it) and their records and vertices written on the job pool in
chunks of 64. On Sunder MAP10 with the monsters awake that took `BuildDynamic` from 0.13 to 0.09 ms.

`computation/levelsetup_compute` steps a level's set-up (bake from the map, upload, retry, give up);
`PresentFrame` and `SceneDrawable` are shared by the world's frames and the 2D-only
ones. A build without the backend (`FUA_DILIGENT` off, a dedicated server) takes its entry points from
`backendstubs.cpp`, each answering that there is none.

## Engine hooks

| file | hook | what for |
|---|---|---|
| `d_main.cpp` | `FrameTimingBegin`, `LiveFrame` | start the renderer clock; draw the backend's frame after GL's |
| `gl/scene/gl_drawinfo.cpp`, `gl_walls_draw.cpp` | `ComputeGLSpritesBefore`, `ComputeWallLightIndex` | GL's draw order and wall light lists, shared with the backend |
| `features/frame/framedriver.cpp` | `FrameRouteNow` (`features/frame/computation/frameroute_compute`) | which renderers draw the frame |
| `features/sidebyside/sidebyside.cpp` | `FrameRouteNow` | whether GL presents, which is whenever it draws |
| `v_video.cpp` | `BackendRateLive`, `RendererRateShown`, `ComputeRateText`, `ComputeRateBox` | `vid_fps` shows each window its own renderer's frame time, `fua_fps_scale` times larger |
| `posix/sdl/hardware.cpp` `I_CreateRenderer`, `posix/cocoa/i_video.mm` | `BackendOnly` | a renderer for a launch without GL however the build was made, and no GL view in one |
| `win32/win32gliface.cpp` | `BackendOnly`, `CreateBackendFrameBuffer` | no pixel format or context, and the backend's frame buffer |
| `posix/sdl/sdlglvideo.cpp` | `CreateBackendFrameBuffer`, `ForgetBackendWindow`, `I_EngineWindow` | a window with no GL context, the backend's frame buffer, and its swapchain dropped before a mode change destroys the window |
| `posix/sdl/i_input.cpp` | `I_EngineWindow` | the window mouse warps and grabs, which has no GL context to find it by |
| `posix/cocoa/i_video.mm` | `CreateBackendFrameBuffer`, `I_EngineContentView` | a CAMetalLayer content view and no GL context, and the backend's frame buffer |
| `features/frame/playerview.cpp` | `NoteWorldView`, `StandaloneActive` | the frame drew the world |
| `features/frame/backendrenderer.cpp` | `BeginBackendOnly`, `SettleLevelScene`, `RenderSavePicture`, `RenderBackendCamera` | a level settled before its first view; the save picture; camera textures |
| `features/hwrender/backendvideo.cpp` | `WipeStart`, `WipeEnd`, `WipeTic`, `WipeCleanup`, `PreparePoly` | the screen wipes and the textured automap |
| `g_game.cpp` `G_DoSaveGame`, `r_renderer.h` `FRenderer::SaveWritten`, `features/frame/framedriver.cpp` | `RenderSavePictureLater` | in a browser the save is written with the placeholder picture, and the picture goes into it once it is read back |
| `gl/data/gl_vertexbuffer.cpp`, `gl/models/gl_models.cpp`, `gl/system/gl_interface.h` | `gl_HasContext` | vertex buffers keep only their CPU copy without GL |
| `features/materials/textureinfo.cpp`, `material.cpp`, `hqresize.cpp`, `features/levelprep/levelprep.cpp` | `FMaterial::TextureBuffer`, `FMaterial::FlushAll` | texture reads and flushes that do not go through `GLRenderer` |
