#ifndef __SDLGLVIDEO_H__
#define __SDLGLVIDEO_H__

#include "sdlvideo.h"
#include "gl/system/gl_system.h"

EXTERN_CVAR (Float, dimamount)
EXTERN_CVAR (Color, dimcolor)

struct FRenderer;
FRenderer *gl_CreateInterface();

// [rc4l] The engine's window with a GL context in it, which GL's own frame buffer draws through.
class SDLGLFB : public SDLFB
{
	DECLARE_CLASS(SDLGLFB, SDLFB)
public:
	// this must have the same parameters as the Windows version, even if they are not used!
	SDLGLFB (void *hMonitor, int width, int height, int, int, bool fullscreen);
	~SDLGLFB ();

	virtual void SetVSync( bool vsync );
	void SwapBuffers();

	// [rc4l] True once a core-profile context was actually obtained; the legacy renderer must not draw into one.
	static bool IsCoreProfile();

protected:
	SDLGLFB () {}

	// [rc4l] SDL2 splits SDL 1.2's single screen surface into a window plus an explicit GL context.
	SDL_GLContext GLContext;
};
#endif
