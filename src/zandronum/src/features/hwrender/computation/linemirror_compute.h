// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A line mirror's reflected eye and how deep GL nests mirrors, as GLMirrorPortal::DrawContents reflects the view and r_mirror_recursions stops it.

#ifndef ZX_LINEMIRROR_COMPUTE_H
#define ZX_LINEMIRROR_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] The eye that sees the reflection, in mirror plane nx * x + ny * y = d, of an eye at (px, py) facing BAM angle `angle`: across the plane, facing twice the mirror's direction less its own; false when the eye is on or behind the plane.
bool ComputeMirrorEye(float nx, float ny, float d, float px, float py, unsigned angle, float &rx, float &ry, unsigned &outAngle);

// [rc4l] Whether GL draws a mirror `level` deep (1 for the one the player sees) under r_mirror_recursions, rather than clearing it black.
bool ComputeMirrorLevelDrawn(int level, int recursions);

}} // namespace zx::hwrender

#endif // ZX_LINEMIRROR_COMPUTE_H
