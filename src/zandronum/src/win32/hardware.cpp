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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define USE_WINDOWS_DWORD
#include "hardware.h"
#include "win32iface.h"
#include "i_video.h"
// [rc4l] video-scale: faithful port of upstream's r_videoscale math. See features/video-scale.
#include "features/video-scale/computation/videoscale_compute.h"
#include "i_system.h"
#include "c_console.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "v_text.h"
#include "doomstat.h"
#include "m_argv.h"
#include "version.h"
// [rc4l] Declares FRenderer and the Renderer global, which used to arrive here through a
// software-renderer header; matches sdl/hardware.cpp.
#include "r_renderer.h"
// [rc4l] Software renderer removed (GL-only build); NO_GL server uses the null renderer.
#ifdef NO_GL
#include "r_nullrenderer.h"
#endif

EXTERN_CVAR (Bool, ticker)
EXTERN_CVAR (Bool, fullscreen)
EXTERN_CVAR (Float, vid_winscale)
// [rc4l] video-scale: the render-scale knob (features/video-scale/videoscale.cpp).
EXTERN_CVAR (Int, vid_scalemode)
EXTERN_CVAR (Float, vid_scalefactor)
EXTERN_CVAR (Int, vid_scale_customwidth)
EXTERN_CVAR (Int, vid_scale_customheight)
EXTERN_CVAR (Float, vid_scale_custompixelaspect)
EXTERN_CVAR (Bool, vid_cropaspect)
extern int zx_pendingClientWidth, zx_pendingClientHeight;

CVAR(Int, win_x, -1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, win_y, -1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// [rc4l] PROVENANCE: NO UPSTREAM COMMIT -- ours, modelled on upstream's approach rather than taken
// from a commit.
//
//   SUPERSEDED BY: uzdoom@f8e23500c73b9ba23a48f3cf0829593d22289f12 (2020-04-23) "moved Windows platform code as well", which
//   creates common/platform/win32/base_sysfb.cpp. That file keeps win_x/win_y/win_w/win_h together
//   and calls SaveWindowedPos() on the transition into fullscreen -- properly doing what the two
//   CVARs below do by hand.
//   ON PORT: adopt its SaveWindowedPos/RestoreWindowedPos, then delete these two CVARs, the size
//   capture in I_SaveWindowedPos, and both uses in win32gliface.cpp.
//
// We had position but not SIZE. Going fullscreen and back therefore restored where the window was
// and whatever size the current mode happened to be. That was invisible while a window could only
// ever BE a video mode -- "the size we left" and "the current mode's size" were the same answer.
// Free resizing separates them.
//
// -1 means "not remembered", matching the convention win_x/win_y already use.
CVAR(Int, win_w, -1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, win_h, -1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

extern HWND Window;

bool ForceWindowed;

IVideo *Video;

// do not include GL headers here, only declare the necessary functions.
IVideo *gl_CreateVideo();
FRenderer *gl_CreateInterface();

void I_RestartRenderer();
int currentrenderer = -1;
bool changerenderer;

// [rc4l] GL-only build: OpenGL is the only renderer. Snap any non-1 value (e.g. a
// software-renderer 0 from a pre-GL-only config) back to 1 instead of switching renderers.
CUSTOM_CVAR (Int, vid_renderer, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self != 1)
	{
		self = 1;
	}
}

CCMD (vid_restart)
{
}

// [rc4l] An empty #ifndef NO_GL/#else/#endif shell with a bare statement in it was left here
// by the GL-only change; under NO_GL it put "Video = new Win32Video(0);" at file scope, which
// does not compile. The statement belongs in I_InitGraphics, where it already is.

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
}

