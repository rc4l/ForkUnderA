// [rc4l] See gamma_uniforms_compute.h.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gamma_uniforms_compute.h"

namespace zx
{

namespace
{
// [rc4l] Self-comparison is the NaN test that does not need <cmath> -- keeping this header-pure.
inline bool IsNaN(float v) { return v != v; }

inline float ClampOr(float v, float lo, float hi, float fallback)
{
	if (IsNaN(v)) return fallback;
	return v < lo ? lo : (v > hi ? hi : v);
}
}

GammaUniforms ComputeGammaUniforms(float gamma, float contrast, float brightness)
{
	GammaUniforms out;

	// [rc4l] Same bounds the hardware-ramp path used. The low gamma bound is also what makes the
	// reciprocal below safe: 0.1 can never be 0.
	const float g = ClampOr(gamma, 0.1f, 4.0f, 1.0f);
	out.invGamma = 1.0f / g;
	out.contrast = ClampOr(contrast, 0.1f, 3.0f, 1.0f);
	out.brightness = ClampOr(brightness, -0.8f, 0.8f, 0.0f);

	return out;
}

bool GammaUniformsAreNeutral(const GammaUniforms &u)
{
	return u.invGamma == 1.0f && u.contrast == 1.0f && u.brightness == 0.0f;
}

} // namespace zx
