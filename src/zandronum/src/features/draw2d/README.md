# features/draw2d

The engine's 2D draws (the status bar, HUD, menus, console, automap) worked out without GL: what a
`DCanvas` call puts on the screen, as one record that GL draws and the 2D capture
(`features/hwrender/hud2d.h`) hands the backend. With one computation behind both, a renderer that
is not GL can take the 2D layer from here directly (P5), and the two cannot drift.

- `computation/quad2d_compute`: `DrawTexture`'s quad from a `DrawParms` (offsets, scale, the window a
  partial draw shows, flips, a camera texture's upside-down coordinates, the colour and a black
  overlay's darkening), and `gl_GetRenderStyle`'s texture mode.
- `draw2d.cpp`: `PrepareTexture`, the glue that reads the `DrawParms`, the material's corners and the
  remap's palette index through `features/materials`; and `PrepareLine`, `PreparePixel`, `PrepareDim`
  and `PrepareClear`, the untextured records.

`FlatFill` (a tiled flat, which needs a repeating sampler in the backend) and `FillSimplePoly` (the
textured automap, a polygon rather than a quad) are not recorded yet, so a backend does not draw them.

## In-engine hooks

- `gl/renderer/gl_renderer.cpp` `FGLRenderer::DrawTexture`, `DrawLine`, `DrawPixel`, `Dim` and `Clear`:
  each records what draw2d works out, and `DrawTexture` draws that quad.
