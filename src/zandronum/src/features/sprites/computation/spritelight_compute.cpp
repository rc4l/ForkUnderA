// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/spritelight_compute.h"

namespace zx { namespace sprites {

int ComputeSpriteGlow(int lightLevel, int floorDiff, int glowHeight)
{
	if (glowHeight == 0 || floorDiff >= glowHeight * 65536) return lightLevel;
	const int maxlight = (255 + lightLevel) >> 1;
	int lightfrac = floorDiff / glowHeight;
	if (lightfrac < 0) lightfrac = 0;
	return (int)((lightfrac * lightLevel + maxlight * (65536 - lightfrac)) >> 16);
}

void ComputeSpriteLightTerm(float dist, float radius, float r, float g, float b, bool subtractive, float bright, float out[3])
{
	if (!(dist < radius)) return;
	const float frac = 1.0f - (dist / radius);
	if (!(frac > 0)) return;
	if (subtractive)
	{
		r = (bright - r) * -1;
		g = (bright - g) * -1;
		b = (bright - b) * -1;
	}
	out[0] += r * frac;
	out[1] += g * frac;
	out[2] += b * frac;
}

void ComputeDecolorize(int &r, int &g, int &b)
{
	const int v = (r + g + b) / 3;
	r = g = b = (255 + v + v) / 3;
}

void ComputeFullbrightColor(bool underwater, int &r, int &g, int &b)
{
	if (!underwater) { r = g = b = 0xff; return; }
	r = (3 * r + 0xff) / 4;
	g = (3 * g + 0xff) / 4;
	b = (3 * b + 0xff) / 4;
}

int ComputeSpriteBands(float z1, float z2, float vt, float vb, const float *bottoms, int n, SpriteBand *out)
{
	int count = 0;
	for (int i = 0; i < n; i++)
	{
		float bottom = bottoms[i];
		if (bottom < z2) bottom = z2;
		if (!(bottom < z1)) continue;
		// [rc4l] The band runs from the top left so far down to this entry's bottom, its v interpolated as GL interpolates it.
		SpriteBand &s = out[count++];
		s.entry = i;
		s.z1 = z1;
		s.vt = vt;
		s.vb = vb;
		s.z2 = bottom;
		const float newVt = s.vt + (bottom - s.z1) * (s.vb - s.vt) / (z2 - s.z1);
		s.vb = newVt;
		z1 = bottom;
		vt = newVt;
	}
	return count;
}

int ComputeSpriteColorEntry(float z2, float centre, const float *bottoms, int n)
{
	for (int i = 0; i < n; i++)
	{
		float bottom = bottoms[i];
		if (bottom < z2) bottom = z2;
		if (bottom < centre) return i;
	}
	return -1;
}

}} // namespace zx::sprites
