# features/hwrender

The Diligent (Vulkan) backend that draws the level mesh beside or over GL's window, and the pure
`computation/` helpers it is built from. `diligent/` holds the backend itself; `hud2d` is the
Diligent-free layer both renderers can see.

The player's view is framed in the view window as GL's `SetViewport` frames it (a status bar raises it,
a smaller `screenblocks` shrinks it): `computation/backendview_compute`'s `ComputeViewWindowViewport` is
GL's viewport, and `ComputeNdcWindow`/`ComputeApplyNdcWindow` fold it into the projection `BuildMVP`
makes, so the target stays whole and every pass that reads `gl_FragCoord` against the MVP stays in step.
Scenario `viewwindow`.

## A launch without GL (`-vulkan`)

`-vulkan` on the command line makes no GL context at all (Windows builds with the backend). `I_CreateRenderer`
installs `FBackendRenderer` (`backendrenderer.cpp`), whose `RenderView` is `features/frame`'s
`RenderPlayerView` and whose level, light and fake-flat work is GL's CPU-side code. `Win32GLVideo` keeps the
window and modes but sets no pixel format, and makes `DiligentFrameBuffer` (`backendvideo.cpp`): the 2D draws
recorded through `features/draw2d`, the palette and gamma from `features/display`, `screenshot` read back from
the frame, and every frame presented from its `Update` by `PresentBackendOnly`, with the world only under
a frame that drew a view of it (the title, menus and console otherwise). The launch turns `fua_vulkan`,
`fua_dg_standalone` and `fua_dg_embed` on, and puts the config's values back
before it is written. An unfocused window is not presented to, as GL's is not, but its level still sets up.
`backendonly.h` is the Diligent-free interface between them. `fuactl play --no-gl` launches one, `dumprenderer`
says `renderer=vulkan` in one, and a scenario side with `noGl` runs as one (scenario `nogl`). Its frames are
the instant replay's own stream, the menus' included (`PresentFrame` records every presented frame).

Its frame is drawn into an offscreen target at the screen's size (`diligent/dgtarget.cpp`) and drawn up to the
window, linearly filtered, at present, as GL's scale buffer is, so `vid_scalefactor` and the scale modes render
at the virtual size; screenshots and the replay read that target. It keeps the last frame for the screen wipes
(`diligent/dgwipe.cpp`): the old screen copied at `WipeStartScreen`, this frame drawn and copied at
`WipeEndScreen` with its 2D dropped, and each tic's melt, crossfade or burn (`features/wipe`) drawn under the
console and menus. The save picture is the player's last view drawn at the picture's size through the camera
texture path (`RenderSavePicture`), with the screen's tint applied on the CPU (`ComputeApplyScreenFill`). A level
settles before its first view (`SettleLevelScene`), since the map bakes it whole, so that view and a wipe's new
screen are drawn rather than black.

Every 2D draw is recorded through `Record2DCanvas` (`record2d.cpp`), `features/draw2d` working it out: the
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
until the next vblank and Sunder MAP10 ran at the monitor's 480 Hz (2.08 ms) instead of 1.0 ms. The
benchmark's unpresented frames are submitted and finished as Present does both; it finished none, so on wdiex02
Diligent's dynamic heap ran out inside `fua_diligent_bench` and the engine died.

The translucent pass sorts by `ComputeTranslucentSortKey`, the order `ComputeDrawsBefore` states packed into
one 128-bit number per draw (tested to agree with the rule on every pair of 576 draws), so the sort compares
integers rather than walking the rule.

A frame's sprites, models and decals are laid out on the game thread (`computation/dynlayout_compute`, each
slot found there, since finding one may make it) and their records and vertices written on the job pool in
chunks of 64. On Sunder MAP10 with the monsters awake that took `BuildDynamic` from 0.13 to 0.09 ms.

`computation/levelsetup_compute` steps a level's set-up (bake from the map, upload, retry, give up);
`PresentFrame` and `SceneDrawable` are shared by the world's frames and the 2D-only
ones. A build without the backend (`FUA_DILIGENT` off, every non-Windows build) takes its entry points from
`backendstubs.cpp`, each answering that there is none.

## Engine hooks

| file | hook | what for |
|---|---|---|
| `d_main.cpp` | `Clear2D`, `FrameTimingBegin`, `LiveFrame` | start the frame's 2D capture and renderer clock; draw the backend's frame after GL's |
| `gl/scene/gl_sprite.cpp`, `gl_spriteview.cpp` | `SpriteViewProvider` | GL's sprites ask GL's own view, the backend's ask `SpriteView()` |
| `gl/scene/gl_drawinfo.cpp`, `gl_walls_draw.cpp` | `ComputeGLSpritesBefore`, `ComputeWallLightIndex` | GL's draw order and wall light lists, shared with the backend |
| `features/frame/framedriver.cpp` | `FrameRouteNow` (`features/frame/computation/frameroute_compute`) | which renderers draw the frame |
| `features/sidebyside/sidebyside.cpp` | `FrameRouteNow` | whether GL presents, which is whenever it draws |
| `features/visibility/viswalk.cpp` | `SetSkyFog` | the sky's fade layer, derived from the walk |
| `v_video.cpp` | `BackendRateLive`, `RendererRateShown`, `ComputeRateText`, `ComputeRateBox`, `Set2DTarget` | `vid_fps` shows each window its own renderer's frame time, `fua_fps_scale` times larger |
| `win32/hardware.cpp` | `BackendOnly`, `BeginBackendOnly`, `CreateBackendRenderer` | the renderer of a launch without GL |
| `win32/win32gliface.cpp` | `BackendOnly`, `CreateBackendFrameBuffer` | no pixel format or context, and the backend's frame buffer |
| `features/sidebyside/sidebyside.cpp` | `Record2DCanvas`, `Get2DTarget` | GL's frame buffer records every 2D draw for the backend, and GL leaves out one meant for the backend's window alone |
| `features/frame/playerview.cpp` | `NoteWorldView`, `SetMainViewLens`, `StandaloneActive` | the frame drew the world, through the player's lens |
| `features/hwrender/backendrenderer.cpp` | `SettleLevelScene`, `RenderSavePicture`, `RenderBackendCamera` | a level settled before its first view; the save picture; camera textures |
| `features/hwrender/backendvideo.cpp` | `WipeStart`, `WipeEnd`, `WipeTic`, `WipeCleanup`, `PreparePoly` | the screen wipes and the textured automap |
| `gl/data/gl_vertexbuffer.cpp`, `gl/models/gl_models.cpp`, `gl/system/gl_interface.h` | `gl_HasContext` | vertex buffers keep only their CPU copy without GL |
| `gl/textures/gl_texture.cpp`, `gl_material.cpp`, `gl_hqresize.cpp`, `gl/data/gl_data.cpp` | `FMaterial::TextureBuffer`, `FMaterial::FlushAll` | texture reads and flushes that do not go through `GLRenderer` |
