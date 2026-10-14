#ifndef NO_GL

// [rc4l] The SDL platform's GL half: a GL context in the engine's window (sdlvideo.cpp) and GL's frame buffer over it.

// HEADER FILES ------------------------------------------------------------

#include <iostream>

#include "doomtype.h"

#include "templates.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"
#include "c_console.h"
#include "c_dispatch.h"
#include "c_cvars.h"

#include "sdlglvideo.h"
#include "gl/system/gl_system.h"
#include "r_defs.h"
#include "gl/gl_functions.h"

#include "gl/renderer/gl_renderer.h"
#include "gl/system/gl_framebuffer.h"
#include "gl/shaders/gl_shader.h"
#include "gl/utility/gl_templates.h"
#include "gl/system/gl_cvars.h"
#include "features/hwrender/computation/glcontext_compute.h"

// TYPES -------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(SDLGLFB)

// PUBLIC DATA DEFINITIONS -------------------------------------------------

CUSTOM_CVAR(Int, gl_vid_multisample, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL )
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

// [rc4l] Records the profile actually obtained, which may differ from what was asked for.
static bool s_coreProfile = false;

bool SDLGLFB::IsCoreProfile()
{
	return s_coreProfile;
}

// CODE --------------------------------------------------------------------

// [rc4l] The pixel format a GL window is made with, which SDL takes before the window exists.
static void SetupPixelFormat(int multisample)
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
}

DFrameBuffer *I_CreateGLFrameBuffer (int width, int height, bool fullscreen)
{
	static int localmultisample = -1;
	if (localmultisample < 0) localmultisample = gl_vid_multisample;
	SetupPixelFormat(localmultisample);
	return new OpenGLFrameBuffer (0, width, height, 32, 60, fullscreen);
}

SDLGLFB::SDLGLFB (void *hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen)
	: SDLFB (hMonitor, width, height, bits, refreshHz, fullscreen, true)
{
	GLContext = NULL;
	if (Screen == NULL)
		return;

	// [rc4l] The staircase summit (upstream fc0cf4f99): the renderer runs on a core profile,
	// so request the core chain (4.1 -> 4.0 -> 3.3). This is the flip that gives macOS its
	// shaders back -- Apple never offered compatibility contexts above 2.1.
	zx::GLContextRequest reqs[zx::kMaxGLContextRequests];
	const int reqCount = zx::ComputeGLContextRequests(true, reqs, zx::kMaxGLContextRequests);
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
		DropWindow ();
		return;
	}

	SDL_GL_MakeCurrent (Screen, GLContext);
}

SDLGLFB::~SDLGLFB ()
{
	if (GLContext != NULL)
	{
		SDL_GL_DeleteContext (GLContext);
		GLContext = NULL;
	}
}

void SDLGLFB::SetVSync( bool vsync )
{
	// [rc4l] SDL2 has a portable swap interval, so the CGL-specific path is gone.
	if (GLContext != NULL) SDL_GL_SetSwapInterval (vsync ? 1 : 0);
}

void SDLGLFB::SwapBuffers()
{
	if (GLContext != NULL) SDL_GL_SwapWindow (Screen);
}

#endif
