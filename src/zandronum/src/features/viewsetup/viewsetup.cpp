// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/viewsetup/viewsetup.h"
#include "features/viewsetup/computation/viewsetup_compute.h"

#include <limits.h>

#include "actor.h"
#include "d_player.h"
#include "doomstat.h"
#include "r_utility.h"   // [rc4l] viewpitch
#include "v_video.h"     // [rc4l] AspectMultiplier, WidescreenRatio
#include "cl_demo.h"     // [rc4l] CLIENTDEMO_IsInFreeSpectateMode
#include "c_cvars.h"
#include "dthinker.h"
#include "g_level.h"
#include "r_defs.h"
#include "p_local.h"     // [rc4l] R_PointInSubsector
#include "r_state.h"
#include "features/hwrender/spriteview.h"
#include "features/materials/materials.h"   // [rc4l] the particle textures

// [rc4l] GL's level settings and view state until they move out of gl/ (P5): the pixel stretch, the view's area, and the light class.
#include "features/rendersettings/rendersettings.h"   // [rc4l] the render settings it reads
#include "features/dynlights/dynlight.h"   // [rc4l] ADynamicLight, FLightNode
#include "g_shared/a_artifacts.h"        // [rc4l] APowerTorch, APowerLightAmp
#include "r_data/colormaps.h"            // [rc4l] INVERSECOLORMAP

EXTERN_CVAR(Bool, r_deathcamera)
EXTERN_CVAR(Bool, gl_enhanced_nightvision)

