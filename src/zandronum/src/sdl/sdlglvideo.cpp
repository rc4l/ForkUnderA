#ifndef NO_GL

// HEADER FILES ------------------------------------------------------------

#include <iostream>

#include "doomtype.h"

#include "templates.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"
#include "v_pfx.h"
#include "stats.h"
#include "version.h"
#include "c_console.h"

#include "sdlglvideo.h"
#include "gl/system/gl_system.h"
#include "r_defs.h"
#include "gl/gl_functions.h"
//#include "gl/gl_intern.h"

#include "gl/renderer/gl_renderer.h"
#include "gl/system/gl_framebuffer.h"
#include "gl/shaders/gl_shader.h"
#include "gl/utility/gl_templates.h"
#include "gl/textures/gl_material.h"
#include "gl/system/gl_cvars.h"
#include "features/hwrender/computation/glcontext_compute.h"

// MACROS ------------------------------------------------------------------

// TYPES -------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(SDLGLFB)

struct MiniModeInfo
{
	WORD Width, Height;
};

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern IVideo *Video;
// extern int vid_renderer;

EXTERN_CVAR (Float, Gamma)
EXTERN_CVAR (Int, vid_displaybits)
EXTERN_CVAR (Int, vid_renderer)


// PUBLIC DATA DEFINITIONS -------------------------------------------------

CUSTOM_CVAR(Int, gl_vid_multisample, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL )
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

// [rc4l] Records the profile actually obtained, which may differ from what was asked for.
static bool s_coreProfile = false;

bool SDLGLVideo::IsCoreProfile()
{
	return s_coreProfile;
}

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
	{ 1280, 800 },	// 16:10
	{ 1280, 960 },
	{ 1344, 756 },  // 16:9
	{ 1360, 768 },	// 16:9
	{ 1400, 787 },	// 16:9
	{ 1400, 875 },	// 16:10
	{ 1440, 900 },
	{ 1400, 1050 },
	{ 1600, 900 },	// 16:9
	{ 1600, 1000 },	// 16:10
	{ 1600, 1200 },
	{ 1680, 1050 }, // 16:10
	{ 1920, 1080 }, // 16:9
	{ 1920, 1200 }, // 16:10
	{ 2054, 1536 },
	{ 2560, 1440 },  // 16:9
	{ 2880, 1800 }  // 16:10
};

// CODE --------------------------------------------------------------------

SDLGLVideo::SDLGLVideo (int parm)
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

SDLGLVideo::~SDLGLVideo ()
{
	if (GLRenderer != NULL) GLRenderer->FlushTextures();
}

void SDLGLVideo::StartModeIterator (int bits, bool fs)
{
	IteratorMode = 0;
	IteratorBits = bits;
	IteratorFS = fs;
}

bool SDLGLVideo::NextMode (int *width, int *height, bool *letterbox)
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

DFrameBuffer *SDLGLVideo::CreateFrameBuffer (int width, int height, bool fullscreen, DFrameBuffer *old)
{
	static int retry = 0;
	static int owidth, oheight;
	
	PalEntry flashColor;
//	int flashAmount;

	if (old != NULL)
	{ // Reuse the old framebuffer if its attributes are the same
		SDLGLFB *fb = static_cast<SDLGLFB *> (old);
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
	
	SDLGLFB *fb = new OpenGLFrameBuffer (0, width, height, 32, 60, fullscreen);
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

			fprintf( stderr, "!!! [SDLGLVideo::CreateFrameBuffer] Got beyond I_FatalError !!!" );
			return NULL;	//[C] actually this shouldn't be reached; probably should be replaced with an ASSERT
		}

		++retry;
		fb = static_cast<SDLGLFB *>(CreateFrameBuffer (width, height, fullscreen, NULL));
	}

//	fb->SetFlash (flashColor, flashAmount);
	return fb;
}

void SDLGLVideo::SetWindowedScale (float scale)
{
}

bool SDLGLVideo::SetResolution (int width, int height, int bits)
{
	// FIXME: Is it possible to do this without completely destroying the old
	// interface?
#ifndef NO_GL

	if (GLRenderer != NULL) GLRenderer->FlushTextures();
	I_ShutdownGraphics();

	Video = new SDLGLVideo(0);
	if (Video == NULL) I_FatalError ("Failed to initialize display");

#if (defined(WINDOWS)) || defined(WIN32)
	bits=32;
#else
	bits=24;
#endif
	
	V_DoModeSetup(width, height, bits);
#endif
	return true;	// We must return true because the old video context no longer exists.
}

//==========================================================================
//
// 
//
//==========================================================================

