// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/virtualspace_compute.h"

namespace zx
{

VirtualSpace ComputeVirtualSpace(int screenW, int screenH, int layoutW, int layoutH)
{
	VirtualSpace out;
	out.width = layoutW;
	out.height = layoutH;

	if ((screenW <= 0) || (screenH <= 0) || (layoutW <= 0) || (layoutH <= 0))
		return out;

	// Whichever axis runs out first keeps the nominal size; the other grows past it, so the space
	// always has the screen's aspect and the scale is the same on both axes.
	if ((screenW * layoutH) <= (screenH * layoutW))
		out.height = (screenH * layoutW) / screenW;
	else
		out.width = (screenW * layoutH) / screenH;

	return out;
}

int VirtualToScreen(int v, int screenSize, int virtualSize)
{
	return (virtualSize > 0) ? (v * screenSize / virtualSize) : 0;
}

int ScreenToVirtual(int pixel, int atZero, int atSpan, int span)
{
	if (atSpan == atZero)
		return 0;

	return ((pixel - atZero) * span) / (atSpan - atZero);
}

} // namespace zx
