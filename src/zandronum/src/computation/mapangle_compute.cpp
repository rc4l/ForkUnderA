// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/mapangle_compute.h"

namespace zx { namespace map {

uint32_t ComputeMapThingAngle(int degrees)
{
	return static_cast<uint32_t>((degrees * INT64_C(0x100000000)) / 360);
}

}} // namespace zx::map
