// [MobiusU9B] The crosshair's on-screen size and position, kept engine-free so they are unit-tested.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_CROSSHAIR_COMPUTE_H
#define ZX_CROSSHAIR_COMPUTE_H

#include <cstdint>

namespace zx { namespace crosshair {

// [MobiusU9B] What placement reads from the engine: crosshairscale, crosshairgrow with CrosshairSize (raw 48.16), cl_fua_crosshair_scale/_x/_y and the 3D view's window.
struct CrosshairRectInput
{
	int imageWidth;
	int imageHeight;
	bool scaleWithScreen;
	int screenHeight;
	bool grow;
	int64_t growSize;
	double userScale;
	int viewX;
	int viewY;
	int viewWidth;
	int viewHeight;
	double offsetXPercent;
	double offsetYPercent;
};

// [MobiusU9B] Where the crosshair is drawn in real screen pixels: (x, y) is the draw point the image's own offsets centre on.
struct CrosshairRect
{
	int x;
	int y;
	int width;
	int height;
};

// [MobiusU9B] The size slider's range; 1 leaves the crosshair exactly as the stock settings draw it.
const double kCrosshairMinScale = 0.25;
const double kCrosshairMaxScale = 4.0;

// [MobiusU9B] The offset sliders' range, as a percentage of the view from its centre.
const double kCrosshairMaxOffset = 50.0;

// [MobiusU9B] A size cvar's value clamped to the slider's range, with NaN treated as 1.
double ComputeCrosshairClampScale(double scale);

// [MobiusU9B] An offset cvar's value clamped to [-50, 50] percent, with NaN treated as 0.
double ComputeCrosshairClampOffset(double percent);

// [MobiusU9B] The crosshair's draw point and size, matching the stock fixed-point maths exactly at scale 1 and offset 0.
CrosshairRect ComputeCrosshairRect(const CrosshairRectInput &in);

} } // namespace zx::crosshair

#endif
