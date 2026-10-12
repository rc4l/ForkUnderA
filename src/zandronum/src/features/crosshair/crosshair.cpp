// [MobiusU9B] See crosshair.h; the arithmetic lives in computation/crosshair_compute so this file is only engine glue.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "c_cvars.h"
#include "doomstat.h"
#include "v_video.h"
#include "textures/textures.h"

#include "features/crosshair/crosshair.h"

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

namespace zx { namespace crosshair {

CrosshairRect GetRect(FTexture *image, fixed_t growSize)
{
	CrosshairRectInput in{};
	in.imageWidth = image->GetWidth();
	in.imageHeight = image->GetHeight();
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

} } // namespace zx::crosshair
