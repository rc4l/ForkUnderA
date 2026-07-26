// [rc4l] See consent_panel_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "consent_panel_compute.h"

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

ConsentPanelRect ComputeConsentPanelRect(int screenW, int screenH, int panelW,
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

	ConsentPanelRect out;
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

ConsentPanelColor ComputeConsentGradient(int row, int height,
	ConsentPanelColor top, ConsentPanelColor bottom)
{
	int den = height > 1 ? height - 1 : 1;
	int n = ClampInt(row, 0, den);
	ConsentPanelColor c;
	c.r = Lerp(top.r, bottom.r, n, den);
	c.g = Lerp(top.g, bottom.g, n, den);
	c.b = Lerp(top.b, bottom.b, n, den);
	c.a = Lerp(top.a, bottom.a, n, den);
	return c;
}

} // namespace zx
