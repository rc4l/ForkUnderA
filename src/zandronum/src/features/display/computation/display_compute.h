// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a frame buffer applies to the screen as a whole, for any renderer: the gamma ramp and the palette's brightness.

#ifndef ZX_DISPLAY_COMPUTE_H
#define ZX_DISPLAY_COMPUTE_H

namespace zx { namespace display {

// [rc4l] Doomsday's gamma, contrast and brightness curve as the 768-entry ramp SetDeviceGammaRamp takes, each input clamped first.
void ComputeGammaRamp(float gamma, float contrast, float brightness, unsigned short ramp[768]);

// [rc4l] The palette's average brightness, which darkens a colormap's screen tint as UpdatePalette works it out; `argb` holds `count` PalEntry values.
int ComputePaletteBrightness(const unsigned *argb, int count);

}} // namespace zx::display

#endif // ZX_DISPLAY_COMPUTE_H
