#ifndef __GL_FRAMEBUFFER
#define __GL_FRAMEBUFFER

#ifdef _WIN32
#include "win32iface.h"
#include "win32gliface.h"
#endif

class FHardwareTexture;

extern long gl_frameMS;
extern long gl_frameCount;
#ifdef _WIN32
class OpenGLFrameBuffer : public Win32GLFrameBuffer
{
	typedef Win32GLFrameBuffer Super;
	DECLARE_CLASS(OpenGLFrameBuffer, Win32GLFrameBuffer)
#else
#include "sdlglvideo.h"
class OpenGLFrameBuffer : public SDLGLFB
{
//	typedef SDLGLFB Super;	//[C]commented, DECLARE_CLASS defines this in linux
	DECLARE_CLASS(OpenGLFrameBuffer, SDLGLFB)
#endif


public:

	explicit OpenGLFrameBuffer() {}
	OpenGLFrameBuffer(void *hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen) ;
	~OpenGLFrameBuffer();

	void InitializeState();
	void Update();

	// Color correction
	bool SetGamma (float gamma);
	bool SetBrightness(float bright);
	bool SetContrast(float contrast);
	void DoSetGamma();

	void UpdatePalette();
	void GetFlashedPalette (PalEntry pal[256]);
	PalEntry *GetPalette ();
	bool SetFlash(PalEntry rgb, int amount);
	void GetFlash(PalEntry &rgb, int &amount);
	int GetPageCount();
	bool Begin2D(bool copy3d);
	void GetHitlist(BYTE *hitlist);
	void GameRestart();

	// Retrieves a buffer containing image data for a screenshot.
	// Hint: Pitch can be negative for upside-down images, in which case buffer
	// points to the last row in the buffer, which will be the first row output.
	virtual void GetScreenshotBuffer(const BYTE *&buffer, int &pitch, ESSType &color_type);

	// Releases the screenshot buffer.
	virtual void ReleaseScreenshotBuffer();

	// 2D drawing
	void STACK_ARGS DrawTextureV(FTexture *img, double x, double y, uint32 tag, va_list tags);
	void DrawLine(int x1, int y1, int x2, int y2, int palcolor, uint32 color);
	void DrawPixel(int x1, int y1, int palcolor, uint32 color);
	void Clear(int left, int top, int right, int bottom, int palcolor, uint32 color);
	void Dim(PalEntry color=0);
	void Dim (PalEntry color, float damount, int x1, int y1, int w, int h);
	void FlatFill (int left, int top, int right, int bottom, FTexture *src, bool local_origin=false);

	void FillSimplePoly(FTexture *tex, FVector2 *points, int npoints,
		double originx, double originy, double scalex, double scaley,
		angle_t rotation, FDynamicColormap *colormap, int lightlevel);

	FNativePalette *CreatePalette(FRemapTable *remap);

	bool WipeStartScreen(int type);
	void WipeEndScreen();
	bool WipeDo(int ticks);
	void WipeCleanup();
	void Swap();
	bool Is8BitMode() { return false; }
	void DumpScaleState();


private:
	// [rc4l] video-scale: offscreen render target for internal-resolution scaling. When
	// mScaleActive is false (the default -- Native mode at factor 1.0) the engine renders straight
	// to the backbuffer, byte-for-byte the old path. When active, the frame is rendered into this
	// FBO at the virtual size (== GetWidth()/GetHeight()) and blit-upscaled to fill the window in
	// Update(). All sizing decisions come from features/video-scale (unit-tested); this is the dumb
	// GL executor. >>> SUPERSEDED-BY-UPSTREAM <<< See features/video-scale/README.md.
	unsigned int mScaleFB, mScaleColorTex, mScaleDepthRB;
	int mScaleFBW, mScaleFBH;
	int mScaleClientW, mScaleClientH; // cached window client size (avoids per-frame GetClientSize)
	bool mScaleActive;
	void GetClientSize(int &w, int &h);
	void UpdateScaleBuffer();   // decide active, (re)build the FBO, bind it as the render target
	void DestroyScaleBuffer();
	void BlitScaleBuffer();     // blit the FBO to the backbuffer, filling the client rect
	void MaybeResizeForScale(); // per-frame: apply a scale change by resizing the render target live
	void ResizeRenderInPlace(int w, int h); // resize render target, no window/context teardown

#ifdef ZX_ENABLE_REPLAY
	// [rc4l] Instant-replay async readback: double-buffered pixel-pack buffers for glReadPixels. These
	// are MEMBERS (not file-statics) so they live and die with THIS framebuffer's GL context. A
	// windowed<->fullscreen switch destroys the framebuffer -- taking these buffers with it -- so a
	// stale buffer name from a dead context can never be reused in the new one. Reusing such a name is
	// what faulted Apple's GL driver (storeVecColor_RGB_UB on a dispatch worker). See fix notes.
	unsigned int mReplayPbo[2];
	int  mReplayPboIndex;
	int  mReplayPboW, mReplayPboH;
	bool mReplayPboFilled;
	void CaptureReplayFramePBO(int w, int h); // issue this frame's read, hand off the previous frame's
	void DestroyReplayCapture();              // delete the PBOs while the context is still current
#endif

	PalEntry Flash;

	// Texture creation info
	int cm;
	int translation;
	bool iscomplex;
	bool needsetgamma;
	bool swapped;

	PalEntry SourcePalette[256];
	BYTE *ScreenshotBuffer;

	class Wiper
	{
	public:
		virtual ~Wiper();
		virtual bool Run(int ticks, OpenGLFrameBuffer *fb) = 0;
	};

	class Wiper_Melt;			friend class Wiper_Melt;
	class Wiper_Burn;			friend class Wiper_Burn;
	class Wiper_Crossfade;		friend class Wiper_Crossfade;

	Wiper *ScreenWipe;
	FHardwareTexture *wipestartscreen;
	FHardwareTexture *wipeendscreen;

public:
	AActor * LastCamera;
	int palette_brightness;
};


#endif //__GL_FRAMEBUFFER
