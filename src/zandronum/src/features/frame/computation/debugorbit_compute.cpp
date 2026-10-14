// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/debugorbit_compute.h"

#include <math.h>

namespace zx { namespace frame {

void ComputeOrbitOffset(double seconds, double radius, double period, double &yaw, double &pitch)
{
	const double a = period > 0.0 ? 6.283185307179586 * seconds / period : 0.0;
	yaw = radius * (1.0 - cos(a));
	pitch = radius * sin(a);
}

int ComputeOrbitUnits(double wantDeg, long long doneUnits)
{
	return (int)(llround(wantDeg * 65536.0 / 360.0) - doneUnits);
}

}} // namespace zx::frame
