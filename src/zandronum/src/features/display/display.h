// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a hardware frame buffer keeps of the palette, the screen flash and the gamma ramp, the same for GL's and the backend's.

#ifndef ZX_DISPLAY_H
#define ZX_DISPLAY_H

#include "v_palette.h"   // PalEntry

namespace zx { namespace display {

// [rc4l] The game palette as a frame buffer took it, its brightness, and the flash the status bar asks for.
struct ScreenColors
{
	PalEntry palette[256];
	PalEntry flash;
	int brightness;

	ScreenColors();
	// [rc4l] Take the game palette and its brightness, as a new frame buffer and a game restart do.
	void Load();
	// [rc4l] The brightness again, after the game palette changed.
	void UpdateBrightness();
	void SetFlash(PalEntry rgb, int amount);
	void GetFlash(PalEntry &rgb, int &amount) const;
};

// [rc4l] The hardware gamma ramp Gamma, vid_contrast and vid_brightness ask for.
void CurrentGammaRamp(unsigned short ramp[768]);

}} // namespace zx::display

#endif // ZX_DISPLAY_H
