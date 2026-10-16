// [rc4l] video-scale CVARs -- the user-facing "knob" for internal-resolution rendering.
//
// Faithful to upstream r_videoscale.cpp's CVAR surface (vid_scalemode / vid_scalefactor /
// vid_scale_custom* / vid_cropaspect), so a later wholesale port of upstream's video backend drops
// in cleanly. The pure math lives in computation/videoscale_compute (unit-tested); this file is
// only the engine glue: the archived CVARs plus the mode-reset trigger. The actual scaling is done
// by the GL executor in gl_framebuffer.cpp (marked [rc4l] video-scale), which renders the frame
// into an FBO of the computed virtual size and blits it up to fill the window.
//
// >>> SUPERSEDED-BY-UPSTREAM <<< See features/video-scale/README.md.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "c_cvars.h"
#include "c_dispatch.h"
#include "v_video.h"
#include "features/video-scale/computation/videoscale_compute.h"
#include "features/video-scale/videoscale.h"
#include "doomstat.h"   // [rc4l] setsizeneeded
#include "sbar.h"       // [rc4l] StatusBar
#include "features/handoff/effects.h"   // [rc4l] BesideTic, QueueUI

// The scale CVARs do NOT trigger a mode reset. A mode reset would destroy and recreate the window +
// GL context (black flash + keyboard-focus loss on every slider tick). Instead the framebuffer's
// MaybeResizeForScale recomputes the render size from these CVARs every frame and resizes the
// render target in place, keeping the window -- exactly as upstream does. So setting one here is
// all that is required; there is nothing to notify.

// vid_scalefactor: a multiplier on the chosen scale mode (upstream range 0.05..2.0). 1.0 = the mode
// as-is. < 1.0 renders fewer pixels (upscaled to fill = faster); > 1.0 supersamples.
CUSTOM_CVAR(Float, vid_scalefactor, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < 0.05f || self > 2.0f)
		self = 1.0f;
}

// vid_scalemode: index into the scale table (Native / min-fill / fixed presets / custom).
CUSTOM_CVAR(Int, vid_scalemode, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (!zx::VideoScaleModeValid(self))
		self = 0;
}

CUSTOM_CVAR(Int, vid_scale_customwidth, zx::VID_SCALE_MIN_WIDTH, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < zx::VID_SCALE_MIN_WIDTH)
		self = zx::VID_SCALE_MIN_WIDTH;
}

CUSTOM_CVAR(Int, vid_scale_customheight, zx::VID_SCALE_MIN_HEIGHT, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < zx::VID_SCALE_MIN_HEIGHT)
		self = zx::VID_SCALE_MIN_HEIGHT;
}

CUSTOM_CVAR(Float, vid_scale_custompixelaspect, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < 0.2f || self > 5.0f)
		self = 1.0f;
}

CUSTOM_CVAR(Bool, vid_cropaspect, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	// Read by MaybeResizeForScale every frame; nothing to do here.
}

// Report the real vs emulated resolution, like upstream's vid_showcurrentscaling.
CCMD(vid_showcurrentscaling)
{
	if (screen == NULL)
		return;
	// The render size the engine is currently using is GetWidth()/GetHeight() (the virtual size);
	// the client size is not separately tracked here, so report the render size and the factor.
	Printf("Current vid_scalefactor: %f\n", (float)vid_scalefactor);
	Printf("Render resolution: %d x %d\n", screen->GetWidth(), screen->GetHeight());
}

namespace zx {

ScaleReconcile ReconcileRenderSize(int clientW, int clientH, int renderW, int renderH, int lastClientW, int lastClientH, int &wantW, int &wantH)
{
	const ScaledViewport v = ComputeScaledViewport(clientW, clientH,
		vid_scalemode, vid_scalefactor,
		vid_scale_customwidth, vid_scale_customheight, vid_scale_custompixelaspect,
		!!vid_cropaspect, 0.f,
		VID_SCALE_MIN_WIDTH, VID_SCALE_MIN_HEIGHT);
	wantW = v.width;
	wantH = v.height;
	return ComputeScaleReconcile(clientW, clientH, renderW, renderH, lastClientW, lastClientH, v.width, v.height);
}

void ResizeScreenInPlace(DSimpleCanvas *canvas, int w, int h)
{
	// [rc4l] Mirrors upstream's V_OutputResized: no window or context is recreated, so there is no black flash or focus loss.
	canvas->Resize(w, h);
	V_RecalcVideoModeState(w, h, DisplayBits);
	// [rc4l] The status bar caches ST_Y from SCREENHEIGHT, and a stale one sizes the 3D view taller than the buffer it draws into; it is the game's, so beside a tic it waits for the handoff.
	if (zx::handoff::BesideTic()) zx::handoff::QueueUI({ [](intptr_t, intptr_t) { if (StatusBar != NULL) StatusBar->ScreenSizeChanged(); }, 0, 0 });
	else if (StatusBar != NULL) StatusBar->ScreenSizeChanged();
	setsizeneeded = true;
}

} // namespace zx
