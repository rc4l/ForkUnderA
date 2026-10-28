# features/draw2d

The engine's 2D draws (the status bar, HUD, menus, console, automap) worked out without GL: what a
`DCanvas` call puts on the screen, as one record that GL draws and the 2D capture
(`record.h`) hands the backend. With one computation behind both, a renderer that
is not GL can take the 2D layer from here directly (P5), and the two cannot drift.

- `computation/quad2d_compute`: `DrawTexture`'s quad from a `DrawParms` (offsets, scale, the window a
  partial draw shows, flips, a camera texture's upside-down coordinates, the colour and a black
  overlay's darkening), and `gl_GetRenderStyle`'s texture mode and blend (additive for a style that
  adds onto the screen whole; a subtracting style still draws as alpha).
  `ComputeOverlayQuad` is the second pass GL draws for any other overlay (a greyed menu row), the glyph's coverage in the overlay's colour, which `record2d.cpp` records too.
  It also holds `Quad2D`, the 2D capture's record, its blend and kind values, and the untextured records
  (`ComputeFillQuad`, `ComputeLineQuad`, `ComputePixelQuad`, `ComputeScreenFillQuad`); `ComputeDrawColorDirect`
  is the rule for a clear given palette entry -1.
- `record.h`/`.cpp`: the frame's 2D capture, its quads in draw order (`Record2D`, `Quads2D`, `Generation2D`,
  `GetScreen2D`), dropped at the top of each frame and each wipe step (`Clear2D`), each draw for both windows
  or one (`Set2DTarget`); `fua_2d_list` lists it. Moved from `features/hwrender/hud2d`, so the engine and
  GL's frame buffer record without reaching into the backend. `MarkViewQuads` and `ViewQuads` say how many of
  the first quads are the view's own (the weapon, the targeter, the screen's tint), which
  `features/postprocess`'s scene shaders run over.
- `record2d.h`/`.cpp`: `Record2DCanvas`, the canvas every 2D draw is recorded through, moved from
  `features/hwrender`.
- `draw2d.cpp`: `PrepareTexture`, the glue that reads the `DrawParms`, the material's corners and the
  remap's palette index through `features/materials`; and `PrepareLine`, `PreparePixel`, `PrepareDim`
  and `PrepareClear`, which take an engine colour to those records.
- `screenblend.cpp` and `computation/screenblend_compute`: the screen's tint as `DrawBlend` fills it, a
  height transfer's colormap or the 3D floor fog the eye is under (`ComputeBandBlend`), a colormap's
  average brightened to the palette (`ComputeBrightenedBlend`), then pickups, pain, powerups and a
  script's fade; GL passes in its view area, fixed colormap and palette brightness.

- `computation/flatfill_compute` and `PrepareFlatFill`: a `FlatFill` (an intermission or text screen's
  background, the status bar's flanks) as one quad per copy of the flat, cut at its edges, since the
  backend's 2D layer clamps rather than wraps.

- `PreparePoly`: a `FillSimplePoly` (the textured automap's floors), its points' texture coordinates
  (`ComputePolyTexCoords`) and colour (`CapturePolyColor`) as GL works them, recorded as the fan's triangles
  (`ComputePolyTriangles`, records of kind `kQuadTri`), which the backend draws with the texture repeating.

## In-engine hooks

- `gl/renderer/gl_renderer.cpp` `FGLRenderer::DrawTexture`, `DrawLine`, `DrawPixel`, `Dim` and `Clear`:
  each records what draw2d works out, and `DrawTexture` draws that quad; `FlatFill` records its tiles.
- `features/hwrender/backendvideo.cpp` `DiligentFrameBuffer`: every one of them recorded, for a launch
  without GL.
- `gl/scene/gl_scene.cpp` `FGLRenderer::DrawBlend`: fills and records each of `ScreenBlends`' fills.
- `gl/renderer/gl_renderer.cpp` `FGLRenderer::FillSimplePoly`: draws from `PreparePoly` and records its triangles.
- `d_main.cpp`: `Clear2D` at the top of each frame and each wipe step.
- `v_video.cpp`: `Set2DTarget` around `vid_fps`, drawn once for each window.
- `features/sidebyside/sidebyside.cpp`: GL's frame buffer records through `Record2DCanvas`, and GL leaves out
  a draw meant for the backend's window alone (`Get2DTarget`).
- `features/hwrender/diligent/dg2d.cpp`: the backend draws `Quads2D`, all of them or a range into another target.
- `features/frame/playerview.cpp` `RenderPlayerView`: `MarkViewQuads` once the view's own are recorded.
