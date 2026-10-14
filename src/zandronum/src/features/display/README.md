# features/display

The screen's palette brightness and hardware gamma ramp, worked out once for both frame buffers: GL's
and the one a launch without GL draws with (`features/hwrender/backendvideo.cpp`).

`computation/display_compute` holds both, tested:

- `ComputePaletteBrightness`: the base palette's mean brightness, which the screen's tint darkens a
  colormap by;
- `ComputeGammaRamp`: the 768-entry ramp from `Gamma`, `vid_contrast` and `vid_brightness`.

`display.h` holds what a frame buffer keeps of them: `ScreenColors` (the palette as it was taken, its
brightness and the flash, loaded on a new frame buffer and a game restart) and `CurrentGammaRamp`.

## In-engine hooks

- `gl/system/gl_framebuffer.cpp` `OpenGLFrameBuffer`: a `ScreenColors` member behind `UpdatePalette`,
  `GetPalette`, `GetFlashedPalette`, `SetFlash`, `GetFlash` and `GameRestart`, and `CurrentGammaRamp` in `DoSetGamma`;
  `gl/scene/gl_scene.cpp` `DrawBlend` reads its brightness.
- `features/hwrender/backendvideo.cpp` `DiligentFrameBuffer`: the same.
