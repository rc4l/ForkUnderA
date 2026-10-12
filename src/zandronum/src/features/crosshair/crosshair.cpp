// [MobiusU9B] See crosshair.h; the arithmetic lives in computation/ so this file is only engine glue.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "actor.h"
#include "c_cvars.h"
#include "d_player.h"
#include "doomdef.h"
#include "doomstat.h"
#include "p_trace.h"
#include "tables.h"
#include "v_font.h"
#include "v_text.h"
#include "v_video.h"
#include "textures/textures.h"
#include "g_shared/sbar.h"
#include "g_shared/st_hud.h"

#include "features/crosshair/crosshair.h"
#include "features/crosshair/computation/targetinfo_compute.h"

#include <string>
#include <vector>

EXTERN_CVAR(Bool, crosshairscale)
EXTERN_CVAR(Bool, crosshairgrow)

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
#undef ZX_TARGETINFO_OFFSET_CVAR

namespace
{
	// [MobiusU9B] The bottom-of-screen message's hold and fade, so text beside the crosshair lingers the same way.
	const int kHoldTics = 2 * TICRATE;
	const int kFadeTics = static_cast<int>(0.35f * TICRATE);

	// [MobiusU9B] Pixels between the crosshair's right edge and the text, in HUD-text pixels.
	const int kGap = 4;

	// [MobiusU9B] The actor-name line's colour, apart from the team or grey of the name above it.
	const EColorRange kActorNameColor = CR_TAN;

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

	// [MobiusU9B] Drawn and stacked in this order; the telemeter will be one more row.
	BesideText g_Beside[] = {
		{ FString(), CR_GRAY, 0, false, &cl_fua_xhairinfo_name_x, &cl_fua_xhairinfo_name_y },
		{ FString(), CR_GRAY, 0, false, &cl_fua_xhairinfo_info_x, &cl_fua_xhairinfo_info_y },
		{ FString(), kActorNameColor, 0, false, &cl_fua_xhairinfo_actor_x, &cl_fua_xhairinfo_actor_y },
	};
	BesideText &g_Name = g_Beside[0];
	BesideText &g_Details = g_Beside[1];
	BesideText &g_ActorName = g_Beside[2];

	// [MobiusU9B] The renderer's own reasons for not drawing a sprite, so the scan never names something you cannot see.
	bool IsInvisible(AActor *actor)
	{
		return (actor->sprite == 0) || (actor->flags & MF_NOSECTOR) || (actor->renderflags & RF_INVISIBLE)
			|| !actor->RenderStyle.IsVisible(actor->alpha) || !actor->IsVisibleToPlayer();
	}

	ETraceStatus PassInvisible(FTraceResults &res, void *)
	{
		const bool hitActor = (res.HitType == TRACE_HitActor);
		return zx::crosshair::ComputeActorScanPassesThrough(hitActor, hitActor && IsInvisible(res.Actor)) ? TRACE_Skip : TRACE_Stop;
	}

	// [MobiusU9B] Shows the name or class of the first visible item, decoration, projectile or corpse in the crosshair, or leaves the line to fade.
	void UpdateActorName(AActor *viewer)
	{
		const int mode = zx::crosshair::ComputeCrosshairClampInt(cl_fua_identify_actorname, zx::crosshair::kActorNameOff, zx::crosshair::kActorNameTag);
		if (!zx::crosshair::ComputeActorNameAllowed(mode, (zadmflags & ZADF_NO_IDENTIFY_TARGET) != 0, (zadmflags & ZADF_FUA_NO_IDENTIFY_ACTORS) != 0))
		{
			g_ActorName.valid = false;
			return;
		}

		FTraceResults trace;
		if (!zx::crosshair::TraceFromView(viewer, 0xFFFFFFFF, true, trace) || (trace.HitType != TRACE_HitActor)
			|| (trace.Actor->STFlags & STFL_DONTIDENTIFYTARGET))
			return;

		// [MobiusU9B] A living monster or player belongs to the identify rows, so drop the line at once rather than let an older name sit beside theirs.
		AActor *actor = trace.Actor;
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

bool TraceFromView(AActor *source, DWORD actorMask, bool skipInvisible, FTraceResults &trace)
{
	// [MobiusU9B] Moved unchanged from HUD_ScanForTarget, so the identify scan and the actor-name scan aim the same way.
	angle_t angle = source->angle >> ANGLETOFINESHIFT;
	angle_t pitch = static_cast<angle_t>( source->pitch ) >> ANGLETOFINESHIFT;
	fixed_t vx = FixedMul( finecosine[pitch], finecosine[angle] );
	fixed_t vy = FixedMul( finecosine[pitch], finesine[angle] );
	fixed_t vz = -finesine[pitch];
	fixed_t eyez = source->player ? source->player->viewz : source->z + source->height / 2;

	return Trace( source->x, source->y, eyez, source->Sector, vx, vy, vz,
		( 32 * 64 * FRACUNIT ) /* MISSILERANGE */, actorMask, ML_BLOCKEVERYTHING, source, trace,
		TRACE_NoSky, skipInvisible ? PassInvisible : NULL );
}

void DrawBesideCrosshair(AActor *viewer, const TargetText *identify)
{
	if ((viewer == NULL) || (SmallFont == NULL) || (StatusBar == NULL))
	{
		for (BesideText &element : g_Beside)
			element.valid = false;
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

	UpdateActorName(viewer);

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
