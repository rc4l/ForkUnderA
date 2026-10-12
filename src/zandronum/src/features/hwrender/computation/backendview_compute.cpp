// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/backendview_compute.h"

#include <math.h>

namespace zx { namespace hwrender {

void ComputeBackendView(const BackendCamera &c, float v[16])
{
	const float stretch = c.pixelStretch > 0.f ? c.pixelStretch : 1.f;

	// [rc4l] The pitch GL turns by, FGLRenderer::RenderViewpoint's: the playsim's angle seen through stretched pixels.
	const double angx = cos(c.pitch);
	const double angy = sin(c.pitch) * stretch;
	const double alen = sqrt(angx * angx + angy * angy);
	const float pitch = (float)asin(angy / alen);
	const float cp = cosf(pitch), sp = sinf(pitch);
	const float ca = cosf(c.yaw), sa = sinf(c.yaw);

	// [rc4l] Mesh space (east, up, north) is left-handed, so the right axis is cross(up, forward), and rows 1 and 2 are up and back turned by the pitch.
	for (int i = 0; i < 16; i++) v[i] = 0.0f;
	v[0] =  sa;      v[4] = 0.0f; v[8]  = -ca;
	v[1] =  sp * ca; v[5] = cp;   v[9]  =  sp * sa;
	v[2] = -cp * ca; v[6] = sp;   v[10] = -cp * sa;
	// [rc4l] Heights stretched before the pitch turns them, as SetViewMatrix's scale(-1, pixelstretch, 1) does: GL draws the world this much taller than square, level or not.
	v[4] *= stretch; v[5] *= stretch; v[6] *= stretch;
	v[15] = 1.0f;
	const float px = c.x, py = c.z, pz = c.y;
	v[12] = -(v[0] * px + v[4] * py + v[8] * pz);
	v[13] = -(v[1] * px + v[5] * py + v[9] * pz);
	v[14] = -(v[2] * px + v[6] * py + v[10] * pz);
}

ScreenRect ComputeViewWindowViewport(int screenH, int screenblocks, int windowX, int windowY, int viewW, int viewH)
{
	const int height = screenblocks >= 10 ? screenH : (screenblocks * screenH / 10) & ~7;
	ScreenRect r;
	r.x = windowX;
	r.y = windowY - (height - viewH) / 2;
	r.w = viewW;
	r.h = height;
	return r;
}

NdcWindow ComputeNdcWindow(const ScreenRect &rect, int screenW, int screenH)
{
	NdcWindow w;
	w.sx = (float)rect.w / screenW;
	w.sy = (float)rect.h / screenH;
	w.ox = (float)(2 * rect.x + rect.w) / screenW - 1.f;
	w.oy = 1.f - (float)(2 * rect.y + rect.h) / screenH;
	return w;
}

void ComputeApplyNdcWindow(const NdcWindow &w, float m[16])
{
	for (int col = 0; col < 4; col++)
	{
		const float clipW = m[col * 4 + 3];
		m[col * 4 + 0] = w.sx * m[col * 4 + 0] + w.ox * clipW;
		m[col * 4 + 1] = w.sy * m[col * 4 + 1] + w.oy * clipW;
	}
}

void ComputeBackendMVP(const BackendCamera &c, float out[16])
{
	const float zn = 5.0f, zf = 65536.0f;
	const float f = 1.0f / tanf(c.fovY * 0.5f);

	// [rc4l] Vulkan clip space maps depth to [0,1].
	float p[16] = {0};
	p[0] = f / c.aspect; p[5] = f; p[10] = zf / (zn - zf); p[11] = -1.0f;
	p[14] = (zf * zn) / (zn - zf);

	float v[16];
	ComputeBackendView(c, v);
	for (int col = 0; col < 4; col++)
		for (int r = 0; r < 4; r++)
		{
			float sum = 0;
			for (int k = 0; k < 4; k++) sum += p[k * 4 + r] * v[col * 4 + k];
			out[col * 4 + r] = sum;
		}
}

}} // namespace zx::hwrender
