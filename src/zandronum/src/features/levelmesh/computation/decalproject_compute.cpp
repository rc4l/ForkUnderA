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

void DecalOriginFromImpact(const float pos[3], const float axis[3], float radius, float outOrigin[3])
{
	for (int i = 0; i < 3; i++) outOrigin[i] = pos[i] + axis[i] * radius;
}

void ComputeDecalBoxDepth(float size, float cosTheta, float spreadFraction,
                          float &outNear, float &outFar)
{
	if (size < 0.f) size = 0.f;
	// A projection running along the surface has no finite depth. The skew clamp keeps real hits
	// well clear of this, so the floor is a guard rather than a behaviour.
	const float kMinCos = 0.2f;
	if (cosTheta < kMinCos) cosTheta = kMinCos;
	if (cosTheta > 1.f) cosTheta = 1.f;

	const float slant = size * std::sqrt(1.f - cosTheta * cosTheta) / cosTheta;
	const float spread = size * ((spreadFraction > 0.f) ? spreadFraction : 0.f);

	outNear = (slant > spread) ? slant : spread;
	outFar = slant + 4.f;
}

void ApplyDecalFlip(bool flipX, bool flipY, float right[3], float up[3])
{
	if (flipX) { right[0] = -right[0]; right[1] = -right[1]; right[2] = -right[2]; }
	if (flipY) { up[0] = -up[0]; up[1] = -up[1]; up[2] = -up[2]; }
}

}} // namespace zx::levelmesh
