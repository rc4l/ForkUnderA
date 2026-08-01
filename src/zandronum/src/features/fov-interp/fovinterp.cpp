// [rc4l] fov-interp glue. See fovinterp.h and features/fov-interp/README.md.
//
// Provenance: Q-Zandronum d2475b676 (2022-04-27) + 390ea5ac2 (2023-04-16).
// https://github.com/IgeNiaI/Q-Zandronum/commit/d2475b6760563f4be4b47c4eff0f82cee5a241c8
// https://github.com/IgeNiaI/Q-Zandronum/commit/390ea5ac290d5260415d458b8934518da1bd2289
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fovinterp.h"
#include "computation/fovinterp_compute.h"

#include "doomstat.h"
#include "d_player.h"
#include "r_utility.h"
#include "m_fixed.h"
#include "c_cvars.h"
#include "cl_demo.h"
#include "network.h"

EXTERN_CVAR (Float, cl_fovchangespeed)

// The title-screen camera is not a player view; upstream pins it at the default rather than
// tweening from whatever the last game left behind.
static const float TITLE_FOV = 90.0f;

//===========================================================================
//
// FOV_InterpolatedForFrame
//
//===========================================================================

float FOV_InterpolatedForFrame(player_t *player)
{
	if (player == NULL)
		return TITLE_FOV;

	// Follow the camera when spying another player (chasecam, spectating, demos), matching how
	// D_Display picks the FOV it feeds R_SetFOV.
	player_t *cameraPlayer = (player->camera && player->camera->player) ? player->camera->player : player;

	if (gamestate == GS_TITLELEVEL)
		return TITLE_FOV;

	const float target = zx::FovTargetForWeapon (cameraPlayer->DesiredFOV,
		cameraPlayer->playerstate != PST_DEAD,
		cameraPlayer->ReadyWeapon != NULL,
		cameraPlayer->ReadyWeapon != NULL ? cameraPlayer->ReadyWeapon->FOVScale : 0.f);

	// While the simulation is stopped there is no next step to tween toward, so hold the view
	// exactly where it is instead of drifting. A menu does not pause a client in a live game,
	// which is why the network state is part of the test.
	const bool interpolate = !paused
		&& !CLIENTDEMO_IsPaused()
		&& (NETWORK_GetState() == NETSTATE_CLIENT || menuactive != MENU_On);

	// [rc4l] fixed64: r_TicFrac is 48.16 fixed point. Cross to float via double, never straight
	// from the 64-bit integer — see the fixed64-widening skill. (Q-Zandronum's original used
	// FixedMul() on a float operand, which was already loose at 32 bits and is wrong at 64.)
	const float ticFrac = static_cast<float>(FIXED2DBL (r_TicFrac));

	const float delta = zx::FovRenderDelta (cameraPlayer->FOV, target, cl_fovchangespeed,
		ticFrac, interpolate);

	return zx::FovClamp (cameraPlayer->FOV + delta);
}
