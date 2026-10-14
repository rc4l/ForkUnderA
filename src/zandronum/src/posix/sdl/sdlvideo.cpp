// [rc4l] The SDL platform's video without GL: the mode list, and the engine's window as a frame buffer that GL's (sdlglvideo.cpp) and the backend's both build on.

// HEADER FILES ------------------------------------------------------------

#include <iostream>
#include <vector>

#include "doomtype.h"

#include "templates.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"
#include "v_pfx.h"
#include "stats.h"
#include "version.h"
#include "features/fua-branding/computation/fua_version_compute.h"
#include "c_console.h"
#include "c_dispatch.h"
#include "c_cvars.h"

#include "sdlvideo.h"
#include "features/hwrender/backendonly.h"   // [rc4l] a launch without GL
#include "features/materials/material.h"   // [rc4l] FMaterial::FlushAll
#include "features/app-icon/appicon.h"   // [rc4l] the window icon

// TYPES -------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(SDLFB)

struct MiniModeInfo
{
	WORD Width, Height;
};

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern IVideo *Video;

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// [rc4l] video-scale: the CLIENT (window drawable) size the next framebuffer should give its SDL
// window. Defined in v_video.cpp (shared with the Win32 backend). See features/video-scale.
extern int zx_pendingClientWidth, zx_pendingClientHeight;

// PRIVATE DATA DEFINITIONS ------------------------------------------------

// Dummy screen sizes to pass when windowed
static MiniModeInfo WinModes[] =
{
	{ 320, 200 },
	{ 320, 240 },
	{ 400, 225 },	// 16:9
	{ 400, 300 },
	{ 480, 270 },	// 16:9
	{ 480, 360 },
	{ 512, 288 },	// 16:9
	{ 512, 384 },
	{ 640, 360 },	// 16:9
	{ 640, 400 },
	{ 640, 480 },
	{ 720, 480 },	// 16:10
	{ 720, 540 },
	{ 800, 450 },	// 16:9
	{ 800, 480 },
	{ 800, 500 },	// 16:10
	{ 800, 600 },
	{ 848, 480 },	// 16:9
	{ 960, 600 },	// 16:10
	{ 960, 720 },
	{ 1024, 576 },	// 16:9
	{ 1024, 600 },	// 17:10
	{ 1024, 640 },	// 16:10
	{ 1024, 768 },
	{ 1088, 612 },	// 16:9
	{ 1152, 648 },	// 16:9
	{ 1152, 720 },	// 16:10
	{ 1152, 864 },
	{ 1280, 720 },	// 16:9
	{ 1280, 854 },
	{ 1280, 800 },	// 16:10
	{ 1280, 960 },
	{ 1280, 1024 },	// 5:4
	{ 1360, 768 },	// 16:9
	{ 1366, 768 },
	{ 1400, 787 },	// 16:9
	{ 1400, 875 },	// 16:10
	{ 1400, 1050 },
	{ 1440, 900 },
	{ 1440, 960 },
	{ 1440, 1080 },
	{ 1600, 900 },	// 16:9
	{ 1600, 1000 },	// 16:10
	{ 1600, 1200 },
	{ 1920, 1080 },
	{ 1920, 1200 },
	{ 2048, 1536 },
	{ 2560, 1440 },
	{ 2560, 1600 },
	{ 2560, 2048 },
	{ 2880, 1800 },
	{ 3200, 1800 },
	{ 3840, 2160 },
	{ 3840, 2400 },
	{ 4096, 2160 },
	{ 5120, 2880 }
};

// CODE --------------------------------------------------------------------

SDLVideo::SDLVideo (int parm)
{
	IteratorBits = 0;
	IteratorFS = false;
    if( SDL_Init( SDL_INIT_VIDEO ) < 0 ) {
        fprintf( stderr, "Video initialization failed: %s\n",
             SDL_GetError( ) );
    }
#ifndef	_WIN32
	// mouse cursor is visible by default on linux systems, we disable it by default
	SDL_ShowCursor (0);
#endif
}

SDLVideo::~SDLVideo ()
{
	FMaterial::FlushAll();
}

void SDLVideo::StartModeIterator (int bits, bool fs)
{
	IteratorMode = 0;
	IteratorBits = bits;
	IteratorFS = fs;
}

bool SDLVideo::NextMode (int *width, int *height, bool *letterbox)
{
	if (IteratorBits != 8)
		return false;

	if (!IteratorFS)
	{
		if ((unsigned)IteratorMode < sizeof(WinModes)/sizeof(WinModes[0]))
		{
			*width = WinModes[IteratorMode].Width;
			*height = WinModes[IteratorMode].Height;
			++IteratorMode;
			return true;
		}
	}
	else
	{
		// [rc4l] SDL2 enumerates modes per display rather than handing back a NULL-terminated
		// list, and it reports one entry per (w, h, refresh rate, format) tuple -- SDL 1.2's
		// SDL_ListModes deduplicated by size for free, so upstream never needed this skip.
		// SDL2's list is sorted largest-first, so consecutive entries with the same w/h are
		// the same resolution at different refresh rates; skip them (the same guard SDL2-era
		// GZDoom uses).
		SDL_DisplayMode mode;
		int prevw = -1, prevh = -1;
		if (IteratorMode > 0 && SDL_GetDisplayMode (0, IteratorMode - 1, &mode) == 0)
		{
			prevw = mode.w;
			prevh = mode.h;
		}
		while (IteratorMode < SDL_GetNumDisplayModes (0) &&
			SDL_GetDisplayMode (0, IteratorMode, &mode) == 0)
		{
			++IteratorMode;
			if (mode.w == prevw && mode.h == prevh)
			{
				continue;
			}
			*width = mode.w;
			*height = mode.h;
			return true;
		}
	}
	return false;
}

