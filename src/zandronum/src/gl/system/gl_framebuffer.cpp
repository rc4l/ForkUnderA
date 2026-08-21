/*
** gl_framebuffer.cpp
** Implementation of the non-hardware specific parts of the
** OpenGL frame buffer
**
**---------------------------------------------------------------------------
** Copyright 2000-2007 Christoph Oelckers
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
** 4. When not used as part of GZDoom or a GZDoom derivative, this code will be
**    covered by the terms of the GNU Lesser General Public License as published
**    by the Free Software Foundation; either version 2.1 of the License, or (at
**    your option) any later version.
** 5. Full disclosure of the entire project's source code, except for third
**    party libraries is mandatory. (NOTE: This clause is non-negotiable!)
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

#include "gl/system/gl_system.h"
#include "files.h"
#include "m_swap.h"
#include "v_video.h"
#include "sbar.h"   // [rc4l] StatusBar->ScreenSizeChanged on an in-place resize
#include "doomstat.h"
#include "m_png.h"
#include "m_crc32.h"
#include "vectors.h"
#include "v_palette.h"
#include "templates.h"
#include "farchive.h"

#include "gl/system/gl_interface.h"
#include "gl/system/gl_framebuffer.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/renderer/gl_lightdata.h"
#include "gl/data/gl_data.h"
#include "gl/textures/gl_hwtexture.h"
#include "gl/textures/gl_texture.h"
#include "gl/textures/gl_translate.h"
#include "gl/textures/gl_skyboxtexture.h"
#include "gl/utility/gl_clock.h"
#include "gl/utility/gl_templates.h"
#include "gl/gl_functions.h"
#include "mcp_glperf.h" // [rc4l] GPU render-pass timer anchors (no-op unless FUA_MCP_BRIDGE)
#ifdef ZX_ENABLE_REPLAY
#include "features/replay/zx_replay.h"   // [rc4l] FUA instant-replay frame capture hook
#include "features/replay/computation/replay_compute.h" // [rc4l] pure readback sizing/lifecycle logic
#endif

IMPLEMENT_CLASS(OpenGLFrameBuffer)
EXTERN_CVAR (Float, vid_brightness)
EXTERN_CVAR (Float, vid_contrast)
EXTERN_CVAR (Bool, vid_vsync)
// [rc4l] video-scale: the render-scale knob (features/video-scale/videoscale.cpp).
EXTERN_CVAR (Int, vid_scalemode)
EXTERN_CVAR (Float, vid_scalefactor)
EXTERN_CVAR (Int, vid_scale_customwidth)
EXTERN_CVAR (Int, vid_scale_customheight)
EXTERN_CVAR (Float, vid_scale_custompixelaspect)
EXTERN_CVAR (Bool, vid_cropaspect)
extern bool setsizeneeded;
extern int DisplayBits;
#include "features/video-scale/computation/videoscale_compute.h"

CVAR(Bool, gl_aalines, false, CVAR_ARCHIVE)

FGLRenderer *GLRenderer;

void gl_LoadExtensions();
void gl_PrintStartupLog();
void gl_SetupMenu();

//==========================================================================
//
//
//
//==========================================================================

OpenGLFrameBuffer::OpenGLFrameBuffer(void *hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen) : 
	Super(hMonitor, width, height, bits, refreshHz, fullscreen) 
{
	GLRenderer = new FGLRenderer(this);
	memcpy (SourcePalette, GPalette.BaseColors, sizeof(PalEntry)*256);
	UpdatePalette ();
	ScreenshotBuffer = NULL;
	LastCamera = NULL;

	// [rc4l] video-scale: no scale buffer until UpdateScaleBuffer decides one is needed.
	mScaleFB = mScaleColorTex = mScaleDepthRB = 0;
	mScaleFBW = mScaleFBH = 0;
	mScaleClientW = mScaleClientH = 0;
	mScaleActive = false;

#ifdef ZX_ENABLE_REPLAY
	// [rc4l] instant-replay readback PBOs: none until the first capture allocates them in THIS context.
	mReplayPbo[0] = mReplayPbo[1] = 0;
	mReplayPboIndex = 0;
	mReplayPboW = mReplayPboH = 0;
	mReplayPboFilled = false;
#endif

	InitializeState();
	gl_SetupMenu();
	gl_GenerateGlobalBrightmapFromColormap();
	DoSetGamma();
	needsetgamma = true;
	swapped = false;
	Accel2D = true;
	SetVSync(vid_vsync);
}

OpenGLFrameBuffer::~OpenGLFrameBuffer()
{
	DestroyScaleBuffer(); // [rc4l] video-scale
#ifdef ZX_ENABLE_REPLAY
	DestroyReplayCapture(); // [rc4l] free the replay readback PBOs before the base dtor kills the context
#endif
	delete GLRenderer;
	GLRenderer = NULL;
}

//==========================================================================
//
// Initializes the GL renderer
//
//==========================================================================

void OpenGLFrameBuffer::InitializeState()
{
	static bool first=true;

	gl_LoadExtensions();
	Super::InitializeState();

	if (first)
	{
		first=false;
		// [BB] For some reason this crashes, if compiled with MinGW and optimization. Has to be investigated.
#ifndef __MINGW32__
		gl_PrintStartupLog();
#endif

		if (gl.flags&RFL_NPOT_TEXTURE)
		{
			Printf("Support for non power 2 textures enabled.\n");
		}
		if (gl.flags&RFL_OCCLUSION_QUERY)
		{
			Printf("Occlusion query enabled.\n");
		}
	}
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(1.0f);
	glDepthFunc(GL_LESS);

	glEnable(GL_DITHER);
	glDisable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glEnable(GL_POLYGON_OFFSET_LINE);
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_CLAMP);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_LINE_SMOOTH);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	int trueH = GetTrueHeight();
	int h = GetHeight();
	glViewport(0, (trueH - h)/2, GetWidth(), GetHeight());

	// [rc4l] video-scale: decide whether we render into an offscreen scale buffer, (re)build it, and
	// bind it as the render target so everything below draws into it at the virtual size.
	UpdateScaleBuffer();

	Begin2D(false);
	GLRenderer->Initialize();
}

//==========================================================================
//
// Updates the screen
//
//==========================================================================

// Testing only for now. 
CVAR(Bool, gl_draw_sync, true, 0) //false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

void ZX_DumpScaleState()
{
	OpenGLFrameBuffer *const fb = static_cast<OpenGLFrameBuffer *>(screen);
	fb->DumpScaleState();
}

void OpenGLFrameBuffer::DumpScaleState()
{
	Printf("scale_active=%d\n", (int)mScaleActive);
	Printf("scale_fb=%d %dx%d\n", (int)mScaleFB, mScaleFBW, mScaleFBH);
	Printf("scale_client=%dx%d\n", mScaleClientW, mScaleClientH);
	Printf("fb_get_size=%dx%d truH=%d\n", GetWidth(), GetHeight(), GetTrueHeight());
}

void OpenGLFrameBuffer::Update()
{
	if (!CanUpdate())
	{
		GLRenderer->Flush();
		return;
	}

	MCP_GLPerf_ZoneBegin(MCP_GLZ_HUD2D); // [rc4l] GPU-time the 2D/HUD pass (no-op unless bridge+capture)

	Begin2D(false);

	DrawRateStuff();
	GLRenderer->Flush();

	if (GetTrueHeight() != GetHeight())
	{
		if (GLRenderer != NULL)
			GLRenderer->ClearBorders();

		Begin2D(false);
	}
	// [rc4l] video-scale: the frame was rendered into the scale FBO at the virtual size; blit it up
	// to fill the window before the backbuffer is presented.
	if (mScaleActive)
		BlitScaleBuffer();

	// [rc4l] Close the 2D span and the whole-frame GPU span here, BEFORE Swap(), so the buffer swap /
	// vsync wait is never charged to render time. No-op unless the bridge is profiling.
	MCP_GLPerf_ZoneEnd(MCP_GLZ_HUD2D);
	MCP_GLPerf_FrameEnd();

	if (gl_draw_sync || !swapped)
	{
		Swap();
	}
	swapped = false;

	Unlock();

	// [rc4l] video-scale: apply any pending scale/window resize AFTER Unlock -- resizing the canvas
	// reallocates its backing store, and while locked `Buffer` aliases that store; doing it here (with
	// Buffer == NULL) avoids a dangling pointer. No window teardown, so no flash/focus loss.
	MaybeResizeForScale();

	// Re-bind the render target AND reset the viewport for the next frame. Binding is a no-op in the
	// common no-change case (the resize above already rebound); the viewport reset is what matters
	// when scaling was just turned OFF at runtime (menu). Begin2D sets the projection matrix but not
	// glViewport, so without resetting here the 2D pass keeps the smaller scaled viewport and draws
	// the whole menu squished into the bottom-left corner over stale pixels. Both branches must set
	// it: the FBO branch to its virtual size, the backbuffer branch to the full client size.
	glBindFramebuffer(GL_FRAMEBUFFER, mScaleActive ? mScaleFB : 0);
	glViewport(0, 0, GetWidth(), GetHeight());

	CheckBench();
}

//==========================================================================
//
// [rc4l] video-scale: the GL executor for internal-resolution scaling. All the *sizing* decisions
// live in features/video-scale (unit-tested); this only creates/binds/blits GL objects.
// >>> SUPERSEDED-BY-UPSTREAM <<< When upstream's render-buffers backend is ported, replace these
// four methods and the two call sites above with it.
//
//==========================================================================

void OpenGLFrameBuffer::GetClientSize(int &w, int &h)
{
#ifdef _WIN32
	// Win32 native backend: the window's client rectangle in pixels (the borderless popup covers the
	// monitor when fullscreen; a normal window when windowed). Window is the global HWND.
	RECT r;
	if (Window != NULL && GetClientRect(Window, &r))
	{
		w = int(r.right - r.left);
		h = int(r.bottom - r.top);
	}
	else
	{
		w = GetWidth();
		h = GetHeight();
	}
#elif defined(ZX_COCOA_BACKEND)
	// [rc4l] Cocoa backend: upstream's own SDLGLFB exposes the drawable size directly, so this needs
	// no SDL. GetClientWidth/Height return BACKING PIXELS -- see the invariant in posix/README.md;
	// mixing them with points renders at quarter size on a Retina display rather than failing.
	w = GetClientWidth();
	h = GetClientHeight();
#else
	// SDL2 (Linux): the real drawable in pixels. For a FULLSCREEN_DESKTOP window this is the
	// desktop; for a window it is the window's drawable.
	SDL_GL_GetDrawableSize(Screen, &w, &h);
#endif
}

void OpenGLFrameBuffer::DestroyScaleBuffer()
{
	if (mScaleColorTex) { glDeleteTextures(1, &mScaleColorTex); mScaleColorTex = 0; }
	if (mScaleDepthRB)  { glDeleteRenderbuffers(1, &mScaleDepthRB); mScaleDepthRB = 0; }
	if (mScaleFB)       { glDeleteFramebuffers(1, &mScaleFB); mScaleFB = 0; }
	mScaleFBW = mScaleFBH = 0;
	mScaleActive = false;
	if (GLRenderer != NULL)
		GLRenderer->mOutputFB = 0;
}

void OpenGLFrameBuffer::UpdateScaleBuffer()
{
	int renderW = GetWidth();
	int renderH = GetHeight();
	int clientW = renderW, clientH = renderH;
	GetClientSize(clientW, clientH);

	// Cache the client size so the per-frame BlitScaleBuffer never has to call the (macOS-expensive)
	// GetClientSize again -- it only changes when we get here (mode set / resize / scale change).
	mScaleClientW = clientW;
	mScaleClientH = clientH;

	// A scale buffer is needed exactly when the render size differs from the window (matches the
	// pure ScalePresentPlan.active decision). Otherwise render straight to the backbuffer.
	if (clientW == renderW && clientH == renderH)
	{
		DestroyScaleBuffer();
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		// Reset the viewport to the full backbuffer here too, so the frame in which scaling turns off
		// is already correct -- not just the next present. (See the matching note in Update().)
		glViewport(0, 0, renderW, renderH);
		return;
	}

	if (mScaleFB == 0 || mScaleFBW != renderW || mScaleFBH != renderH)
	{
		DestroyScaleBuffer();

		glGenTextures(1, &mScaleColorTex);
		glBindTexture(GL_TEXTURE_2D, mScaleColorTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, renderW, renderH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glGenRenderbuffers(1, &mScaleDepthRB);
		glBindRenderbuffer(GL_RENDERBUFFER, mScaleDepthRB);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, renderW, renderH);

		glGenFramebuffers(1, &mScaleFB);
		glBindFramebuffer(GL_FRAMEBUFFER, mScaleFB);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mScaleColorTex, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mScaleDepthRB);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			// Fall back to no scaling rather than render nothing.
			Printf("video-scale: scale framebuffer incomplete; disabling scaling\n");
			DestroyScaleBuffer();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			return;
		}

		mScaleFBW = renderW;
		mScaleFBH = renderH;
	}

	mScaleActive = true;
	if (GLRenderer != NULL)
		GLRenderer->mOutputFB = mScaleFB;

	glBindFramebuffer(GL_FRAMEBUFFER, mScaleFB);
	glViewport(0, 0, renderW, renderH);
}

// [rc4l] The inverse of BlitScaleBuffer, reading the same two cached numbers that blit does,
// because that blit is what put the pixels where the pointer now is.
//
// Not gated on mScaleActive, because the cached client size equals the render size when scaling is
// off, making this an identity rather than a special case worth branching for.
void OpenGLFrameBuffer::ScaleCoordsFromWindow(int &x, int &y)
{
	const int clientW = (mScaleClientW > 0) ? mScaleClientW : GetWidth();
	const int clientH = (mScaleClientH > 0) ? mScaleClientH : GetHeight();

	zx::ScaleWindowPointToRender(clientW, clientH, GetWidth(), GetHeight(), x, y);
}

void OpenGLFrameBuffer::BlitScaleBuffer()
{
	// Cached client size (set in UpdateScaleBuffer); avoids a per-frame GetClientSize, which is an
	// expensive Cocoa/Metal query on macOS.
	int clientW = (mScaleClientW > 0) ? mScaleClientW : GetWidth();
	int clientH = (mScaleClientH > 0) ? mScaleClientH : GetHeight();

	glBindFramebuffer(GL_READ_FRAMEBUFFER, mScaleFB);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBlitFramebuffer(0, 0, GetWidth(), GetHeight(),
	                  0, 0, clientW, clientH,
	                  GL_COLOR_BUFFER_BIT, GL_LINEAR);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLFrameBuffer::ResizeRenderInPlace(int w, int h)
{
	// Resize the render target without recreating the SDL window or GL context -- mirrors upstream's
	// V_OutputResized. No black flash, no keyboard-focus loss (the window never goes away).
	Resize(w, h);                                // canvas backing store + Width/Height/Pitch
	V_RecalcVideoModeState(w, h, DisplayBits);   // Clean facs, DisplayW/H, mode-set recompute

	// [rc4l] The status bar caches ST_Y, computed from SCREENHEIGHT in DBaseStatusBar::SetScaled.
	// Resizing the render target without telling it leaves that cache pointing at the OLD height,
	// and R_SetWindow then sizes the 3D view against a stale ST_Y -- a view TALLER than the buffer
	// it draws into, which shows up as the status-bar strip rendering garbage while screenblocks 11
	// (no status bar) looks fine. A real mode set goes through this; an in-place resize has to too.
	if (StatusBar != NULL)
	{
		StatusBar->ScreenSizeChanged();
	}

	setsizeneeded = true;                        // recompute the 3D view for the new render size
	UpdateScaleBuffer();                         // (re)build + bind the scale FBO, or drop to backbuffer
}

void OpenGLFrameBuffer::MaybeResizeForScale()
{
	// [rc4l] uzdoom@c3702ae9e: this reconcile is UNCONDITIONAL, every frame, exactly as upstream's
	// OpenGLFrameBuffer::Update does it.
	//
	// It used to be gated on a zx_videoScaleDirty flag because the old query --
	// SDL_GL_GetDrawableSize -- cost tens of milliseconds on macOS, so polling it tanked the frame
	// rate. That reasoning died with the SDL backend: the Cocoa path reads the view's backing
	// bounds, which is cheap, and upstream polls on every platform including their own SDL one.
	//
	// The gate was also wrong, not just conservative. Anything that resized the window WITHOUT
	// raising the flag -- a drag, a fullscreen toggle, an OS-driven resize -- left the render target
	// at the old size with no way to notice. Comparing the sizes IS the check; a separate flag can
	// only ever get out of sync with it, so the flag is gone rather than merely unread.

	int cw = GetWidth(), ch = GetHeight();
	GetClientSize(cw, ch);
	// [rc4l] A MINIMIZED window has no client area, and following it there is worse than useless.
	//
	// This reconcile runs every frame against the window's live client rectangle, which is right for
	// a drag or a fullscreen toggle and wrong for minimize: the client rect goes to nothing, the
	// render target is rebuilt at one pixel square, and everything drawn until the window comes back
	// is drawn into it. Harmless while nobody is looking -- and not harmless at all with
	// fua_render_in_background on, where the whole point is that the frames still count. A minimized
	// window's shots came out 1x1.
	//
	// Keeping the last good size costs nothing: restoring sends a real client rect and the next
	// frame reconciles to it, which is the same path a drag already takes.
	if (cw <= 0 || ch <= 0)
		return;
	zx::ScaledViewport v = zx::ComputeScaledViewport(cw, ch,
		vid_scalemode, vid_scalefactor,
		vid_scale_customwidth, vid_scale_customheight, vid_scale_custompixelaspect,
		!!vid_cropaspect, 0.f,
		zx::VID_SCALE_MIN_WIDTH, zx::VID_SCALE_MIN_HEIGHT);
	switch (zx::ComputeScaleReconcile(cw, ch, GetWidth(), GetHeight(),
		mScaleClientW, mScaleClientH, v.width, v.height))
	{
	case zx::SCALE_RECONCILE_RESIZE:
		ResizeRenderInPlace(v.width, v.height);
		break;

	case zx::SCALE_RECONCILE_REBUILD:
		// [rc4l] The WINDOW changed but the render size did not, so the branch above cannot see it
		// -- and UpdateScaleBuffer is the only thing that refreshes mScaleClientW/H and mScaleActive,
		// which BlitScaleBuffer uses as its destination rect.
		//
		// This is not an edge case, it is EVERY launch: CreateCocoaWindow deliberately opens the
		// window at a temporary 319x199 (VideoModes[0] - 1), so the first UpdateScaleBuffer cached
		// a 638x398 client on a Retina display. SetMode then resized the window to the real size
		// while the render size stayed put, so nothing re-ran, and every frame was rendered at full
		// size into the FBO and then blitted into a 638x398 rect at the origin -- the whole game in
		// the bottom-left corner of a black window, with every size the engine reported correct
		// because the only wrong number was this cache.
		UpdateScaleBuffer();
		break;

	case zx::SCALE_RECONCILE_NONE:
		break;
	}
}


//==========================================================================
//
// Swap the buffers
//
//==========================================================================

#ifdef ZX_ENABLE_REPLAY
// [rc4l] Double-buffered PBO async framebuffer readback for the instant-replay capture. Issuing
// glReadPixels into a bound pixel-pack buffer returns immediately (the copy DMAs in the background);
// we then map the OTHER PBO, whose read was issued a frame ago and is already done, so nothing
// stalls the render thread the way the synchronous screenshot readback did. The two buffers are
// reallocated whenever the framebuffer size changes (e.g. a window resize).
//
// The PBOs are per-framebuffer MEMBERS (mReplayPbo), so they belong to THIS GL context and are
// destroyed with it on a video-mode switch -- see the header note and DestroyReplayCapture(). That
// is the fix for the Apple-driver crash: a buffer name from a destroyed context is never reused.
//
// w,h are the window's drawable (client) size -- we read the DEFAULT framebuffer, which holds the
// final image the player sees (the staircase/video-scale renderer draws into an offscreen buffer at
// a virtual size and upscales it into this back buffer before present, so reading the back buffer at
// the client size captures the whole frame -- reading the virtual size would crop it).
void OpenGLFrameBuffer::CaptureReplayFramePBO(int w, int h)
{
	if (w <= 0 || h <= 0) return;

	const bool haveBuffers = (mReplayPbo[0] != 0);
	if (zx::ComputePboAction(haveBuffers, mReplayPboW, mReplayPboH, w, h) == zx::PboAction::Allocate)
	{
		if (!haveBuffers) glGenBuffers(2, mReplayPbo);
		const GLsizeiptr bytes = (GLsizeiptr)zx::ComputeRgbReadbackBytes(w, h);
		for (int i = 0; i < 2; ++i)
		{
			glBindBuffer(GL_PIXEL_PACK_BUFFER, mReplayPbo[i]);
			glBufferData(GL_PIXEL_PACK_BUFFER, bytes, NULL, GL_STREAM_READ);
		}
		mReplayPboW = w; mReplayPboH = h; mReplayPboFilled = false; mReplayPboIndex = 0;
	}

	const int a = mReplayPboIndex;      // issue this frame's read here
	const int b = mReplayPboIndex ^ 1;  // this one holds the previous frame's (completed) read

	GLint prevFB = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFB);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);   // the window back buffer (final, upscaled image)

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, mReplayPbo[a]);
	glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, 0);

	if (mReplayPboFilled)
	{
		glBindBuffer(GL_PIXEL_PACK_BUFFER, mReplayPbo[b]);
		const BYTE *data = (const BYTE *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
		if (data != NULL)
		{
			// glReadPixels rows are bottom-up; hand off the last row with a negative stride so the
			// consumer walks it top-to-bottom.
			const zx::BottomUpView view = zx::ComputeBottomUpView(w, h);
			zx::replay::SubmitFrame(data + view.firstRowOffset, w, h, view.rowStride);
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		}
	}

	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, prevFB);   // restore whatever the renderer had bound
	mReplayPboIndex = b;
	mReplayPboFilled = true;
}

// Delete the readback PBOs. Called from ~OpenGLFrameBuffer while the GL context is still current (the
// base ~SDLGLFB destroys the context afterward), so the names are freed in the context that owns them.
void OpenGLFrameBuffer::DestroyReplayCapture()
{
	if (mReplayPbo[0] != 0)
	{
		glDeleteBuffers(2, mReplayPbo);
		mReplayPbo[0] = mReplayPbo[1] = 0;
	}
	mReplayPboW = mReplayPboH = 0;
	mReplayPboIndex = 0;
	mReplayPboFilled = false;
}
#endif

void OpenGLFrameBuffer::Swap()
{
	Finish.Reset();
	Finish.Clock();
	glFinish();
#ifdef ZX_ENABLE_REPLAY
	// [rc4l] FUA instant replay: when a frame is due, kick off an async PBO readback of the just-
	// rendered back buffer and hand the PREVIOUS frame's (already-completed) readback to the encoder.
	if (zx::replay::WantsFrame())
	{
		// Capture at the window's drawable size (the full presented frame), not the virtual render size.
		const int cw = mScaleClientW > 0 ? mScaleClientW : SCREENWIDTH;
		const int ch = mScaleClientH > 0 ? mScaleClientH : SCREENHEIGHT;
		CaptureReplayFramePBO(cw, ch);
	}
#endif
	if (needsetgamma)
	{
		//DoSetGamma();
		needsetgamma = false;
	}
	SwapBuffers();
	Finish.Unclock();
	swapped = true;
	FHardwareTexture::UnbindAll();
}

//===========================================================================
//
// DoSetGamma
//
// (Unfortunately Windows has some safety precautions that block gamma ramps
//  that are considered too extreme. As a result this doesn't work flawlessly)
//
//===========================================================================

void OpenGLFrameBuffer::DoSetGamma()
{
	WORD gammaTable[768];

	if (m_supportsGamma)
	{
		// This formula is taken from Doomsday
		float gamma = clamp<float>(Gamma, 0.1f, 4.f);
		float contrast = clamp<float>(vid_contrast, 0.1f, 3.f);
		float bright = clamp<float>(vid_brightness, -0.8f, 0.8f);

		double invgamma = 1 / gamma;
		double norm = pow(255., invgamma - 1);

		for (int i = 0; i < 256; i++)
		{
			double val = i * contrast - (contrast - 1) * 127;
			if(gamma != 1) val = pow(val, invgamma) / norm;
			val += bright * 128;

			gammaTable[i] = gammaTable[i + 256] = gammaTable[i + 512] = (WORD)clamp<double>(val*256, 0, 0xffff);
		}
		SetGammaTable(gammaTable);
	}
}

bool OpenGLFrameBuffer::SetGamma(float gamma)
{
	DoSetGamma();
	return true;
}

bool OpenGLFrameBuffer::SetBrightness(float bright)
{
	DoSetGamma();
	return true;
}

bool OpenGLFrameBuffer::SetContrast(float contrast)
{
	DoSetGamma();
	return true;
}

//===========================================================================
//
//
//===========================================================================

void OpenGLFrameBuffer::UpdatePalette()
{
	int rr=0,gg=0,bb=0;
	for(int x=0;x<256;x++)
	{
		rr+=GPalette.BaseColors[x].r;
		gg+=GPalette.BaseColors[x].g;
		bb+=GPalette.BaseColors[x].b;
	}
	rr>>=8;
	gg>>=8;
	bb>>=8;

	palette_brightness = (rr*77 + gg*143 + bb*35)/255;
}

void OpenGLFrameBuffer::GetFlashedPalette (PalEntry pal[256])
{
	memcpy(pal, SourcePalette, 256*sizeof(PalEntry));
}

PalEntry *OpenGLFrameBuffer::GetPalette ()
{
	return SourcePalette;
}

bool OpenGLFrameBuffer::SetFlash(PalEntry rgb, int amount)
{
	Flash = PalEntry(amount, rgb.r, rgb.g, rgb.b);
	return true;
}

void OpenGLFrameBuffer::GetFlash(PalEntry &rgb, int &amount)
{
	rgb = Flash;
	rgb.a = 0;
	amount = Flash.a;
}

int OpenGLFrameBuffer::GetPageCount()
{
	return 1;
}


void OpenGLFrameBuffer::GetHitlist(BYTE *hitlist)
{
	Super::GetHitlist(hitlist);

	// check skybox textures and mark the separate faces as used
	for(int i=0;i<TexMan.NumTextures(); i++)
	{
		// HIT_Wall must be checked for MBF-style sky transfers. 
		if (hitlist[i] & (FTextureManager::HIT_Sky|FTextureManager::HIT_Wall))
		{
			FTexture *tex = TexMan.ByIndex(i);
			if (tex->gl_info.bSkybox)
			{
				FSkyBox *sb = static_cast<FSkyBox*>(tex);
				for(int i=0;i<6;i++) 
				{
					if (sb->faces[i]) 
					{
						int index = sb->faces[i]->id.GetIndex();
						hitlist[index] |= FTextureManager::HIT_Flat;
					}
				}
			}
		}
	}


	// check model skins
}

//==========================================================================
//
// DFrameBuffer :: CreatePalette
//
// Creates a native palette from a remap table, if supported.
//
//==========================================================================

FNativePalette *OpenGLFrameBuffer::CreatePalette(FRemapTable *remap)
{
	return GLTranslationPalette::CreatePalette(remap);
}

//==========================================================================
//
//
//
//==========================================================================
bool OpenGLFrameBuffer::Begin2D(bool)
{
	gl_RenderState.mViewMatrix.loadIdentity();
	gl_RenderState.mProjectionMatrix.ortho(0, GetWidth(), GetHeight(), 0, -1.0f, 1.0f);
	gl_RenderState.ApplyMatrices();

	glDisable(GL_DEPTH_TEST);

	// Korshun: ENABLE AUTOMAP ANTIALIASING!!!
	if (gl_aalines)
		glEnable(GL_LINE_SMOOTH);
	else
	{
		glDisable(GL_MULTISAMPLE);
		glDisable(GL_LINE_SMOOTH);
		glLineWidth(1.0);
	}

	if (GLRenderer != NULL)
			GLRenderer->Begin2D();
	return true;
}

//==========================================================================
//
// Draws a texture
//
//==========================================================================

void STACK_ARGS OpenGLFrameBuffer::DrawTextureV(FTexture *img, double x0, double y0, uint32 tag, va_list tags)
{
	DrawParms parms;

	if (ParseDrawTextureTags(img, x0, y0, tag, tags, &parms, true))
	{
		if (GLRenderer != NULL) GLRenderer->DrawTexture(img, parms);
	}
}

//==========================================================================
//
//
//
//==========================================================================
void OpenGLFrameBuffer::DrawLine(int x1, int y1, int x2, int y2, int palcolor, uint32 color)
{
	if (GLRenderer != NULL) 
		GLRenderer->DrawLine(x1, y1, x2, y2, palcolor, color);
}

//==========================================================================
//
//
//
//==========================================================================
void OpenGLFrameBuffer::DrawPixel(int x1, int y1, int palcolor, uint32 color)
{
	if (GLRenderer != NULL) 
		GLRenderer->DrawPixel(x1, y1, palcolor, color);
}

//==========================================================================
//
//
//
//==========================================================================
void OpenGLFrameBuffer::Dim(PalEntry)
{
	// Unlike in the software renderer the color is being ignored here because
	// view blending only affects the actual view with the GL renderer.
	Super::Dim(0);
}

void OpenGLFrameBuffer::Dim(PalEntry color, float damount, int x1, int y1, int w, int h)
{
	if (GLRenderer != NULL) 
		GLRenderer->Dim(color, damount, x1, y1, w, h);
}

//==========================================================================
//
//
//
//==========================================================================
void OpenGLFrameBuffer::FlatFill (int left, int top, int right, int bottom, FTexture *src, bool local_origin)
{

	if (GLRenderer != NULL) 
		GLRenderer->FlatFill(left, top, right, bottom, src, local_origin);
}

//==========================================================================
//
//
//
//==========================================================================
void OpenGLFrameBuffer::Clear(int left, int top, int right, int bottom, int palcolor, uint32 color)
{
	if (GLRenderer != NULL) 
		GLRenderer->Clear(left, top, right, bottom, palcolor, color);
}

//==========================================================================
//
// D3DFB :: FillSimplePoly
//
// Here, "simple" means that a simple triangle fan can draw it.
//
//==========================================================================

void OpenGLFrameBuffer::FillSimplePoly(FTexture *texture, FVector2 *points, int npoints,
	double originx, double originy, double scalex, double scaley,
	angle_t rotation, FDynamicColormap *colormap, int lightlevel)
{
	if (GLRenderer != NULL)
	{
		GLRenderer->FillSimplePoly(texture, points, npoints, originx, originy, scalex, scaley,
			rotation, colormap, lightlevel);
	}
}


//===========================================================================
// 
//	Takes a screenshot
//
//===========================================================================

void OpenGLFrameBuffer::GetScreenshotBuffer(const BYTE *&buffer, int &pitch, ESSType &color_type)
{
	int w = SCREENWIDTH;
	int h = SCREENHEIGHT;

	ReleaseScreenshotBuffer();
	ScreenshotBuffer = new BYTE[w * h * 3];

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0,(GetTrueHeight() - GetHeight()) / 2,w,h,GL_RGB,GL_UNSIGNED_BYTE,ScreenshotBuffer);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	pitch = -w*3;
	color_type = SS_RGB;
	buffer = ScreenshotBuffer + w * 3 * (h - 1);
}

//===========================================================================
// 
// Releases the screenshot buffer.
//
//===========================================================================

void OpenGLFrameBuffer::ReleaseScreenshotBuffer()
{
	if (ScreenshotBuffer != NULL) delete [] ScreenshotBuffer;
	ScreenshotBuffer = NULL;
}


void OpenGLFrameBuffer::GameRestart()
{
	memcpy (SourcePalette, GPalette.BaseColors, sizeof(PalEntry)*256);
	UpdatePalette ();
	ScreenshotBuffer = NULL;
	LastCamera = NULL;
	gl_GenerateGlobalBrightmapFromColormap();
}