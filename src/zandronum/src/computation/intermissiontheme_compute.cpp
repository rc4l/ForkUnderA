// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/intermissiontheme_compute.h"

namespace zx {

int ComputeIntermissionThemeOrder(bool teamHasTheme, int teamOrder, int mapOrder)
{
	return teamHasTheme ? teamOrder : mapOrder;
}

bool ComputePlaysIntermissionTheme(bool isMapTheme, bool lumpFound)
{
	return !isMapTheme || lumpFound;
}

} // namespace zx