bool SDLGLVideo::SetupPixelFormat(bool allowsoftware, int multisample)
{
	SDL_GL_SetAttribute( SDL_GL_RED_SIZE,  8 );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE,  8 );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE,  8 );
	SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE,  8 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE,  24 );
	SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE,  8 );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER,  1 );
	if (multisample > 0) {
		SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 1 );
		SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, multisample );
	}
	return true;
}

//==========================================================================
//
// 
//
//==========================================================================

bool SDLGLVideo::InitHardware (bool allowsoftware, int multisample)
{
	if (!SetupPixelFormat(allowsoftware, multisample))
	{
		Printf ("R_OPENGL: Reverting to software mode...\n");
		return false;
	}
	return true;
}


// FrameBuffer implementation -----------------------------------------------

SDLGLFB::SDLGLFB (void *, int width, int height, int, int, bool fullscreen)
	: DFrameBuffer (width, height)
{
	static int localmultisample=-1;

	if (localmultisample<0) localmultisample=gl_vid_multisample;

	int i;
	
	m_Lock=0;

	UpdatePending = false;
	
	if (!static_cast<SDLGLVideo*>(Video)->InitHardware(false, localmultisample))
	{
		vid_renderer = 0;
		return;
	}

	char caption[100];
	mysnprintf(caption, countof(caption), GAMESIG " %s (%s)", GetVersionString(), GetGitTime());

	Screen = SDL_CreateWindow (caption,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		width, height,
		SDL_WINDOW_OPENGL | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));

	if (Screen == NULL)
		return;

	// [rc4l] The staircase summit (upstream fc0cf4f99): the renderer runs on a core profile,
	// so request the core chain (4.1 -> 4.0 -> 3.3). This is the flip that gives macOS its
	// shaders back -- Apple never offered compatibility contexts above 2.1.
	zx::GLContextRequest reqs[zx::kMaxGLContextRequests];
	const int reqCount = zx::ComputeGLContextRequests(true, reqs, zx::kMaxGLContextRequests);
	GLContext = NULL;
	for (int attempt = 0; attempt < reqCount; attempt++)
	{
		const zx::GLContextRequest &req = reqs[attempt];

		SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, req.major);
		SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, req.minor);
		SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, req.coreProfile
			? SDL_GL_CONTEXT_PROFILE_CORE : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

		GLContext = SDL_GL_CreateContext (Screen);
		if (GLContext != NULL)
		{
			s_coreProfile = req.coreProfile;
			Printf ("GL context: %d.%d %s\n", req.major, req.minor,
				req.coreProfile ? "core" : "compatibility");
			break;
		}
	}

	if (GLContext == NULL)
	{
		Printf ("Failed to create a GL context: %s\n", SDL_GetError ());
		SDL_DestroyWindow (Screen);
		Screen = NULL;
		return;
	}

	SDL_GL_MakeCurrent (Screen, GLContext);

	m_supportsGamma = 0 == SDL_GetWindowGammaRamp(Screen, m_origGamma[0], m_origGamma[1], m_origGamma[2]);
}

SDLGLFB::~SDLGLFB ()
{
	if (m_supportsGamma && Screen != NULL)
	{
		SDL_SetWindowGammaRamp(Screen, m_origGamma[0], m_origGamma[1], m_origGamma[2]);
	}
	if (GLContext != NULL)
	{
		SDL_GL_DeleteContext (GLContext);
		GLContext = NULL;
	}
	if (Screen != NULL)
	{
		SDL_DestroyWindow (Screen);
		Screen = NULL;
	}
}




void SDLGLFB::InitializeState() 
{
}

bool SDLGLFB::CanUpdate ()
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

void SDLGLFB::SetGammaTable(WORD *tbl)
{
	if (Screen != NULL) SDL_SetWindowGammaRamp(Screen, &tbl[0], &tbl[256], &tbl[512]);
}

bool SDLGLFB::Lock(bool buffered)
{
	m_Lock++;
	Buffer = MemBuffer;
	return true;
}

bool SDLGLFB::Lock () 
{ 	
	return Lock(false); 
}

void SDLGLFB::Unlock () 	
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

bool SDLGLFB::IsLocked () 
{ 
	return m_Lock>0;// true;
}

bool SDLGLFB::IsFullscreen ()
{
	return (SDL_GetWindowFlags (Screen) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}


bool SDLGLFB::IsValid ()
{
	return DFrameBuffer::IsValid() && Screen != NULL;
}

void SDLGLFB::SetVSync( bool vsync )
{
	// [rc4l] SDL2 has a portable swap interval, so the CGL-specific path is gone.
	SDL_GL_SetSwapInterval (vsync ? 1 : 0);
}

void SDLGLFB::NewRefreshRate ()
{
}

void SDLGLFB::SwapBuffers()
{
	SDL_GL_SwapWindow (Screen);
}


#endif
