// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The projection of the floor and ceiling marks features/decals makes: each mark as the render copy holds it, given its box on the main thread when its picture, size or place changes, for the backend's deferred decal pass.

#ifndef ZX_PROJDECALS_H
#define ZX_PROJDECALS_H

#include "doomtype.h"

namespace zx { namespace levelmesh {

// [rc4l] Whether there is a mark to draw.
bool ProjectedDecalsActive();

// [rc4l] As a level is set up, before its marks are made or loaded.
void ClearProjectedDecals();

int GetProjectedDecalCount();
void PrintProjectedDecals(int limit);

// [rc4l] One mark as the backend's pass reads it.
struct GpuDecal
{
	float centre[3];
	float right[3];      // the picture's axes, each divided by its own half-extent, so the box
	float up[3];         // test is |dot(rel, right)| <= 1
	float axis[3];       // unit, pointing the way the projectile went
	float halfW, halfH;  // in world units, for the run-out radius
	float near_, far_;   // how far the box reaches either side of the contact point
	float r, g, b, a;
	const void  *material;
	int          translation;
	int          anchorSector;
	int          anchorPlane;    // 0 floor, 1 ceiling
	float        anchorOffset;   // world height at spawn, minus that plane's height there
	bool         redToAlpha;
	int          blend;          // [rc4l] spritestyle_compute's kBlend*, as the wall path takes a decal's style
	bool         fullbright;
};

int GetProjectedDecalsGpu(const GpuDecal **out);

}} // namespace zx::levelmesh

#endif // ZX_PROJDECALS_H