DFrameBuffer *SDLVideo::CreateFrameBuffer (int width, int height, bool fullscreen, DFrameBuffer *old)
{
	static int retry = 0;
	static int owidth, oheight;

	PalEntry flashColor;
//	int flashAmount;

	if (old != NULL)
	{ // Reuse the old framebuffer if its attributes are the same
		SDLFB *fb = static_cast<SDLFB *> (old);
		if (fb->Width == width &&
			fb->Height == height)
		{
			bool fsnow = (SDL_GetWindowFlags (fb->Screen) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;

			if (fsnow != fullscreen)
			{
				SDL_SetWindowFullscreen (fb->Screen, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
			}
			return old;
		}
//		old->GetFlash (flashColor, flashAmount);
		delete old;
	}
	else
	{
		flashColor = 0;
//		flashAmount = 0;
	}

	// [rc4l] A launch without GL draws the screen with the Vulkan backend's frame buffer (features/hwrender/backendvideo.cpp).
	SDLFB *fb = static_cast<SDLFB *> (zx::hwrender::BackendOnly ()
		? zx::hwrender::CreateBackendFrameBuffer (0, width, height, 32, 60, fullscreen)
		: I_CreateGLFrameBuffer (width, height, fullscreen));
	retry = 0;

	// If we could not create the framebuffer, try again with slightly
	// different parameters in this order:
	// 1. Try with the closest size
	// 2. Try in the opposite screen mode with the original size
	// 3. Try in the opposite screen mode with the closest size
	// This is a somewhat confusing mass of recursion here.

	while (fb == NULL || !fb->IsValid ())
	{
		if (fb != NULL)
		{
			delete fb;
		}

		switch (retry)
		{
		case 0:
			owidth = width;
			oheight = height;
		case 2:
			// Try a different resolution. Hopefully that will work.
			I_ClosestResolution (&width, &height, 8);
			break;

		case 1:
			// Try changing fullscreen mode. Maybe that will work.
			width = owidth;
			height = oheight;
			fullscreen = !fullscreen;
			break;

		default:
			// I give up!
			I_FatalError ("Could not create new screen (%d x %d)", owidth, oheight);

			fprintf( stderr, "!!! [SDLVideo::CreateFrameBuffer] Got beyond I_FatalError !!!" );
			return NULL;	//[C] actually this shouldn't be reached; probably should be replaced with an ASSERT
		}

		++retry;
		fb = static_cast<SDLFB *>(CreateFrameBuffer (width, height, fullscreen, NULL));
	}

//	fb->SetFlash (flashColor, flashAmount);
	return fb;
}

void SDLVideo::SetWindowedScale (float scale)
{
}

bool SDLVideo::SetResolution (int width, int height, int bits)
{
	// FIXME: Is it possible to do this without completely destroying the old
	// interface?
	FMaterial::FlushAll();
	I_ShutdownGraphics();

	Video = new SDLVideo(0);
	if (Video == NULL) I_FatalError ("Failed to initialize display");

#if (defined(WINDOWS)) || defined(WIN32)
	bits=32;
#else
	bits=24;
#endif

	V_DoModeSetup(width, height, bits);
	return true;	// We must return true because the old video context no longer exists.
}


// FrameBuffer implementation -----------------------------------------------

// [rc4l] The window the frame buffer drew into, which the Vulkan backend takes over in a launch without GL (features/hwrender/diligent/dgsdl.cpp).
static SDL_Window *s_engineWindow = NULL;
SDL_Window *I_EngineWindow() { return s_engineWindow; }

// [rc4l] app-icon: the core pk3's picture becomes the window's icon (Linux; Windows and macOS compile theirs in).
static void SetAppIcon(SDL_Window *window)
{
	const int size = 128;
	std::vector<unsigned char> pixels;
	if (!zx::appicon::LoadAppIcon(size, pixels)) return;
	SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(pixels.data(), size, size, 32, size * 4,
		MAKEARGB(0,255,0,0), MAKEARGB(0,0,255,0), MAKEARGB(0,0,0,255), MAKEARGB(255,0,0,0));
	if (surf == NULL) return;
	SDL_SetWindowIcon(window, surf);
	SDL_FreeSurface(surf);
}

SDLFB::SDLFB (void *, int width, int height, int, int, bool fullscreen, bool withGL)
	: DFrameBuffer (width, height)
{
	Screen = NULL;
	m_Lock=0;
	m_supportsGamma = false;
	UpdatePending = false;

	// [rc4l] Window/taskbar title names OUR build. It used to read "ZANDRONUM 3.2.1 (260731-0646)"
	// -- upstream's name, upstream's version and a build timestamp. On an experimental build the
	// commit is included too: those are the builds handed round for testing, so the title alone has
	// to be enough to say exactly which one someone is looking at. A stable build needs no hash --
	// the tag already identifies it uniquely. GAMESIG is left alone; it identifies the engine
	// inside savegames.
	char caption[100];
	{
		const char *tag = GetFuaVersionTag();
		if (zx::FuaIsStableBuild(GetFuaDescribe()))
			mysnprintf(caption, countof(caption), FUA_NAME " %s (stable)", tag);
		else
			mysnprintf(caption, countof(caption), FUA_NAME " %s (experimental %s)", tag, GetGitHash());
	}

	// [rc4l] video-scale: the OS window is the CLIENT size; the render size (width/height above) may
	// be smaller when internal-resolution scaling is on. For fullscreen FULLSCREEN_DESKTOP ignores
	// the size and uses the desktop anyway. See features/video-scale.
	int winW = (zx_pendingClientWidth  > 0) ? zx_pendingClientWidth  : width;
	int winH = (zx_pendingClientHeight > 0) ? zx_pendingClientHeight : height;

	// [rc4l] windowed-video: a normal window is freely resizable (drag the edges), matching upstream.
	// The render target follows the drawable live via OpenGLFrameBuffer::MaybeResizeForScale, and the
	// new size is persisted on the resize event (see i_input.cpp / features/windowed-video).
	Uint32 windowFlags = (withGL ? SDL_WINDOW_OPENGL : 0) | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE);

	Screen = SDL_CreateWindow (caption,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		winW, winH,
		windowFlags);

	if (Screen == NULL)
		return;
	s_engineWindow = Screen;
	SetAppIcon(Screen);

	m_supportsGamma = 0 == SDL_GetWindowGammaRamp(Screen, m_origGamma[0], m_origGamma[1], m_origGamma[2]);
}

SDLFB::~SDLFB ()
{
	if (m_supportsGamma && Screen != NULL)
	{
		SDL_SetWindowGammaRamp(Screen, m_origGamma[0], m_origGamma[1], m_origGamma[2]);
	}
	DropWindow();
}

void SDLFB::DropWindow ()
{
	if (Screen == NULL) return;
	if (s_engineWindow == Screen) s_engineWindow = NULL;
	zx::hwrender::ForgetBackendWindow (Screen);
	SDL_DestroyWindow (Screen);
	Screen = NULL;
}

void SDLFB::InitializeState()
{
}

bool SDLFB::CanUpdate ()
{
	if (m_Lock != 1)
	{
		if (m_Lock > 0)
		{
			UpdatePending = true;
			--m_Lock;
		}
		return false;
	}
	return true;
}

void SDLFB::SetGammaTable(WORD *tbl)
{
	if (Screen != NULL) SDL_SetWindowGammaRamp(Screen, &tbl[0], &tbl[256], &tbl[512]);
}

bool SDLFB::Lock(bool buffered)
{
	m_Lock++;
	Buffer = MemBuffer;
	return true;
}

bool SDLFB::Lock ()
{
	return Lock(false);
}

void SDLFB::Unlock ()
{
	if (UpdatePending && m_Lock == 1)
	{
		Update ();
	}
	else if (--m_Lock <= 0)
	{
		m_Lock = 0;
	}
}

bool SDLFB::IsLocked ()
{
	return m_Lock>0;// true;
}

bool SDLFB::IsFullscreen ()
{
	return (SDL_GetWindowFlags (Screen) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

void SDLFB::SetWindowSize (int w, int h)
{
	// [rc4l] windowed-video: only meaningful when windowed; fullscreen covers the desktop. The
	// SDL resize event that follows persists the new size and MaybeResizeForScale resizes the
	// render target -- so this is all it needs to do.
	if (Screen != NULL && !IsFullscreen ())
	{
		SDL_SetWindowSize (Screen, w, h);
		SDL_SetWindowPosition (Screen, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	}
}

// [rc4l] windowed-video: SetWindowSize overrides DFrameBuffer::SetWindowSize; the cross-platform
// vid_setsize CCMD (features/windowed-video/windowedvideo.cpp) calls it through that virtual.


bool SDLFB::IsValid ()
{
	return DFrameBuffer::IsValid() && Screen != NULL;
}

// [rc4l] A window alone has no swap interval; GL's frame buffer sets its context's, and the backend's swapchain keeps its own.
void SDLFB::SetVSync( bool vsync )
{
}

void SDLFB::NewRefreshRate ()
{
}

bool SDLFB::ClientSize (int &w, int &h)
{
	if (Screen == NULL) return false;
	SDL_GetWindowSize (Screen, &w, &h);
	return w > 0 && h > 0;
}
