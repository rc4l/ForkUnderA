// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The view a frame is drawn from, set up once without GL: what FGLRenderer::RenderViewpoint worked out for itself and the walks read back from GLRenderer.

#ifndef ZX_VIEWSETUP_H
#define ZX_VIEWSETUP_H

class AActor;

namespace zx { namespace viewsetup {

struct ViewSetup
{
	AActor  *camera;
	AActor  *viewActor;      // [rc4l] the actor the eye is in, not drawn; none in the chase camera or a dead player's death camera
	float    pitch, roll;    // [rc4l] degrees, the pitch stretched by the level's pixel ratio
	float    fov;            // [rc4l] degrees across
	unsigned frustumAngle;   // [rc4l] the walks' frustum half-angle (BAM), 2^32-1 for all round
};

// [rc4l] Set the view up from the camera R_SetupFrame placed, at field of view `fov`; call once a viewpoint, after R_SetupFrame.
void SetupView(AActor *camera, float fov);

// [rc4l] The view last set up.
const ViewSetup &CurrentView();

}} // namespace zx::viewsetup

#endif // ZX_VIEWSETUP_H
