// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [KI] The percentage behind the nettraffic statistics, mirroring 3.3's getCompressionFactorLastSecond and getZstdPercentageLastSecond arithmetic.

#include "netstats_compute.h"

namespace zx { namespace netclient {

//*****************************************************************************
//
float ComputeSharePercent(unsigned int part, unsigned int whole)
{
	if (part == whole || whole == 0)
		return 100.0f;
	return 100.0f * static_cast<float>(part) / static_cast<float>(whole);
}

}} // namespace zx::netclient
