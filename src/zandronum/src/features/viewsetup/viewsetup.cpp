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
#include "dthinker.h"
#include "g_level.h"
#include "r_defs.h"
#include "p_local.h"     // [rc4l] R_PointInSubsector
#include "r_state.h"
#include "features/hwrender/spriteview.h"
#include "features/materials/materials.h"   // [rc4l] the particle textures

// [rc4l] GL's level settings and view state until they move out of gl/ (P5): the pixel stretch, the view's area, and the light class.
#include "gl/data/gl_data.h"
#include "gl/renderer/gl_renderer.h"     // in_area
#include "gl/dynlights/gl_dynlight.h"    // ADynamicLight, STAT_DLIGHT

EXTERN_CVAR(Bool, r_deathcamera)

namespace zx { namespace viewsetup {

static_assert(area_normal == kAreaNormal && area_below == kAreaBelow && area_above == kAreaAbove && area_default == kAreaDefault,
	"area_t must match viewsetup_compute.h");

static ViewSetup g_view = { NULL, NULL, 0.f, 0.f, 90.f, 0xffffffffu, 270.f, 1.f, 0.f, 0.f, 0.f, false };

void SetupView(AActor *camera, float fov, long frameMS)
{
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
	// [BB] consoleplayer can toggle the chase cam; [AK] not in free spectate. The chase camera and a dead player's death camera see the player's own body.
	const bool outside = camera->player != NULL && !CLIENTDEMO_IsInFreeSpectateMode() &&
		(camera->player - players != consoleplayer || !players[consoleplayer].bSpectating) &&
		((players[consoleplayer].cheats & CF_CHASECAM) || (r_deathcamera && camera->health <= 0)) && camera == camera->player->mo;
	g_view.viewActor = outside ? NULL : camera;
}

const ViewSetup &CurrentView() { return g_view; }

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

int g_spriteIndex = 0;

AActor *ViewActor() { return g_view.viewActor; }
void ViewVector(float &x, float &y) { x = g_view.vectorX; y = g_view.vectorY; }
int NextSpriteIndex() { return g_spriteIndex++; }
bool NoPortalRejects(fixed_t, fixed_t) { return false; }
bool NoPortalMirrors() { return false; }
int DynamicLightCount() { return g_view.dynamicLights ? 1 : 0; }
void ViewAngles(float &yaw, float &pitch) { yaw = g_view.yaw; pitch = g_view.pitch; }
FTexture *ParticleTexture(int style) { return zx::materials::ParticleTexture(style); }

const zx::hwrender::SpriteViewProvider g_provider =
{
	ViewActor, ViewVector, NextSpriteIndex, NoPortalRejects, NoPortalMirrors, DynamicLightCount, ViewAngles, ParticleTexture
};

} // namespace

void InstallSpriteView() { zx::hwrender::SetSpriteViewProvider(&g_provider); }

}} // namespace zx::viewsetup
