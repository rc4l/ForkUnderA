// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/draw2d/computation/quad2d_compute.h"

#include <math.h>
#include <string.h>

namespace zx { namespace draw2d {

zx::hwrender::Quad2D ComputeFillQuad(float x, float y, float w, float h, unsigned rgb, float alpha)
{
	zx::hwrender::Quad2D q;
	memset(&q, 0, sizeof(q));
	q.x = x; q.y = y; q.w = w; q.h = h;
	q.u2 = 1.f; q.v2 = 1.f;
	q.r = ((rgb >> 16) & 0xff) / 255.f; q.g = ((rgb >> 8) & 0xff) / 255.f; q.b = (rgb & 0xff) / 255.f; q.a = alpha;
	q.blend = kBlend2DAlpha;
	q.kind = kQuadRect;
	return q;
}

zx::hwrender::Quad2D ComputeLineQuad(int x1, int y1, int x2, int y2, unsigned rgb)
{
	zx::hwrender::Quad2D q = ComputeFillQuad((float)x1, (float)y1, 0.f, 0.f, rgb, 1.f);
	q.u2 = q.v2 = 0.f;
	q.kind = kQuadLine;
	q.lx2 = (float)x2; q.ly2 = (float)y2;
	return q;
}

zx::hwrender::Quad2D ComputePixelQuad(int x, int y, unsigned rgb)
{
	zx::hwrender::Quad2D q = ComputeFillQuad((float)x, (float)y, 1.f, 1.f, rgb, 1.f);
	q.u2 = q.v2 = 0.f;
	return q;
}

zx::hwrender::Quad2D ComputeScreenFillQuad(float w, float h, float r, float g, float b, float a, bool multiply)
{
	zx::hwrender::Quad2D q = ComputeFillQuad(0.f, 0.f, w, h, 0, a);
	q.r = r; q.g = g; q.b = b;
	q.blend = multiply ? kBlend2DMultiply : kBlend2DAlpha;
	return q;
}

void ComputePolyTexCoords(const PolyTexIn &in, const float *points, int count, float *uv)
{
	// [rc4l] As GL works them, in float from the turn's radians on.
	const float rot = float(in.rotation * 3.14159265358979323846 / float(1u << 31));
	const bool rotate = rot != 0;
	const float cosrot = cosf(rot), sinrot = sinf(rot);
	const float uscale = float(1.f / (in.textureWidth * in.scaleX));
	float vscale = float(1.f / (in.textureHeight * in.scaleY));
	if (in.canvas) vscale = 0 - vscale;
	const float ox = float(in.originX), oy = float(in.originY);
	for (int i = 0; i < count; i++)
	{
		float u = points[2 * i] - 0.5f - ox;
		float v = points[2 * i + 1] - 0.5f - oy;
		if (rotate)
		{
			const float t = u;
			u = t * cosrot - v * sinrot;
			v = v * cosrot + t * sinrot;
		}
		uv[2 * i] = u * uscale;
		uv[2 * i + 1] = v * vscale;
	}
}

void ComputePolyTriangles(const zx::hwrender::Quad2D &base, const float *points, const float *uv, int count, std::vector<zx::hwrender::Quad2D> &out)
{
	out.clear();
	// [rc4l] GL draws it as a fan from the first point.
	for (int i = 1; i + 1 < count; i++)
	{
		zx::hwrender::Quad2D q = base;
		q.kind = kQuadTri;
		q.x = points[0];              q.y = points[1];              q.u1 = uv[0];            q.v1 = uv[1];
		q.lx2 = points[2 * i];        q.ly2 = points[2 * i + 1];    q.u2 = uv[2 * i];        q.v2 = uv[2 * i + 1];
		q.x3 = points[2 * i + 2];     q.y3 = points[2 * i + 3];     q.u3 = uv[2 * i + 2];    q.v3 = uv[2 * i + 3];
		out.push_back(q);
	}
}

bool ComputeDrawColorDirect(int palcolor, unsigned color)
{
	return color != 0 || palcolor < 0;
}

int ComputeTextureMode(int styleFlags, bool drawOpaque)
{
	if (styleFlags & kStyleRedIsAlpha) return kTexRedToAlpha;
	if (styleFlags & kStyleColorIsFixed) return kTexMask;
	if (styleFlags & kStyleInvertSource) return kTexInverse;
	return drawOpaque ? kTexOpaque : kTexModulate;
}

int ComputeQuadBlend(int blendOp, int destAlpha)
{
	return (blendOp == kStyleOpAdd && destAlpha == kStyleAlphaOne) ? kBlend2DAdditive : kBlend2DAlpha;
}

void ComputeTextureQuad(const TextureQuadIn &in, TextureQuadOut &out)
{
	const double xscale = in.destWidth / in.texWidth;
	const double yscale = in.destHeight / in.texHeight;
	double x = in.x - in.left * xscale;
	const double y = in.y - in.top * yscale;
	double w = in.destWidth;
	const double h = in.destHeight;

	// [rc4l] A black overlay is only a darkening, so it becomes the light; any other overlay is drawn over the quad.
	int light = 255;
	out.colorOverlay = in.colorOverlay;
	if (in.colorOverlay && (in.colorOverlay & 0xffffff) == 0)
	{
		light = 255 - int((in.colorOverlay >> 24) & 0xff);
		out.colorOverlay = 0;
	}

	float u1 = in.ul, v1 = in.vt, u2 = in.ur, v2 = in.vb;
	if (in.canvas) { u1 = 0.f; v1 = 1.f; u2 = 1.f; v2 = 0.f; }
	if (in.flipX) { const float t = u1; u1 = u2; u2 = t; }
	if (in.windowLeft > 0 || in.windowRight < in.texWidth)
	{
		x += in.windowLeft * xscale;
		w -= (in.texWidth - in.windowRight + in.windowLeft) * xscale;
		u1 = float(u1 + in.windowLeft / in.texWidth);
		u2 = float(u2 - (in.texWidth - in.windowRight) / in.texWidth);
	}

	out.x = float(x); out.y = float(y); out.w = float(w); out.h = float(h);
	out.x2 = float(x + w); out.y2 = float(y + h);
	out.u1 = u1; out.v1 = v1; out.u2 = u2; out.v2 = v2;
	if (in.colorIsFixed)
	{
		out.r = (unsigned char)(in.fillColor >> 16);
		out.g = (unsigned char)(in.fillColor >> 8);
		out.b = (unsigned char)in.fillColor;
	}
	else out.r = out.g = out.b = (unsigned char)light;
	out.a = (unsigned char)(in.alpha * 255.f);
}

}} // namespace zx::draw2d
