// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] A model surface's frame resolved on the CPU: the two keyframes lerped and the matrix applied, as world-space triangle corners.
#ifndef ZX_MODELSURFACE_COMPUTE_H
#define ZX_MODELSURFACE_COMPUTE_H

namespace zx { namespace levelmesh {

// [Doruk-Aksoy] One surface's draw: its keyframes' vertex offsets, the lerp between them, and its index range.
struct ModelSurfaceSpan
{
	unsigned base1, base2;
	float inter;
	unsigned firstIndex, count;
};

// [Doruk-Aksoy] Writes span.count corners to out, five floats each as MeshVertex holds them (x, height, y, u, v), from verts (FModelVertex's x, y, z, u, v) under the column-major matrix m; indices NULL draws the vertices in order; false, with out partly written, when the span is under a triangle or reads past verts or indices.
bool ComputeModelSurfaceCorners(const float *verts, unsigned nverts, const unsigned *indices, unsigned nindices,
	const ModelSurfaceSpan &span, const float *m, float *out);

}} // namespace zx::levelmesh

#endif
