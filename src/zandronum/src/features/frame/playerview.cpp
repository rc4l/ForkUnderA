// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/playerview.h"
#include "features/frame/computation/frameclock_compute.h"
#include "features/gamecopy/players.h"   // [rc4l] the camera a view is set up from
#include "features/frame/standaloneframe.h"
#include "features/frame/viewtrace.h"

#include "features/viewsetup/viewsetup.h"
#include "features/viewsetup/computation/viewsetup_compute.h"
#include "features/levelmesh/flatmesh.h"       // [rc4l] SetMainView, EnterScene, LeaveScene
#include "features/draw2d/record.h"
#include "features/sprites/pspritebuild.h"
#include "features/sprites/spritebuild.h"      // [rc4l] CheckFrameSprites
#include "features/draw2d/screenblend.h"
#include "features/fov-interp/fovinterp.h"
#include "features/hwrender/backendonly.h"   // [rc4l] NoteWorldView
#include "features/rendersettings/rendersettings.h"   // [rc4l] gl_frameCount

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
#include "r_main.h"       // [rc4l] fixedcolormap, realfixedcolormap, which a camera texture's view clobbers
#include "r_renderer.h"   // [rc4l] Renderer
#include "textures/textures.h"   // [rc4l] FCanvasTexture

#include <string.h>

// [rc4l] GL's frame globals, which the derivation and the backend read, until they move out of gl/ (P5).
#include "features/rendersettings/rendersettings.h"   // [rc4l] the render settings it reads

EXTERN_CVAR(Bool, cl_capfps)
// [rc4l] A frame clock pinned at this many milliseconds (0 runs it), so captures from two launches show shader animation, warps and fuzz, at the same moment.
CVAR(Int, fua_debug_frameclock, 0, 0)

namespace zx { namespace frame {

// [rc4l] FCanvasTextureInfo::UpdateAll on the copy's list, keeping the colormap globals a camera's view clobbers.
static void RenderCameraTextures()
{
	unsigned char *const savecolormap = fixedcolormap;
	FSpecialColormap *const savecm = realfixedcolormap;
	for (const zx::gamecopy::CameraTexture &c : zx::gamecopy::CameraTextures())
		if (c.record >= 0 && c.texture->NeedsUpdate()) Renderer->RenderCameraRecord(c.texture, c.record, c.fov);
	fixedcolormap = savecolormap;
	realfixedcolormap = savecm;
}

unsigned int FrameClock(unsigned int nowMs)
{
	return ComputeFrameClock(fua_debug_frameclock, nowMs);
}

void BeginPlayerFrame(int player)
{
	const zx::gamecopy::ActorRecord *const cam = zx::gamecopy::CameraOf(player);
	if (zx::viewsetup::CameraChanged(cam != NULL ? cam->id : 0u)) R_ResetViewInterpolation();
	if (cl_capfps || r_NoInterpolate) r_TicFrac = FRACUNIT;
	else r_TicFrac = I_GetTimeFrac(&r_FrameTime);
	r_frameSerial++;
	gl_frameMS = (long)FrameClock(I_MSTime());
	// [rc4l] Camera textures the last frame used, each drawn from the render copy's record of its viewpoint.
	RenderCameraTextures();

	r_viewLens.ratio = WidescreenRatio;
	r_viewLens.fovRatio = zx::viewsetup::ComputeFovRatio(r_viewLens.ratio, AspectTallerThanWide(r_viewLens.ratio));
	gl_fixedcolormap = zx::viewsetup::FixedColormap(player);
	// [rc4l] fov-interp: the sub-tic position of the zoom P_PlayerThink is about to take.
	r_viewLens.fov = FOV_InterpolatedForFrame(player);
}

bool BeginRecordViewpoint(int record, const FViewLens &lens, bool mainview, bool toscreen)
{
	const bool saved = zx::levelmesh::MainView();
	zx::levelmesh::SetMainView(mainview);
	R_SetupFrameFromRecord(record, false);
	SetViewpointUp(lens, mainview, toscreen);
	return saved;
}

void SetViewpointUp(const FViewLens &lens, bool mainview, bool toscreen)
{
	if (mainview && toscreen) NoteMainView();   // [rc4l] fua_viewtrace
	zx::viewsetup::SetViewArea(zx::viewsetup::CurrentView().viewRecord);
	zx::viewsetup::SetupView(zx::gamecopy::ViewCamera(), lens, gl_frameMS);
}

void EndViewpoint(bool savedMainView)
{
	gl_frameCount++;   // [rc4l] right before the interpolations are restored
	R_RestoreLiveInterpolation();
	zx::levelmesh::SetMainView(savedMainView);
}

void MarkMapSection()
{
	if (currentmapsection.Size() == 0) return;
	ClearMapSections();
	MarkMapSectionBit(R_PointInSubsector(viewx, viewy)->mapsection);
}

void RenderPlayerView(int player, int paletteBrightness)
{
	const bool savedMain = BeginRecordViewpoint(zx::gamecopy::IndexOf(zx::gamecopy::CameraOf(player)), r_viewLens, true, true);
	zx::viewsetup::TurnView(viewangle);
	sector_t *const sector = viewsector;
	MarkMapSection();
	StandaloneScene();
	// [rc4l] GL drew the frame first when a check was on, so its sprites are there to compare the derived ones against.
	zx::sprites::CheckFrameSprites();

	// [rc4l] EndDrawScene: the weapon unless a HUD model replaces it, the targeter, and the screen's tint.
	const zx::gamecopy::ActorRecord *const eye = zx::gamecopy::CameraOf(consoleplayer);
	const zx::gamecopy::PlayerRecord *const eyePlayer = eye != NULL ? zx::gamecopy::Player(eye->playerIndex) : NULL;
	zx::sprites::BuildPlayerSprites(sector, in_area, eyePlayer != NULL && eyePlayer->hudModel);
	zx::sprites::BuildTargeterSprites(zx::viewsetup::CurrentView().viewActor);
	zx::sprites::CheckFramePSprites();
	zx::draw2d::ScreenFill fills[2];
	const int n = zx::draw2d::ScreenBlends(sector, in_area, gl_fixedcolormap != 0, paletteBrightness, fills);
	for (int i = 0; i < n; i++) zx::draw2d::Record2D(zx::draw2d::PrepareScreenFill(fills[i]));
	// [Doruk-Aksoy] features/postprocess: the 2D a scene shader runs over ends here, as UZDoom draws the weapon before its scene shaders.
	zx::draw2d::MarkViewQuads();

	zx::hwrender::NoteWorldView();
	EndViewpoint(savedMain);
}

}} // namespace zx::frame
