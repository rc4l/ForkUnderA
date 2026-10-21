// [rc4l] See promptpanel_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/updater/computation/promptpanel_compute.h"

namespace zx
{

static int ClampInt(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

// Floor of the integer square root of a non-negative value.
static int FloorSqrt(int v)
{
	if (v <= 0)
		return 0;
	int r = 1;
	while ((r + 1) * (r + 1) <= v)
		++r;
	return r;
}

// Linear interpolation a + (b - a) * num / den. Callers guarantee den >= 1.
static int Lerp(int a, int b, int num, int den)
{
	return a + (b - a) * num / den;
}

PanelRect ComputePanelRect(int screenW, int screenH, int panelW,
	int contentTop, int contentBottom, int padY, int radius)
{
	int maxW = screenW > 0 ? screenW : 0;
	int w = ClampInt(panelW, 0, maxW); // never wider than the screen, never negative

	int top = contentTop - padY;
	if (top < 0)
		top = 0;
	int bottom = contentBottom + padY;
	if (bottom > screenH)
		bottom = screenH;
	int h = bottom - top;
	if (h < 0)
		h = 0;

	PanelRect out;
	out.x = (screenW - w) / 2; // centred; w <= screenW so this is always >= 0
	out.y = top;
	out.w = w;
	out.h = h;
	int half = (w < h ? w : h) / 2;
	out.radius = ClampInt(radius, 0, half);
	return out;
}

int ComputeCenteredTop(int screenH, int blockH)
{
	int top = (screenH - blockH) / 2;
	if (top < 0)
		top = 0;
	return top;
}

int ComputeRoundedInset(int row, int height, int radius)
{
	if (radius <= 0)
		return 0;
	if (row < 0 || row >= height)
		return 0;

	int fromTop = row;
	int fromBottom = height - 1 - row;
	int v = fromTop < fromBottom ? fromTop : fromBottom; // distance to the nearest edge
	if (v >= radius)
		return 0; // straight middle band

	// The corner follows a circle of the given radius: inset = radius - sqrt(radius^2 - (radius-v)^2)
	int d = radius - v;
	return radius - FloorSqrt(radius * radius - d * d);
}

PanelColor ComputePanelGradient(int row, int height, PanelColor top, PanelColor bottom)
{
	int den = height > 1 ? height - 1 : 1;
	int n = ClampInt(row, 0, den);
	PanelColor c;
	c.r = Lerp(top.r, bottom.r, n, den);
	c.g = Lerp(top.g, bottom.g, n, den);
	c.b = Lerp(top.b, bottom.b, n, den);
	c.a = Lerp(top.a, bottom.a, n, den);
	return c;
}

int ComputeSeparatorAlpha(int x, int width, int peak)
{
	if ((width <= 0) || (peak <= 0))
		return 0;
	if ((x < 0) || (x >= width))
		return 0;

	// Distance from the middle column, scaled so the ends land on zero. Integer throughout: this is
	// an alpha in 0..255 and a fractional one would only be rounded away at the Dim() call.
	const int mid = width / 2;
	const int dist = (x < mid) ? (mid - x) : (x - mid);
	const int half = (mid > 0) ? mid : 1;

	const int a = peak - (peak * dist) / half;
	return (a > 0) ? a : 0;
}

} // namespace zx
