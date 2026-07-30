// [MGOOOOOO] See vizgate_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "features/hitboxviz/computation/vizgate_compute.h"

namespace zx { namespace hitboxviz {

bool ShouldDraw(bool cvarEnabled, bool svCheats)
{
	return cvarEnabled && svCheats;
}

float ResolveLineWidth(float requested, float glMin, float glMax)
{
	if (glMax < glMin)
		return glMin;
	if (requested < glMin)
		return glMin;
	if (requested > glMax)
		return glMax;
	return requested;
}

}} // namespace zx::hitboxviz
