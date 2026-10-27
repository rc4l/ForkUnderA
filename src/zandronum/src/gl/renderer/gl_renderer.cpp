/*
** gl1_renderer.cpp
** Renderer interface
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
#include "m_swap.h"
#include "v_video.h"
#include "r_data/r_translate.h"
#include "m_png.h"
#include "m_crc32.h"
#include "w_wad.h"
//#include "gl/gl_intern.h"
#include "gl/gl_functions.h"
#include "vectors.h"

#include "gl/system/gl_interface.h"
#include "gl/system/gl_framebuffer.h"
#include "gl/system/gl_cvars.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/renderer/gl_lightdata.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/data/gl_data.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/scene/gl_drawinfo.h"
#include "features/draw2d/draw2d.h"   // [rc4l] the 2D draws, worked out without GL
#include "gl/shaders/gl_shader.h"
#include "features/materials/textureinfo.h"
#include "features/materials/translationpalette.h"
#include "gl/textures/gl_material.h"
#include "features/hwshaders/computation/hwshader_compute.h"   // [Doruk-Aksoy] ComputeDrawTextureShader
#include "gl/textures/gl_samplers.h"
#include "gl/utility/gl_clock.h"
#include "gl/utility/gl_templates.h"
#include "gl/models/gl_models.h"
#include "gl/dynlights/gl_lightbuffer.h"

//===========================================================================
// 
// Renderer interface
//
//===========================================================================

//-----------------------------------------------------------------------------
//
// Initialize
//
//-----------------------------------------------------------------------------

FGLRenderer::FGLRenderer(OpenGLFrameBuffer *fb) 
{
	framebuffer = fb;
	mCurrentPortal = NULL;
	mMirrorCount = 0;
	mPlaneMirrorCount = 0;
	mLightCount = 0;
	mAngles = FRotator(0,0,0);
	mViewVector = FVector2(0,0);
	mVBO = NULL;
	mSkyVBO = NULL;
	gl_spriteindex = 0;
	mShaderManager = NULL;
	mLights = NULL;
}

void gl_LoadModels();
void gl_FlushModels();

void FGLRenderer::Initialize()
{
	mVBO = new FFlatVertexBuffer;
	mSkyVBO = new FSkyVertexBuffer;
	mLights = new FLightBuffer();
	gl_RenderState.SetVertexBuffer(mVBO);
	mFBID = 0;
	mOutputFB = 0; // [rc4l] video-scale
	SetupLevel();
	mShaderManager = new FShaderManager;
	mSamplerManager = new FSamplerManager;
	gl_LoadModels();
}

FGLRenderer::~FGLRenderer() 
{
	gl_FlushModels();
	gl_DeleteAllAttachedLights();
	FMaterial::FlushAll();
	if (mShaderManager != NULL) delete mShaderManager;
	if (mSamplerManager != NULL) delete mSamplerManager;
	if (mVBO != NULL) delete mVBO;
	if (mSkyVBO != NULL) delete mSkyVBO;
	if (mLights != NULL) delete mLights;
	if (mFBID != 0) glDeleteFramebuffers(1, &mFBID);
}

//===========================================================================
// 
//
//
//===========================================================================

void FGLRenderer::SetupLevel()
{
	mVBO->CreateVBO();
}

void FGLRenderer::Begin2D()
{
	gl_RenderState.EnableFog(false);
	gl_RenderState.Set2DMode(true);
}

//===========================================================================
// 
//
//
//===========================================================================

void FGLRenderer::ProcessLowerMiniseg(seg_t *seg, sector_t * frontsector, sector_t * backsector)
{
	GLWall wall;
	wall.ProcessLowerMiniseg(seg, frontsector, backsector);
	rendered_lines++;
}

//===========================================================================
// 
//
//
//===========================================================================

void FGLRenderer::ProcessSprite(AActor *thing, sector_t *sector)
{
	GLSprite glsprite;
	glsprite.Process(thing, sector);
}

//===========================================================================
// 
//
//
//===========================================================================

void FGLRenderer::ProcessParticle(particle_t *part, sector_t *sector)
{
	GLSprite glsprite;
	glsprite.ProcessParticle(part, sector);//, 0, 0);
}

//===========================================================================
// 
//
//
//===========================================================================

void FGLRenderer::ProcessSector(sector_t *fakesector)
{
	GLFlat glflat;
	glflat.ProcessSector(fakesector);
}

//===========================================================================
// 
//
//
//===========================================================================



//===========================================================================
// 
//
//
//===========================================================================

bool FGLRenderer::StartOffscreen()
{
	if (mFBID == 0) glGenFramebuffers(1, &mFBID);
	glBindFramebuffer(GL_FRAMEBUFFER, mFBID);
	return true;
}

//===========================================================================
// 
//
//
//===========================================================================

void FGLRenderer::EndOffscreen()
{
	// [rc4l] video-scale: restore the active screen target (backbuffer, or the scale FBO when
	// internal-resolution scaling is on) rather than always the backbuffer.
	glBindFramebuffer(GL_FRAMEBUFFER, mOutputFB);
}

//===========================================================================
// 
//
//
//===========================================================================



//===========================================================================
// 
//
//
//===========================================================================

void FGLRenderer::ClearBorders()
{
	OpenGLFrameBuffer *glscreen = static_cast<OpenGLFrameBuffer*>(screen);

	// Letterbox time! Draw black top and bottom borders.
	int width = glscreen->GetWidth();
	int height = glscreen->GetHeight();
	int trueHeight = glscreen->GetTrueHeight();

	int borderHeight = (trueHeight - height) / 2;

	glViewport(0, 0, width, trueHeight);
	gl_RenderState.mProjectionMatrix.loadIdentity();
	gl_RenderState.mProjectionMatrix.ortho(0.0f, width * 1.0f, 0.0f, trueHeight, -1.0f, 1.0f);
	gl_RenderState.SetColor(0.f ,0.f ,0.f ,1.f);
	gl_RenderState.Set2DMode(true);
	gl_RenderState.EnableTexture(false);
	gl_RenderState.Apply();
	gl_RenderState.ApplyMatrices(); 

	FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
	ptr->Set(0, borderHeight, 0, 0, 0); ptr++;
	ptr->Set(0, 0, 0, 0, 0); ptr++;
	ptr->Set(width, 0, 0, 0, 0); ptr++;
	ptr->Set(width, borderHeight, 0, 0, 0); ptr++;
	GLRenderer->mVBO->RenderCurrent(ptr, GL_TRIANGLE_STRIP);
	ptr->Set(0, trueHeight, 0, 0, 0); ptr++;
	ptr->Set(0, trueHeight - borderHeight, 0, 0, 0); ptr++;
	ptr->Set(width, trueHeight - borderHeight, 0, 0, 0); ptr++;
	ptr->Set(width, trueHeight, 0, 0, 0); ptr++;
	GLRenderer->mVBO->RenderCurrent(ptr, GL_TRIANGLE_STRIP);
	gl_RenderState.EnableTexture(true);

	glViewport(0, (trueHeight - height) / 2, width, height); 
}

//==========================================================================
//
// Draws a texture
//
//==========================================================================

void FGLRenderer::DrawTexture(FTexture *img, DCanvas::DrawParms &parms)
{
	// [rc4l] features/draw2d works the draw out, the quad GL draws and the backend records alike.
	zx::draw2d::TextureDraw d;
	zx::draw2d::PrepareTexture(img, parms, d);
	FMaterial *gltex = (FMaterial *)d.quad.material;
	const float x = d.quad.x, y = d.quad.y, x2 = d.x2, y2 = d.y2;
	const float u1 = d.quad.u1, v1 = d.quad.v1, u2 = d.quad.u2, v2 = d.quad.v2;
	parms.colorOverlay = d.colorOverlay;

	gl_SetRenderStyle(parms.style, !parms.masked, false);
	if (!img->bHasCanvas)
	{
		// [rc4l] GL applies the remap in the shader from a palette texture, so it takes the index negated.
		// [Doruk-Aksoy] A GLDEFS shader runs on a HUD image too, as Zandronum 3.3's BindPatch ran it.
		gl_RenderState.SetMaterial(gltex, CLAMP_XY_NOMIP, -d.translation, zx::hwshaders::ComputeDrawTextureShader(gltex->GetShaderIndex()),
			!!(parms.style.Flags & STYLEF_RedIsAlpha));
	}
	else
	{
		gl_RenderState.SetMaterial(gltex, CLAMP_XY_NOMIP, 0, -1, false);
		gl_RenderState.SetTextureMode(TM_OPAQUE);
	}

	PalEntry color(d.a, d.r, d.g, d.b);

	// scissor test doesn't use the current viewport for the coordinates, so use real screen coordinates
	int btm = (SCREENHEIGHT - screen->GetHeight()) / 2;
	btm = SCREENHEIGHT - btm;

	glEnable(GL_SCISSOR_TEST);
	int space = (static_cast<OpenGLFrameBuffer*>(screen)->GetTrueHeight()-screen->GetHeight())/2;
	glScissor(parms.lclip, btm - parms.dclip + space, parms.rclip - parms.lclip, parms.dclip - parms.uclip);
	
	gl_RenderState.SetColor(color);
	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
	gl_RenderState.Apply();

	FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
	ptr->Set(x, y, 0, u1, v1); ptr++;
	ptr->Set(x, y2, 0, u1, v2); ptr++;
	ptr->Set(x2, y, 0, u2, v1); ptr++;
	ptr->Set(x2, y2, 0, u2, v2); ptr++;
	GLRenderer->mVBO->RenderCurrent(ptr, GL_TRIANGLE_STRIP);

	if (parms.colorOverlay)
	{
		gl_RenderState.SetTextureMode(TM_MASK);
		gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		gl_RenderState.BlendEquation(GL_FUNC_ADD);
		gl_RenderState.SetColor(PalEntry(parms.colorOverlay));
		gl_RenderState.Apply();

		FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
		ptr->Set(x, y, 0, u1, v1); ptr++;
		ptr->Set(x, y2, 0, u1, v2); ptr++;
		ptr->Set(x2, y, 0, u2, v1); ptr++;
		ptr->Set(x2, y2, 0, u2, v2); ptr++;
		GLRenderer->mVBO->RenderCurrent(ptr, GL_TRIANGLE_STRIP);
	}

	glScissor(0, 0, screen->GetWidth(), screen->GetHeight());
	glDisable(GL_SCISSOR_TEST);
	gl_RenderState.SetTextureMode(TM_MODULATE);
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_RenderState.BlendEquation(GL_FUNC_ADD);
}

//==========================================================================
//
//
//
//==========================================================================
void FGLRenderer::DrawLine(int x1, int y1, int x2, int y2, int palcolor, uint32 color)
{
	const PalEntry p = zx::draw2d::DrawColor(palcolor, color);
	gl_RenderState.EnableTexture(false);
	gl_RenderState.SetColorAlpha(p, 1.f);
	gl_RenderState.Apply();

	FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
	ptr->Set(x1, y1, 0, 0, 0); ptr++;
	ptr->Set(x2, y2, 0, 0, 0); ptr++;
	GLRenderer->mVBO->RenderCurrent(ptr, GL_LINES);

	gl_RenderState.EnableTexture(true);
}

//==========================================================================
//
//
//
//==========================================================================
void FGLRenderer::DrawPixel(int x1, int y1, int palcolor, uint32 color)
{
	const PalEntry p = zx::draw2d::DrawColor(palcolor, color);
	gl_RenderState.EnableTexture(false);
	gl_RenderState.SetColorAlpha(p, 1.f);
	gl_RenderState.Apply();

	FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
	ptr->Set(x1, y1, 0, 0, 0); ptr++;
	GLRenderer->mVBO->RenderCurrent(ptr, GL_POINTS);

	gl_RenderState.EnableTexture(true);
}

//===========================================================================
// 
//
//
//===========================================================================

void FGLRenderer::Dim(PalEntry color, float damount, int x1, int y1, int w, int h)
{
	gl_RenderState.EnableTexture(false);
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_RenderState.AlphaFunc(GL_GREATER,0);
	gl_RenderState.SetColorAlpha(color, damount);
	gl_RenderState.Apply();

	FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
	ptr->Set(x1, y1, 0, 0, 0); ptr++;
	ptr->Set(x1, y1+h, 0, 0, 0); ptr++;
	ptr->Set(x1+w, y1+h, 0, 0, 0); ptr++;
	ptr->Set(x1+w, y1, 0, 0, 0); ptr++;
	GLRenderer->mVBO->RenderCurrent(ptr, GL_TRIANGLE_FAN);

	gl_RenderState.EnableTexture(true);
}

//==========================================================================
//
//
//
//==========================================================================
void FGLRenderer::FlatFill (int left, int top, int right, int bottom, FTexture *src, bool local_origin)
{
	float fU1,fU2,fV1,fV2;

	FMaterial *gltexture=FMaterial::ValidateTexture(src, false);
	
	if (!gltexture) return;

	gl_RenderState.SetMaterial(gltexture, CLAMP_NONE, 0, -1, false);
	
	// scaling is not used here.
	if (!local_origin)
	{
		fU1 = float(left) / src->GetWidth();
		fV1 = float(top) / src->GetHeight();
		fU2 = float(right) / src->GetWidth();
		fV2 = float(bottom) / src->GetHeight();
	}
	else
	{		
		fU1 = 0;
		fV1 = 0;
		fU2 = float(right-left) / src->GetWidth();
		fV2 = float(bottom-top) / src->GetHeight();
	}
	gl_RenderState.ResetColor();
	gl_RenderState.Apply();

	FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
	ptr->Set(left, top, 0, fU1, fV1); ptr++;
	ptr->Set(left, bottom, 0, fU1, fV2); ptr++;
	ptr->Set(right, top, 0, fU2, fV1); ptr++;
	ptr->Set(right, bottom, 0, fU2, fV2); ptr++;
	GLRenderer->mVBO->RenderCurrent(ptr, GL_TRIANGLE_STRIP);
}

//==========================================================================
//
//
//
//==========================================================================
void FGLRenderer::Clear(int left, int top, int right, int bottom, int palcolor, uint32 color)
{
	int rt;
	int offY = 0;
	const PalEntry p = zx::draw2d::DrawColor(palcolor, color);
	int width = right-left;
	int height= bottom-top;
	
	
	rt = screen->GetHeight() - top;
	
	int space = (static_cast<OpenGLFrameBuffer*>(screen)->GetTrueHeight()-screen->GetHeight())/2;	// ugh...
	rt += space;
	/*
	if (!m_windowed && (m_trueHeight != m_height))
	{
		offY = (m_trueHeight - m_height) / 2;
		rt += offY;
	}
	*/

	glEnable(GL_SCISSOR_TEST);
	glScissor(left, rt - height, width, height);

	glClearColor(p.r/255.0f, p.g/255.0f, p.b/255.0f, 0.f);
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(0.f, 0.f, 0.f, 0.f);
	
	glDisable(GL_SCISSOR_TEST);
}

