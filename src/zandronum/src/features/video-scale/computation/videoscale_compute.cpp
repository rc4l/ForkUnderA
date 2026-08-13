// [rc4l] See videoscale_compute.h. Faithful port of upstream r_videoscale's vScaleTable math.
// Pure: no engine/GL/SDL/CVAR dependencies. >>> SUPERSEDED-BY-UPSTREAM <<< (see header).
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "videoscale_compute.h"

namespace zx
{

namespace
{
	// Upstream v_MinimumToFill: the minimum uniform scale that still fills the screen while getting
	// as close to the 640x400 UI floor as possible. (We drop upstream's static memo cache -- it is
	// a pure function of its inputs; the memo only saved arithmetic.)
	float MinimumToFill(int inwidth, int inheight)
	{
		float sx = (float)inwidth, sy = (float)inheight;
		if (sx <= 0.f || sy <= 0.f)
			return 1.f; // prevent x/0
		float ssx = (float)VID_SCALE_UI_MIN_WIDTH / sx;
		float ssy = (float)VID_SCALE_UI_MIN_HEIGHT / sy;
		return (ssx < ssy) ? ssy : ssx;
	}
	int MinFillX(int w, int h) { return (int)((float)w * MinimumToFill(w, h)); }
	int MinFillY(int w, int h) { return (int)((float)h * MinimumToFill(w, h)); }

	// Upstream v_MinimumToFill2: same idea, biased by the 1.2 pixel aspect used by mode 7.
	float MinimumToFill2(int inwidth, int inheight)
	{
		float sx = (float)inwidth * 1.2f, sy = (float)inheight;
		if (sx <= 0.f || sy <= 0.f)
			return 1.f;
		float ssx = (float)VID_SCALE_UI_MIN_WIDTH / 1.2f / sx;
		float ssy = (float)VID_SCALE_UI_MIN_HEIGHT / sy;
		return (ssx < ssy) ? ssy : ssx;
	}
	int MinFillX2(int w, int h) { return (int)((float)w * MinimumToFill2(w, h) * 1.2f); }
	int MinFillY2(int w, int h) { return (int)((float)h * MinimumToFill2(w, h)); }

	int32_t Max32(int32_t a, int32_t b) { return a > b ? a : b; }

