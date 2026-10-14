// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A world-space line drawn as a screen-space quad of a width in pixels, as the backend draws the hitbox view's edges where GL calls glLineWidth: the shader's corner, line for line.

#ifndef ZX_WIDELINE_COMPUTE_H
#define ZX_WIDELINE_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] The clip-space w an endpoint behind the eye is pulled up to along its line, the same 0.0001 as FUA_WIDELINE_GLSL.
const float kWideLineNearW = 0.0001f;

// [rc4l] Vertex k (0-5) of a line's two triangles: which end it sits at (0 start, 1 end) and which side of the line (+1 or -1).
void ComputeWideLineVertex(int k, float &at, float &side);

// [rc4l] Where that vertex lands in clip space: the line from clip-space a to b, either end behind the eye pulled up to kWideLineNearW, the end `at` picks moved `side` half of widthPx across the line on a screenW by screenH screen; false when the whole line is behind the eye.
bool ComputeWideLineCorner(const float a[4], const float b[4], float at, float side, float widthPx, float screenW, float screenH, float out[4]);

}} // namespace zx::hwrender

#endif // ZX_WIDELINE_COMPUTE_H
