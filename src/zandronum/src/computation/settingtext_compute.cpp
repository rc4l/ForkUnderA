// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/settingtext_compute.h"

#include <cmath>
#include <cstdio>

namespace zx {

std::string ComputeFloatSettingText(float value, int maxDecimals)
{
	if (std::isinf(value))
		return value < 0 ? "-infinity" : "infinity";
	if (std::isnan(value))
		return "NaN";

	char text[64];
	if (maxDecimals <= 0)
		std::snprintf(text, sizeof(text), "%f", value);
	else
		std::snprintf(text, sizeof(text), "%.*f", maxDecimals, value);
	return text;
}

} // namespace zx
