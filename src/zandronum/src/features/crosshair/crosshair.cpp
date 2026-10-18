// [MobiusU9B] See crosshair.h; the arithmetic lives in computation/ so this file is only engine glue.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "actor.h"
#include "c_cvars.h"
#include "d_player.h"
#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_trace.h"
#include "r_main.h"
#include "r_state.h"
#include "r_utility.h"
#include "v_font.h"
#include "v_text.h"
#include "v_video.h"
#include "textures/textures.h"
#include "g_shared/a_pickups.h"
#include "g_shared/sbar.h"
#include "g_shared/st_hud.h"
#ifndef NO_GL
#include "gl/data/gl_data.h"
#endif

#include "features/crosshair/crosshair.h"
#include "features/crosshair/computation/targetinfo_compute.h"
#include "features/crosshair/computation/telemeter_compute.h"
#include "features/fov-interp/fovinterp.h"

#include <cmath>
#include <string>
#include <vector>

EXTERN_CVAR(Bool, crosshairscale)
EXTERN_CVAR(Bool, crosshairgrow)
EXTERN_CVAR(Int, screenblocks)

namespace
{
	// [MobiusU9B] Writing back only on change stops the callback re-entering forever.
	void ClampCVar(FFloatCVar &cvar, double (*clampFn)(double))
	{
		const float clamped = static_cast<float>(clampFn(cvar));
		if (clamped != cvar)
			cvar = clamped;
	}

	void ClampIntCVar(FIntCVar &cvar, int lo, int hi)
	{
		const int clamped = zx::crosshair::ComputeCrosshairClampInt(cvar, lo, hi);
		if (clamped != cvar)
			cvar = clamped;
	}
}

// [MobiusU9B] 1 and 0 leave the crosshair exactly where and how big the stock settings draw it.
CUSTOM_CVAR(Float, cl_fua_crosshair_scale, 1.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	ClampCVar(self, zx::crosshair::ComputeCrosshairClampScale);
}
CUSTOM_CVAR(Float, cl_fua_crosshair_x, 0.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	ClampCVar(self, zx::crosshair::ComputeCrosshairClampOffset);
}
CUSTOM_CVAR(Float, cl_fua_crosshair_y, 0.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	ClampCVar(self, zx::crosshair::ComputeCrosshairClampOffset);
}

