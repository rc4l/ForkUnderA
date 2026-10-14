#ifndef __SDLVIDEO_H__
#define __SDLVIDEO_H__

// [rc4l] The SDL platform's video without GL: the mode list, and a frame buffer that is the engine's window alone, which GL's (sdlglvideo.h) and the backend's both build on.

#include "hardware.h"
#include "v_video.h"
#include <SDL.h>

class SDLVideo : public IVideo
{
 public:
	SDLVideo (int parm);
	~SDLVideo ();

	EDisplayType GetDisplayType () { return DISPLAY_Both; }
	void SetWindowedScale (float scale);

	DFrameBuffer *CreateFrameBuffer (int width, int height, bool fs, DFrameBuffer *old);

	void StartModeIterator (int bits, bool fs);
	bool NextMode (int *width, int *height, bool *letterbox);
	bool SetResolution (int width, int height, int bits);

private:
	int IteratorMode;
	int IteratorBits;
	bool IteratorFS;
};

class SDLFB : public DFrameBuffer
{
	DECLARE_CLASS(SDLFB, DFrameBuffer)
public:
	// this must have the same parameters as the Windows version, even if they are not used!
	// [rc4l] withGL makes the window one a GL context can be made in, which SDLGLFB then does.
	SDLFB (void *hMonitor, int width, int height, int, int, bool fullscreen, bool withGL = false);
	~SDLFB ();

	bool Lock(bool buffered);
	bool Lock ();
	void Unlock();
	bool IsLocked ();

	bool IsValid ();
	bool IsFullscreen ();

	// [rc4l] windowed-video: resize the OS window (windowed only); the render target follows live.
	void SetWindowSize (int w, int h);

	virtual void SetVSync( bool vsync );

	void NewRefreshRate ();
	// [rc4l] The window's size in pixels; false while it has none.
	bool ClientSize (int &w, int &h);

	friend class SDLVideo;

//[C]
	int GetTrueHeight() { return GetHeight();}

protected:
	bool CanUpdate();
	void SetGammaTable(WORD *tbl);
	void InitializeState();
	// [rc4l] Close the window, for a frame buffer whose GL context could not be made in it.
	void DropWindow();

	SDLFB () {}
	BYTE GammaTable[3][256];
	bool UpdatePending;

	SDL_Window *Screen;

	void UpdateColors ();

	int m_Lock;
	Uint16 m_origGamma[3][256];
	bool m_supportsGamma;
};

// [rc4l] GL's frame buffer at that size, or NULL in a build without GL (sdlglvideo.cpp, glstubs.cpp).
DFrameBuffer *I_CreateGLFrameBuffer (int width, int height, bool fullscreen);

// [rc4l] The window the frame buffer drew into, which the backend takes over in a launch without GL.
SDL_Window *I_EngineWindow ();

#endif
