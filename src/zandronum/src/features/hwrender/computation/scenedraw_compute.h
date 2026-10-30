// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_SCENEDRAW_COMPUTE_H
#define ZX_SCENEDRAW_COMPUTE_H

namespace zx { namespace hwrender {

// [Doruk-Aksoy] Why a command that draws the scene itself must not now, or nullptr when it may, since outside a level the game copy still points into the freed one.
const char *ComputeSceneDrawRefusal(bool inLevel, bool sceneUp);

}} // namespace zx::hwrender

#endif // ZX_SCENEDRAW_COMPUTE_H
