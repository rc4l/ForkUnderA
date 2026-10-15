# features/draw2d

The engine's 2D draws (the status bar, HUD, menus, console, automap) worked out without GL: what a
`DCanvas` call puts on the screen, as one record that GL draws and the 2D capture
(`features/hwrender/hud2d.h`) hands the backend. With one computation behind both, a renderer that
is not GL can take the 2D layer from here directly (P5), and the two cannot drift.

- `computation/quad2d_compute`: `DrawTexture`'s quad from a `DrawParms` (offsets, scale, the window a
  partial draw shows, flips, a camera texture's upside-down coordinates, the colour and a black
  overlay's darkening), and `gl_GetRenderStyle`'s texture mode and blend (additive for a style that
  adds onto the screen whole; a subtracting style still draws as alpha).
  `ComputeOverlayQuad` is the second pass GL draws for any other overlay (a greyed menu row), the glyph's coverage in the overlay's colour, which `features/hwrender/record2d.cpp` records too.
  It also holds `Quad2D`, the 2D capture's record, its blend and kind values, and the untextured records
  (`ComputeFillQuad`, `ComputeLineQuad`, `ComputePixelQuad`, `ComputeScreenFillQuad`); `ComputeDrawColorDirect`
  is the rule for a clear given palette entry -1.
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
