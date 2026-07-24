// [rc4l] Video-scale math -- faithful port of GZDoom/UZDoom's r_videoscale vScaleTable.
//
// This is the pure decision core for "what internal (virtual) resolution do we render at, given
// the window's client/drawable size and the user's scale settings." It is the same math upstream
// uses to drive its render-buffers present FBO (render at the virtual size, then blit-upscale to
// the client size).
//
// >>> SUPERSEDED-BY-UPSTREAM <<<
// Our 2016-era renderer has NO scene-present FBO -- the scene and 2D draw straight to the window
// backbuffer -- so genuine sub-native upscaling cannot be applied yet (there is no buffer to
// upscale from). This unit is the faithful math, landed and tested, so that when the
// render-buffers / present-FBO pipeline is ported (a later staircase flight, GZDoom's
// FGLRenderBuffers, ~mid-2016), the engine can feed `vid_scalemode`/`vid_scalefactor` straight in
// and delete our bespoke wiring. At that point prefer replacing this unit's *callers* with
// upstream's r_videoscale.cpp verbatim. Until then the only live caller pins scale mode to Native,
// which returns the client size unchanged (native fill) -- see features/video-scale/README.md.
//
// Upstream provenance: r_videoscale.cpp/.h, Copyright 2017 Magnus Norddahl, 2017-2020 Rachael
// Alexanderson (BSD-3-Clause / GPL-3.0-or-later). Header-pure: no engine/GL/SDL/CVAR includes.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_VIDEOSCALE_COMPUTE_H
#define ZX_VIDEOSCALE_COMPUTE_H

#include <stdint.h>

namespace zx
{

// Absolute minimum render dimensions, matching upstream's VID_MIN_WIDTH/HEIGHT and the
// 640x400 "special UI" floor.
enum
{
	VID_SCALE_MIN_WIDTH     = 320,
	VID_SCALE_MIN_HEIGHT    = 200,
	VID_SCALE_UI_MIN_WIDTH  = 640,
	VID_SCALE_UI_MIN_HEIGHT = 400,
};

// The built-in scale modes, in the same order as upstream's vScaleTable. Kept as named constants
// so callers and tests do not hard-code magic indices.
enum VideoScaleMode
{
	VID_SCALEMODE_NATIVE       = 0, // render at the client size (no scaling)
	VID_SCALEMODE_MIN_FILL     = 1, // minimum scale that still fills the whole screen
	VID_SCALEMODE_640x400      = 2, // pixel aspect 1.2
	VID_SCALEMODE_960x600      = 3, // pixel aspect 1.2
	VID_SCALEMODE_1280x800     = 4, // pixel aspect 1.2
	VID_SCALEMODE_CUSTOM       = 5, // uses customWidth/customHeight/customPixelAspect
	VID_SCALEMODE_320x200      = 6, // pixel aspect 1.2
	VID_SCALEMODE_MIN_FILL_1_2 = 7, // minimum scale to fill, at pixel aspect 1.2
	VID_SCALEMODE_COUNT        = 8,
};

// The result of a scale computation: the virtual render size plus the pixel aspect for that mode.
struct ScaledViewport
{
	int   width;
	int   height;
	float pixelAspect;
};

// A complete, backend-agnostic plan for presenting one frame with internal-resolution scaling.
// The GL glue is a dumb executor of this: render into an FBO of (virtualWidth x virtualHeight),
// then, if `active`, blit that FBO to the backbuffer rect (destX,destY,destW,destH). All the
// decisions live here so they are unit-tested; the glue holds none.
struct ScalePresentPlan
{
	int  virtualWidth;   // the render / FBO size == SCREENWIDTH
	int  virtualHeight;
	bool active;         // true when an offscreen scale buffer is needed (virtual != client)
	int  destX, destY, destW, destH; // where to blit on the backbuffer (fills the client rect)
};

// True if `mode` is a valid index into the scale table.
bool VideoScaleModeValid(int mode);

// The virtual render size for a window whose client/drawable size is clientWidth x clientHeight.
// Faithful to upstream ViewportScaledWidth/Height/ViewportPixelAspect combined:
//   - `scaleMode` picks a row of the scale table (invalid -> Native).
//   - `scaleFactor` multiplies the table's width/height (upstream vid_scalefactor).
//   - custom* are only consulted for VID_SCALEMODE_CUSTOM.
//   - when `cropAspect` is set, the client rect is first cropped to `activeRatio` (upstream
//     vid_cropaspect); pass the caller's ActiveRatio(w,h).
//   - the result is floored at (minWidth, minHeight) per axis (upstream's min_width/min_height,
//     which is VID_MIN_* normally and VID_SCALE_UI_MIN_* while a high-res UI font is required).
ScaledViewport ComputeScaledViewport(
	int clientWidth, int clientHeight,
	int scaleMode, float scaleFactor,
	int customWidth, int customHeight, float customPixelAspect,
	bool cropAspect, float activeRatio,
	int minWidth, int minHeight);

// The full present plan for a window whose client/drawable size is clientWidth x clientHeight.
// Same inputs as ComputeScaledViewport. `active` is false (and the render size equals the client
// size) exactly when no scaling is requested -- so the caller renders straight to the backbuffer,
// unchanged from the no-scaling path. Otherwise the caller renders into an FBO of the virtual size
// and blits it (stretched to fill) into the returned dest rect.
ScalePresentPlan ComputeScalePresentPlan(
	int clientWidth, int clientHeight,
	int scaleMode, float scaleFactor,
	int customWidth, int customHeight, float customPixelAspect,
	bool cropAspect, float activeRatio,
	int minWidth, int minHeight);

} // namespace zx

#endif // ZX_VIDEOSCALE_COMPUTE_H
