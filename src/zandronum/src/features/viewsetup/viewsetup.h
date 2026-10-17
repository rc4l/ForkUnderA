// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The view a frame is drawn from, set up once without GL: what FGLRenderer::RenderViewpoint worked out for itself and the walks read back from GLRenderer.

#ifndef ZX_VIEWSETUP_H
#define ZX_VIEWSETUP_H

#include "features/viewsetup/computation/viewsetup_compute.h"   // [rc4l] FixedColormapShade
#include "features/hwrender/computation/backendview_compute.h"   // [rc4l] BackendCamera

class AActor;
namespace zx { namespace gamecopy { struct ActorRecord; } }
struct player_t;
struct FViewLens;

namespace zx { namespace viewsetup {

struct ViewSetup
{
	const zx::gamecopy::ActorRecord *camera;
	const void *viewActor;   // [rc4l] who the eye is in, not drawn; none in the chase camera or a dead player's death camera
	int viewRecord;          // [rc4l] its record in the render copy, -1 for none
	float    pitch, roll;    // [rc4l] degrees, the pitch stretched by the level's pixel ratio
	float    fov;            // [rc4l] degrees across
	float    ratio, fovRatio;   // [MobiusU9B] the lens's width over height (a forced aspect ratio's, not the window's) and the ratio its vertical field of view is derived with
	unsigned frustumAngle;   // [rc4l] the walks' frustum half-angle (BAM), 2^32-1 for all round
	float    yaw;            // [rc4l] degrees, as GL's matrices take the view angle
	float    vectorX, vectorY;   // [rc4l] the unit vector the view faces in the plane
	float    sky1Pos, sky2Pos;   // [rc4l] how far the two sky layers have scrolled this frame, in degrees
	bool     dynamicLights;  // [rc4l] the level has a dynamic light, which decides whether a sprite is lit per pixel at all
};

// [rc4l] Set the view up from the camera R_SetupFrame placed, through `lens` and at the frame's clock; call once a viewpoint, after R_SetupFrame.
void SetupView(const zx::gamecopy::ActorRecord *camera, const FViewLens &lens, long frameMS);

// [rc4l] SetViewAngle: turn the view to `viewAngle`, as a mirror's walk turns it and back, R_SetViewAngle with it.
void TurnView(unsigned viewAngle);

// [rc4l] SetViewArea: the render sector the eye is in, the eye kept inside it unless the actor of record `viewRecord` is an unrestricted spectator, and the area against a height transfer.
void SetViewArea(int viewRecord);

// [rc4l] SetFixedColormap: the colormap player number `player`'s powers fix the view in (invulnerability's inverse, which also clears extralight, a special one, light amplification or a torch), or CM_DEFAULT.
int FixedColormap(int player);

// [rc4l] What GL's shader and the backend's do to a texel under `colormap`, from its special colormap and r_enhanced_nightvision.
FixedColormapShade FixedColormapShadeFor(int colormap);

// [rc4l] Register the sprite view this view answers, for a frame GL is not drawing.
void InstallSpriteView();

// [rc4l] The view last set up.
const ViewSetup &CurrentView();

// [rc4l] The current view's eye and turn as a backend's camera takes them: where it stands, its yaw, its pitch as the playsim aims it, and GL's pixel stretch; the projection is left to the caller.
void ViewCameraFromEngine(zx::hwrender::BackendCamera &c);

// [rc4l] Whether the camera differs from the last frame's, remembering it, so a new one starts uninterpolated instead of sweeping across from the old.
bool CameraChanged(unsigned int camera);

// [rc4l] Forget the last camera, so the next frame starts uninterpolated (a new frame buffer, a game restart).
void ForgetCamera();

// [rc4l] Whether sprites, the weapon and decals take dynamic light this frame: lights on and in the view, no fixed colormap, and r_light_sprites.
bool SpritesTakeDynamicLight();

}} // namespace zx::viewsetup

#endif // ZX_VIEWSETUP_H
