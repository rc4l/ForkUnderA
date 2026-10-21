// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/surfaces/computation/gapfill_compute.h"

namespace zx { namespace surfaces {

int ComputeGapFill(const GapInputs &in)
{
	if (in.sloped && !in.frontSky && !in.backSky) return kGapFrontFlat;
	if (in.polyobj || in.backSky || in.backPortal || in.transdoor || in.fakeFlat || !in.hasPartner) return kGapNone;
	if (!in.top && in.fixAbuse) return kGapNone;
	return kGapFlood;
}

}} // namespace zx::surfaces