// [MobiusU9B] The Target info toggles, replacing Zandronum's cl_identifytarget/cl_identifymonsters levels with the same defaults: player names and the Ally / Enemy label.
CVAR(Bool, cl_fua_identify_players, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, cl_fua_identify_monsters, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, cl_fua_identify_health, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, cl_fua_identify_weapon, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, cl_fua_identify_class, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, cl_fua_identify_allyenemy, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, cl_fua_identify_ghost, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

// [MobiusU9B] Bottom of the screen by default, so the identify text stays where Zandronum players expect it until they move it.
CUSTOM_CVAR(Int, cl_fua_identify_position, zx::crosshair::kTargetInfoAtBottom, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	ClampIntCVar(self, zx::crosshair::kTargetInfoAtBottom, zx::crosshair::kTargetInfoNextToCrosshair);
}
CUSTOM_CVAR(Int, cl_fua_identify_actorname, zx::crosshair::kActorNameOff, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	ClampIntCVar(self, zx::crosshair::kActorNameOff, zx::crosshair::kActorNameTag);
}

// [MobiusU9B] How big the text beside the crosshair is, as a multiple of chat's size; spacing and offsets grow with it.
CUSTOM_CVAR(Float, cl_fua_xhairinfo_textsize, 1.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	ClampCVar(self, zx::crosshair::ComputeTargetInfoClampTextSize);
}

// [MobiusU9B] Each element's nudge from its stacked place beside the crosshair, in HUD-text pixels.
#define ZX_TARGETINFO_OFFSET_CVAR(name) \
	CUSTOM_CVAR(Int, name, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) \
	{ \
		ClampIntCVar(self, -zx::crosshair::kTargetInfoMaxOffset, zx::crosshair::kTargetInfoMaxOffset); \
	}
ZX_TARGETINFO_OFFSET_CVAR(cl_fua_xhairinfo_name_x)
ZX_TARGETINFO_OFFSET_CVAR(cl_fua_xhairinfo_name_y)
ZX_TARGETINFO_OFFSET_CVAR(cl_fua_xhairinfo_info_x)
ZX_TARGETINFO_OFFSET_CVAR(cl_fua_xhairinfo_info_y)
ZX_TARGETINFO_OFFSET_CVAR(cl_fua_xhairinfo_actor_x)
ZX_TARGETINFO_OFFSET_CVAR(cl_fua_xhairinfo_actor_y)
ZX_TARGETINFO_OFFSET_CVAR(cl_fua_xhairinfo_telemeter_x)
ZX_TARGETINFO_OFFSET_CVAR(cl_fua_xhairinfo_telemeter_y)
#undef ZX_TARGETINFO_OFFSET_CVAR

// [MobiusU9B] The telemeter: off until asked for, then measuring every kind until one is turned off.
CVAR(Bool, cl_fua_telemeter, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, cl_fua_telemeter_decorations, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, cl_fua_telemeter_monsters, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, cl_fua_telemeter_players, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, cl_fua_telemeter_projectiles, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

namespace
{
	// [MobiusU9B] The bottom-of-screen message's hold and fade, so text beside the crosshair lingers the same way.
	const int kHoldTics = 2 * TICRATE;
	const int kFadeTics = static_cast<int>(0.35f * TICRATE);

	// [MobiusU9B] Pixels between the crosshair's right edge and the text, in HUD-text pixels.
	const int kGap = 4;

	// [MobiusU9B] The actor-name line's colour, apart from the team or grey of the name above it.
	const EColorRange kActorNameColor = CR_TAN;

	// [MobiusU9B] The telemeter's colour, apart from the name lines and the health colours.
	const EColorRange kTelemeterColor = CR_CYAN;

	// [MobiusU9B] One block of text beside the crosshair, kept after its target leaves the crosshair so it can fade.
	struct BesideText
	{
		FString text;
		EColorRange color;
		int seenTic;
		bool valid;
		FIntCVar *offsetX;
		FIntCVar *offsetY;

		void Show(const char *newText, EColorRange newColor)
		{
			text = newText;
			color = newColor;
			seenTic = gametic;
			valid = true;
		}

		double Alpha() const
		{
			return valid ? zx::crosshair::ComputeIdentifyFadeAlpha(gametic - seenTic, kHoldTics, kFadeTics) : 0.0;
		}
	};

	// [MobiusU9B] Drawn and stacked in this order.
	BesideText g_Beside[] = {
		{ FString(), CR_GRAY, 0, false, &cl_fua_xhairinfo_name_x, &cl_fua_xhairinfo_name_y },
		{ FString(), CR_GRAY, 0, false, &cl_fua_xhairinfo_info_x, &cl_fua_xhairinfo_info_y },
		{ FString(), kActorNameColor, 0, false, &cl_fua_xhairinfo_actor_x, &cl_fua_xhairinfo_actor_y },
		{ FString(), kTelemeterColor, 0, false, &cl_fua_xhairinfo_telemeter_x, &cl_fua_xhairinfo_telemeter_y },
	};
	BesideText &g_Name = g_Beside[0];
	BesideText &g_Details = g_Beside[1];
	BesideText &g_ActorName = g_Beside[2];
	BesideText &g_Telemeter = g_Beside[3];

	// [MobiusU9B] The tic and viewer the view sweep last ran for; the viewer is only compared, never followed.
	int g_SweepTic = -1;
	const AActor *g_SweepViewer = NULL;

	// [MobiusU9B] Where an actor looks from, as HUD_ScanForTarget always aimed it.
	fixed_t EyeZ(AActor *source)
	{
		return source->player ? source->player->viewz : source->z + source->height / 2;
	}

	fixed_t DoubleToFixed(double value)
	{
		return fixed_t(std::llround(value * 65536.0));
	}

	// [MobiusU9B] How the running renderer maps the screen to the world, so a line can be aimed through any pixel of the view.
	zx::crosshair::ReticleProjection ReadProjection()
	{
		// [MobiusU9B] A NO_GL build (the headless server) has no glset to read, and no GL renderer to match.
#ifndef NO_GL
		if (currentrenderer == 1)
		{
			return zx::crosshair::ComputeGLReticleProjection(FOV_InterpolatedForFrame(consoleplayer), WidescreenRatio,
				viewwidth, zx::crosshair::ComputeGLViewportHeight(screenblocks, SCREENHEIGHT), glset.pixelstretch);
		}
#endif
		return zx::crosshair::ReticleProjection{ FIXED2DBL(FocalLengthX), FIXED2DBL(FocalLengthY), 1.0, true };
	}

	// [MobiusU9B] The line out of an actor's eyes through the drawn crosshair, which the offset sliders may have moved off the view's centre.
	zx::crosshair::SweepRay GetViewRay(AActor *source)
	{
		const zx::crosshair::CrosshairRect reticle = zx::crosshair::GetRect(NULL, 0);
		const zx::crosshair::SweepVector line = zx::crosshair::ComputeReticleDirection(
			zx::crosshair::ComputeBamRadians(static_cast<int64_t>(source->angle)), -zx::crosshair::ComputeBamRadians(source->pitch.Raw()),
			reticle.x - (viewwindowx + viewwidth / 2), reticle.y - (viewwindowy + viewheight / 2), ReadProjection());
		return zx::crosshair::SweepRay{ FIXED2DBL(source->x), FIXED2DBL(source->y), FIXED2DBL(EyeZ(source)), line.x, line.y, line.z };
	}

	// [MobiusU9B] The renderer's own reasons for not drawing a sprite, so the sweep never names or measures something you cannot see.
	bool IsInvisible(AActor *actor)
	{
		return (actor->sprite == 0) || (actor->flags & MF_NOSECTOR) || (actor->renderflags & RF_INVISIBLE)
			|| !actor->RenderStyle.IsVisible(actor->alpha) || !actor->IsVisibleToPlayer();
	}

	// [MobiusU9B] An actor's collision box, the one that touches other bodies.
	zx::crosshair::SweepBox BodyBox(AActor *actor)
	{
		return zx::crosshair::SweepBox{ FIXED2DBL(actor->x), FIXED2DBL(actor->y), FIXED2DBL(actor->z),
			FIXED2DBL(actor->radius), FIXED2DBL(actor->height) };
	}

	// [MobiusU9B] An actor's shootable box, which only a mod makes bigger than its body.
	zx::crosshair::SweepBox AttackBox(AActor *actor)
	{
		return zx::crosshair::SweepBox{ FIXED2DBL(actor->x), FIXED2DBL(actor->y), FIXED2DBL(actor->z),
			FIXED2DBL(actor->GetAttackRadius()), FIXED2DBL(actor->GetAttackHeight()) };
	}

	zx::crosshair::SweepActor DescribeForSweep(AActor *actor, AActor *viewer)
	{
		const bool isMissile = (actor->flags & MF_MISSILE) != 0;
		return zx::crosshair::SweepActor{
			actor == viewer,
			IsInvisible(actor),
			(actor->flags & MF_NOBLOCKMAP) == 0,
			isMissile,
			isMissile && (actor->target == viewer),
			actor->IsKindOf(RUNTIME_CLASS(APlayerPawn)),
			(actor->flags3 & MF3_ISMONSTER) != 0,
			actor->IsKindOf(RUNTIME_CLASS(AInventory)),
		};
	}

	// [MobiusU9B] What reads the view sweep this frame, from the cvars and server flags.
	struct SweepReaders
	{
		int nameMode;
		bool nameAllowed;
		zx::crosshair::TelemeterToggles telemeter;
		bool telemeterActive;
		bool serverHidesKinds;
	};

	SweepReaders ReadSweepReaders()
	{
		SweepReaders readers;
		readers.nameMode = zx::crosshair::ComputeCrosshairClampInt(cl_fua_identify_actorname, zx::crosshair::kActorNameOff, zx::crosshair::kActorNameTag);
		readers.serverHidesKinds = (zadmflags & ZADF_NO_IDENTIFY_TARGET) != 0;
		readers.nameAllowed = zx::crosshair::ComputeActorNameAllowed(readers.nameMode, readers.serverHidesKinds, (zadmflags & ZADF_FUA_NO_IDENTIFY_ACTORS) != 0);
		readers.telemeter = zx::crosshair::TelemeterToggles{ cl_fua_telemeter, cl_fua_telemeter_decorations, cl_fua_telemeter_monsters,
			cl_fua_telemeter_players, cl_fua_telemeter_projectiles };
		readers.telemeterActive = zx::crosshair::ComputeTelemeterActive(readers.telemeter);
		return readers;
	}

	// [MobiusU9B] Names the item, decoration, projectile or corpse the sweep found, or leaves the line to fade when it found nothing.
	void ShowActorName(AActor *actor, int mode)
	{
		if ((actor == NULL) || (actor->STFlags & STFL_DONTIDENTIFYTARGET))
			return;

		// [MobiusU9B] A living monster or player belongs to the identify rows, so drop the line at once rather than let an older name sit beside theirs.
		if (!zx::crosshair::ComputeActorNameCoversTarget(actor->player != NULL, (actor->flags3 & MF3_ISMONSTER) != 0, (actor->flags & MF_SHOOTABLE) != 0))
		{
			g_ActorName.valid = false;
			return;
		}

		// [MobiusU9B] Same as the identify name: a tag may carry unformatted colour codes.
		FString tag = actor->GetTag();
		V_ColorizeString(tag);
		const std::string text = zx::crosshair::ComputeActorNameText(mode, tag.GetChars(), actor->GetClass()->TypeName.GetChars());
		g_ActorName.Show(text.c_str(), kActorNameColor);
	}

	// [MobiusU9B] One pass over every sector's actors for what the view line enters before a wall, which the engine trace can't do for flagless or out-of-blockmap actors.
	void SweepFromView(AActor *viewer, const SweepReaders &readers)
	{
		const int range = zx::crosshair::ComputeSweepRange(readers.telemeterActive);
		FTraceResults wall;
		zx::crosshair::TraceFromView(viewer, 0, range, wall);
		// [MobiusU9B] The trace's map-edge clip can overshoot, so the range is the cap.
		const double traced = FIXED2DBL(wall.Distance);
		const double wallDistance = traced < range ? traced : range;

		const zx::crosshair::SweepRay ray = GetViewRay(viewer);

		std::vector<zx::crosshair::SweepHit> hits;
		std::vector<AActor *> hitActors;
		for (int i = 0; i < numsectors; ++i)
		{
			for (AActor *actor = sectors[i].thinglist; actor != NULL; actor = actor->snext)
			{
				const zx::crosshair::SweepEntry entry = zx::crosshair::ComputeRayBoxEntry(ray, AttackBox(actor), wallDistance);
				if (!entry.hit)
					continue;

				const zx::crosshair::SweepActor info = DescribeForSweep(actor, viewer);
				if (!zx::crosshair::ComputeSweepConsiders(info))
					continue;

				// [MobiusU9B] The identify trace's anti-bleed guard: a box widened past the body must not be seen through a wall.
				if (actor->AttackHitboxEnlarged() && !P_CheckSight(actor, viewer, SF_IGNOREVISIBILITY | SF_IGNOREWATERBOUNDARY))
					continue;

				const zx::crosshair::SweepKind kind = zx::crosshair::ComputeSweepKind(info);
				hits.push_back(zx::crosshair::SweepHit{ entry.distance, zx::crosshair::ComputeTelemeterMeasures(readers.telemeter, kind, readers.serverHidesKinds) });
				hitActors.push_back(actor);
			}
		}

		if (readers.nameAllowed)
		{
			const int named = zx::crosshair::ComputeNearestSweepHit(hits, false, zx::crosshair::kIdentifyRange);
			ShowActorName(named >= 0 ? hitActors[static_cast<size_t>(named)] : NULL, readers.nameMode);
		}

		if (readers.telemeterActive)
		{
			const int measured = zx::crosshair::ComputeNearestSweepHit(hits, true, zx::crosshair::kTelemeterRange);
			if (measured >= 0)
			{
				// [MobiusU9B] Body to body along the view line, so the reading is 0 when they touch.
				const size_t index = static_cast<size_t>(measured);
				const zx::crosshair::SweepEntry body = zx::crosshair::ComputeRayBoxEntry(ray, BodyBox(hitActors[index]), wallDistance);
				const double gap = zx::crosshair::ComputeTelemeterGap(body, hits[index].distance, zx::crosshair::ComputeRayBoxExit(ray, BodyBox(viewer)));
				g_Telemeter.Show(zx::crosshair::ComputeTelemeterText(gap).c_str(), kTelemeterColor);
			}
			else
			{
				g_Telemeter.valid = false;
			}
		}
	}

	// [MobiusU9B] Clears the lines that are switched off at once, and runs the sweep at most once a tic for those that are on.
	void UpdateSweepRows(AActor *viewer)
	{
		const SweepReaders readers = ReadSweepReaders();
		if (!readers.nameAllowed)
			g_ActorName.valid = false;
		if (!readers.telemeterActive)
			g_Telemeter.valid = false;
		if (!readers.nameAllowed && !readers.telemeterActive)
			return;

		// [MobiusU9B] Aim only changes once a tic, so sweeping every frame would find the same thing again.
		if (!zx::crosshair::ComputeSweepDue(g_SweepTic, gametic, viewer == g_SweepViewer))
			return;
		g_SweepTic = gametic;
		g_SweepViewer = viewer;
		SweepFromView(viewer, readers);
	}
}

namespace zx { namespace crosshair {

CrosshairRect GetRect(FTexture *image, fixed_t growSize)
{
	CrosshairRectInput in{};
	in.imageWidth = image != NULL ? image->GetWidth() : 0;
	in.imageHeight = image != NULL ? image->GetHeight() : 0;
	in.scaleWithScreen = crosshairscale;
	in.screenHeight = SCREENHEIGHT;
	in.grow = crosshairgrow;
	in.growSize = growSize.Raw();
	in.userScale = cl_fua_crosshair_scale;
	in.viewX = viewwindowx;
	in.viewY = viewwindowy;
	in.viewWidth = viewwidth;
	in.viewHeight = viewheight;
	in.offsetXPercent = cl_fua_crosshair_x;
	in.offsetYPercent = cl_fua_crosshair_y;
	return ComputeCrosshairRect(in);
}

IdentifyToggles ReadIdentifyToggles()
{
	return IdentifyToggles{ cl_fua_identify_players, cl_fua_identify_monsters, cl_fua_identify_health, cl_fua_identify_weapon,
		cl_fua_identify_class, cl_fua_identify_allyenemy, cl_fua_identify_ghost };
}

bool IsTargetInfoNextToCrosshair()
{
	return ComputeCrosshairClampInt(cl_fua_identify_position, kTargetInfoAtBottom, kTargetInfoNextToCrosshair) == kTargetInfoNextToCrosshair;
}

bool TraceFromView(AActor *source, DWORD actorMask, int range, FTraceResults &trace)
{
	const SweepRay ray = GetViewRay(source);
	return Trace( source->x, source->y, EyeZ(source), source->Sector, DoubleToFixed(ray.dx), DoubleToFixed(ray.dy), DoubleToFixed(ray.dz),
		range * FRACUNIT, actorMask, ML_BLOCKEVERYTHING, source, trace, TRACE_NoSky );
}

void DrawBesideCrosshair(AActor *viewer, const TargetText *identify)
{
	if ((viewer == NULL) || (SmallFont == NULL) || (StatusBar == NULL))
	{
		for (BesideText &element : g_Beside)
			element.valid = false;
		g_SweepViewer = NULL;
		return;
	}

	if (identify != NULL)
	{
		// [MobiusU9B] The details start with the newline that joined them to the name in the bottom-of-screen message.
		const char *details = identify->details.GetChars();
		if (*details == '\n')
			++details;
		g_Name.Show(identify->name.GetChars(), identify->color);
		g_Details.Show(details, identify->color);
	}
	else if (!IsTargetInfoNextToCrosshair())
	{
		g_Name.valid = false;
		g_Details.valid = false;
	}

	UpdateSweepRows(viewer);

	// [MobiusU9B] Nothing to draw is the common case, so skip the layout entirely.
	bool anyVisible = false;
	for (const BesideText &element : g_Beside)
		anyVisible = anyVisible || (element.Alpha() > 0.0);
	if (!anyVisible)
		return;

	const int lineHeight = SmallFont->GetHeight() + 1;
	const double textScale = ComputeCrosshairTextScale(SCREENHEIGHT, HUD_GetHeight(), cl_fua_xhairinfo_textsize);
	const InfoCanvas canvas = ComputeCrosshairInfoCanvas(SCREENWIDTH, SCREENHEIGHT, textScale);
	const InfoPos anchor = ComputeCrosshairInfoAnchor(GetRect(CrosshairImage, StatusBar->CrosshairSize), textScale, lineHeight, kGap);

	std::vector<InfoElement> elements;
	for (const BesideText &element : g_Beside)
		elements.push_back(InfoElement{ element.Alpha() > 0.0, ComputeLineCount(element.text.GetChars()), *element.offsetX, *element.offsetY });
	const std::vector<InfoPos> positions = ComputeCrosshairInfoLayout(elements, anchor, lineHeight);

	for (size_t i = 0; i < elements.size(); ++i)
	{
		if (!elements[i].visible)
			continue;
		// [MobiusU9B] KeepRatio makes the virtual canvas a plain scale of the screen, so the real-pixel anchor converts exactly.
		screen->DrawText(SmallFont, g_Beside[i].color, positions[i].x, positions[i].y, g_Beside[i].text.GetChars(),
			DTA_VirtualWidth, canvas.width,
			DTA_VirtualHeight, canvas.height,
			DTA_KeepRatio, true,
			DTA_Alpha, fixed_t(FLOAT2FIXED(g_Beside[i].Alpha())),
			TAG_DONE);
	}
}

} } // namespace zx::crosshair