namespace zx { namespace viewsetup {

static_assert(area_normal == kAreaNormal && area_below == kAreaBelow && area_above == kAreaAbove && area_default == kAreaDefault,
	"area_t must match viewsetup_compute.h");

static ViewSetup g_view = { NULL, NULL, 0.f, 0.f, 90.f, 0xffffffffu, 270.f, 1.f, 0.f, 0.f, 0.f, false };

// [rc4l] The sprite order index InstallSpriteView hands out, from zero each view as GL's CreateScene starts gl_spriteindex.
static int g_spriteIndex = 0;

void SetupView(AActor *camera, float fov, long frameMS)
{
	g_spriteIndex = 0;
	g_view.camera = camera;
	g_view.fov = fov;
	ComputeViewAngleVector(viewangle, g_view.yaw, g_view.vectorX, g_view.vectorY);
	g_view.sky1Pos = ComputeSkyScroll(frameMS, level.skyspeed1);
	g_view.sky2Pos = ComputeSkyScroll(frameMS, level.skyspeed2);
	TThinkerIterator<ADynamicLight> lights(STAT_DLIGHT);
	g_view.dynamicLights = lights.Next() != NULL;
	// [rc4l] The playsim treats pitch as 1:1, so it is scaled for the pixel stretch the view is drawn with.
	g_view.pitch = ComputeStretchedPitch(viewpitch, glset.pixelstretch);
	g_view.roll = (float)(camera->roll >> ANGLETOFINESHIFT) * 360.0f / FINEANGLES;
	g_view.frustumAngle = ComputeFrustumAngle(g_view.pitch, fov, AspectMultiplier(WidescreenRatio));
	// [rc4l] The chase camera (consoleplayer only, never in free spectate) and a dead player's death camera see the player's own body.
	const bool outside = camera->player != NULL && ComputeSeesOwnBody(true, CLIENTDEMO_IsInFreeSpectateMode(), camera->player - players == consoleplayer,
		players[consoleplayer].bSpectating, (players[consoleplayer].cheats & CF_CHASECAM) != 0, r_deathcamera, camera->health <= 0,
		camera == camera->player->mo);
	g_view.viewActor = outside ? NULL : camera;
}

const ViewSetup &CurrentView() { return g_view; }

static_assert(CM_DEFAULT == kCmDefault && CM_FIRSTSPECIALCOLORMAP == kCmFirstSpecial && CM_FOGLAYER == kCmFogLayer && CM_LITE == kCmLite && CM_TORCH == kCmTorch &&
	NOFIXEDCOLORMAP == kNoFixedColormap && INVERSECOLORMAP == kInverseColormap, "the colormap values must match viewsetup_compute.h");

int FixedColormap(player_t *player)
{
	player_t *cplayer = player->camera->player;
	FixedColormapIn in = { cplayer != NULL, false, kNoFixedColormap, -1, kNoLightPower };
	if (cplayer != NULL)
	{
		in.inverse = cplayer->extralight == INT_MIN;
		// [rc4l] The inverse map draws without the extra light it was signalled through.
		if (in.inverse) extralight = 0;
		in.fixedColormap = cplayer->fixedcolormap;
		in.fixedLightLevel = cplayer->fixedlightlevel;
		for (AInventory *inv = cplayer->mo->Inventory; inv; inv = inv->Inventory)
		{
			if (inv->IsKindOf(RUNTIME_CLASS(APowerTorch))) in.lightPower = kTorchPower;
			else if (inv->IsKindOf(RUNTIME_CLASS(APowerLightAmp))) in.lightPower = kLightAmpPower;
		}
	}
	return ComputeFixedColormap(in);
}

FixedColormapShade FixedColormapShadeFor(int colormap)
{
	static const float none[3] = { 0.f, 0.f, 0.f };
	const int special = colormap - CM_FIRSTSPECIALCOLORMAP;
	const bool isSpecial = special >= 0 && special < (int)SpecialColormaps.Size();
	return ComputeFixedColormapShade(colormap, SpecialColormaps.Size(), isSpecial ? SpecialColormaps[special].ColorizeStart : none,
		isSpecial ? SpecialColormaps[special].ColorizeEnd : none, gl_enhanced_nightvision);
}

void TurnView(unsigned viewAngle)
{
	ComputeViewAngleVector(viewAngle, g_view.yaw, g_view.vectorX, g_view.vectorY);
	R_SetViewAngle();
}

void SetViewArea(AActor *viewActor)
{
	// [rc4l] The render sector suits the eye's place in a hardware view better than the sector it stands in.
	viewsector = R_PointInSubsector(viewx, viewy)->render_sector;
	// [rc4l] [AK] A spectator without physical restrictions may pass through floors and ceilings.
	if (!P_IsSpectatorUnrestricted(viewActor))
		viewz = fixed_t::FromRaw(ComputeClampedViewZ(viewz.Raw(), viewsector->floorplane.ZatPoint(viewx, viewy).Raw(),
			viewsector->ceilingplane.ZatPoint(viewx, viewy).Raw()));
	// [rc4l] The height transfer of the render sector, not the current one.
	sector_t *hs = viewsector->heightsec;
	const bool transfer = hs != NULL && !(hs->MoreFlags & SECF_IGNOREHEIGHTSEC);
	in_area = (area_t)ComputeViewArea(transfer, viewz.Raw(), transfer ? hs->floorplane.ZatPoint(viewx, viewy).Raw() : 0,
		transfer ? hs->ceilingplane.ZatPoint(viewx, viewy).Raw() : 0, transfer && (hs->MoreFlags & SECF_FAKEFLOORONLY));
}

namespace {

AActor *ViewActor() { return g_view.viewActor; }
void ViewVector(float &x, float &y) { x = g_view.vectorX; y = g_view.vectorY; }
int NextSpriteIndex() { return g_spriteIndex++; }
bool NoPortalRejects(fixed_t, fixed_t) { return false; }
bool NoPortalMirrors() { return false; }
int DynamicLightCount() { return g_view.dynamicLights ? 1 : 0; }
void ViewAngles(float &yaw, float &pitch) { yaw = g_view.yaw; pitch = g_view.pitch; }

const zx::hwrender::SpriteViewProvider g_provider =
{
	ViewActor, ViewVector, NextSpriteIndex, NoPortalRejects, NoPortalMirrors, DynamicLightCount, ViewAngles
};

} // namespace

void InstallSpriteView() { zx::hwrender::SetSpriteViewProvider(&g_provider); }

}} // namespace zx::viewsetup