void I_InitGraphics ()
{
	UCVarValue val;

	// If the focus window is destroyed, it doesn't go back to the active window.
	// (e.g. because the net pane was up, and a button on it had focus)
	if (GetFocus() == NULL && GetActiveWindow() == Window)
	{
		// Make sure it's in the foreground and focused. (It probably is
		// already foregrounded but may not be focused.)
		SetForegroundWindow(Window);
		SetFocus(Window);
		// Note that when I start a 2-player game on the same machine, the
		// window for the game that isn't focused, active, or foregrounded
		// still receives a WM_ACTIVATEAPP message telling it that it's the
		// active window. The window that is really the active window does
		// not receive a WM_ACTIVATEAPP message, so both games think they
		// are the active app. Huh?
	}
	val.Bool = !!Args->CheckParm ("-devparm");
	ticker.SetGenericRepDefault (val, CVAR_Bool);

	//currentrenderer = vid_renderer;
#ifndef NO_GL
	if (currentrenderer==1) Video = gl_CreateVideo();
	else Video = new Win32Video (0);
#else
	Video = new Win32Video (0);
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
		if (ForceWindowed)
		{
			fs = false;
		}
		else
		{
			fs = fullscreen;
		}
		break;
	}

	// [rc4l] video-scale: split the window's CLIENT size from the RENDER (virtual) size, mirroring
	// the SDL backend. Client = the primary monitor for fullscreen (borderless-desktop), or the
	// requested size for a window. Render/virtual = what the engine draws, from the scale unit
	// (Native/1.0 => virtual == client => native fill, exactly as before). We pass the virtual size
	// on as width/height and stash the client size for the window creation in win32gliface.cpp.
	// >>> SUPERSEDED-BY-UPSTREAM <<< See features/video-scale/README.md.
	int clientW = width, clientH = height;
	if (fs)
	{
		int mw = GetSystemMetrics (SM_CXSCREEN);
		int mh = GetSystemMetrics (SM_CYSCREEN);
		if (mw > 0 && mh > 0)
		{
			clientW = mw;
			clientH = mh;
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
	zx_pendingClientWidth  = clientW;
	zx_pendingClientHeight = clientH;

	DFrameBuffer *res = Video->CreateFrameBuffer (width, height, fs, old);

	//* Right now, CreateFrameBuffer cannot return NULL
	if (res == NULL)
	{
		I_FatalError ("Mode %dx%d is unavailable\n", width, height);
	}
	//*/
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

static void GetCenteredPos (int &winx, int &winy, int &winw, int &winh, int &scrwidth, int &scrheight)
{
	DEVMODE displaysettings;
	RECT rect;
	int cx, cy;

	memset (&displaysettings, 0, sizeof(displaysettings));
	displaysettings.dmSize = sizeof(displaysettings);
	EnumDisplaySettings (NULL, ENUM_CURRENT_SETTINGS, &displaysettings);
	scrwidth = (int)displaysettings.dmPelsWidth;
	scrheight = (int)displaysettings.dmPelsHeight;
	GetWindowRect (Window, &rect);
	cx = scrwidth / 2;
	cy = scrheight / 2;
	winx = cx - (winw = rect.right - rect.left) / 2;
	winy = cy - (winh = rect.bottom - rect.top) / 2;
}

static void KeepWindowOnScreen (int &winx, int &winy, int winw, int winh, int scrwidth, int scrheight)
{
	// If the window is too large to fit entirely on the screen, at least
	// keep its upperleft corner visible.
	if (winx + winw > scrwidth)
	{
		winx = scrwidth - winw;
	}
	if (winx < 0)
	{
		winx = 0;
	}
	if (winy + winh > scrheight)
	{
		winy = scrheight - winh;
	}
	if (winy < 0)
	{
		winy = 0;
	}
}

void I_SaveWindowedPos ()
{
	// Don't save if we were run with the -0 option.
	if (Args->CheckParm ("-0"))
	{
		return;
	}
	// Make sure we only save the window position if it's not fullscreen.
	//
	// [rc4l] Test for the absence of WS_POPUP rather than an exact WS_OVERLAPPEDWINDOW match. The GL
	// backend builds its windowed style as (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX), so the equality
	// below could never hold and this function has been saving nothing at all -- which is why the
	// window position was not being remembered either, not just the size.
	//
	// WS_POPUP is what borderless fullscreen sets (see WindowKindForFullscreen), so its absence is
	// the accurate test for "we are windowed" given the two window kinds this engine has.
	if ((GetWindowLong (Window, GWL_STYLE) & WS_POPUP) == 0)
	{
		RECT wrect;

		if (GetWindowRect (Window, &wrect))
		{
			// If (win_x,win_y) specify to center the window, don't change them
			// if the window is still centered.
			if (win_x < 0 || win_y < 0)
			{
				int winx, winy, winw, winh, scrwidth, scrheight;

				GetCenteredPos (winx, winy, winw, winh, scrwidth, scrheight);
				KeepWindowOnScreen (winx, winy, winw, winh, scrwidth, scrheight);
				if (win_x < 0 && winx == wrect.left)
				{
					wrect.left = win_x;
				}
				if (win_y < 0 && winy == wrect.top)
				{
					wrect.top = win_y;
				}
			}
			win_x = wrect.left;
			win_y = wrect.top;

			// [rc4l] Remember the CLIENT size, not the window size: that is what the caller sizes
			// the window by, and the frame it has to add differs with DPI and theme. Saving the
			// outer rect would drift by the frame thickness on every fullscreen round trip.
			RECT crect;
			if (GetClientRect (Window, &crect))
			{
				win_w = crect.right - crect.left;
				win_h = crect.bottom - crect.top;
			}
		}
	}
}

void I_RestoreWindowedPos ()
{
	int winx, winy, winw, winh, scrwidth, scrheight;

	GetCenteredPos (winx, winy, winw, winh, scrwidth, scrheight);

	// Just move to (0,0) if we were run with the -0 option.
	if (Args->CheckParm ("-0"))
	{
		winx = winy = 0;
	}
	else
	{
		if (win_x >= 0)
		{
			winx = win_x;
		}
		if (win_y >= 0)
		{
			winy = win_y;
		}
		KeepWindowOnScreen (winx, winy, winw, winh, scrwidth, scrheight);
	}
	MoveWindow (Window, winx, winy, winw, winh, TRUE);
}

extern int NewWidth, NewHeight, NewBits, DisplayBits;

CUSTOM_CVAR (Bool, fullscreen, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG|CVAR_NOINITCALL)
{
	// [BB] The server doesn't have a screen.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		return;

	// [rc4l] PROVENANCE: NO UPSTREAM COMMIT -- ours.
	//   SUPERSEDED BY: uzdoom@f8e23500c73b9ba23a48f3cf0829593d22289f12, the same commit named on
	//   win_w/win_h above -- base_sysfb.cpp owns the fullscreen transition there and saves/restores
	//   the windowed rect itself, so this handler stops needing to know about it.
	//   ON PORT: delete this branch along with win_w/win_h.
	//
	// This is where the windowed size was actually lost. Taking NewWidth/NewHeight from the CURRENT
	// screen means that on the way BACK from fullscreen we ask for a mode the size of the fullscreen
	// display -- so the window returns at desktop dimensions no matter what it was before. Restoring
	// the window later cannot help: the mode set that follows re-sizes it.
	//
	// Saving here rather than relying on the backend is deliberate. This runs at the moment the CVAR
	// flips, while the window is still windowed and its size still means something.
	if ( self )
	{
		I_SaveWindowedPos();

		NewWidth = screen->GetWidth();
		NewHeight = screen->GetHeight();
	}
	else if (( win_w > 0 ) && ( win_h > 0 ))
	{
		NewWidth = win_w;
		NewHeight = win_h;
	}
	else
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
		//setmodeneeded = true;	// This CVAR doesn't do anything and only causes problems!
	}
}

CCMD (vid_listmodes)
{
	static const char *ratios[5] = { "", " - 16:9", " - 16:10", " - 17:10", " - 5:4" };
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

