// [MobiusU9B] See speedometer_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/speedometer/computation/speedometer_compute.h"

#include <cmath>

namespace zx { namespace speedometer {

namespace
{
	// [MobiusU9B] The largest value the readout shows, so a runaway velocity can never overflow the int conversion.
	const double kMaxDisplayValue = 99999.0;

	// [MobiusU9B] Scales the room left beside a box by a clamped percentage, giving the box's near edge.
	int PlaceInFreeSpace(double percent, int screenSize, int boxSize)
	{
		const int freeSpace = screenSize > boxSize ? screenSize - boxSize : 0;
		return static_cast<int>(std::lround(ComputeSpeedometerClampPercent(percent) / 100.0 * freeSpace));
	}
}

double ComputeSpeedometerSpeed(double vx, double vy, double vz, bool includeVertical, int ticRate)
{
	const double squared = vx * vx + vy * vy + (includeVertical ? vz * vz : 0.0);
	return std::sqrt(squared) * ticRate;
}

int ComputeSpeedometerDisplayValue(double unitsPerSecond)
{
	// [MobiusU9B] Written so NaN fails the first test and lands on 0 rather than reaching lround.
	if (!(unitsPerSecond > 0.0))
		return 0;
	if (unitsPerSecond >= kMaxDisplayValue)
		return static_cast<int>(kMaxDisplayValue);
	return static_cast<int>(std::lround(unitsPerSecond));
}

double ComputeSpeedometerClampPercent(double percent)
{
	if (!(percent > 0.0))
		return 0.0;
	if (percent > 100.0)
		return 100.0;
	return percent;
}

int ComputeSpeedometerColor(int color, int numColors, int fallback)
{
	return (color >= 0 && color < numColors) ? color : fallback;
}

TextPos ComputeSpeedometerTextPos(double xPct, double yPct, int screenW, int screenH, int boxW, int boxH, int textW)
{
	const int boxX = PlaceInFreeSpace(xPct, screenW, boxW);
	const int textX = boxX + boxW - textW;
	TextPos pos;
	pos.x = textX > 0 ? textX : 0;
	pos.y = PlaceInFreeSpace(yPct, screenH, boxH);
	return pos;
}

} } // namespace zx::speedometer
