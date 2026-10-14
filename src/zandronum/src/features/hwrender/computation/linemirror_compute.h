// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A line mirror's reflected eye and how deep GL nests mirrors, as GLMirrorPortal::DrawContents reflects the view and r_mirror_recursions stops it.

#ifndef ZX_LINEMIRROR_COMPUTE_H
#define ZX_LINEMIRROR_COMPUTE_H

#include <vector>

namespace zx { namespace hwrender {

// [rc4l] The eye that sees the reflection, in mirror plane nx * x + ny * y = d, of an eye at (px, py) facing BAM angle `angle`: across the plane, facing twice the mirror's direction less its own; false when the eye is on or behind the plane.
bool ComputeMirrorEye(float nx, float ny, float d, float px, float py, unsigned angle, float &rx, float &ry, unsigned &outAngle);

// [rc4l] Whether GL draws a mirror `level` deep (1 for the one the player sees) under r_mirror_recursions, rather than clearing it black.
bool ComputeMirrorLevelDrawn(int level, int recursions);

// [rc4l] The floats a mirror face takes in the traced structure's vertices (six vertices of position and normal) and in its plane records (normal and line).
enum { kMirrorFaceFloats = 36, kMirrorPlaneFloats = 4 };

// [rc4l] Each of `mirrors` faces' plane record for the traced shader: its first vertex's normal and its mirror's line, which a ray bouncing off it steps views by.
void ComputeMirrorPlaneRecords(const float *faceVerts, unsigned mirrors, const int *lines, float *out);

// [rc4l] The nodes from the root down to `node`, root first, following parentOf (-1 above the root).
template <class ParentOf> void ComputeRootPath(int node, ParentOf parentOf, std::vector<int> &out)
{
	out.clear();
	for (int n = node; n >= 0; n = parentOf(n)) out.insert(out.begin(), n);
}

}} // namespace zx::hwrender

#endif // ZX_LINEMIRROR_COMPUTE_H
