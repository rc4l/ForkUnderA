// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/draw2d/computation/drawtags_compute.h"

namespace zx { namespace draw2d {

int64_t ComputeTagAlpha(TagFixed raw)
{
	const int64_t opaque = 1 << 16;
	return raw < opaque ? raw : opaque;
}

}} // namespace zx::draw2d
