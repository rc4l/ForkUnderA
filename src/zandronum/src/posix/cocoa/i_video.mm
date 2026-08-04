/*
 ** i_video.mm
 **
 **---------------------------------------------------------------------------
 ** Copyright 2012-2015 Alexey Lysiuk
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

// [rc4l] uzdoom@e132fc5ee (GLEW -> GLLoadGen loader swap) is a recorded skip in the commit
// tracker; our loader is still GLEW, which gl/system/gl_system.h pulls in.
#include "gl/system/gl_system.h"

#include "i_common.h"

#import <Carbon/Carbon.h>

// Avoid collision between DObject class and Objective-C
#define Class ObjectClass

#include "bitmap.h"
#include "c_dispatch.h"
#include "doomstat.h"
#include "hardware.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_png.h"
#include "r_renderer.h"
// [rc4l] GL-only build: no software renderer here; the dedicated server uses the null one,
// exactly as posix/sdl/hardware.cpp does.
#include "r_nullrenderer.h"
#include "st_console.h"
#include "stats.h"
#include "textures.h"
#include "v_palette.h"
#include "v_pfx.h"
#include "v_text.h"
#include "v_video.h"
#include "version.h"
#include "features/fua-branding/computation/fua_version_compute.h"   // [rc4l] FUA title
#include "features/hwrender/computation/glcontext_compute.h"        // [rc4l] GL profile chain

// [rc4l] video-scale: client size vs render size (v_video.cpp).
extern int zx_pendingClientWidth, zx_pendingClientHeight;

// [rc4l] windowed-video: the persisted windowed size, updated on resize.
EXTERN_CVAR (Int, vid_defwidth)
EXTERN_CVAR (Int, vid_defheight)

#include "gl/system/gl_system.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/system/gl_framebuffer.h"
#include "gl/system/gl_interface.h"
#include "gl/textures/gl_samplers.h"
#include "gl/utility/gl_clock.h"

#undef Class


@implementation NSWindow(ExitAppOnClose)

- (void)exitAppOnClose
{
	NSButton* closeButton = [self standardWindowButton:NSWindowCloseButton];
	[closeButton setAction:@selector(terminate:)];
	[closeButton setTarget:NSApp];
}

@end

@interface NSWindow(EnterFullscreenOnZoom)
- (void)enterFullscreenOnZoom;
@end

@implementation NSWindow(EnterFullscreenOnZoom)

- (void)enterFullscreen:(id)sender
{
	ToggleFullscreen = true;
}

- (void)enterFullscreenOnZoom
{
	NSButton* zoomButton = [self standardWindowButton:NSWindowZoomButton];
	[zoomButton setEnabled:YES];
	[zoomButton setAction:@selector(enterFullscreen:)];
	[zoomButton setTarget:self];
}

@end


EXTERN_CVAR(Bool, ticker   )
EXTERN_CVAR(Bool, vid_vsync)
EXTERN_CVAR(Bool, vid_hidpi)

CUSTOM_CVAR(Bool, fullscreen, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	extern int NewWidth, NewHeight, NewBits, DisplayBits;

	NewWidth      = screen->GetWidth();
	NewHeight     = screen->GetHeight();
	NewBits       = DisplayBits;
	setmodeneeded = true;
}

CUSTOM_CVAR(Bool, vid_autoswitch, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	Printf("You must restart " GAMENAME " to apply graphics switching mode\n");
}

// [rc4l] Zandronum's cl_main.cpp, mcp_renderinfo.cpp and v_video.h all reference a global
// `currentrenderer`, which posix/sdl/hardware.cpp used to define. The Cocoa backend owns it
// now. GL-only build, so it is 1 unless this is a renderer-less dedicated server.
#ifndef NO_GL
int currentrenderer = 1;
#else
int currentrenderer = 0;
#endif

static int s_currentRenderer;

// [rc4l] Which Apple profile the context actually came up with; backs CocoaVideo::IsCoreProfile(),
// the same query SDLGLVideo::IsCoreProfile() answers on the other backend.
static int s_cocoaGLProfile = zx::kNSGLProfileLegacy;

CUSTOM_CVAR(Int, vid_renderer, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	// 0: Software renderer
	// 1: OpenGL renderer

	// [rc4l] GL-only build: there is nothing to switch to, so snap back rather than offering the
	// software renderer. Same body as posix/sdl/hardware.cpp:88-94.
	if (self != 1)
	{
		self = 1;
	}
}

EXTERN_CVAR(Bool, gl_smooth_rendered)


RenderBufferOptions rbOpts;


// ---------------------------------------------------------------------------


namespace
{
	const NSInteger LEVEL_FULLSCREEN = NSMainMenuWindowLevel + 1;
	const NSInteger LEVEL_WINDOWED   = NSNormalWindowLevel;

	const NSUInteger STYLE_MASK_FULLSCREEN = NSBorderlessWindowMask;
	const NSUInteger STYLE_MASK_WINDOWED   = NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask | NSResizableWindowMask;
}


// ---------------------------------------------------------------------------


// [rc4l] windowed-video: defined below, once CocoaVideo is a complete type.
static void ZX_CocoaWindowResized();

@interface CocoaWindow : NSWindow<NSWindowDelegate>
{
}

- (BOOL)canBecomeKeyWindow;

// [rc4l] windowed-video: Cocoa has no SDL_WINDOWEVENT_SIZE_CHANGED, so the window is its own
// delegate and reports resizes itself.
- (void)windowDidResize:(NSNotification*)notification;

@end


@implementation CocoaWindow

- (BOOL)canBecomeKeyWindow
{
	return true;
}

// [rc4l] windowed-video: dragged an edge, or vid_setsize. The work lives on CocoaVideo, which is
// declared below this class, so it is reached through the ZX_CocoaWindowResized hook.
- (void)windowDidResize:(NSNotification*)notification
{
	(void)notification;
	ZX_CocoaWindowResized();
}

@end


// ---------------------------------------------------------------------------


@interface CocoaView : NSOpenGLView
{
	NSCursor* m_cursor;
}

- (void)resetCursorRects;

- (void)setCursor:(NSCursor*)cursor;

@end


@implementation CocoaView

- (void)resetCursorRects
{
	[super resetCursorRects];

	NSCursor* const cursor = nil == m_cursor
		? [NSCursor arrowCursor]
		: m_cursor;

	[self addCursorRect:[self bounds]
				 cursor:cursor];
}

- (void)setCursor:(NSCursor*)cursor
{
	m_cursor = cursor;
}

@end


// ---------------------------------------------------------------------------


class CocoaVideo : public IVideo
{
public:
	CocoaVideo();

	virtual EDisplayType GetDisplayType() { return DISPLAY_Both; }
	virtual void SetWindowedScale(float scale);

	virtual DFrameBuffer* CreateFrameBuffer(int width, int height, bool fs, DFrameBuffer* old);

	virtual void StartModeIterator(int bits, bool fullscreen);
	virtual bool NextMode(int* width, int* height, bool* letterbox);

	static bool IsFullscreen();
	static void UseHiDPI(bool hiDPI);
	static void SetCursor(NSCursor* cursor);
	static void SetWindowVisible(bool visible);

	// [rc4l] windowed-video. GetInstance moves up from private because SDLGLFB::SetWindowSize and
	// the window delegate both need it.
	static CocoaVideo* GetInstance();

	// [rc4l] The view we own. GetClientWidth/Height reached it through
	// [[NSOpenGLContext currentContext] view], which is only valid on the thread that made the
	// context current; from the per-frame resize check it came back nil, so the client size fell
	// back to the OLD render size and window and render could never reconcile.
	static NSView* GetContentView();

	// [rc4l] Resize the OS window and bring the render target with it.
	void ResizeWindow(int width, int height);

	// [rc4l] The drawable changed -- a drag, or ResizeWindow. Reconciles rbOpts, the GL context and
	// the render target with the window's new size.
	void OnWindowResized();

private:
	struct ModeIterator
	{
		size_t index;
		int    bits;
		bool   fullscreen;
	};

	ModeIterator m_modeIterator;

	CocoaWindow* m_window;

	int  m_width;
	int  m_height;
	bool m_fullscreen;
	bool m_hiDPI;

	void SetStyleMask(NSUInteger styleMask);
	void SetFullscreenMode(int width, int height);
	void SetWindowedMode(int width, int height);
	void SetMode(int width, int height, bool fullscreen, bool hiDPI);

};


// ---------------------------------------------------------------------------


// [rc4l] GL-only build: class CocoaFrameBuffer removed -- see the note at its implementation.


// ---------------------------------------------------------------------------


EXTERN_CVAR(Float, Gamma)

CUSTOM_CVAR(Float, rgamma, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (NULL != screen)
	{
		screen->SetGamma(Gamma);
	}
}

CUSTOM_CVAR(Float, ggamma, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (NULL != screen)
	{
		screen->SetGamma(Gamma);
	}
}

CUSTOM_CVAR(Float, bgamma, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (NULL != screen)
	{
		screen->SetGamma(Gamma);
	}
}


// ---------------------------------------------------------------------------


extern id appCtrl;


namespace
{

const struct
{
	uint16_t width;
	uint16_t height;
}
VideoModes[] =
{
	{  320,  200 },
	{  320,  240 },
	{  400,  225 },	// 16:9
	{  400,  300 },
	{  480,  270 },	// 16:9
	{  480,  360 },
	{  512,  288 },	// 16:9
	{  512,  384 },
	{  640,  360 },	// 16:9
	{  640,  400 },
	{  640,  480 },
	{  720,  480 },	// 16:10
	{  720,  540 },
	{  800,  450 },	// 16:9
	{  800,  480 },
	{  800,  500 },	// 16:10
	{  800,  600 },
	{  848,  480 },	// 16:9
	{  960,  600 },	// 16:10
	{  960,  720 },
	{ 1024,  576 },	// 16:9
	{ 1024,  600 },	// 17:10
	{ 1024,  640 },	// 16:10
	{ 1024,  768 },
	{ 1088,  612 },	// 16:9
	{ 1152,  648 },	// 16:9
	{ 1152,  720 },	// 16:10
	{ 1152,  864 },
	{ 1280,  540 }, // 21:9
	{ 1280,  720 },	// 16:9
	{ 1280,  854 },
	{ 1280,  800 },	// 16:10
	{ 1280,  960 },
	{ 1280, 1024 },	// 5:4
	{ 1360,  768 },	// 16:9
	{ 1366,  768 },
	{ 1400,  787 },	// 16:9
	{ 1400,  875 },	// 16:10
	{ 1400, 1050 },
	{ 1440,  900 },
	{ 1440,  960 },
	{ 1440, 1080 },
	{ 1600,  900 },	// 16:9
	{ 1600, 1000 },	// 16:10
	{ 1600, 1200 },
	{ 1680, 1050 },	// 16:10
	{ 1920, 1080 },
	{ 1920, 1200 },
	{ 2048, 1152 }, // 16:9, iMac Retina 4K 21.5", HiDPI off
	{ 2048, 1536 },
	{ 2304, 1440 }, // 16:10, MacBook Retina 12"
	{ 2560, 1080 }, // 21:9
	{ 2560, 1440 },
	{ 2560, 1600 },
	{ 2560, 2048 },
	{ 2880, 1800 }, // 16:10, MacBook Pro Retina 15"
	{ 3200, 1800 },
	{ 3440, 1440 }, // 21:9
	{ 3840, 2160 },
	{ 3840, 2400 },
	{ 4096, 2160 },
	{ 4096, 2304 }, // 16:9, iMac Retina 4K 21.5"
	{ 5120, 2160 }, // 21:9
	{ 5120, 2880 }  // 16:9, iMac Retina 5K 27"
};


// [rc4l] GL-only build: BlitCycles/FlipCycles only ever measured the software framebuffer.


CocoaWindow* CreateCocoaWindow(const NSUInteger styleMask)
{
	static const CGFloat TEMP_WIDTH  = VideoModes[0].width  - 1;
	static const CGFloat TEMP_HEIGHT = VideoModes[0].height - 1;

	CocoaWindow* const window = [CocoaWindow alloc];
	[window initWithContentRect:NSMakeRect(0, 0, TEMP_WIDTH, TEMP_HEIGHT)
					  styleMask:styleMask
						backing:NSBackingStoreBuffered
						  defer:NO];
	[window setOpaque:YES];
	[window makeFirstResponder:appCtrl];
	[window setAcceptsMouseMovedEvents:YES];

	return window;
}

// [rc4l] Upstream tried Core then Legacy. We drive the attempt order from zx::ComputeCocoaGLProfileChain
// so the Cocoa backend negotiates the same 4.1 -> 4.0 -> 3.3 chain the SDL one did, collapsed onto
// Apple's three profile constants. Takes the raw attribute now instead of a two-valued enum.
NSOpenGLPixelFormat* CreatePixelFormat(const NSOpenGLPixelFormatAttribute wantProfile)
{
	NSOpenGLPixelFormatAttribute attributes[16];
	size_t i = 0;

	attributes[i++] = NSOpenGLPFADoubleBuffer;
	attributes[i++] = NSOpenGLPFAColorSize;
	attributes[i++] = NSOpenGLPixelFormatAttribute(32);
	attributes[i++] = NSOpenGLPFADepthSize;
	attributes[i++] = NSOpenGLPixelFormatAttribute(24);
	attributes[i++] = NSOpenGLPFAStencilSize;
	attributes[i++] = NSOpenGLPixelFormatAttribute(8);

	if (NSAppKitVersionNumber >= AppKit10_5 && !vid_autoswitch)
	{
		attributes[i++] = NSOpenGLPFAAllowOfflineRenderers;
	}

	if (NSAppKitVersionNumber >= AppKit10_7 && NSOpenGLPixelFormatAttribute(zx::kNSGLProfileLegacy) != wantProfile)
	{
		NSOpenGLPixelFormatAttribute profile = wantProfile;

		// [rc4l] upstream's -glversion override, kept on the same line so a future diff to it still
		// applies: asking for below 3.2 means asking for the legacy profile.
		const char* const glversion = Args->CheckValue("-glversion");

		if (nullptr != glversion)
		{
			const double version = strtod(glversion, nullptr) + 0.01;
			if (version < 3.2)
			{
				profile = NSOpenGLProfileVersionLegacy;
			}
		}

		attributes[i++] = NSOpenGLPFAOpenGLProfile;
		attributes[i++] = profile;
	}

	attributes[i] = NSOpenGLPixelFormatAttribute(0);

	return [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
}

} // unnamed namespace


// ---------------------------------------------------------------------------


CocoaVideo::CocoaVideo()
: m_window(CreateCocoaWindow(STYLE_MASK_WINDOWED))
, m_width(-1)
, m_height(-1)
, m_fullscreen(false)
, m_hiDPI(false)
{
	memset(&m_modeIterator, 0, sizeof m_modeIterator);

	// [rc4l] windowed-video: attach the delegate ONCE, here. It used to be set lazily inside
	// SetWindowedMode behind an "if the delegate is nil" guard, so it only ever attached during a
	// mode set -- dragging a window edge fired nothing at all.
	[m_window setDelegate:m_window];

	// Create OpenGL pixel format

	int profiles[zx::kMaxCocoaGLProfiles];
	const int profileCount = zx::ComputeCocoaGLProfileChain(true, profiles, zx::kMaxCocoaGLProfiles);

	NSOpenGLPixelFormat* pixelFormat = nil;

	for (int i = 0; i < profileCount; ++i)
	{
		pixelFormat = CreatePixelFormat(NSOpenGLPixelFormatAttribute(profiles[i]));

		if (nil != pixelFormat)
		{
			s_cocoaGLProfile = profiles[i];
			Printf("GL context: %s\n",
				zx::kNSGLProfileCore41 == profiles[i] ? "4.1 core" :
				zx::kNSGLProfileCore32 == profiles[i] ? "3.2 core" : "legacy");
			break;
		}
	}

	if (nil == pixelFormat)
	{
		I_FatalError("Cannot OpenGL create pixel format, graphics hardware is not supported");
	}

	// Create OpenGL context and view

	const NSRect contentRect = [m_window contentRectForFrameRect:[m_window frame]];
	NSOpenGLView* glView = [[CocoaView alloc] initWithFrame:contentRect
												pixelFormat:pixelFormat];
	[[glView openGLContext] makeCurrentContext];

	[m_window setContentView:glView];

	FConsoleWindow::GetInstance().Show(false);
}

void CocoaVideo::StartModeIterator(const int bits, const bool fullscreen)
{
	m_modeIterator.index      = 0;
	m_modeIterator.bits       = bits;
	m_modeIterator.fullscreen = fullscreen;
}

bool CocoaVideo::NextMode(int* const width, int* const height, bool* const letterbox)
{
	assert(NULL != width);
	assert(NULL != height);

	const int bits = m_modeIterator.bits;

	if (8 != bits && 16 != bits && 24 != bits && 32 != bits)
	{
		return false;
	}

	size_t& index = m_modeIterator.index;

	if (index < sizeof(VideoModes) / sizeof(VideoModes[0]))
	{
		*width  = VideoModes[index].width;
		*height = VideoModes[index].height;

		if (m_modeIterator.fullscreen && NULL != letterbox)
		{
			const NSSize screenSize  = [[m_window screen] frame].size;
			const float  screenRatio = screenSize.width / screenSize.height;
			const float  modeRatio   = float(*width) / *height;

			*letterbox = fabs(screenRatio - modeRatio) > 0.001f;
		}

		++index;

		return true;
	}

	return false;
}

DFrameBuffer* CocoaVideo::CreateFrameBuffer(const int width, const int height, const bool fullscreen, DFrameBuffer* const old)
{
	PalEntry flashColor  = 0;
	int      flashAmount = 0;

	if (NULL != old)
	{
		if (width == m_width && height == m_height)
		{
			SetMode(width, height, fullscreen, vid_hidpi);
			return old;
		}

		old->GetFlash(flashColor, flashAmount);
		old->ObjectFlags |= OF_YesReallyDelete;

		if (old == screen)
		{
			screen = NULL;
		}

		delete old;
	}

	// [rc4l] GL-only build: there is no second renderer to choose between. Mirrors what
	// posix/sdl/hardware.cpp already does for the SDL backend.
	DFrameBuffer* fb = new OpenGLFrameBuffer(NULL, width, height, 32, 60, fullscreen);

	fb->SetFlash(flashColor, flashAmount);

	SetMode(width, height, fullscreen, vid_hidpi);

	return fb;
}

void CocoaVideo::SetWindowedScale(float scale)
{
}


bool CocoaVideo::IsFullscreen()
{
	CocoaVideo* const video = GetInstance();
	return NULL == video
		? false
		: video->m_fullscreen;
}

void CocoaVideo::UseHiDPI(const bool hiDPI)
{
	if (CocoaVideo* const video = GetInstance())
	{
		video->SetMode(video->m_width, video->m_height, video->m_fullscreen, hiDPI);
	}
}

void CocoaVideo::SetCursor(NSCursor* cursor)
{
	if (CocoaVideo* const video = GetInstance())
	{
		NSWindow*  const window = video->m_window;
		CocoaView* const view   = [window contentView];

		[view setCursor:cursor];
		[window invalidateCursorRectsForView:view];
	}
}

void CocoaVideo::SetWindowVisible(bool visible)
{
	if (CocoaVideo* const video = GetInstance())
	{
		if (visible)
		{
			[video->m_window orderFront:nil];
		}
		else
		{
			[video->m_window orderOut:nil];
		}

		I_SetNativeMouse(!visible);
	}
}


static bool HasModernFullscreenAPI()
{
	return NSAppKitVersionNumber >= AppKit10_6;
}

void CocoaVideo::SetStyleMask(const NSUInteger styleMask)
{
	// Before 10.6 it's impossible to change window's style mask
	// To workaround this new window should be created with required style mask
	// This method should not be called when running on Snow Leopard or newer

	assert(!HasModernFullscreenAPI());

	CocoaWindow* tempWindow = CreateCocoaWindow(styleMask);
	[tempWindow setContentView:[m_window contentView]];

	[m_window close];
	m_window = tempWindow;
}

void CocoaVideo::SetFullscreenMode(const int width, const int height)
{
	NSScreen* screen = [m_window screen];

	const NSRect screenFrame = [screen frame];
	const NSRect displayRect = vid_hidpi
		? [screen convertRectToBacking:screenFrame]
		: screenFrame;

	const float  displayWidth  = displayRect.size.width;
	const float  displayHeight = displayRect.size.height;

	const float pixelScaleFactorX = displayWidth  / static_cast<float>(width );
	const float pixelScaleFactorY = displayHeight / static_cast<float>(height);

	rbOpts.pixelScale = MIN(pixelScaleFactorX, pixelScaleFactorY);

	rbOpts.width  = width  * rbOpts.pixelScale;
	rbOpts.height = height * rbOpts.pixelScale;

	rbOpts.shiftX = (displayWidth  - rbOpts.width ) / 2.0f;
	rbOpts.shiftY = (displayHeight - rbOpts.height) / 2.0f;

	if (!m_fullscreen)
	{
		if (HasModernFullscreenAPI())
		{
			[m_window setLevel:LEVEL_FULLSCREEN];
			[m_window setStyleMask:STYLE_MASK_FULLSCREEN];
		}
		else
		{
			// Old Carbon-based way to make fullscreen window above dock and menu
			// It's supported on 64-bit, but on 10.6 and later the following is preferred:
			// [NSWindow setLevel:NSMainMenuWindowLevel + 1]

			SetSystemUIMode(kUIModeAllHidden, 0);
			SetStyleMask(STYLE_MASK_FULLSCREEN);
		}

		[m_window setHidesOnDeactivate:YES];
	}

	[m_window setFrame:screenFrame display:YES];
}

void CocoaVideo::SetWindowedMode(const int width, const int height)
{
	rbOpts.pixelScale = 1.0f;

	rbOpts.width  = static_cast<float>(width );
	rbOpts.height = static_cast<float>(height);

	rbOpts.shiftX = 0.0f;
	rbOpts.shiftY = 0.0f;

	// [rc4l] video-scale: the OS window is the CLIENT size; the render size (width/height above,
	// already stored in rbOpts) may be smaller when internal-resolution scaling is on. Both are in
	// BACKING PIXELS -- see the invariant in posix/README.md -- and only setContentSize: below
	// converts to points. See features/video-scale.
	const int clientW = (zx_pendingClientWidth  > 0) ? zx_pendingClientWidth  : width;
	const int clientH = (zx_pendingClientHeight > 0) ? zx_pendingClientHeight : height;

	const NSSize windowPixelSize = NSMakeSize(clientW, clientH);
	const NSSize windowSize = vid_hidpi
		? [[m_window contentView] convertSizeFromBacking:windowPixelSize]
		: windowPixelSize;

	if (m_fullscreen)
	{
		if (HasModernFullscreenAPI())
		{
			[m_window setLevel:LEVEL_WINDOWED];
			[m_window setStyleMask:STYLE_MASK_WINDOWED];
		}
		else
		{
			SetSystemUIMode(kUIModeNormal, 0);
			SetStyleMask(STYLE_MASK_WINDOWED);
		}

		[m_window setHidesOnDeactivate:NO];
	}

	[m_window setContentSize:windowSize];
	[m_window center];
	[m_window enterFullscreenOnZoom];
	[m_window exitAppOnClose];
}

void CocoaVideo::SetMode(const int width, const int height, const bool fullscreen, const bool hiDPI)
{
	if (fullscreen == m_fullscreen
		&& width   == m_width
		&& height  == m_height
		&& hiDPI   == m_hiDPI)
	{
		return;
	}

	if (I_IsHiDPISupported())
	{
		NSOpenGLView* const glView = [m_window contentView];
		[glView setWantsBestResolutionOpenGLSurface:hiDPI];
	}

	if (fullscreen)
	{
		SetFullscreenMode(width, height);
	}
	else
	{
		SetWindowedMode(width, height);
	}

	rbOpts.dirty = true;

	const NSSize viewSize = I_GetContentViewSize(m_window);

	glViewport(0, 0, static_cast<GLsizei>(viewSize.width), static_cast<GLsizei>(viewSize.height));
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	[[NSOpenGLContext currentContext] flushBuffer];

	// [rc4l] FUA branding: the window title carries OUR version, and for a non-stable build the
	// commit hash too, so a screenshot of an experimental build always says which one it is. Same
	// text as the SDL backend produced (posix/sdl/sdlglvideo.cpp). Not cached in a static: the
	// string is build-constant but the code reads better without pretending otherwise.
	char caption[100];
	{
		const char *tag = GetFuaVersionTag();
		if (zx::FuaIsStableBuild(GetFuaDescribe()))
			mysnprintf(caption, countof(caption), FUA_NAME " %s (stable)", tag);
		else
			mysnprintf(caption, countof(caption), FUA_NAME " %s (experimental %s)", tag, GetGitHash());
	}
	[m_window setTitle:[NSString stringWithUTF8String:caption]];

	if (![m_window isKeyWindow])
	{
		[m_window makeKeyAndOrderFront:nil];
	}

	m_fullscreen = fullscreen;
	m_width      = width;
	m_height     = height;
	m_hiDPI      = hiDPI;
}


NSView* CocoaVideo::GetContentView()
{
	CocoaVideo* const video = GetInstance();
	return NULL == video ? nil : [video->m_window contentView];
}

// [rc4l] windowed-video: resize the window, then reconcile. Windowed only -- fullscreen already
// covers the display.
void CocoaVideo::ResizeWindow(const int width, const int height)
{
	if (m_fullscreen)
	{
		return;
	}

	const NSSize pixels = NSMakeSize(width, height);
	const NSSize points = vid_hidpi
		? [[m_window contentView] convertSizeFromBacking:pixels]
		: pixels;

	[m_window setContentSize:points];
	[m_window center];

	OnWindowResized();
}

// [rc4l] windowed-video: the drawable changed.
//
// Upstream needs no equivalent: their CocoaView is an NSOpenGLView, which updates its GL context on
// frame change by itself. Ours is a plain NSView, so the context must be told by hand -- and rbOpts,
// which the Cocoa backend uses for render-buffer geometry, has to come along or the per-frame
// reconcile in MaybeResizeForScale compares against stale values.
void CocoaVideo::OnWindowResized()
{
	if (NULL == screen || m_fullscreen)
	{
		return;
	}

	const NSSize viewSize = I_GetContentViewSize(m_window);

	if (viewSize.width <= 0.0 || viewSize.height <= 0.0)
	{
		return;
	}

	rbOpts.width  = static_cast<float>(viewSize.width );
	rbOpts.height = static_cast<float>(viewSize.height);
	rbOpts.shiftX = 0.0f;
	rbOpts.shiftY = 0.0f;
	rbOpts.dirty  = true;

	// The context must be told its view changed, or the drawable stays at the old size and the game
	// keeps rendering into a stale buffer -- "the window resizes but the content doesn't".
	[[NSOpenGLContext currentContext] update];

	glViewport(0, 0, static_cast<GLsizei>(viewSize.width), static_cast<GLsizei>(viewSize.height));

	vid_defwidth  = static_cast<int>(viewSize.width );
	vid_defheight = static_cast<int>(viewSize.height);
}

static void ZX_CocoaWindowResized()
{
	if (CocoaVideo* const video = CocoaVideo::GetInstance())
	{
		video->OnWindowResized();
	}
}

CocoaVideo* CocoaVideo::GetInstance()
{
	return static_cast<CocoaVideo*>(Video);
}


// [rc4l] GL-only build: the software CocoaFrameBuffer (a GPfx palette blit through
// GL_TEXTURE_RECTANGLE_ARB and glBegin/glEnd) is removed here. We have no software
// renderer -- r_nullrenderer.h replaced FSoftwareRenderer -- and that fixed-function
// path would be rejected by a core profile anyway. See posix/README.md.

// ---------------------------------------------------------------------------


// [rc4l] Companion to the DECLARE_CLASS in sdlglvideo.h -- see the note there. Matches how
// posix/sdl/sdlglvideo.cpp:39 registers the same class for the SDL backend.
IMPLEMENT_ABSTRACT_CLASS(SDLGLFB)

// [rc4l] See the declaration in sdlglvideo.h.
// [rc4l] windowed-video: overrides DFrameBuffer::SetWindowSize, a no-op in the base. vid_setsize
// and the menu's "Apply windowed size" reach the backend only through this virtual, so without the
// override they silently did nothing on Cocoa.
void SDLGLFB::SetWindowSize(const int w, const int h)
{
	if (CocoaVideo* const video = CocoaVideo::GetInstance())
	{
		video->ResizeWindow(w, h);
	}
}

bool SDLGLFB::IsCoreProfile()
{
	return zx::kNSGLProfileLegacy != s_cocoaGLProfile;
}

SDLGLFB::SDLGLFB(void*, const int width, const int height, int, int, const bool fullscreen)
: DFrameBuffer(width, height)
, m_lock(-1)
, m_isUpdatePending(false)
{
	CGGammaValue gammaTable[GAMMA_TABLE_SIZE];
	uint32_t actualChannelSize;

	const CGError result = CGGetDisplayTransferByTable(kCGDirectMainDisplay, GAMMA_CHANNEL_SIZE,
		gammaTable, &gammaTable[GAMMA_CHANNEL_SIZE], &gammaTable[GAMMA_CHANNEL_SIZE * 2], &actualChannelSize);
	m_supportsGamma = kCGErrorSuccess == result && GAMMA_CHANNEL_SIZE == actualChannelSize;

	if (m_supportsGamma)
	{
		for (uint32_t i = 0; i < GAMMA_TABLE_SIZE; ++i)
		{
			m_originalGamma[i] = static_cast<WORD>(gammaTable[i] * 65535.0f);
		}
	}
}

SDLGLFB::SDLGLFB()
{
}

SDLGLFB::~SDLGLFB()
{
}


bool SDLGLFB::Lock(bool buffered)
{
	m_lock++;

	Buffer = MemBuffer;

	return true;
}

void SDLGLFB::Unlock()
{
	if (m_isUpdatePending && 1 == m_lock)
	{
		Update();
	}
	else if (--m_lock <= 0)
	{
		m_lock = 0;
	}
}

bool SDLGLFB::IsLocked()
{
	return m_lock > 0;
}


bool SDLGLFB::IsFullscreen()
{
	return CocoaVideo::IsFullscreen();
}

void SDLGLFB::SetVSync(bool vsync)
{
#if MAC_OS_X_VERSION_MAX_ALLOWED < 1050
	const long value = vsync ? 1 : 0;
#else // 10.5 or newer
	const GLint value = vsync ? 1 : 0;
#endif // prior to 10.5

	[[NSOpenGLContext currentContext] setValues:&value
								   forParameter:NSOpenGLCPSwapInterval];
}


void SDLGLFB::InitializeState()
{
}

bool SDLGLFB::CanUpdate()
{
	if (m_lock != 1)
	{
		if (m_lock > 0)
		{
			m_isUpdatePending = true;
			--m_lock;
		}

		return false;
	}

	return true;
}

void SDLGLFB::SwapBuffers()
{
	[[NSOpenGLContext currentContext] flushBuffer];
}

void SDLGLFB::SetGammaTable(WORD* table)
{
	if (m_supportsGamma)
	{
		CGGammaValue gammaTable[GAMMA_TABLE_SIZE];

		for (uint32_t i = 0; i < GAMMA_TABLE_SIZE; ++i)
		{
			gammaTable[i] = static_cast<CGGammaValue>(table[i] / 65535.0f);
		}

		CGSetDisplayTransferByTable(kCGDirectMainDisplay, GAMMA_CHANNEL_SIZE,
			gammaTable, &gammaTable[GAMMA_CHANNEL_SIZE], &gammaTable[GAMMA_CHANNEL_SIZE * 2]);
	}
}

void SDLGLFB::ResetGammaTable()
{
	if (m_supportsGamma)
	{
		SetGammaTable(m_originalGamma);
	}
}

int SDLGLFB::GetClientWidth()
{
	// [rc4l] see CocoaVideo::GetContentView.
	NSView *view = CocoaVideo::GetContentView();
	if (nil == view) return Width;
	NSRect backingBounds = [view convertRectToBacking: [view bounds]];
	int clientWidth = (int)backingBounds.size.width;
	return clientWidth > 0 ? clientWidth : Width;
}

int SDLGLFB::GetClientHeight()
{
	// [rc4l] see GetClientWidth.
	NSView *view = CocoaVideo::GetContentView();
	if (nil == view) return Height;
	NSRect backingBounds = [view convertRectToBacking: [view bounds]];
	int clientHeight = (int)backingBounds.size.height;
	return clientHeight > 0 ? clientHeight : Height;
}


// ---------------------------------------------------------------------------


// [rc4l] GL-only build: the 'blit' stat reported the software framebuffer's blit and flip timings
// and would now be permanently zero, so it goes with the framebuffer rather than lying.


IVideo* Video;


// ---------------------------------------------------------------------------


void I_ShutdownGraphics()
{
	if (NULL != screen)
	{
		screen->ObjectFlags |= OF_YesReallyDelete;
		delete screen;
		screen = NULL;
	}

	delete Video;
	Video = NULL;
}

void I_InitGraphics()
{
	UCVarValue val;

	val.Bool = !!Args->CheckParm("-devparm");
	ticker.SetGenericRepDefault(val, CVAR_Bool);

	Video = new CocoaVideo;
	atterm(I_ShutdownGraphics);
}


static void I_DeleteRenderer()
{
	delete Renderer;
	Renderer = NULL;
}

void I_CreateRenderer()
{
	s_currentRenderer = vid_renderer;

	if (NULL == Renderer)
	{
		extern FRenderer* gl_CreateInterface();

#ifndef NO_GL
		// [rc4l] GL-only build: always the OpenGL renderer.
		Renderer = gl_CreateInterface();
#else
		// [rc4l] Dedicated server (no OpenGL): the trivial null renderer.
		Renderer = new FNullRenderer;
#endif
		atterm(I_DeleteRenderer);
	}
}


DFrameBuffer* I_SetMode(int &width, int &height, DFrameBuffer* old)
{
	return Video->CreateFrameBuffer(width, height, fullscreen, old);
}

bool I_CheckResolution(const int width, const int height, const int bits)
{
	int twidth, theight;

	Video->StartModeIterator(bits, fullscreen);

	while (Video->NextMode(&twidth, &theight, NULL))
	{
		if (width == twidth && height == theight)
		{
			return true;
		}
	}

	return false;
}

void I_ClosestResolution(int *width, int *height, int bits)
{
	int twidth, theight;
	int cwidth = 0, cheight = 0;
	int iteration;
	DWORD closest = DWORD(-1);

	for (iteration = 0; iteration < 2; ++iteration)
	{
		Video->StartModeIterator(bits, fullscreen);

		while (Video->NextMode(&twidth, &theight, NULL))
		{
			if (twidth == *width && theight == *height)
			{
				return;
			}

			if (iteration == 0 && (twidth < *width || theight < *height))
			{
				continue;
			}

			const DWORD dist = (twidth - *width) * (twidth - *width)
				+ (theight - *height) * (theight - *height);

			if (dist < closest)
			{
				closest = dist;
				cwidth = twidth;
				cheight = theight;
			}
		}

		if (closest != DWORD(-1))
		{
			*width = cwidth;
			*height = cheight;
			return;
		}
	}
}


// ---------------------------------------------------------------------------


EXTERN_CVAR(Int, vid_maxfps);
EXTERN_CVAR(Bool, cl_capfps);

// So Apple doesn't support POSIX timers and I can't find a good substitute short of
// having Objective-C Cocoa events or something like that.
void I_SetFPSLimit(int limit)
{
}

CUSTOM_CVAR(Int, vid_maxfps, 200, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
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

CUSTOM_CVAR(Bool, vid_hidpi, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (I_IsHiDPISupported())
	{
		CocoaVideo::UseHiDPI(self);
	}
	else if (0 != self)
	{
		self = 0;
	}
}


// ---------------------------------------------------------------------------


CCMD(vid_listmodes)
{
	if (Video == NULL)
	{
		return;
	}

	static const char* const ratios[7] = { "", " - 16:9", " - 16:10", " - 17:10", " - 5:4", "", " - 21:9" };
	int width, height;
	bool letterbox;

	Video->StartModeIterator(32, screen->IsFullscreen());

	while (Video->NextMode(&width, &height, &letterbox))
	{
		const bool current = width == DisplayWidth && height == DisplayHeight;
		const int  ratio   = CheckRatio(width, height);

		Printf(current ? PRINT_BOLD : PRINT_HIGH, "%s%4d x%5d x%3d%s%s\n",
			current || !(ratio & 3) ? "" : TEXTCOLOR_GOLD,
			width, height, 32, ratios[ratio],
			current || !letterbox ? "" : TEXTCOLOR_BROWN " LB");
	}
}

CCMD(vid_currentmode)
{
	Printf("%dx%dx%d\n", DisplayWidth, DisplayHeight, DisplayBits);
}


// ---------------------------------------------------------------------------


bool I_SetCursor(FTexture* cursorpic)
{
	NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
	NSCursor* cursor = nil;

	if (NULL != cursorpic && FTexture::TEX_Null != cursorpic->UseType)
	{
		// Create bitmap image representation

		const NSInteger imageWidth  = cursorpic->GetWidth();
		const NSInteger imageHeight = cursorpic->GetHeight();
		const NSInteger imagePitch  = imageWidth * 4;

		NSBitmapImageRep* bitmapImageRep = [NSBitmapImageRep alloc];
		[bitmapImageRep initWithBitmapDataPlanes:NULL
									  pixelsWide:imageWidth
									  pixelsHigh:imageHeight
								   bitsPerSample:8
								 samplesPerPixel:4
										hasAlpha:YES
										isPlanar:NO
								  colorSpaceName:NSDeviceRGBColorSpace
									 bytesPerRow:imagePitch
									bitsPerPixel:0];

		// Load bitmap data to representation

		BYTE* buffer = [bitmapImageRep bitmapData];
		memset(buffer, 0, imagePitch * imageHeight);

		FBitmap bitmap(buffer, imagePitch, imageWidth, imageHeight);
		cursorpic->CopyTrueColorPixels(&bitmap, 0, 0);

		// Swap red and blue components in each pixel

		for (size_t i = 0; i < size_t(imageWidth * imageHeight); ++i)
		{
			const size_t offset = i * 4;

			const BYTE temp    = buffer[offset    ];
			buffer[offset    ] = buffer[offset + 2];
			buffer[offset + 2] = temp;
		}

		// Create image from representation and set it as cursor

		NSData* imageData = [bitmapImageRep representationUsingType:NSPNGFileType
														 properties:[NSDictionary dictionary]];
		NSImage* cursorImage = [[NSImage alloc] initWithData:imageData];

		cursor = [[NSCursor alloc] initWithImage:cursorImage
										 hotSpot:NSMakePoint(0.0f, 0.0f)];
	}
	
	CocoaVideo::SetCursor(cursor);
	
	[pool release];
	
	return true;
}


NSSize I_GetContentViewSize(const NSWindow* const window)
{
	const NSView* const view = [window contentView];
	const NSSize frameSize   = [view frame].size;

	return (vid_hidpi)
		? [view convertSizeToBacking:frameSize]
		: frameSize;
}

void I_SetMainWindowVisible(bool visible)
{
	CocoaVideo::SetWindowVisible(visible);
}
