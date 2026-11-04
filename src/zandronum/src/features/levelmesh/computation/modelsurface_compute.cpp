// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] A model surface's frame resolved on the CPU.
#include "features/levelmesh/computation/modelsurface_compute.h"

namespace zx { namespace levelmesh {

bool ComputeModelSurfaceCorners(const float *verts, unsigned nverts, const unsigned *indices, unsigned nindices,
	const ModelSurfaceSpan &span, const float *m, float *out)
{
	if (verts == nullptr || span.count < 3) return false;
	// [rc4l] Bounds checked rather than trusted, as these offsets come from a file on disk the loaders do not validate.
	if (indices != nullptr && (span.firstIndex > nindices || span.count > nindices - span.firstIndex)) return false;
	const bool lerp = span.base1 != span.base2 && span.inter > 0.f;
	for (unsigned i = 0; i < span.count; i++)
	{
		const unsigned vi = indices != nullptr ? indices[span.firstIndex + i] : span.firstIndex + i;
		if (span.base1 + vi >= nverts || span.base2 + vi >= nverts) return false;
		const float *a = verts + 5 * (span.base1 + vi);
		float x = a[0], y = a[1], z = a[2];
		if (lerp)
		{
			// [rc4l] The lerp GL's vertex shader does, on the same pairs of keyframe vertices.
			const float *b = verts + 5 * (span.base2 + vi);
			x += (b[0] - x) * span.inter;
			y += (b[1] - y) * span.inter;
			z += (b[2] - z) * span.inter;
		}
		float *o = out + 5 * i;
		o[0] = m[0] * x + m[4] * y + m[8]  * z + m[12];
		o[1] = m[1] * x + m[5] * y + m[9]  * z + m[13];
		o[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
		o[3] = a[3];
		o[4] = a[4];
	}
	return true;
}

}} // namespace zx::levelmesh
