# features/viewsetup

The view a frame is drawn from, set up once without GL. `FGLRenderer::RenderViewpoint` worked these
out for itself, and the walks read them back from `GLRenderer`; now both read `CurrentView()`:

- the pitch, stretched by the level's pixel ratio since the playsim treats it as 1:1;
- the roll;
- the actor the eye is in, which is not drawn (none in the chase camera or a dead player's death camera);
- the walks' frustum half-angle (`FrustumAngle`), widened for a tilted view.

`computation/viewsetup_compute` holds the pitch and frustum arithmetic, tested.

## In-engine hooks

- `gl/scene/gl_scene.cpp` `FGLRenderer::RenderViewpoint`: `SetupView` after `R_SetupFrame`, and GL's
  pitch, roll and view actor taken from it; `FGLRenderer::FrustumAngle` is `ComputeFrustumAngle`.
- `features/visibility/viswalk.cpp`: the player's view actor and every walk's frustum.

GL state it still reads, until the level's GL settings move out of `gl/` (P5): `glset.pixelstretch`.
