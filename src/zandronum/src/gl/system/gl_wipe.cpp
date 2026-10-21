/*
** gl_wipe.cpp
** Screen wipe stuff
** (This uses immediate mode and the fixed function pipeline 
**  even if the new renderer is active)
**
**---------------------------------------------------------------------------
** Copyright 2008 Christoph Oelckers
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
#include "f_wipe.h"
#include "m_random.h"
#include "w_wad.h"
#include "v_palette.h"
#include "templates.h"
#include "vectors.h"

#include "gl/system/gl_interface.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/system/gl_framebuffer.h"
#include "gl/shaders/gl_shader.h"
#include "features/materials/translationpalette.h"
#include "gl/textures/gl_material.h"
#include "gl/textures/gl_samplers.h"
#include "gl/utility/gl_templates.h"
#include "gl/data/gl_vertexbuffer.h"
#include "features/wipe/computation/wipe_compute.h"   // [rc4l] the melt, crossfade and burn mask, shared with the backend

#ifndef _WIN32
struct POINT {
  SDWORD x; 
  SDWORD y; 
};
struct RECT {
  SDWORD left; 
  SDWORD top; 
  SDWORD right; 
  SDWORD bottom; 
}; 
#endif

//===========================================================================
// 
//	Screen wipes
//
//===========================================================================

// TYPES -------------------------------------------------------------------

class OpenGLFrameBuffer::Wiper_Crossfade : public OpenGLFrameBuffer::Wiper
{
public:
	Wiper_Crossfade();
	bool Run(int ticks, OpenGLFrameBuffer *fb);

private:
	int Clock;
};

class OpenGLFrameBuffer::Wiper_Melt : public OpenGLFrameBuffer::Wiper
{
public:
	Wiper_Melt();
	bool Run(int ticks, OpenGLFrameBuffer *fb);

private:
	int y[zx::wipe::kMeltColumns];
};

class OpenGLFrameBuffer::Wiper_Burn : public OpenGLFrameBuffer::Wiper
{
public:
	Wiper_Burn();
	~Wiper_Burn();
	bool Run(int ticks, OpenGLFrameBuffer *fb);

private:
	static const int WIDTH = 64, HEIGHT = 64;
	BYTE BurnArray[WIDTH * (HEIGHT + 5)];
	BYTE BurnMask[WIDTH * HEIGHT];
	FHardwareTexture *BurnTexture;
	int Density;
	int BurnTime;
};

//==========================================================================
//
// OpenGLFrameBuffer :: WipeStartScreen
//
// Called before the current screen has started rendering. This needs to
// save what was drawn the previous frame so that it can be animated into
// what gets drawn this frame.
//
//==========================================================================

bool OpenGLFrameBuffer::WipeStartScreen(int type)
{
	switch (type)
	{
	case wipe_Burn:
		ScreenWipe = new Wiper_Burn;
		break;

	case wipe_Fade:
		ScreenWipe = new Wiper_Crossfade;
		break;

	case wipe_Melt:
		ScreenWipe = new Wiper_Melt;
		break;

	default:
		return false;
	}

	wipestartscreen = new FHardwareTexture(Width, Height, true);
	wipestartscreen->CreateTexture(NULL, Width, Height, 0, false, 0);
	GLRenderer->mSamplerManager->Bind(0, CLAMP_NOFILTER);
	GLRenderer->mSamplerManager->Bind(1, CLAMP_NONE);
	glFinish();
	wipestartscreen->Bind(0, false, false);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, Width, Height);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	return true;
}

//==========================================================================
//
// OpenGLFrameBuffer :: WipeEndScreen
//
// The screen we want to animate to has just been drawn.
//
//==========================================================================

void OpenGLFrameBuffer::WipeEndScreen()
{
	wipeendscreen = new FHardwareTexture(Width, Height, true);
	wipeendscreen->CreateTexture(NULL, Width, Height, 0, false, 0);
	GLRenderer->mSamplerManager->Bind(0, CLAMP_NOFILTER);
	glFinish();
	wipeendscreen->Bind(0, false, false);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, Width, Height);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	Unlock();
}

//==========================================================================
//
// OpenGLFrameBuffer :: WipeDo
//
// Perform the actual wipe animation. The number of tics since the last
// time this function was called is passed in. Returns true when the wipe
// is over. The first time this function has been called, the screen is
// still locked from before and EndScene() still has not been called.
// Successive times need to call BeginScene().
//
//==========================================================================

bool OpenGLFrameBuffer::WipeDo(int ticks)
{
	// Sanity checks.
	if (wipestartscreen == NULL || wipeendscreen == NULL)
	{
		return true;
	}

	Lock(true);
	
	gl_RenderState.EnableTexture(true);
	gl_RenderState.EnableFog(false);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(false);

	bool done = ScreenWipe->Run(ticks, this);
	glDepthMask(true);
	//DrawLetterbox();
	return done;
}

//==========================================================================
//
// OpenGLFrameBuffer :: WipeCleanup
//
// Release any resources that were specifically created for the wipe.
//
//==========================================================================

void OpenGLFrameBuffer::WipeCleanup()
{
	if (ScreenWipe != NULL)
	{
		delete ScreenWipe;
		ScreenWipe = NULL;
	}
	if (wipestartscreen != NULL)
	{
		delete wipestartscreen;
		wipestartscreen = NULL;
	}
	if (wipeendscreen != NULL)
	{
		delete wipeendscreen;
		wipeendscreen = NULL;
	}
	gl_ClearLastMaterial();
}

//==========================================================================
//
// OpenGLFrameBuffer :: Wiper Constructor
//
//==========================================================================

OpenGLFrameBuffer::Wiper::~Wiper()
{
}

// WIPE: CROSSFADE ---------------------------------------------------------

//==========================================================================
//
// OpenGLFrameBuffer :: Wiper_Crossfade Constructor
//
//==========================================================================

OpenGLFrameBuffer::Wiper_Crossfade::Wiper_Crossfade()
: Clock(0)
{
}

//==========================================================================
//
// OpenGLFrameBuffer :: Wiper_Crossfade :: Run
//
// Fades the old screen into the new one over 32 ticks.
//
//==========================================================================

bool OpenGLFrameBuffer::Wiper_Crossfade::Run(int ticks, OpenGLFrameBuffer *fb)
{
	Clock += ticks;

	float ur = fb->GetWidth() / FHardwareTexture::GetTexDimension(fb->GetWidth());
	float vb = fb->GetHeight() / FHardwareTexture::GetTexDimension(fb->GetHeight());

	gl_RenderState.SetTextureMode(TM_OPAQUE);
	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
	gl_RenderState.ResetColor();
	gl_RenderState.Apply();
	fb->wipestartscreen->Bind(0, 0, false);

	FFlatVertex *ptr;
	unsigned int offset, count;
	ptr = GLRenderer->mVBO->GetBuffer();
	ptr->Set(0, 0, 0, 0, vb);
	ptr++;
	ptr->Set(0, fb->Height, 0, 0, 0);
	ptr++;
	ptr->Set(fb->Width, 0, 0, ur, vb);
	ptr++;
	ptr->Set(fb->Width, fb->Height, 0, ur, 0);
	ptr++;
	GLRenderer->mVBO->RenderCurrent(ptr, GL_TRIANGLE_STRIP, &offset, &count);

	fb->wipeendscreen->Bind(0, 0, false);
	gl_RenderState.SetColorAlpha(0xffffff, zx::wipe::ComputeCrossfadeAlpha(Clock));
	gl_RenderState.Apply();
	GLRenderer->mVBO->RenderArray(GL_TRIANGLE_STRIP, offset, count);
	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.5f);
	gl_RenderState.SetTextureMode(TM_MODULATE);

	return Clock >= 32;
}

//==========================================================================
//
// OpenGLFrameBuffer :: Wiper_Melt Constructor
//
//==========================================================================

OpenGLFrameBuffer::Wiper_Melt::Wiper_Melt()
{
	// [rc4l] One random byte a column, drawn in the order the columns were.
	unsigned char randoms[zx::wipe::kMeltColumns];
	for (int i = 0; i < zx::wipe::kMeltColumns; ++i) randoms[i] = (unsigned char)M_Random();
	zx::wipe::ComputeMeltStart(randoms, y);
}

//==========================================================================
//
// OpenGLFrameBuffer :: Wiper_Melt :: Run
//
// Fades the old screen into the new one over 32 ticks.
//
//==========================================================================

bool OpenGLFrameBuffer::Wiper_Melt::Run(int ticks, OpenGLFrameBuffer *fb)
{
	float ur = fb->GetWidth() / FHardwareTexture::GetTexDimension(fb->GetWidth());
	float vb = fb->GetHeight() / FHardwareTexture::GetTexDimension(fb->GetHeight());

	// Draw the new screen on the bottom.
	gl_RenderState.SetTextureMode(TM_OPAQUE);
	gl_RenderState.ResetColor();
	gl_RenderState.Apply();
	fb->wipeendscreen->Bind(0, 0, false);
	FFlatVertex *ptr;
	ptr = GLRenderer->mVBO->GetBuffer();
	ptr->Set(0, 0, 0, 0, vb);
	ptr++;
	ptr->Set(0, fb->Height, 0, 0, 0);
	ptr++;
	ptr->Set(fb->Width, 0, 0, ur, vb);
	ptr++;
	ptr->Set(fb->Width, fb->Height, 0, ur, 0);
	ptr++;
	GLRenderer->mVBO->RenderCurrent(ptr, GL_TRIANGLE_STRIP);

	bool done = false;
	const bool ticked = ticks > 0;

	fb->wipestartscreen->Bind(0, 0, false);
	// [rc4l] features/wipe moves the columns; only the final tick is drawn, as before.
	while (ticks--) done = zx::wipe::ComputeMeltTic(y);
	// Copy the old screen in vertical strips on top of the new one.
	for (int i = 0; ticked && i < zx::wipe::kMeltColumns; i++)
	{
		zx::wipe::MeltStrip strip;
		if (!zx::wipe::ComputeMeltStrip(i, y[i], fb->Width, fb->Height, strip)) continue;
		// [rc4l] The strip in GL's rows, which count up from the bottom of the screen texture.
		const float tw = (float)FHardwareTexture::GetTexDimension(fb->Width);
		const float th = (float)FHardwareTexture::GetTexDimension(fb->Height);
		const float left = strip.x, right = strip.x + strip.w, bottom = strip.y, top = (float)fb->Height;

		ptr = GLRenderer->mVBO->GetBuffer();
		ptr->Set(left, bottom, 0, left / tw, top / th);
		ptr++;
		ptr->Set(left, top, 0, left / tw, bottom / th);
		ptr++;
		ptr->Set(right, bottom, 0, right / tw, top / th);
		ptr++;
		ptr->Set(right, top, 0, right / tw, bottom / th);
		ptr++;
		GLRenderer->mVBO->RenderCurrent(ptr, GL_TRIANGLE_STRIP);
	}
	gl_RenderState.SetTextureMode(TM_MODULATE);
	return done;
}

//==========================================================================
//
// OpenGLFrameBuffer :: Wiper_Burn Constructor
//
//==========================================================================

OpenGLFrameBuffer::Wiper_Burn::Wiper_Burn()
{
	Density = 4;
	BurnTime = 0;
	memset(BurnArray, 0, sizeof(BurnArray));
	BurnTexture = NULL;
}

//==========================================================================
//
// OpenGLFrameBuffer :: Wiper_Burn Destructor
//
//==========================================================================

OpenGLFrameBuffer::Wiper_Burn::~Wiper_Burn()
{
	if (BurnTexture != NULL)
	{
		delete BurnTexture;
		BurnTexture = NULL;
	}
}

//==========================================================================
//
// OpenGLFrameBuffer :: Wiper_Burn :: Run
//
//==========================================================================

bool OpenGLFrameBuffer::Wiper_Burn::Run(int ticks, OpenGLFrameBuffer *fb)
{
	bool done;

	BurnTime += ticks;
	ticks *= 2;

	// Make the fire burn
	done = false;
	while (!done && ticks--)
	{
		Density = wipe_CalcBurn(BurnArray, WIDTH, HEIGHT, Density);
		done = (Density < 0);
	}

	if (BurnTexture != NULL) delete BurnTexture;
	BurnTexture = new FHardwareTexture(WIDTH, HEIGHT, true);

	// Update the burn texture with the new burn data
	BYTE rgb_buffer[WIDTH*HEIGHT*4];

	zx::wipe::ComputeBurnMask(BurnArray, WIDTH * HEIGHT, BurnMask);
	DWORD *dest = (DWORD *)rgb_buffer;
	for (int i = 0; i < WIDTH * HEIGHT; i++) *dest++ = MAKEARGB(BurnMask[i], 255, 255, 255);

	float ur = fb->GetWidth() / FHardwareTexture::GetTexDimension(fb->GetWidth());
	float vb = fb->GetHeight() / FHardwareTexture::GetTexDimension(fb->GetHeight());


	// Put the initial screen back to the buffer.
	gl_RenderState.SetTextureMode(TM_OPAQUE);
	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
	gl_RenderState.ResetColor();
	gl_RenderState.Apply();
	fb->wipestartscreen->Bind(0, 0, false);
	FFlatVertex *ptr;
	unsigned int offset, count;
	ptr = GLRenderer->mVBO->GetBuffer();
	ptr->Set(0, 0, 0, 0, vb);
	ptr++;
	ptr->Set(0, fb->Height, 0, 0, 0);
	ptr++;
	ptr->Set(fb->Width, 0, 0, ur, vb);
	ptr++;
	ptr->Set(fb->Width, fb->Height, 0, ur, 0);
	ptr++;
	GLRenderer->mVBO->RenderCurrent(ptr, GL_TRIANGLE_STRIP, &offset, &count);

	gl_RenderState.SetTextureMode(TM_MODULATE);
	gl_RenderState.SetEffect(EFF_BURN);
	gl_RenderState.ResetColor();
	gl_RenderState.Apply();

	// Burn the new screen on top of it.
	fb->wipeendscreen->Bind(0, 0, false);

	BurnTexture->CreateTexture(rgb_buffer, WIDTH, HEIGHT, 1, true, 0);

	GLRenderer->mVBO->RenderArray(GL_TRIANGLE_STRIP, offset, count);
	gl_RenderState.SetEffect(EFF_NONE);

	// The fire may not always stabilize, so the wipe is forced to end
	// after an arbitrary maximum time.
	return done || (BurnTime > 40);
}
