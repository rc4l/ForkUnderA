# features/display

The screen's palette brightness and hardware gamma ramp, worked out once for both frame buffers: GL's
and the one a launch without GL draws with (`features/hwrender/backendvideo.cpp`).

`computation/display_compute` holds both, tested:

- `ComputePaletteBrightness`: the base palette's mean brightness, which the screen's tint darkens a
  colormap by;
- `ComputeGammaRamp`: the 768-entry ramp from `Gamma`, `vid_contrast` and `vid_brightness`.

## In-engine hooks

- `gl/system/gl_framebuffer.cpp` `OpenGLFrameBuffer::UpdatePalette` and `DoSetGamma`.
- `features/hwrender/backendvideo.cpp` `DiligentFrameBuffer::UpdatePalette` and `DoSetGamma`.
