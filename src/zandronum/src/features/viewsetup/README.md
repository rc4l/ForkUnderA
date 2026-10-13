# features/viewsetup

The view a frame is drawn from, set up once without GL. `FGLRenderer::RenderViewpoint` worked these
out for itself, and the walks read them back from `GLRenderer`; now both read `CurrentView()`:

- the pitch, stretched by the level's pixel ratio since the playsim treats it as 1:1;
- the roll;
- the actor the eye is in, which is not drawn (none in the chase camera or a dead player's death camera);
- the walks' frustum half-angle (`FrustumAngle`), widened for a tilted view;
- the yaw and the vector the view faces (`SetViewAngle`), which `TurnView` turns for a mirror's walk;
- how far the two sky layers have scrolled at the frame's clock (GL's `mSky1Pos` and `mSky2Pos`);
- whether the level has a dynamic light.

`SetViewArea` places the eye in its render sector, inside its floor and ceiling, and finds its area
against a height transfer (`in_area`), for GL's view and a skybox's walk alike. `InstallSpriteView`
registers a sprite view answered from this view, for a frame GL does not draw. `FixedColormap` is the
player's fixed colormap as GL's `SetFixedColormap` picks it (`ComputeFixedColormap`), `FixedColormapShadeFor`
what main.fp then does to every texel (`ComputeFixedColormapShade`: a special colormap's ramp, light
amplification's or a torch's tint), and `ComputeFovRatio`
the lens's ratio; `ComputeSeesOwnBody` is the chase and death camera's test, and `ComputeMaxViewPitch`
Zandronum's pitch limits.

`computation/viewsetup_compute` holds the pitch, frustum, sky scroll, view vector, view area, eye
clamp and fixed colormap arithmetic, tested.

## In-engine hooks

- `gl/scene/gl_scene.cpp` `FGLRenderer::RenderViewpoint`: `SetupView` after `R_SetupFrame` with
  `gl_frameMS`, and GL's pitch, roll, view actor and sky scroll taken from it; `FGLRenderer::FrustumAngle`
  is `ComputeFrustumAngle`, `SetViewArea` is `SetViewArea`, and `SetViewAngle` is `ComputeViewAngleVector`.
- `features/visibility/viswalk.cpp`: the player's view actor, every walk's frustum, a skybox walk's
  `SetViewArea` and a mirror walk's `TurnView`, and the view vector and angles inside a portal view.
- `features/hwrender/diligent/dgsky.cpp`: the sky's scroll.
- `features/frame/playerview.cpp` `BeginPlayerFrame` and `BeginViewpoint`: `FixedColormap`, `ComputeFovRatio`,
  `SetViewArea` and `SetupView`, for GL's frame and a launch without GL alike; GL's `SetFixedColormap` (the
  save picture) calls `FixedColormap` too.
- `gl/renderer/gl_renderstate.cpp` `FRenderState::ApplyShader`: the fixed colormap uniforms from `FixedColormapShadeFor`.
- `features/hwrender/diligent/dgviews.cpp` `WriteSceneConstants` and `dg2d.cpp` `Draw2D`: the same shade for the
  backend's world and weapon, none in a camera texture as GL's `RenderTextureView` draws it.
- `features/frame/levelrenderer.cpp` `GetMaxViewPitch`: `ComputeMaxViewPitch`.
- `features/hwrender/backendrenderer.cpp` `Init`: `InstallSpriteView`.

GL state it still reads, until it moves out of `gl/` (P5): `glset.pixelstretch`, `in_area`, and the
`ADynamicLight` class.
