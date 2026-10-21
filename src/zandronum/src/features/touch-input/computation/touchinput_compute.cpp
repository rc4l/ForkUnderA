// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/touch-input/computation/touchinput_compute.h"

#include <math.h>

namespace zx { namespace touch {

TouchAxes ComputeTouchAxes(float dx, float dy, float radius, float deadZone)
{
	TouchAxes out = { 0.f, 0.f };
	if (radius <= 0.f) return out;
	const float length = sqrtf(dx * dx + dy * dy);
	float amount = length / radius;
	if (amount <= deadZone || length <= 0.f) return out;
	// [rc4l] Past the dead zone the thumb's travel to the rim is the whole range, and past the rim it is the rim.
	amount = deadZone < 1.f ? (amount - deadZone) / (1.f - deadZone) : 1.f;
	if (amount > 1.f) amount = 1.f;
	out.x = dx / length * amount;
	out.y = dy / length * amount;
	return out;
}

void ComputeTouchLook(float dx, float dy, float scale, bool invertY, int &mouseX, int &mouseY)
{
	mouseX = (int)lroundf(dx * scale);
	// [rc4l] A drag down the screen looks down, which PostMouseMove takes as negative y, unless inverted.
	mouseY = (int)lroundf((invertY ? dy : -dy) * scale);
}

}} // namespace zx::touch
