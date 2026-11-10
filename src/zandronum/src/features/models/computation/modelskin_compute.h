// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_MODELSKIN_COMPUTE_H
#define ZX_MODELSKIN_COMPUTE_H

class FTexture;

namespace zx { namespace models {

// [Doruk-Aksoy] The skin a model surface draws with: MODELDEF's Skin, else its SurfaceSkin, else the file's own, NULL when none is set and the surface is not drawn.
FTexture *ComputeModelSurfaceSkin(FTexture *modeldefSkin, FTexture *surfaceSkin, FTexture *ownSkin);

}} // namespace zx::models

#endif // ZX_MODELSKIN_COMPUTE_H
