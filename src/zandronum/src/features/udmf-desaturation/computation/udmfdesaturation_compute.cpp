// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/udmf-desaturation/computation/udmfdesaturation_compute.h"

namespace zx { namespace udmf {

int ComputeUdmfDesaturation(double value)
{
	const double scaled = 255.0 * value;
	if (scaled <= 0.0) return 0;
	if (scaled >= 255.0) return 255;
	return int(scaled);
}

}} // namespace zx::udmf
