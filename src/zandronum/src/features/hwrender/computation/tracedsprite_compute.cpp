// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/tracedsprite_compute.h"

#include <math.h>

namespace zx { namespace hwrender {

namespace {

float Dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void Cross(const float *a, const float *b, float *out)
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

} // namespace

bool ComputeTracesRun(unsigned vertexCount, bool solid, bool depthBias, bool hud, int stackSlot, int onMirror, int mirrorView, int blend)
{
	return vertexCount == 6 && !solid && !depthBias && !hud && stackSlot < 0 && onMirror < 0 && mirrorView >= 0 && blend == 0;
}

bool ComputeTracedSprite(const float *first, const float *second, const float *third, unsigned piece, int view, TracedSprite &out)
{
	for (int i = 0; i < 3; i++)
	{
		out.origin[i] = first[i];
		out.edgeU[i] = second[i] - first[i];
		out.edgeV[i] = third[i] - first[i];
	}
	float n[3];
	Cross(out.edgeU, out.edgeV, n);
	if (Dot(n, n) <= 1e-8f) return false;
	out.u0 = first[3]; out.u1 = second[3];
	out.v0 = first[4]; out.v1 = third[4];
	out.piece = piece;
	out.view = view;
	return true;
}

void ComputeTracedSpriteBox(const TracedSprite &s, float mn[3], float mx[3])
{
	for (int i = 0; i < 3; i++)
	{
		const float a = s.origin[i], b = a + s.edgeU[i], c = a + s.edgeV[i], d = b + s.edgeV[i];
		mn[i] = fminf(fminf(a, b), fminf(c, d));
		mx[i] = fmaxf(fmaxf(a, b), fmaxf(c, d));
	}
}

void ComputeTracedSpriteRecord(const TracedSprite &s, float out[kTracedSpriteFloats])
{
	for (int i = 0; i < 3; i++) { out[i] = s.origin[i]; out[4 + i] = s.edgeU[i]; out[8 + i] = s.edgeV[i]; }
	out[3] = (float)s.piece; out[7] = (float)s.view; out[11] = 0.f;
	out[12] = s.u0; out[13] = s.u1; out[14] = s.v0; out[15] = s.v1;
}

unsigned ComputeTracedSpriteHeaderFloats(unsigned stepCount)
{
	return 4u * (1u + stepCount);
}

void ComputeTracedSpriteHeader(const int *steps, unsigned stepCount, float *out)
{
	out[0] = (float)stepCount; out[1] = out[2] = out[3] = 0.f;
	for (unsigned i = 0; i < stepCount; i++)
	{
		float *e = out + 4 * (i + 1);
		e[0] = (float)steps[i * 3]; e[1] = (float)steps[i * 3 + 1]; e[2] = (float)steps[i * 3 + 2]; e[3] = 0.f;
	}
}

bool ComputeTracedSpriteHit(const TracedSprite &s, const float o[3], const float d[3], float tMin, float tMax, float &t, float &u, float &v)
{
	float n[3];
	Cross(s.edgeU, s.edgeV, n);
	const float den = Dot(d, n);
	if (fabsf(den) < 1e-6f) return false;
	const float toOrigin[3] = { s.origin[0] - o[0], s.origin[1] - o[1], s.origin[2] - o[2] };
	t = Dot(toOrigin, n) / den;
	if (t <= tMin || t >= tMax) return false;
	// [rc4l] The hit in the quad's own edges, from the 2x2 system their dot products make, so a quad that is not square still maps.
	const float w[3] = { o[0] + d[0] * t - s.origin[0], o[1] + d[1] * t - s.origin[1], o[2] + d[2] * t - s.origin[2] };
	const float uu = Dot(s.edgeU, s.edgeU), uv = Dot(s.edgeU, s.edgeV), vv = Dot(s.edgeV, s.edgeV);
	const float wu = Dot(w, s.edgeU), wv = Dot(w, s.edgeV);
	const float det = uu * vv - uv * uv;
	const float a = (wu * vv - wv * uv) / det, b = (wv * uu - wu * uv) / det;
	if (a < 0.f || a > 1.f || b < 0.f || b > 1.f) return false;
	u = s.u0 + (s.u1 - s.u0) * a;
	v = s.v0 + (s.v1 - s.v0) * b;
	return true;
}

}} // namespace zx::hwrender
