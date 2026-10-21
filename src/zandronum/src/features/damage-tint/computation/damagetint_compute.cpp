// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Implementation of the pure damage-tint math. See damagetint_compute.h.

#include "damagetint_compute.h"

namespace zx { namespace damagetint {

float RampStep(float current, bool active, int dtTics, float riseTics, float fallTics)
{
	if (dtTics < 0) dtTics = 0;
	if (dtTics > 35) dtTics = 35; // a stall (pause, long frame) never teleports the ramp

	float v = current;
	if (active)
	{
		if (riseTics <= 0.0f) return 1.0f;
		v += dtTics / riseTics;
	}
	else
	{
		if (fallTics <= 0.0f) return 0.0f;
		v -= dtTics / fallTics;
	}
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	return v;
}

float PulseFactor(int ticsIntoCycle, float peak, int decayTics)
{
	if (peak <= 0.0f || decayTics <= 0) return 1.0f;
	if (ticsIntoCycle < 0 || ticsIntoCycle >= decayTics) return 1.0f;
	// Linear decay from full spike at the cycle boundary (when the damage lands) back to rest.
	return 1.0f + peak * (1.0f - (float)ticsIntoCycle / (float)decayTics);
}

int EffectivePct(int basePct, float intensity, float pulse)
{
	if (basePct <= 0 || intensity <= 0.0f) return 0;
	float p = basePct * intensity * pulse;
	if (p < 0.0f) p = 0.0f;
	if (p > 100.0f) p = 100.0f;
	return (int)(p + 0.5f);
}

int TintChannel(int avgChannel, int pct)
{
	if (avgChannel < 0) avgChannel = 0;
	if (avgChannel > 255) avgChannel = 255;
	if (pct <= 0) return 255;
	if (pct > 100) pct = 100;
	return (255 * (100 - pct) + avgChannel * pct) / 100;
}

float CoverageSpan(int coveragePct, float span)
{
	if (coveragePct <= 0 || span <= 0.0f) return 0.0f;
	if (coveragePct > 100) coveragePct = 100;
	return span * coveragePct / 100.0f;
}

int SliceTintPct(int basePct, float fracFromBottom, float coverageFrac)
{
	if (basePct <= 0 || coverageFrac <= 0.0f) return 0;
	if (fracFromBottom < 0.0f) fracFromBottom = 0.0f;
	if (fracFromBottom >= coverageFrac) return 0;
	float p = basePct * (1.0f - fracFromBottom / coverageFrac);
	if (p <= 0.0f) return 0;
	if (p > 100.0f) p = 100.0f;
	return (int)(p + 0.5f);
}

TintGradient ComputeTintGradient(float r, float g, float b, float strength, float va, float vb, float coverageFrac, bool glow)
{
	const float bottom = vb > va ? vb : va;
	const float top = vb > va ? va : vb;
	const TintGradient t = { r, g, b, strength, bottom - (bottom - top) * coverageFrac, bottom, glow };
	return t;
}

void ComputeTintTexel(const TintGradient &t, float v, float rgb[3])
{
	if (t.strength <= 0.0f) return;
	const float span = t.bottom - t.top > 0.0001f ? t.bottom - t.top : 0.0001f;
	float f = (v - t.top) / span;
	if (f < 0.0f) f = 0.0f;
	if (f > 1.0f) f = 1.0f;
	const float tint[3] = { t.r, t.g, t.b };
	for (int i = 0; i < 3; i++)
	{
		const float tinted = t.glow ? 1.0f - (1.0f - rgb[i]) * (1.0f - tint[i]) : rgb[i] * tint[i];
		rgb[i] += (tinted - rgb[i]) * f * t.strength;
	}
}

}} // namespace zx::damagetint
