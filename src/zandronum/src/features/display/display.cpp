// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/display/display.h"
#include "features/display/computation/display_compute.h"

#include "c_cvars.h"

#include <string.h>

EXTERN_CVAR(Float, Gamma)
EXTERN_CVAR(Float, vid_contrast)
EXTERN_CVAR(Float, vid_brightness)

namespace zx { namespace display {

ScreenColors::ScreenColors() : brightness(0)
{
	memset(palette, 0, sizeof(palette));
}

void ScreenColors::Load()
{
	memcpy(palette, GPalette.BaseColors, sizeof(PalEntry) * 256);
	UpdateBrightness();
}

void ScreenColors::UpdateBrightness()
{
	brightness = ComputePaletteBrightness(reinterpret_cast<const unsigned *>(&GPalette.BaseColors[0]), 256);
}

void ScreenColors::SetFlash(PalEntry rgb, int amount)
{
	flash = PalEntry(amount, rgb.r, rgb.g, rgb.b);
}

void ScreenColors::GetFlash(PalEntry &rgb, int &amount) const
{
	rgb = flash;
	rgb.a = 0;
	amount = flash.a;
}

void CurrentGammaRamp(unsigned short ramp[768])
{
	ComputeGammaRamp(Gamma, vid_contrast, vid_brightness, ramp);
}

}} // namespace zx::display