//==========================================================================
//
// D3DFB :: FillSimplePoly
//
// Here, "simple" means that a simple triangle fan can draw it.
//
//==========================================================================

void FGLRenderer::FillSimplePoly(FTexture *texture, FVector2 *points, int npoints,
	double originx, double originy, double scalex, double scaley,
	angle_t rotation, FDynamicColormap *colormap, int lightlevel)
{
	// [rc4l] features/draw2d works out the points' texture coordinates.
	if (!zx::draw2d::PreparePoly(texture, points, npoints, originx, originy, scalex, scaley, rotation, colormap, lightlevel, mPoly, mPolyTris)) return;
	FMaterial *gltexture = (FMaterial *)mPoly.material;

	FColormap cm;
	cm = colormap;

	// We cannot use the software light mode here because it doesn't properly calculate the light for 2D rendering.
	SBYTE savedlightmode = glset.lightmode;
	if (glset.lightmode == 8) glset.lightmode = 0;

	gl_SetColor(lightlevel, 0, cm, 1.f);

	glset.lightmode = savedlightmode;

	gl_RenderState.SetMaterial(gltexture, CLAMP_NONE, 0, -1, false);
	gl_RenderState.Apply();

	FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
	for (int i = 0; i < npoints; ++i)
	{
		ptr->Set(points[i].X, points[i].Y, 0, mPoly.uv[2 * i], mPoly.uv[2 * i + 1]);
		ptr++;
	}
	GLRenderer->mVBO->RenderCurrent(ptr, GL_TRIANGLE_FAN);
}

