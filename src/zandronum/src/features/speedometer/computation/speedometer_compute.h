// [MobiusU9B] The speedometer's speed, readout value, text colour and on-screen position, kept engine-free so they are unit-tested.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_SPEEDOMETER_COMPUTE_H
#define ZX_SPEEDOMETER_COMPUTE_H

namespace zx { namespace speedometer {

// [MobiusU9B] Where the readout's text is drawn, in the same (possibly virtual) coordinates as the screen size it was given.
struct TextPos
{
	int x;
	int y;
};

// [MobiusU9B] Speed in map units per second from a velocity in map units per tic, horizontal only unless includeVertical is set.
double ComputeSpeedometerSpeed(double vx, double vy, double vz, bool includeVertical, int ticRate);

// [MobiusU9B] The whole number the readout shows: rounded to nearest, clamped to [0, 99999], and 0 for NaN.
int ComputeSpeedometerDisplayValue(double unitsPerSecond);

// [MobiusU9B] A position cvar's value clamped to [0, 100] percent, with NaN treated as 0.
double ComputeSpeedometerClampPercent(double percent);

// [MobiusU9B] The text colour to draw with: color itself when it is a valid range below numColors, otherwise fallback.
int ComputeSpeedometerColor(int color, int numColors, int fallback);

// [MobiusU9B] Places a boxW x boxH box at xPct/yPct of the free space so it stays on screen, then right-aligns textW inside it without going off the left edge.
TextPos ComputeSpeedometerTextPos(double xPct, double yPct, int screenW, int screenH, int boxW, int boxH, int textW);

} } // namespace zx::speedometer

#endif
