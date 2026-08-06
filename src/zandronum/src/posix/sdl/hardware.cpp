/*
** hardware.cpp
** Somewhat OS-independant interface to the screen, mouse, keyboard, and stick
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include <SDL.h>
#include <signal.h>
#include <time.h>

#include "version.h"
#include "hardware.h"
#include "i_video.h"
#include "i_system.h"
#include "c_console.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "v_text.h"
#include "doomstat.h"
#include "m_argv.h"
#ifndef NO_GL
#include "sdlglvideo.h"
#endif
// [rc4l] video-scale: faithful port of upstream's r_videoscale math. See features/video-scale.
#include "features/video-scale/computation/videoscale_compute.h"
#include "r_renderer.h"
// [rc4l] Software renderer removed (GL-only build); NO_GL server uses the null renderer.
#ifdef NO_GL
#include "r_nullrenderer.h"
#endif

EXTERN_CVAR (Bool, ticker)
EXTERN_CVAR (Bool, fullscreen)
EXTERN_CVAR (Float, vid_winscale)
// [rc4l] video-scale: the internal-resolution knob (features/video-scale/videoscale.cpp).
EXTERN_CVAR (Int, vid_scalemode)
EXTERN_CVAR (Float, vid_scalefactor)
EXTERN_CVAR (Int, vid_scale_customwidth)
EXTERN_CVAR (Int, vid_scale_customheight)
EXTERN_CVAR (Float, vid_scale_custompixelaspect)
EXTERN_CVAR (Bool, vid_cropaspect)

IVideo *Video;

extern int NewWidth, NewHeight, NewBits, DisplayBits;
#ifndef NO_GL
bool V_DoModeSetup (int width, int height, int bits);
void I_RestartRenderer();
#endif

#ifndef NO_GL
int currentrenderer=1;
#else
int currentrenderer=0;
#endif

// [rc4l] GL-only build: OpenGL is the only renderer. Kept so old configs and code that
// read vid_renderer still resolve, but any non-1 value (e.g. a software-renderer 0 from a
// pre-GL-only config) is snapped back to 1 rather than pretending to switch renderers.
CUSTOM_CVAR (Int, vid_renderer, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self != 1)
	{
		self = 1;
	}
}

void I_ShutdownGraphics ()
{
	if (screen)
	{
		DFrameBuffer *s = screen;
		screen = NULL;
		s->ObjectFlags |= OF_YesReallyDelete;
		delete s;
	}
	if (Video)
		delete Video, Video = NULL;

	SDL_QuitSubSystem (SDL_INIT_VIDEO);	// [rc4l] uzdoom@8e1b1aa20
}

void I_InitGraphics ()
{
	// [rc4l] uzdoom@8e1b1aa20
	if (SDL_InitSubSystem (SDL_INIT_VIDEO) < 0)
	{
		I_FatalError ("Could not initialize SDL video:\n%s\n", SDL_GetError());
		return;
	}

	Printf("Using video driver %s\n", SDL_GetCurrentVideoDriver());

	UCVarValue val;

	val.Bool = !!Args->CheckParm ("-devparm");
	ticker.SetGenericRepDefault (val, CVAR_Bool);

#ifndef NO_GL
	// [rc4l] GL-only build: OpenGL is the only renderer, so there is no software video backend to fall back to.
	Video = new SDLGLVideo(0);
#else
	// [rc4l] The dedicated server has no renderer and must never try to open a window.
	I_FatalError ("This is a server-only build; it cannot initialize graphics");
#endif
	if (Video == NULL)
		I_FatalError ("Failed to initialize display");

	atterm (I_ShutdownGraphics);

	Video->SetWindowedScale (vid_winscale);
}

static void I_DeleteRenderer()
{
	if (Renderer != NULL) delete Renderer;
}

void I_CreateRenderer()
{
	currentrenderer = vid_renderer;
	if (Renderer == NULL)
	{
#ifndef NO_GL
		// [rc4l] GL-only build: always use the OpenGL renderer.
		Renderer = gl_CreateInterface();
#else
		// [rc4l] Dedicated server (no OpenGL): use the trivial null renderer.
		Renderer = new FNullRenderer;
#endif
		atterm(I_DeleteRenderer);
	}
}


/** Remaining code is common to Win32 and Linux **/

// VIDEO WRAPPERS ---------------------------------------------------------

