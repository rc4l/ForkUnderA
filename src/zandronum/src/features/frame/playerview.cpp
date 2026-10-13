// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/playerview.h"
#include "features/frame/standaloneframe.h"

#include "features/viewsetup/viewsetup.h"
#include "features/viewsetup/computation/viewsetup_compute.h"
#include "features/levelmesh/flatmesh.h"       // [rc4l] SetMainView, EnterScene, LeaveScene
#include "features/hwrender/hud2d.h"           // [rc4l] StandaloneActive, Record2D
#include "features/hwrender/diligent/dglive.h" // [rc4l] SetMainViewLens
#include "features/sprites/pspritebuild.h"
#include "features/draw2d/screenblend.h"
#include "features/fov-interp/fovinterp.h"
#include "features/hwrender/backendonly.h"   // [rc4l] NoteWorldView

#include "c_cvars.h"
#include "d_player.h"
#include "doomstat.h"
#include "i_system.h"
#include "p_effect.h"
#include "p_local.h"
#include "r_state.h"
#include "r_utility.h"
#include "v_video.h"
#include "r_data/r_interpolate.h"

#include <string.h>

// [rc4l] GL's frame globals, which the derivation and the backend read, until they move out of gl/ (P5).
#include "gl/data/gl_data.h"           // [rc4l] gl_frameMS, currentmapsection
#include "gl/renderer/gl_renderer.h"   // [rc4l] in_area
#include "gl/renderer/gl_lightdata.h"  // [rc4l] gl_fixedcolormap
#include "gl/models/gl_models.h"       // [rc4l] gl_IsHUDModelForPlayerAvailable

EXTERN_CVAR(Bool, cl_capfps)
extern long gl_frameCount;

namespace zx { namespace frame {

namespace {

// [rc4l] The camera the last frame was drawn from, so a new one starts uninterpolated instead of sweeping across from it.
AActor *g_lastCamera = NULL;

} // namespace

void ForgetCamera() { g_lastCamera = NULL; }

FrameLens BeginPlayerFrame(player_t *player)
{
	if (player->camera != g_lastCamera)
	{
		R_ResetViewInterpolation();
		g_lastCamera = player->camera;
	}
	if (cl_capfps || r_NoInterpolate) r_TicFrac = FRACUNIT;
	else r_TicFrac = I_GetTimeFrac(&r_FrameTime);
	gl_frameMS = I_MSTime();
	P_FindParticleSubsectors();
	// [rc4l] Camera textures the last frame used, each drawn by the renderer's RenderTextureView.
	FCanvasTextureInfo::UpdateAll();

	FrameLens lens;
	lens.ratio = WidescreenRatio;
	lens.fovRatio = zx::viewsetup::ComputeFovRatio(lens.ratio, AspectTallerThanWide(lens.ratio));
	gl_fixedcolormap = zx::viewsetup::FixedColormap(player);
	// [rc4l] fov-interp: the sub-tic position of the zoom P_PlayerThink is about to take.
	lens.fov = FOV_InterpolatedForFrame(player);
	return lens;
}

bool BeginViewpoint(AActor *camera, float fov, float fovRatio, bool mainview, bool toscreen)
{
	const bool saved = zx::levelmesh::MainView();
	zx::levelmesh::SetMainView(mainview);
	if (mainview && toscreen) zx::hwrender::SetMainViewLens(fov, fovRatio);
	R_SetupFrame(camera);
	zx::viewsetup::SetViewArea(zx::viewsetup::CurrentView().viewActor);
	zx::viewsetup::SetupView(camera, fov, gl_frameMS);
	return saved;
}

void EndViewpoint(bool savedMainView)
{
	gl_frameCount++;   // [rc4l] right before the interpolations are restored
	interpolator.RestoreInterpolations();
	zx::levelmesh::SetMainView(savedMainView);
}

void MarkMapSection()
{
	if (currentmapsection.Size() == 0) return;
	const int mapsection = R_PointInSubsector(viewx, viewy)->mapsection;
	memset(&currentmapsection[0], 0, currentmapsection.Size());
	currentmapsection[mapsection >> 3] |= 1 << (mapsection & 7);
}

void RenderPlayerView(player_t *player, int paletteBrightness)
{
	const FrameLens lens = BeginPlayerFrame(player);
	const bool savedMain = BeginViewpoint(player->camera, lens.fov, lens.fovRatio, true, true);
	zx::viewsetup::TurnView(viewangle);
	sector_t *const sector = viewsector;
	MarkMapSection();
	if (zx::hwrender::StandaloneActive())
	{
		zx::levelmesh::EnterScene();
		const SceneHooks none = { NULL, NULL };
		// [rc4l] Always true here, since BeginBackendOnly keeps the map bake on, which a moved sector otherwise waits for.
		StandaloneScene(none);
		zx::levelmesh::LeaveScene();
	}

	// [rc4l] EndDrawScene: the weapon unless a HUD model replaces it, the targeter, and the screen's tint.
	zx::sprites::BuildPlayerSpritesBobbed(sector, in_area, gl_IsHUDModelForPlayerAvailable(players[consoleplayer].camera->player));
	zx::sprites::BuildTargeterSprites(zx::viewsetup::CurrentView().viewActor);
	zx::sprites::CheckFramePSprites();
	zx::draw2d::ScreenFill fills[2];
	const int n = zx::draw2d::ScreenBlends(sector, in_area, gl_fixedcolormap != 0, paletteBrightness, fills);
	for (int i = 0; i < n; i++) zx::hwrender::Record2D(zx::draw2d::PrepareScreenFill(fills[i]));

	zx::hwrender::NoteWorldView();
	EndViewpoint(savedMain);
}

}} // namespace zx::frame
