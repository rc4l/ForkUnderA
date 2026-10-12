// [MobiusU9B] See crosshair_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/crosshair/computation/crosshair_compute.h"

#include "features/fixed64/computation/fixed64_scale_compute.h"

#include <cmath>

namespace zx { namespace crosshair {

namespace
{
	// [MobiusU9B] The engine's fixed_t fraction, spelled out so this file needs no engine header.
	const int kFracBits = 16;
	const int64_t kFracUnit = int64_t(1) << kFracBits;

	// [MobiusU9B] crosshairscale sizes the image as if the screen were this many lines tall.
	const int64_t kScaleBaseHeight = 200;

	// [MobiusU9B] Turns a clamped percentage of one view dimension into whole pixels from its centre.
	int OffsetPixels(double percent, int viewSize)
	{
		return static_cast<int>(std::lround(ComputeCrosshairClampOffset(percent) / 100.0 * viewSize));
	}
}

double ComputeCrosshairClampScale(double scale)
{
	if (std::isnan(scale)) {
		return 1.0;
	}
	if (scale < kCrosshairMinScale) {
		return kCrosshairMinScale;
	}
	if (scale > kCrosshairMaxScale) {
		return kCrosshairMaxScale;
	}
	return scale;
}

double ComputeCrosshairClampOffset(double percent)
{
	if (std::isnan(percent)) {
		return 0.0;
	}
	if (percent < -kCrosshairMaxOffset) {
		return -kCrosshairMaxOffset;
	}
	if (percent > kCrosshairMaxOffset) {
		return kCrosshairMaxOffset;
	}
	return percent;
}

CrosshairRect ComputeCrosshairRect(const CrosshairRectInput &in)
{
	// [MobiusU9B] The same 64-bit steps as the stock fixed_t maths, with FixedMul's own helper, so scale 1 is bit-identical.
	int64_t size = in.scaleWithScreen ? in.screenHeight * kFracUnit / kScaleBaseHeight : kFracUnit;
	size = std::llround(static_cast<double>(size) * ComputeCrosshairClampScale(in.userScale));
	if (in.grow) {
		size = Fixed64Mul(size, in.growSize);
	}

	return CrosshairRect{
		in.viewX + (in.viewWidth / 2) + OffsetPixels(in.offsetXPercent, in.viewWidth),
		in.viewY + (in.viewHeight / 2) + OffsetPixels(in.offsetYPercent, in.viewHeight),
		static_cast<int>((in.imageWidth * size) >> kFracBits),
		static_cast<int>((in.imageHeight * size) >> kFracBits),
	};
}

} } // namespace zx::crosshair