DFrameBuffer *I_SetMode (int &width, int &height, DFrameBuffer *old)
{
	bool fs = false;
	switch (Video->GetDisplayType ())
	{
	case DISPLAY_WindowOnly:
		fs = false;
		break;
	case DISPLAY_FullscreenOnly:
		fs = true;
		break;
	case DISPLAY_Both:
		fs = fullscreen;
		break;
	}

	// [rc4l] video-scale: split the window's CLIENT size from the RENDER (virtual) size.
	//   - client size = the window's drawable: the desktop for fullscreen (borderless-desktop), or
	//     the requested size for a window.
	//   - render/virtual size = what the engine actually renders, from the scale unit. Native/1.0
	//     (default) => virtual == client => renders straight to the backbuffer, exactly as before;
	//     e.g. vid_scalefactor 0.5 => virtual == half the client, blit-upscaled to fill by the GL
	//     executor in gl_framebuffer.cpp.
	// We pass the virtual size on as width/height (so the framebuffer, SCREENWIDTH and the GL
	// viewport all agree) and stash the client size for the SDL window creation.
	// >>> SUPERSEDED-BY-UPSTREAM <<< See features/video-scale/README.md.
	int clientW = width, clientH = height;
	if (fs)
	{
		SDL_DisplayMode desktop;
		if (SDL_GetDesktopDisplayMode (0, &desktop) == 0 && desktop.w > 0 && desktop.h > 0)
		{
			clientW = desktop.w;
			clientH = desktop.h;
		}
	}
	{
		zx::ScalePresentPlan plan = zx::ComputeScalePresentPlan (
			clientW, clientH,
			vid_scalemode, vid_scalefactor,
			vid_scale_customwidth, vid_scale_customheight, vid_scale_custompixelaspect,
			!!vid_cropaspect, 0.f,
			zx::VID_SCALE_MIN_WIDTH, zx::VID_SCALE_MIN_HEIGHT);
		width  = plan.virtualWidth;
		height = plan.virtualHeight;
	}
	extern int zx_pendingClientWidth, zx_pendingClientHeight;
	zx_pendingClientWidth  = clientW;
	zx_pendingClientHeight = clientH;

	DFrameBuffer *res = Video->CreateFrameBuffer (width, height, fs, old);

	/* Right now, CreateFrameBuffer cannot return NULL
	if (res == NULL)
	{
		I_FatalError ("Mode %dx%d is unavailable\n", width, height);
	}
	*/
	return res;
}

bool I_CheckResolution (int width, int height, int bits)
{
	int twidth, theight;

	Video->StartModeIterator (bits, screen ? screen->IsFullscreen() : fullscreen);
	while (Video->NextMode (&twidth, &theight, NULL))
	{
		if (width == twidth && height == theight)
			return true;
	}
	return false;
}

void I_ClosestResolution (int *width, int *height, int bits)
{
	int twidth, theight;
	int cwidth = 0, cheight = 0;
	int iteration;
	DWORD closest = 4294967295u;

	for (iteration = 0; iteration < 2; iteration++)
	{
		Video->StartModeIterator (bits, screen ? screen->IsFullscreen() : fullscreen);
		while (Video->NextMode (&twidth, &theight, NULL))
		{
			if (twidth == *width && theight == *height)
				return;

			if (iteration == 0 && (twidth < *width || theight < *height))
				continue;

			DWORD dist = (twidth - *width) * (twidth - *width)
				+ (theight - *height) * (theight - *height);

			if (dist < closest)
			{
				closest = dist;
				cwidth = twidth;
				cheight = theight;
			}
		}
		if (closest != 4294967295u)
		{
			*width = cwidth;
			*height = cheight;
			return;
		}
	}
}

//==========================================================================
//
// SetFPSLimit
//
// Initializes an event timer to fire at a rate of <limit>/sec. The video
// update will wait for this timer to trigger before updating.
//
// Pass 0 as the limit for unlimited.
// Pass a negative value for the limit to use the value of vid_maxfps.
//
//==========================================================================

EXTERN_CVAR(Int, vid_maxfps);
EXTERN_CVAR(Bool, cl_capfps);

#ifndef __APPLE__
Semaphore FPSLimitSemaphore;

static void FPSLimitNotify(sigval val)
{
	SEMAPHORE_SIGNAL(FPSLimitSemaphore)
}

