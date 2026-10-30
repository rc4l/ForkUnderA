// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/hwrender/computation/scenedraw_compute.h"

namespace zx { namespace hwrender {

const char *ComputeSceneDrawRefusal(bool inLevel, bool sceneUp)
{
	// [Doruk-Aksoy] Outside a level first, since uploading a scene there would not make drawing it safe.
	if (!inLevel) return "not in a level";
	if (!sceneUp) return "call fua_diligent_scene first";
	return nullptr;
}

}} // namespace zx::hwrender
