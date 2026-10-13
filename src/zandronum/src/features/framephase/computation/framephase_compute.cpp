// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/framephase/computation/framephase_compute.h"

namespace zx { namespace framephase {

const char *ComputePhaseName(int phase)
{
	static const char *const kNames[kPhaseCount] = { "sim", "walk", "rebake", "actors", "backend", "upload", "lights", "present", "input", "display", "flip" };
	return phase >= 0 && phase < kPhaseCount ? kNames[phase] : "?";
}

}} // namespace zx::framephase
