// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/models/computation/modelskin_compute.h"

#include <cstddef>

namespace zx { namespace models {

FTexture *ComputeModelSurfaceSkin(FTexture *modeldefSkin, FTexture *surfaceSkin, FTexture *ownSkin)
{
	if (modeldefSkin != NULL) return modeldefSkin;
	if (surfaceSkin != NULL) return surfaceSkin;
	return ownSkin;
}

}} // namespace zx::models
