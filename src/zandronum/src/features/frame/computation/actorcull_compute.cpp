// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/actorcull_compute.h"

namespace zx { namespace frame {

int ComputeActorCull(const float *mvp, float px, float py, float pz, float pad)
{
	const float w = mvp[3] * px + mvp[7] * py + mvp[11] * pz + mvp[15];
	if (w < -pad) return kActorBehind;
	// [rc4l] Near the eye's plane the division would mislead, so only a point clearly in front is tested against the sides.
	if (w <= 0.f) return kActorInView;
	const float cx = mvp[0] * px + mvp[4] * py + mvp[8] * pz + mvp[12];
	const float cy = mvp[1] * px + mvp[5] * py + mvp[9] * pz + mvp[13];
	// [rc4l] The pad in NDC, plus slack for the projection.
	const float m = pad / w + 0.15f;
	if (cx / w < -1.f - m || cx / w > 1.f + m) return kActorOffScreen;
	if (cy / w < -1.f - m || cy / w > 1.f + m) return kActorOffScreen;
	return kActorInView;
}

}} // namespace zx::frame
