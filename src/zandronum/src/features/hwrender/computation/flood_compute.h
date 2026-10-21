// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's flooded gap (FDrawInfo::FloodLowerGap and FloodUpperGap): a step with no texture is painted with the neighbouring plane, projected from the eye; the scene shader does this per fragment, and this is its tested twin.

#ifndef ZX_FLOOD_COMPUTE_H
#define ZX_FLOOD_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] Where the eye's ray through a fragment meets the level plane at planeZ, in map space (z up); false unless the plane lies between them, which is when GL floods.
bool ComputeFloodPoint(float eyeX, float eyeY, float eyeZ, float fragX, float fragY, float fragZ, float planeZ,
	float &x, float &y);

}} // namespace zx::hwrender

#endif // ZX_FLOOD_COMPUTE_H
