// [rc4l] Turns the Gamma / vid_contrast / vid_brightness cvars into the three uniforms the
// present shader consumes.
//
// Extracted because the clamps are the part with teeth: they are what stop a hostile or
// fat-fingered cvar from producing a black, white or NaN screen that the user then cannot see
// well enough to fix. The old hardware-ramp path clamped in DoSetGamma() before building the
// 256-entry table; the same bounds move here so behaviour is unchanged where the cvars are sane.
//
// InvGamma (not Gamma) is what crosses into GLSL: the shader does pow(val, InvGamma), and doing
// the reciprocal here keeps a divide out of the per-fragment path and gives one place to prove
// Gamma can never be zero.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_GAMMA_UNIFORMS_COMPUTE_H
#define ZX_GAMMA_UNIFORMS_COMPUTE_H

namespace zx
{

struct GammaUniforms
{
	float invGamma;   // pow() exponent: 1 / clamped gamma
	float contrast;
	float brightness;
};

// [rc4l] Bounds match the historic DoSetGamma() clamps exactly: gamma 0.1..4, contrast 0.1..3,
// brightness -0.8..0.8. NaN inputs fall back to the neutral value rather than propagating into
// the shader, where a NaN uniform would blank the frame.
GammaUniforms ComputeGammaUniforms(float gamma, float contrast, float brightness);

// [rc4l] True when the uniforms leave the image untouched (gamma 1, contrast 1, brightness 0).
// The present pass still runs -- it is how the scene texture reaches the backbuffer at all -- but
// the caller can use this to skip redundant uniform uploads.
bool GammaUniformsAreNeutral(const GammaUniforms &u);

} // namespace zx

#endif // ZX_GAMMA_UNIFORMS_COMPUTE_H
