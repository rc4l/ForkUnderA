// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/decalproject_compute.h"

#include <cmath>

namespace zx { namespace levelmesh {

namespace {

inline float Dot3(const float a[3], const float b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

inline void Cross3(const float a[3], const float b[3], float out[3])
{
	out[0] = a[1]*b[2] - a[2]*b[1];
	out[1] = a[2]*b[0] - a[0]*b[2];
	out[2] = a[0]*b[1] - a[1]*b[0];
}

// Returns the length before normalising, so a caller can tell "too short to mean anything" from
// "pointing that way".
inline float Normalise3(float v[3])
{
	const float len = std::sqrt(Dot3(v, v));
	if (len > 1e-6f)
	{
		const float inv = 1.0f / len;
		v[0] *= inv; v[1] *= inv; v[2] *= inv;
	}
	return len;
}

inline void Copy3(const float src[3], float dst[3]) { dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; }

// Whatever is left of `v` after removing everything that lies along `axis`.
inline void RejectFrom(const float v[3], const float axis[3], float out[3])
{
	const float d = Dot3(v, axis);
	out[0] = v[0] - axis[0]*d;
	out[1] = v[1] - axis[1]*d;
	out[2] = v[2] - axis[2]*d;
}

} // namespace

bool BuildDecalBasis(const float vel[3], const float surfN[3], float maxSkewCos,
                     float outRight[3], float outUp[3], float outAxis[3])
{
	float n[3]; Copy3(surfN, n);
	if (Normalise3(n) <= 0.f) { n[0] = 0.f; n[1] = 0.f; n[2] = 1.f; }

	float axis[3]; Copy3(vel, axis);
	const bool haveVelocity = Normalise3(axis) > 1e-4f;
	if (!haveVelocity)
	{
		// Straight into the surface. Not a guess: it is what a decal glued to that surface does, and
		// it is the right answer for every case where Doom simply never gave the projectile a
		// direction -- a floor hit under autoaim, a mark placed by a script.
		axis[0] = -n[0]; axis[1] = -n[1]; axis[2] = -n[2];
	}
	else
	{
		// How square-on the hit is. -axis is "back the way it came", so this is 1 head-on and 0
		// exactly along the surface.
		float back[3] = { -axis[0], -axis[1], -axis[2] };
		float facing = Dot3(back, n);
		if (facing < maxSkewCos)
		{
			// Tilt back towards the normal until the hit is exactly at the limit, keeping the
			// direction it was skewed IN. Decomposing rather than lerping because a lerp's result
			// depends on how far past the limit the input was, so two nearly identical grazing hits
			// would land at visibly different angles.
			float tangent[3];
			RejectFrom(back, n, tangent);
			if (Normalise3(tangent) <= 1e-6f)
			{
				// Only reachable when `back` is antiparallel to the normal, i.e. the projectile is
				// travelling out of the surface. There is no skew direction to preserve; face it on.
				Copy3(n, back);
			}
			else
			{
				float perp = 1.0f - maxSkewCos * maxSkewCos;
				perp = (perp > 0.f) ? std::sqrt(perp) : 0.f;
				for (int i = 0; i < 3; i++) back[i] = n[i]*maxSkewCos + tangent[i]*perp;
				Normalise3(back);
			}
			axis[0] = -back[0]; axis[1] = -back[1]; axis[2] = -back[2];
		}
	}

	// [rc4l] The picture's up is world up, because that is what every decal in Doom looks like:
	// scorch marks are not rolled to match the shot. On a floor or a ceiling world up has nothing
	// left after the axis is taken out of it, and there the direction of travel across the surface
	// is the only meaningful "up" -- a splat pointing the way the rocket was going.
	const float worldUp[3] = { 0.f, 0.f, 1.f };
	float up[3];
	RejectFrom(worldUp, axis, up);
	if (Normalise3(up) <= 1e-3f)
	{
		const float horizontal[3] = { vel[0], vel[1], 0.f };
		float flat[3];
		RejectFrom(horizontal, axis, flat);
		if (Normalise3(flat) <= 1e-3f)
		{
			// Straight down onto a floor with no horizontal travel at all. Any perpendicular will
			// do and north is the one that does not depend on the order of the arithmetic.
			const float north[3] = { 0.f, 1.f, 0.f };
			RejectFrom(north, axis, flat);
			if (Normalise3(flat) <= 1e-3f) { flat[0] = 1.f; flat[1] = 0.f; flat[2] = 0.f; }
		}
		Copy3(flat, up);
	}

	// right = up x axis, so (right, up, -axis) is right-handed: looking back along the projection,
	// +u runs to the right and +v runs up, which is how the texture is authored.
	float right[3];
	Cross3(up, axis, right);
	Normalise3(right);
	// Re-orthogonalise: `up` came from a rejection and `axis` may have been re-aimed, so the two are
	// only perpendicular to within the arithmetic. One cross fixes it exactly.
	Cross3(axis, right, up);
	Normalise3(up);

	Copy3(right, outRight);
	Copy3(up, outUp);
	Copy3(axis, outAxis);
	return haveVelocity;
}

bool AcceptSurfaceForDecal(const float n[3], const float axis[3], float minFacing)
{
	float un[3]; Copy3(n, un);
	if (Normalise3(un) <= 0.f) return false;
	return Dot3(un, axis) <= -minFacing;
}

void DecalOriginFromImpact(const float pos[3], const float axis[3], float radius, float outOrigin[3])
{
	for (int i = 0; i < 3; i++) outOrigin[i] = pos[i] + axis[i] * radius;
}

int ClipPolygonToDecalBox(const float *worldPoly, int count, const DecalBox &box,
                          float *outLocal, int maxOut)
{
	if (count < 3 || maxOut < 3) return 0;

	// Into box-local coordinates first: six axis-aligned slabs are far easier to get right than six
	// arbitrary planes, and the transform is needed anyway for the texture coordinate.
	const int kMaxPoints = 64;
	float buf[2][kMaxPoints * 3];
	int cur = 0, n = 0;
	for (int i = 0; i < count && n < kMaxPoints; i++)
	{
		const float rel[3] = {
			worldPoly[i*3 + 0] - box.origin[0],
			worldPoly[i*3 + 1] - box.origin[1],
			worldPoly[i*3 + 2] - box.origin[2],
		};
		buf[cur][n*3 + 0] = Dot3(rel, box.right);
		buf[cur][n*3 + 1] = Dot3(rel, box.up);
		buf[cur][n*3 + 2] = Dot3(rel, box.axis);
		n++;
	}

	// Each slab is (component, sign, limit): keep points where sign*value <= limit.
	struct Slab { int comp; float sign; float limit; };
	const Slab slabs[6] = {
		{ 0,  1.f, box.halfW }, { 0, -1.f, box.halfW },
		{ 1,  1.f, box.halfH }, { 1, -1.f, box.halfH },
		{ 2,  1.f, box.far_  }, { 2, -1.f, box.near_ },
	};

	for (int s = 0; s < 6 && n > 0; s++)
	{
		const Slab &sl = slabs[s];
		const float *in = buf[cur];
		float *out = buf[cur ^ 1];
		int m = 0;

		for (int i = 0; i < n; i++)
		{
			const float *a = in + i*3;
			const float *b = in + ((i + 1) % n)*3;
			const float da = sl.sign * a[sl.comp] - sl.limit;
			const float db = sl.sign * b[sl.comp] - sl.limit;
			const bool aIn = (da <= 0.f), bIn = (db <= 0.f);

			if (aIn && m < kMaxPoints)
			{
				out[m*3 + 0] = a[0]; out[m*3 + 1] = a[1]; out[m*3 + 2] = a[2];
				m++;
			}
			if (aIn != bIn && m < kMaxPoints)
			{
				// The crossing point. da != db here because their signs differ.
				const float t = da / (da - db);
				out[m*3 + 0] = a[0] + (b[0] - a[0]) * t;
				out[m*3 + 1] = a[1] + (b[1] - a[1]) * t;
				out[m*3 + 2] = a[2] + (b[2] - a[2]) * t;
				m++;
			}
		}

		cur ^= 1;
		n = m;
		if (n < 3) return 0;   // a sliver or nothing: no triangles either way
	}

	if (n > maxOut) n = maxOut;
	for (int i = 0; i < n*3; i++) outLocal[i] = buf[cur][i];
	return n;
}

void DecalUV(const float local[3], const DecalBox &box, float &u, float &v)
{
	u = (box.halfW > 1e-6f) ? (local[0] / (2.0f * box.halfW) + 0.5f) : 0.5f;
	// v runs DOWN the picture: a texture's first row is its top, and +up is up in the world.
	v = (box.halfH > 1e-6f) ? (0.5f - local[1] / (2.0f * box.halfH)) : 0.5f;
}

void DecalFlipUV(bool flipX, bool flipY, float &u, float &v)
{
	if (flipX) u = 1.0f - u;
	if (flipY) v = 1.0f - v;
}

}} // namespace zx::levelmesh
