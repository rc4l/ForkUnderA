// [rc4l] The debug hitbox overlay's line width clamped to what the driver supports, pure so tests pin it; its cheat gate is features/dev-mode.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#ifndef ZX_HITBOXVIZ_VIZGATE_COMPUTE_H
#define ZX_HITBOXVIZ_VIZGATE_COMPUTE_H

namespace zx { namespace hitboxviz {

// Clamps a requested GL line width into the range the driver actually supports.
//
// glLineWidth() values above 1.0 are not required to work in an OpenGL core profile, so the
// requested width has to be clamped to GL_ALIASED_LINE_WIDTH_RANGE rather than passed through --
// an out-of-range width raises GL_INVALID_VALUE and the call is ignored, which would silently leave
// the overlay at whatever width was last set. A degenerate or inverted range is treated as "only
// glMin is available".
float ResolveLineWidth(float requested, float glMin, float glMax);

}} // namespace zx::hitboxviz

#endif // ZX_HITBOXVIZ_VIZGATE_COMPUTE_H
