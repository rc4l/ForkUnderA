// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/draw2d/computation/quad2d_compute.h"

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