	// The scale-table width/height for a mode, before scaleFactor and the min-clamp. Mirrors the
	// GetScaledWidth/GetScaledHeight lambdas in upstream's vScaleTable, row for row.
	int ScaledWidthForMode(int mode, int w, int h, int customWidth)
	{
		switch (mode)
		{
		case VID_SCALEMODE_NATIVE:       return w;
		case VID_SCALEMODE_MIN_FILL:     return MinFillX(w, h);
		case VID_SCALEMODE_640x400:      return 640;
		case VID_SCALEMODE_960x600:      return 960;
		case VID_SCALEMODE_1280x800:     return 1280;
		case VID_SCALEMODE_CUSTOM:       return customWidth;
		case VID_SCALEMODE_320x200:      return 320;
		case VID_SCALEMODE_MIN_FILL_1_2: return (int)((float)MinFillX2(w, h) * 1.2f);
		default:                         return w;
		}
	}
	int ScaledHeightForMode(int mode, int w, int h, int customHeight)
	{
		switch (mode)
		{
		case VID_SCALEMODE_NATIVE:       return h;
		case VID_SCALEMODE_MIN_FILL:     return MinFillY(w, h);
		case VID_SCALEMODE_640x400:      return 400;
		case VID_SCALEMODE_960x600:      return 600;
		case VID_SCALEMODE_1280x800:     return 800;
		case VID_SCALEMODE_CUSTOM:       return customHeight;
		case VID_SCALEMODE_320x200:      return 200;
		case VID_SCALEMODE_MIN_FILL_1_2: return MinFillY2(w, h);
		default:                         return h;
		}
	}
	float PixelAspectForMode(int mode, float customPixelAspect)
	{
		switch (mode)
		{
		case VID_SCALEMODE_640x400:
		case VID_SCALEMODE_960x600:
		case VID_SCALEMODE_1280x800:
		case VID_SCALEMODE_320x200:
		case VID_SCALEMODE_MIN_FILL_1_2: return 1.2f;
		case VID_SCALEMODE_CUSTOM:       return customPixelAspect;
		default:                         return 1.0f; // Native, MinFill
		}
	}
} // namespace

bool VideoScaleModeValid(int mode)
{
	return mode >= 0 && mode < VID_SCALEMODE_COUNT;
}

ScaledViewport ComputeScaledViewport(
	int clientWidth, int clientHeight,
	int scaleMode, float scaleFactor,
	int customWidth, int customHeight, float customPixelAspect,
	bool cropAspect, float activeRatio,
	int minWidth, int minHeight)
{
	// An out-of-range scaleMode is handled by the switch `default:` cases below (ScaledWidthForMode /
	// ScaledHeightForMode return the client size, PixelAspectForMode returns 1.0) -- i.e. it renders
	// exactly like Native. No separate clamp here: that would just duplicate the defaults and leave
	// them dead/untestable.
	int w = clientWidth;
	int h = clientHeight;

	// Upstream vid_cropaspect: crop the client rect to the active aspect ratio before scaling.
	// Applied to width and height in turn, exactly as ViewportScaledWidth/Height do.
	if (cropAspect && h > 0 && activeRatio > 0.f)
	{
		int cw = ((float)w / h > activeRatio) ? (int)(h * activeRatio) : w;
		int ch = ((float)w / h < activeRatio) ? (int)(w / activeRatio) : h;
		w = cw;
		h = ch;
	}

	ScaledViewport out;
	out.width  = (int)Max32((int32_t)minWidth,  (int32_t)(scaleFactor * ScaledWidthForMode(scaleMode, w, h, customWidth)));
	out.height = (int)Max32((int32_t)minHeight, (int32_t)(scaleFactor * ScaledHeightForMode(scaleMode, w, h, customHeight)));
	out.pixelAspect = PixelAspectForMode(scaleMode, customPixelAspect);
	return out;
}

ScalePresentPlan ComputeScalePresentPlan(
	int clientWidth, int clientHeight,
	int scaleMode, float scaleFactor,
	int customWidth, int customHeight, float customPixelAspect,
	bool cropAspect, float activeRatio,
	int minWidth, int minHeight)
{
	ScaledViewport v = ComputeScaledViewport(
		clientWidth, clientHeight, scaleMode, scaleFactor,
		customWidth, customHeight, customPixelAspect,
		cropAspect, activeRatio, minWidth, minHeight);

	ScalePresentPlan p;
	p.virtualWidth  = v.width;
	p.virtualHeight = v.height;
	// An offscreen scale buffer is only needed when the render size differs from the window: at
	// native 1:1 we render straight to the backbuffer, exactly as the no-scaling path did.
	p.active = (v.width != clientWidth || v.height != clientHeight);
	// Stretch the virtual frame to fill the whole client rect (faithful to upstream, which blits
	// the render buffer across the entire output viewport).
	p.destX = 0;
	p.destY = 0;
	p.destW = clientWidth;
	p.destH = clientHeight;
	return p;
}

ScaleReconcile ComputeScaleReconcile(
	int clientWidth, int clientHeight,
	int renderWidth, int renderHeight,
	int cachedClientWidth, int cachedClientHeight,
	int wantWidth, int wantHeight)
{
	if (wantWidth != renderWidth || wantHeight != renderHeight)
		return SCALE_RECONCILE_RESIZE;
	if (clientWidth != cachedClientWidth || clientHeight != cachedClientHeight)
		return SCALE_RECONCILE_REBUILD;
	return SCALE_RECONCILE_NONE;
}

void ScaleWindowPointToRender(
	int clientWidth, int clientHeight,
	int renderWidth, int renderHeight,
	int &x, int &y)
{
	// Each axis on its own, because the present stretches each to fill the client and the two
	// ratios differ whenever the window is not the render buffer's shape.
	if ((clientWidth > 0) && (renderWidth > 0))
		x = (x * renderWidth) / clientWidth;

	if ((clientHeight > 0) && (renderHeight > 0))
		y = (y * renderHeight) / clientHeight;
}

} // namespace zx
