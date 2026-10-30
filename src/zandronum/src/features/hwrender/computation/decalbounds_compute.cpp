// [Doruk-Aksoy] The screen rectangle the deferred decal pass draws each mark over, worked out on the CPU for its vertex stage.
#include "features/hwrender/computation/decalbounds_compute.h"

#include <math.h>

namespace zx { namespace hwrender {

float ComputeDecalRiddenHeight(const float centre[3], const float plane[4], float offset)
{
	return -(plane[0] * centre[0] + plane[1] * centre[2] + plane[2]) * plane[3] + offset;
}

double ComputeDecalRectPixels(const DecalScreenRect &r, float width, float height)
{
	const double w = r.hi[0] > r.lo[0] ? (r.hi[0] - r.lo[0]) * 0.5 * width : 0.0;
	const double h = r.hi[1] > r.lo[1] ? (r.hi[1] - r.lo[1]) * 0.5 * height : 0.0;
	return w * h;
}

float ComputeDecalPaintRadius(const float params[4], const float axisU[3], const float axisV[3])
{
	const float lu = sqrtf(axisU[0] * axisU[0] + axisU[1] * axisU[1] + axisU[2] * axisU[2]);
	const float lv = sqrtf(axisV[0] * axisV[0] + axisV[1] * axisV[1] + axisV[2] * axisV[2]);
	const float hw = 1.f / (lu > 1e-6f ? lu : 1e-6f), hh = 1.f / (lv > 1e-6f ? lv : 1e-6f);
	const float inner = sqrtf(hw * hw + hh * hh);
	// [Doruk-Aksoy] A margin past the shader's own test, since it rebuilds the corners from the same axes in its own precision.
	if (params[2] > inner * 1.001f + 0.01f) return params[2];
	const float reach = params[0] > params[1] ? params[0] : params[1];
	return reach > params[2] ? reach : params[2];
}

bool ComputeDecalEye(const float *invMVP, float eye[3])
{
	// [Doruk-Aksoy] The eye projects to clip (0, 0, z, 0), so it is the inverse's third column brought back from homogeneous form.
	const float w = invMVP[11];
	if (fabsf(w) < 1e-12f) return false;
	eye[0] = invMVP[8] / w;
	eye[1] = invMVP[9] / w;
	eye[2] = invMVP[10] / w;
	return true;
}

DecalScreenRect ComputeDecalClippedRect(const float centre[3], float radius, const float eye[3], const float *mvp)
{
	DecalScreenRect r = { false, { 1e9f, 1e9f }, { -1e9f, -1e9f } };
	bool inside = true;
	for (int k = 0; k < 3; k++) inside = inside && fabsf(eye[k] - centre[k]) <= radius;
	if (inside)
	{
		r.whole = true;
		r.lo[0] = r.lo[1] = -1.f;
		r.hi[0] = r.hi[1] = 1.f;
		return r;
	}
	float clip[8][4];
	for (int i = 0; i < 8; i++)
	{
		const float p[3] = { centre[0] + ((i & 1) ? radius : -radius), centre[1] + ((i & 2) ? radius : -radius), centre[2] + ((i & 4) ? radius : -radius) };
		for (int k = 0; k < 4; k++) clip[i][k] = mvp[k] * p[0] + mvp[4 + k] * p[1] + mvp[8 + k] * p[2] + mvp[12 + k];
	}
	auto take = [&r](const float *c)
	{
		for (int k = 0; k < 2; k++)
		{
			const float ndc = c[k] / c[3];
			if (ndc < r.lo[k]) r.lo[k] = ndc;
			if (ndc > r.hi[k]) r.hi[k] = ndc;
		}
	};
	for (int i = 0; i < 8; i++)
	{
		if (clip[i][2] >= 0.f) take(clip[i]);
		// [Doruk-Aksoy] Each edge from this corner to the one across one axis, cut where it crosses the near plane.
		for (int bit = 1; bit < 8; bit <<= 1)
		{
			const int j = i | bit;
			if (j == i || (clip[i][2] >= 0.f) == (clip[j][2] >= 0.f)) continue;
			const float t = clip[i][2] / (clip[i][2] - clip[j][2]);
			float c[4];
			for (int k = 0; k < 4; k++) c[k] = clip[i][k] + t * (clip[j][k] - clip[i][k]);
			if (c[3] > 1e-6f) take(c);
		}
	}
	for (int k = 0; k < 2; k++)
	{
		if (r.lo[k] < -1.f) r.lo[k] = -1.f;
		if (r.hi[k] > 1.f) r.hi[k] = 1.f;
	}
	return r;
}

bool ComputeDecalRectShows(const DecalScreenRect &r)
{
	return r.hi[0] > r.lo[0] && r.hi[1] > r.lo[1];
}

}} // namespace zx::hwrender
