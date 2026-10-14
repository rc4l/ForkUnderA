// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GLWall::CheckGlowing and main.vp's and main.fp's glow: a GLDEFS glowing flat lighting the walls of its sector, fading over the glow's height.

#ifndef ZX_WALLGLOW_COMPUTE_H
#define ZX_WALLGLOW_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] Whether a wall takes its sector's glow: not when it is drawn fullbright, white light at level 255, as gl_isFullbright says.
bool ComputeWallTakesGlow(unsigned lightColorRGB, int lightLevel);

// [rc4l] How far a point (x, height, y in map units) is below the ceiling plane and above the floor plane, each plane as GL hands it to the shader: a, b, 1/c and d.
void ComputeGlowDistances(const float top[4], const float bottom[4], float x, float height, float y, float &belowTop, float &aboveBottom);

// [rc4l] The share of a glow's colour a point `dist` from its flat takes: all of it at the flat, none past `height`, and none from a glow of no height.
float ComputeGlowFactor(float height, float dist);

}} // namespace zx::levelmesh

#endif // ZX_WALLGLOW_COMPUTE_H
