// [MobiusU9B] See speedometer.h; the arithmetic lives in computation/speedometer_compute so this file is only engine glue.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "actor.h"
#include "c_cvars.h"
#include "d_player.h"     // players, MAXPLAYERS
#include "doomdef.h"      // TICRATE
#include "m_fixed.h"      // FIXED2DBL
#include "v_font.h"       // SmallFont, CR_WHITE, NUM_TEXT_COLORS
#include "zstring.h"
#include "g_shared/st_hud.h"

#include "features/speedometer/speedometer.h"
#include "features/speedometer/computation/speedometer_compute.h"

// [MobiusU9B] Off by default so the stock HUD looks unchanged until the player turns it on.
CVAR(Bool, cl_fua_speedometer, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, cl_fua_speedometer_vertical, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, cl_fua_speedometer_color, CR_WHITE, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

// [MobiusU9B] Positions are percentages of the screen, and assigning only on change stops the callback re-entering forever.
CUSTOM_CVAR(Float, cl_fua_speedometer_x, 50.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	const float clamped = static_cast<float>(zx::speedometer::ComputeSpeedometerClampPercent(self));
	if (clamped != self)
		self = clamped;
}

CUSTOM_CVAR(Float, cl_fua_speedometer_y, 60.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	const float clamped = static_cast<float>(zx::speedometer::ComputeSpeedometerClampPercent(self));
	if (clamped != self)
		self = clamped;
}

namespace zx { namespace speedometer {

namespace
{
	// [MobiusU9B] The widest reading the box is sized for, so the suffix holds still until speeds pass 9999 u/s.
	const char kWidestReading[] = "0000 u/s";
}

void Draw(ULONG displayPlayer)
{
	if (!cl_fua_speedometer || !HUD_IsVisible() || displayPlayer >= MAXPLAYERS || SmallFont == NULL)
		return;

	const AActor *mo = players[displayPlayer].mo;
	if (mo == NULL)
		return;

	const double speed = ComputeSpeedometerSpeed(FIXED2DBL(mo->velx), FIXED2DBL(mo->vely), FIXED2DBL(mo->velz),
		cl_fua_speedometer_vertical, TICRATE);

	FString text;
	text.Format("%d u/s", ComputeSpeedometerDisplayValue(speed));

	const TextPos pos = ComputeSpeedometerTextPos(cl_fua_speedometer_x, cl_fua_speedometer_y,
		HUD_GetWidth(), HUD_GetHeight(), SmallFont->StringWidth(kWidestReading), SmallFont->GetHeight(),
		SmallFont->StringWidth(text.GetChars()));

	HUD_DrawText(SmallFont, ComputeSpeedometerColor(cl_fua_speedometer_color, NUM_TEXT_COLORS, CR_WHITE),
		pos.x, pos.y, text.GetChars());
}

} } // namespace zx::speedometer
