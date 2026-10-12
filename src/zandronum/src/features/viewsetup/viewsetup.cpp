// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/viewsetup/viewsetup.h"
#include "features/viewsetup/computation/viewsetup_compute.h"

#include "actor.h"
#include "d_player.h"
#include "doomstat.h"
#include "r_utility.h"   // viewpitch
#include "v_video.h"     // AspectMultiplier, WidescreenRatio
#include "cl_demo.h"     // CLIENTDEMO_IsInFreeSpectateMode
#include "c_cvars.h"

// [rc4l] The level's pixel stretch, among GL's level settings until they move out of gl/ (P5).
#include "gl/data/gl_data.h"

EXTERN_CVAR(Bool, r_deathcamera)

namespace zx { namespace viewsetup {

static ViewSetup g_view = { NULL, NULL, 0.f, 0.f, 90.f, 0xffffffffu };

void SetupView(AActor *camera, float fov)
{
	g_view.camera = camera;
	g_view.fov = fov;
	// [rc4l] The playsim treats pitch as 1:1, so it is scaled for the pixel stretch the view is drawn with.
	g_view.pitch = ComputeStretchedPitch(viewpitch, glset.pixelstretch);
	g_view.roll = (float)(camera->roll >> ANGLETOFINESHIFT) * 360.0f / FINEANGLES;
	g_view.frustumAngle = ComputeFrustumAngle(g_view.pitch, fov, AspectMultiplier(WidescreenRatio));
	// [BB] consoleplayer can toggle the chase cam; [AK] not in free spectate. The chase camera and a dead player's death camera see the player's own body.
	const bool outside = camera->player != NULL && !CLIENTDEMO_IsInFreeSpectateMode() &&
		(camera->player - players != consoleplayer || !players[consoleplayer].bSpectating) &&
		((players[consoleplayer].cheats & CF_CHASECAM) || (r_deathcamera && camera->health <= 0)) && camera == camera->player->mo;
	g_view.viewActor = outside ? NULL : camera;
}

const ViewSetup &CurrentView() { return g_view; }

}} // namespace zx::viewsetup
