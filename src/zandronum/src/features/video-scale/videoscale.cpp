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

// Set by the platform CVAR layer; consumed by the main loop to re-run mode setup.
extern bool setmodeneeded;
extern int NewWidth, NewHeight, NewBits, DisplayBits;

// Re-run V_DoModeSetup so I_SetMode recomputes the virtual (render) size from the new scale
// settings. For fullscreen the size is derived from the desktop, so the New* values just carry the
// request through; mirrors the `fullscreen` CVAR handler.
static void VideoScale_RequestModeReset()
{
	if (screen != NULL)
	{
		NewWidth  = screen->GetWidth();
		NewHeight = screen->GetHeight();
	}
	NewBits = DisplayBits;
	setmodeneeded = true;
}

// vid_scalefactor: a multiplier on the chosen scale mode (upstream range 0.05..2.0). 1.0 = the mode
// as-is. < 1.0 renders fewer pixels (upscaled to fill = faster); > 1.0 supersamples.
CUSTOM_CVAR(Float, vid_scalefactor, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < 0.05f || self > 2.0f)
		self = 1.0f;
	VideoScale_RequestModeReset();
}

// vid_scalemode: index into the scale table (Native / min-fill / fixed presets / custom).
CUSTOM_CVAR(Int, vid_scalemode, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (!zx::VideoScaleModeValid(self))
		self = 0;
	VideoScale_RequestModeReset();
}

CUSTOM_CVAR(Int, vid_scale_customwidth, zx::VID_SCALE_MIN_WIDTH, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < zx::VID_SCALE_MIN_WIDTH)
		self = zx::VID_SCALE_MIN_WIDTH;
	VideoScale_RequestModeReset();
}

CUSTOM_CVAR(Int, vid_scale_customheight, zx::VID_SCALE_MIN_HEIGHT, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < zx::VID_SCALE_MIN_HEIGHT)
		self = zx::VID_SCALE_MIN_HEIGHT;
	VideoScale_RequestModeReset();
}

CUSTOM_CVAR(Float, vid_scale_custompixelaspect, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < 0.2f || self > 5.0f)
		self = 1.0f;
	VideoScale_RequestModeReset();
}

CUSTOM_CVAR(Bool, vid_cropaspect, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	VideoScale_RequestModeReset();
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
