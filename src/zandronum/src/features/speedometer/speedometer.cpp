// [MobiusU9B] See speedometer.h; the arithmetic lives in computation/speedometer_compute so this file is only engine glue.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "actor.h"
#include "c_cvars.h"
#include "d_player.h"     // players
#include "doomdef.h"      // TICRATE, MAXPLAYERS
#include "m_fixed.h"      // FIXED2DBL
#include "v_font.h"       // SmallFont, CR_WHITE, NUM_TEXT_COLORS
#include "g_shared/st_hud.h"

#include "features/speedometer/speedometer.h"
#include "features/speedometer/computation/speedometer_compute.h"

#include <string>

namespace
{
	// [MobiusU9B] Shared by both position cvars, and writing back only on change stops the callback re-entering forever.
	void ClampPercentCVar(FFloatCVar &cvar)
	{
		const float clamped = static_cast<float>(zx::speedometer::ComputeSpeedometerClampPercent(cvar));
		if (clamped != cvar)
			cvar = clamped;
	}
}

// [MobiusU9B] Off by default so the stock HUD looks unchanged until the player turns it on.
CVAR(Bool, cl_fua_speedometer, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, cl_fua_speedometer_vertical, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, cl_fua_speedometer_color, CR_WHITE, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

// [MobiusU9B] Positions are percentages of the screen.
CUSTOM_CVAR(Float, cl_fua_speedometer_x, 50.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	ClampPercentCVar(self);
}

CUSTOM_CVAR(Float, cl_fua_speedometer_y, 60.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	ClampPercentCVar(self);
}

namespace zx { namespace speedometer {

void Draw(ULONG displayPlayer)
{
	if (!cl_fua_speedometer || !HUD_IsVisible() || displayPlayer >= MAXPLAYERS || SmallFont == NULL)
		return;

	const AActor *mo = players[displayPlayer].mo;
	if (mo == NULL)
		return;

	const double speed = ComputeSpeedometerSpeed(FIXED2DBL(mo->velx), FIXED2DBL(mo->vely), FIXED2DBL(mo->velz),
		cl_fua_speedometer_vertical, TICRATE);
	const std::string text = ComputeSpeedometerText(speed);
	const std::string box = ComputeSpeedometerBoxText();

	const TextPos pos = ComputeSpeedometerTextPos(cl_fua_speedometer_x, cl_fua_speedometer_y,
		HUD_GetWidth(), HUD_GetHeight(), SmallFont->StringWidth(box.c_str()), SmallFont->GetHeight(),
		SmallFont->StringWidth(text.c_str()));

	HUD_DrawText(SmallFont, ComputeSpeedometerColor(cl_fua_speedometer_color, NUM_TEXT_COLORS, CR_WHITE),
		pos.x, pos.y, text.c_str());
}

} } // namespace zx::speedometer