void I_SetFPSLimit(int limit)
{
	static sigevent FPSLimitEvent;
	static timer_t FPSLimitTimer;
	static bool FPSLimitTimerEnabled = false;
	static bool EventSetup = false;
	if(!EventSetup)
	{
		EventSetup = true;
		FPSLimitEvent.sigev_notify = SIGEV_THREAD;
		FPSLimitEvent.sigev_signo = 0;
		FPSLimitEvent.sigev_value.sival_int = 0;
		FPSLimitEvent.sigev_notify_function = FPSLimitNotify;
		FPSLimitEvent.sigev_notify_attributes = NULL;

		SEMAPHORE_INIT(FPSLimitSemaphore, 0, 0)
	}

	if (limit < 0)
	{
		limit = vid_maxfps;
	}
	// Kill any leftover timer.
	if (FPSLimitTimerEnabled)
	{
		timer_delete(FPSLimitTimer);
		FPSLimitTimerEnabled = false;
	}
	if (limit == 0)
	{ // no limit
		DPrintf("FPS timer disabled\n");
	}
	else
	{
		FPSLimitTimerEnabled = true;
		if(timer_create(CLOCK_REALTIME, &FPSLimitEvent, &FPSLimitTimer) == -1)
			Printf("Failed to create FPS limitter event\n");
		itimerspec period = { {0, 0}, {0, 0} };
		period.it_value.tv_nsec = period.it_interval.tv_nsec = 1000000000 / limit;
		if(timer_settime(FPSLimitTimer, 0, &period, NULL) == -1)
			Printf("Failed to set FPS limitter timer\n");
		DPrintf("FPS timer set to %u ms\n", (unsigned int) period.it_interval.tv_nsec / 1000000);
	}
}
#else
// So Apple doesn't support POSIX timers and I can't find a good substitute short of
// having Objective-C Cocoa events or something like that.
void I_SetFPSLimit(int limit)
{
}
#endif

// [rc4l] Default 500 to match upstream UZDoom/GZDoom (was 200). vsync and cl_capfps are already off
// by default like upstream; this was the only framerate-cap divergence. 0 = uncapped, clamp [35,1000].
CUSTOM_CVAR (Int, vid_maxfps, 500, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (vid_maxfps < TICRATE && vid_maxfps != 0)
	{
		vid_maxfps = TICRATE;
	}
	else if (vid_maxfps > 1000)
	{
		vid_maxfps = 1000;
	}
	else if (cl_capfps == 0)
	{
		I_SetFPSLimit(vid_maxfps);
	}
}

#ifndef NO_GL
CUSTOM_CVAR (Bool, fullscreen, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG|CVAR_NOINITCALL)
#else
CUSTOM_CVAR (Bool, fullscreen, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
#endif
{
	if ( screen )
	{
		NewWidth = screen->GetWidth();
		NewHeight = screen->GetHeight();
	}
	NewBits = DisplayBits;
	setmodeneeded = true;
}

CUSTOM_CVAR (Float, vid_winscale, 1.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (self < 1.f)
	{
		self = 1.f;
	}
	else if (Video)
	{
		Video->SetWindowedScale (self);
		NewWidth = screen->GetWidth();
		NewHeight = screen->GetHeight();
		NewBits = DisplayBits;
#ifdef NO_GL
		setmodeneeded = true;
#else
		//setmodeneeded = true;	// This CVAR doesn't do anything and only causes problems!
#endif
	}
}

CCMD (vid_listmodes)
{
	static const char *ratios[5] = { "", " - 16:9", " - 16:10", "", " - 5:4" };
	int width, height, bits;
	bool letterbox;

	if (Video == NULL)
	{
		return;
	}
	for (bits = 1; bits <= 32; bits++)
	{
		Video->StartModeIterator (bits, screen->IsFullscreen());
		while (Video->NextMode (&width, &height, &letterbox))
		{
			bool thisMode = (width == DisplayWidth && height == DisplayHeight && bits == DisplayBits);
			int ratio = CheckRatio (width, height);
			Printf (thisMode ? PRINT_BOLD : PRINT_HIGH,
				"%s%4d x%5d x%3d%s%s\n",
				thisMode || !(ratio & 3) ? "" : TEXTCOLOR_GOLD,
				width, height, bits,
				ratios[ratio],
				thisMode || !letterbox ? "" : TEXTCOLOR_BROWN " LB"
				);
		}
	}
}

CCMD (vid_currentmode)
{
	Printf ("%dx%dx%d\n", DisplayWidth, DisplayHeight, DisplayBits);
}
