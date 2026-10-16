/*
** gl_scene.cpp
** manages the rendering of the player's view
**
**---------------------------------------------------------------------------
** Copyright 2004-2005 Christoph Oelckers
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
#include "gi.h"
#include "m_png.h"
#include "m_random.h"
#include "st_stuff.h"
#include "dobject.h"
#include "doomstat.h"
#include "g_level.h"
#include "r_data/r_interpolate.h"
#include "r_utility.h"
#include "d_player.h"
#include "p_effect.h"
#include "sbar.h"
#include "po_man.h"
#include "r_utility.h"
#include "a_hexenglobal.h"
#include "p_local.h"
#include "gl/gl_functions.h"

#include "gl/dynlights/gl_lightbuffer.h"
#include "gl/system/gl_interface.h"
#include "gl/system/gl_framebuffer.h"
#include "gl/system/gl_cvars.h"
#include "gl/renderer/gl_lightdata.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/data/gl_data.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/dynlights/gl_dynlight.h"
#include "gl/models/gl_models.h"
#include "gl/scene/gl_clipper.h"
#include "gl/scene/gl_drawinfo.h"
#include "gl/scene/gl_portal.h"
#include "gl/shaders/gl_shader.h"
#include "gl/stereo3d/gl_stereo3d.h"
#include "gl/stereo3d/scoped_view_shifter.h"
#include "gl/textures/gl_material.h"
#include "gl/utility/gl_clock.h"
#include "gl/utility/gl_convert.h"
#include "gl/utility/gl_templates.h"
#include "features/hitboxviz/hitboxviz.h"
#include "c_dispatch.h"   // [rc4l] fua_gl_viewstate
#include "gl/scene/gl_observer.h"   // [rc4l] what GL reports to a check against it
#include "features/viewsetup/viewsetup.h"   // [rc4l] the view, set up without GL
#include "features/draw2d/screenblend.h"   // [rc4l] the screen's tint, worked out without GL
#include "features/viewsetup/computation/viewsetup_compute.h"
#include "features/fua-caching/fua_caching.h"
#include "features/frame/levelrenderer.h"   // [rc4l] FLevelRenderer
#include "features/frame/playerview.h"      // [rc4l] BeginViewpoint, EndViewpoint, MarkMapSection
#include "mcp_glperf.h" // [rc4l] GPU render-pass timer anchors (no-op unless FUA_MCP_BRIDGE)

//==========================================================================
//
// CVARs
//
//==========================================================================
CVAR(Bool, gl_texture, true, 0)
CVAR(Bool, gl_no_skyclear, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
// [BB] Don't allow this in release builds.
CVAR(Float, gl_mask_threshold, 0.5f,CVAR_ARCHIVE|CVAR_GLOBALCONFIG|CVAR_DEBUGONLY)
EXTERN_CVAR(Float, r_mask_sprite_threshold)   // [rc4l] features/rendersettings, which both renderers read
CVAR(Bool, gl_sort_textures, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
EXTERN_CVAR(Bool, gl_batch_walls)  // [rc4l] features/levelmesh wall batching


EXTERN_CVAR (Int, screenblocks)
EXTERN_CVAR (Bool, cl_capfps)
EXTERN_CVAR (Bool, r_deathcamera)


EXTERN_CVAR (Bool, cl_oldfreelooklimit)

extern int viewpitch;
 
FGLObserver		*gl_observer = NULL;


//-----------------------------------------------------------------------------
//
// R_FrustumAngle
//
//-----------------------------------------------------------------------------
angle_t FGLRenderer::FrustumAngle()
{
	// [rc4l] features/viewsetup, which the walks without GL read too.
	return zx::viewsetup::ComputeFrustumAngle(mAngles.Pitch, mCurrentFoV, AspectMultiplier(WidescreenRatio));
}

//-----------------------------------------------------------------------------
//
// Sets the area the camera is in
//
//-----------------------------------------------------------------------------
void FGLRenderer::SetViewArea()
{
	// [rc4l] features/viewsetup places the eye and finds its area, for a frame GL draws or not.
	zx::viewsetup::SetViewArea(mViewActor);
}

//-----------------------------------------------------------------------------
//
// resets the 3D viewport
//
//-----------------------------------------------------------------------------

void FGLRenderer::ResetViewport()
{
	int trueheight = static_cast<OpenGLFrameBuffer*>(screen)->GetTrueHeight();	// ugh...
	glViewport(0, (trueheight-screen->GetHeight())/2, screen->GetWidth(), screen->GetHeight()); 
}

//-----------------------------------------------------------------------------
//
// sets 3D viewport and initial state
//
//-----------------------------------------------------------------------------

void FGLRenderer::SetViewport(GL_IRECT *bounds)
{
	if (!bounds)
	{
		int height, width;

		// Special handling so the view with a visible status bar displays properly

		if (screenblocks >= 10)
		{
			height = SCREENHEIGHT;
			width  = SCREENWIDTH;
		}
		else
		{
			height = (screenblocks*SCREENHEIGHT/10) & ~7;
			width = (screenblocks*SCREENWIDTH/10);
		}

		int trueheight = static_cast<OpenGLFrameBuffer*>(screen)->GetTrueHeight();	// ugh...
		int bars = (trueheight-screen->GetHeight())/2; 

		int vw = viewwidth;
		int vh = viewheight;
		glViewport(viewwindowx, trueheight-bars-(height+viewwindowy-((height-vh)/2)), vw, height);
		glScissor(viewwindowx, trueheight-bars-(vh+viewwindowy), vw, vh);
	}
	else
	{
		glViewport(bounds->left, bounds->top, bounds->width, bounds->height);
		glScissor(bounds->left, bounds->top, bounds->width, bounds->height);
	}
	glEnable(GL_SCISSOR_TEST);
	
	#ifdef _DEBUG
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f); 
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	#else
		glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	#endif

	glEnable(GL_MULTISAMPLE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_ALWAYS,0,~0);	// default stencil
	glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
}

//-----------------------------------------------------------------------------
//
// Setup the camera position
//
//-----------------------------------------------------------------------------

void FGLRenderer::SetViewAngle(angle_t viewangle)
{
	// [rc4l] The yaw and facing as features/viewsetup works them out, so the two cannot drift.
	float yaw, vx, vy;
	zx::viewsetup::ComputeViewAngleVector(viewangle, yaw, vx, vy);
	mAngles.Yaw = yaw;
	mViewVector = FVector2(vx, vy);
	R_SetViewAngle();
}
	

//-----------------------------------------------------------------------------
//
// SetProjection
// sets projection matrix
//
//-----------------------------------------------------------------------------

void FGLRenderer::SetProjection(float fov, float ratio, float fovratio, float eyeShift) // [BB] Added eyeShift from GZ3Doom.
{
	// [rc4l] upstream ce3653f6e: the projection lives in the render state's own
	// matrix now; nothing touches the builtin GL matrix stack anymore.
	float fovy = 2 * RAD2DEG(atan(tan(DEG2RAD(fov) / 2) / fovratio));
	// [BB] Added eyeShift from GZ3Doom.
	if ( eyeShift == 0 )
	{
		gl_RenderState.mProjectionMatrix.loadIdentity();
		gl_RenderState.mProjectionMatrix.perspective(fovy, ratio, 5.f, 65536.f);
	}
	else
	{
		const float zNear = 5.0f;
		const float zFar = 65536.0f;
		const float pi = 3.1415926535897932384626433832795;
		double fH = tan( fovy / 360 * pi ) * zNear;
		double fW = fH * ratio;

		float screenZ = 25.0;
		float frustumShift = eyeShift * zNear / screenZ;

		gl_RenderState.mProjectionMatrix.loadIdentity();
		gl_RenderState.mProjectionMatrix.frustum( -fW - frustumShift, fW - frustumShift,
			-fH, fH,
			zNear, zFar);
		gl_RenderState.mProjectionMatrix.translate(-eyeShift, 0, 0);
	}

	gl_RenderState.Set2DMode(false);
}

// raw matrix input from stereo 3d modes
void FGLRenderer::SetProjection(VSMatrix matrix)
{
	gl_RenderState.mProjectionMatrix.loadIdentity();
	gl_RenderState.mProjectionMatrix.multMatrix(matrix);
	gl_RenderState.Set2DMode(false);
}

//-----------------------------------------------------------------------------
//
// Setup the modelview matrix
//
//-----------------------------------------------------------------------------

void FGLRenderer::SetViewMatrix(fixed_t viewx, fixed_t viewy, fixed_t viewz, bool mirror, bool planemirror)
{
	float mult = mirror? -1:1;
	float planemult = planemirror? -glset.pixelstretch : glset.pixelstretch;

	gl_RenderState.mViewMatrix.loadIdentity();
	gl_RenderState.mViewMatrix.rotate(GLRenderer->mAngles.Roll,  0.0f, 0.0f, 1.0f);
	gl_RenderState.mViewMatrix.rotate(GLRenderer->mAngles.Pitch, 1.0f, 0.0f, 0.0f);
	gl_RenderState.mViewMatrix.rotate(GLRenderer->mAngles.Yaw,   0.0f, mult, 0.0f);
	gl_RenderState.mViewMatrix.translate(FIXED2FLOAT(viewx) * mult, -FIXED2FLOAT(viewz) * planemult , -FIXED2FLOAT(viewy));
	gl_RenderState.mViewMatrix.scale(-mult, planemult, 1);
}


//-----------------------------------------------------------------------------
//
// SetupView
// Setup the view rotation matrix for the given viewpoint
//
//-----------------------------------------------------------------------------
void FGLRenderer::SetupView(fixed_t viewx, fixed_t viewy, fixed_t viewz, angle_t viewangle, bool mirror, bool planemirror)
{
	SetViewAngle(viewangle);
	SetViewMatrix(viewx, viewy, viewz, mirror, planemirror);
	gl_RenderState.ApplyMatrices();
}

//-----------------------------------------------------------------------------
//
// CreateScene
//
// creates the draw lists for the current scene
//
//-----------------------------------------------------------------------------

void FGLRenderer::CreateScene()
{
	// [MGOOOOOO] Debug hitbox overlay: open this scene's geometry range before the BSP walk starts
	// collecting actors. Paired with the Draw() at the end of RenderTranslucent.
	zx::hitboxviz::BeginFrame();

	// [rc4l] A check against GL's walk learns which view this walk's sky surfaces belong to.
	if (gl_observer) gl_observer->WalkBegins();

	// reset the portal manager
	GLPortal::StartFrame();
	PO_LinkToSubsectors();

	ProcessAll.Clock();

	// clip the scene and fill the drawlists
	for(unsigned i=0;i<portals.Size(); i++) portals[i]->glportal = NULL;
	gl_spriteindex=0;
	Bsp.Clock();
	const area_t areaBefore = in_area;
	gl_RenderBSPNode (nodes + numnodes - 1);
	Bsp.Unclock();
	// [rc4l] A check against GL's walk compares now, while the draw info holds it, from the view area GL started with.
	if (gl_observer)
	{
		const area_t areaAfter = in_area;
		in_area = areaBefore;
		gl_observer->PlayerWalkDone();
		in_area = areaAfter;
	}

	// And now the crappy hacks that have to be done to avoid rendering anomalies:

	gl_drawinfo->HandleMissingTextures();	// Missing upper/lower textures
	gl_drawinfo->HandleHackedSubsectors();	// open sector hacks for deep water
	gl_drawinfo->ProcessSectorStacks();		// merge visplanes of sector stacks

	ProcessAll.Unclock();

}

//-----------------------------------------------------------------------------
//
// RenderScene
//
// Draws the current draw lists for the non GLSL renderer
//
//-----------------------------------------------------------------------------

void FGLRenderer::RenderScene(int recursion)
{
	RenderAll.Clock();

	glDepthMask(true);
	if (!gl_no_skyclear) GLPortal::RenderFirstSkyPortal(recursion);

	gl_RenderState.SetCameraPos(FIXED2FLOAT(viewx), FIXED2FLOAT(viewy), FIXED2FLOAT(viewz));

	gl_RenderState.EnableFog(true);
	gl_RenderState.BlendFunc(GL_ONE,GL_ZERO);

	// [rc4l] features/levelmesh wall batching merges only ADJACENT equal-state walls, so the sort is
	// what makes it work at all -- without it the runs are one wall long and batching buys nothing.
	// gl_sort_textures still forces the flat sorts too; batching needs only the wall lists.
	if (gl_sort_textures || gl_batch_walls)
	{
		gl_drawinfo->drawlists[GLDL_PLAINWALLS].SortWalls();
		gl_drawinfo->drawlists[GLDL_MASKEDWALLS].SortWalls();
		gl_drawinfo->drawlists[GLDL_MASKEDWALLSOFS].SortWalls();
	}
	if (gl_sort_textures)
	{
		gl_drawinfo->drawlists[GLDL_PLAINFLATS].SortFlats();
		gl_drawinfo->drawlists[GLDL_MASKEDFLATS].SortFlats();
	}

	// if we don't have a persistently mapped buffer, we have to process all the dynamic lights up front,
	// so that we don't have to do repeated map/unmap calls on the buffer.
	if (mLightCount > 0 && gl_fixedcolormap == CM_DEFAULT && r_lights && !(gl.flags & RFL_BUFFER_STORAGE))
	{
		GLRenderer->mLights->Begin();
		gl_drawinfo->drawlists[GLDL_PLAINWALLS].DrawWalls(GLPASS_LIGHTSONLY);
		gl_drawinfo->drawlists[GLDL_PLAINFLATS].DrawFlats(GLPASS_LIGHTSONLY);
		gl_drawinfo->drawlists[GLDL_MASKEDWALLS].DrawWalls(GLPASS_LIGHTSONLY);
		gl_drawinfo->drawlists[GLDL_MASKEDFLATS].DrawFlats(GLPASS_LIGHTSONLY);
		gl_drawinfo->drawlists[GLDL_MASKEDWALLSOFS].DrawWalls(GLPASS_LIGHTSONLY);
		gl_drawinfo->drawlists[GLDL_TRANSLUCENTBORDER].Draw(GLPASS_LIGHTSONLY);
		gl_drawinfo->drawlists[GLDL_TRANSLUCENT].Draw(GLPASS_LIGHTSONLY, true);
		GLRenderer->mLights->Finish();
	}

	// Part 1: solid geometry. This is set up so that there are no transparent parts
	glDepthFunc(GL_LESS);
	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
	glDisable(GL_POLYGON_OFFSET_FILL);

	int pass;

	// [AK] Take care of gl_texture and ZADF_FORCE_VIDEO_DEFAULTS.
	OVERRIDE_GL_TEXTURE_IF_NECESSARY

	if (mLightCount > 0 && gl_fixedcolormap == CM_DEFAULT && r_lights && (gl.flags & RFL_BUFFER_STORAGE))
	{
		pass = GLPASS_ALL;
	}
	else
	{
		pass = GLPASS_PLAIN;
	}

	gl_RenderState.EnableTexture(gl_texture);
	gl_RenderState.EnableBrightmap(true);
	gl_drawinfo->drawlists[GLDL_PLAINWALLS].DrawWalls(pass);
	gl_drawinfo->drawlists[GLDL_PLAINFLATS].DrawFlats(pass);


	// Part 2: masked geometry. This is set up so that only pixels with alpha>gl_mask_threshold will show
	if (!gl_texture) 
	{
		gl_RenderState.EnableTexture(true);
		gl_RenderState.SetTextureMode(TM_MASK);
	}
	gl_RenderState.AlphaFunc(GL_GEQUAL, gl_mask_threshold);
	gl_drawinfo->drawlists[GLDL_MASKEDWALLS].DrawWalls(pass);
	gl_drawinfo->drawlists[GLDL_MASKEDFLATS].DrawFlats(pass);

	// Part 3: masked geometry with polygon offset. This list is empty most of the time so only waste time on it when in use.
	if (gl_drawinfo->drawlists[GLDL_MASKEDWALLSOFS].Size() > 0)
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -128.0f);
		gl_drawinfo->drawlists[GLDL_MASKEDWALLSOFS].DrawWalls(pass);
		glDisable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(0, 0);
	}

	gl_drawinfo->drawlists[GLDL_MODELS].Draw(pass);

	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Part 4: Draw decals (not a real pass)
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0f, -128.0f);
	glDepthMask(false);

	// this is the only geometry type on which decals can possibly appear
	gl_drawinfo->drawlists[GLDL_PLAINWALLS].DrawDecals();

	gl_RenderState.SetTextureMode(TM_MODULATE);

	glDepthMask(true);


	// Push bleeding floor/ceiling textures back a little in the z-buffer
	// so they don't interfere with overlapping mid textures.
	glPolygonOffset(1.0f, 128.0f);

	// Part 5: flood all the gaps with the back sector's flat texture
	// This will always be drawn like GLDL_PLAIN, depending on the fog settings

	// [BB] We may only do this when drawing the final eye.
	GLint drawBuffer;
	glGetIntegerv ( GL_DRAW_BUFFER, &drawBuffer );
	if ( drawBuffer != GL_BACK_LEFT )
	{
		glDepthMask(false);							// don't write to Z-buffer!
		gl_RenderState.EnableFog(true);
		gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
		gl_RenderState.BlendFunc(GL_ONE,GL_ZERO);
		gl_drawinfo->DrawUnhandledMissingTextures();
	}
	glDepthMask(true);

	glPolygonOffset(0.0f, 0.0f);
	glDisable(GL_POLYGON_OFFSET_FILL);
	RenderAll.Unclock();

}

//-----------------------------------------------------------------------------
//
// RenderTranslucent
//
// Draws the current draw lists for the non GLSL renderer
//
//-----------------------------------------------------------------------------

void FGLRenderer::RenderTranslucent()
{
	RenderAll.Clock();

	glDepthMask(false);
	gl_RenderState.SetCameraPos(FIXED2FLOAT(viewx), FIXED2FLOAT(viewy), FIXED2FLOAT(viewz));

	// final pass: translucent stuff
	gl_RenderState.AlphaFunc(GL_GEQUAL, r_mask_sprite_threshold);
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	gl_RenderState.EnableBrightmap(true);
	gl_drawinfo->drawlists[GLDL_TRANSLUCENTBORDER].Draw(GLPASS_TRANSLUCENT);
	gl_drawinfo->drawlists[GLDL_TRANSLUCENT].DrawSorted();
	gl_RenderState.EnableBrightmap(false);

	glDepthMask(true);

	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.5f);

	// [MGOOOOOO] Debug hitbox overlay, drawn last: the 3D projection is still current and the depth
	// buffer is writable again, so boxes are correctly occluded by world geometry. Closes the
	// geometry range opened by BeginFrame in CreateScene.
	zx::hitboxviz::Draw();

	RenderAll.Unclock();
}


//-----------------------------------------------------------------------------
//
// gl_drawscene - this function renders the scene from the current
// viewpoint, including mirrors and skyboxes and other portals
// It is assumed that the GLPortal::EndFrame returns with the 
// stencil, z-buffer and the projection matrix intact!
//
//-----------------------------------------------------------------------------
EXTERN_CVAR(Bool, gl_batch_walls)
EXTERN_CVAR(Bool, gl_draw_sync)
void FGLRenderer::DrawScene(bool toscreen)
{
	static int recursion=0;
	// [rc4l] Which scene a check against GL is told about: this one is outermost only when nothing encloses it.
	const FGLObservedScene nest;

	CreateScene();
	GLRenderer->mCurrentPortal = NULL;	// this must be reset before any portal recursion takes place.

	// Up to this point in the main draw call no rendering is performed so we can wait
	// with swapping the render buffer until now.
	if (!gl_draw_sync && toscreen)
	{
		All.Unclock();
		static_cast<OpenGLFrameBuffer*>(screen)->Swap();
		All.Clock();
	}
	MCP_GLPerf_ZoneBegin(MCP_GLZ_SCENE); // [rc4l] GPU-time the opaque BSP pass (no-op unless bridge+capture)
	RenderScene(recursion);
	MCP_GLPerf_ZoneEnd(MCP_GLZ_SCENE);

	// Handle all portals after rendering the opaque objects but before
	// doing all translucent stuff
	recursion++;
	GLPortal::EndFrame();
	recursion--;
	// [rc4l] GL has walked its stacked views and skyboxes now, for a check against GL's walk.
	if (gl_observer) gl_observer->PortalViewsDone();
	MCP_GLPerf_ZoneBegin(MCP_GLZ_TRANSLUCENT); // [rc4l] GPU-time the translucent/overdraw pass
	RenderTranslucent();
	MCP_GLPerf_ZoneEnd(MCP_GLZ_TRANSLUCENT);
}


// [rc4l] features/hwrender: record what FillScreen is about to draw, so a foreign backend gets the
// same tint without reimplementing any of the logic above it.
//
// The screen blend is not a nicety -- it is every pickup, every hit taken, berserk, a radiation suit
// and invulnerability. Without it the Vulkan view was missing the red flash entirely, which measured
// as 96.8% of the frame differing from GL and looked, in motion, like damage simply not registering.
//
// It goes into the 2D quad list rather than anywhere new because the ordering falls out for free:
// DrawBlend runs after the weapon sprite and before the status bar, so a quad recorded here lands
// between them, exactly where GL puts it.
static void FillScreen()
{
	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
	gl_RenderState.EnableTexture(false);
	gl_RenderState.Apply();
	FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
	ptr->Set(0, 0, 0, 0, 0);
	ptr++;
	ptr->Set(0, (float)SCREENHEIGHT, 0, 0, 0);
	ptr++;
	ptr->Set((float)SCREENWIDTH, 0, 0, 0, 0);
	ptr++;
	ptr->Set((float)SCREENWIDTH, (float)SCREENHEIGHT, 0, 0, 0);
	ptr++;
	GLRenderer->mVBO->RenderCurrent(ptr, GL_TRIANGLE_STRIP);
}

// [rc4l] features/draw2d spells the view areas as GL does.
static_assert(area_normal == zx::draw2d::kViewAreaNormal && area_below == zx::draw2d::kViewAreaBelow && area_above == zx::draw2d::kViewAreaAbove,
	"area_t must match screenblend.h");

//==========================================================================
//
// Draws a blend over the entire view
//
//==========================================================================

void FGLRenderer::DrawBlend(sector_t * viewsector)
{
	// [rc4l] features/draw2d works the tint out without GL, and GL fills each part.
	zx::draw2d::ScreenFill fills[2];
	const int n = zx::draw2d::ScreenBlends(viewsector, in_area, gl_fixedcolormap != 0, framebuffer->Colors.brightness, fills);
	for (int i = 0; i < n; i++)
	{
		const zx::draw2d::ScreenFill &f = fills[i];
		if (f.multiply) gl_RenderState.BlendFunc(GL_DST_COLOR, GL_ZERO);
		else gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		gl_RenderState.SetColor(f.r, f.g, f.b, f.a);
		FillScreen();
	}
}


//-----------------------------------------------------------------------------
//
// Draws player sprites and color blend
//
//-----------------------------------------------------------------------------


void FGLRenderer::EndDrawScene(sector_t * viewsector)
{
	gl_RenderState.EnableFog(false);

	// [BB] HUD models need to be rendered here. Make sure that
	// DrawPlayerSprites is only called once. Either to draw
	// HUD models or to draw the weapon sprites.
	const bool renderHUDModel = gl_IsHUDModelForPlayerAvailable( players[consoleplayer].camera->player );
	if ( renderHUDModel )
	{
		// [BB] The HUD model should be drawn over everything else already drawn.
		glClear(GL_DEPTH_BUFFER_BIT);
		DrawPlayerSprites (viewsector, true);
	}

	glDisable(GL_STENCIL_TEST);

	framebuffer->Begin2D(false);

	ResetViewport();
	// [BB] Only draw the sprites if we didn't render a HUD model before.
	if ( renderHUDModel == false )
	{
		DrawPlayerSprites (viewsector, false);
	}
	gl_RenderState.SetFixedColormap(CM_DEFAULT);
	gl_RenderState.SetSoftLightLevel(-1);
	DrawTargeterSprites();
	DrawBlend(viewsector);

	// Restore standard rendering state
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_RenderState.ResetColor();
	gl_RenderState.EnableTexture(true);
	glDisable(GL_SCISSOR_TEST);
}


//-----------------------------------------------------------------------------
//
// R_RenderView - renders one view - either the screen or a camera texture
//
//-----------------------------------------------------------------------------

void FGLRenderer::ProcessScene(bool toscreen)
{
	FDrawInfo::StartDrawInfo();
	iter_dlightf = iter_dlight = draw_dlight = draw_dlightf = 0;
	GLPortal::BeginScene();

	zx::frame::MarkMapSection();   // [rc4l] shared with a frame no GL renderer draws
	DrawScene(toscreen);
	FDrawInfo::EndDrawInfo();

}

//-----------------------------------------------------------------------------
//
// gl_SetFixedColormap
//
//-----------------------------------------------------------------------------

void FGLRenderer::SetFixedColormap (player_t *player)
{
	// [rc4l] features/viewsetup works the fixed colormap out, for a frame GL draws or not.
	gl_fixedcolormap = zx::viewsetup::FixedColormap(player);
	gl_RenderState.SetFixedColormap(gl_fixedcolormap);
}

//-----------------------------------------------------------------------------
//
// Renders one viewpoint in a scene
//
//-----------------------------------------------------------------------------

extern bool CLIENTDEMO_IsInFreeSpectateMode(); // [AK]

// [rc4l] fua_gl_viewstate -- the GL state the next main view draws the world with, for a window that shows 2D but no world.
static bool g_viewStateArmed = false;
CCMD(fua_gl_viewstate) { g_viewStateArmed = true; Printf("recording the next main view's GL state\n"); }

// [rc4l] Armed by the probe below, so the same view also reports how its drawing went.
static bool g_viewErrorsArmed = false;

static void PrintViewErrorsIfArmed()
{
	if (!g_viewErrorsArmed) return;
	g_viewErrorsArmed = false;
	GLint arrayBuf = 0, vao = 0, program = 0;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuf);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	Printf("after the world: GL error 0x%x, array buffer %d, vertex array %d, program %d\n", glGetError(), arrayBuf, vao, program);
}

static void PrintViewStateIfArmed()
{
	if (!g_viewStateArmed) return;
	g_viewStateArmed = false;
	g_viewErrorsArmed = true;
	Printf("before the world: GL error 0x%x\n", glGetError());
	GLint vp[4], sc[4], fb = 0, rfb = 0, drawBuf = 0;
	GLboolean cm[4], scissorOn, depthOn, stencilOn;
	glGetIntegerv(GL_VIEWPORT, vp);
	glGetIntegerv(GL_SCISSOR_BOX, sc);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rfb);
	glGetIntegerv(GL_DRAW_BUFFER, &drawBuf);
	glGetBooleanv(GL_COLOR_WRITEMASK, cm);
	scissorOn = glIsEnabled(GL_SCISSOR_TEST); depthOn = glIsEnabled(GL_DEPTH_TEST); stencilOn = glIsEnabled(GL_STENCIL_TEST);
	Printf("viewport %d,%d %dx%d, scissor %s %d,%d %dx%d, draw framebuffer %d (read %d, buffer 0x%x), colour mask %d%d%d%d, depth test %d, stencil test %d, screen %dx%d true height %d\n",
		vp[0], vp[1], vp[2], vp[3], scissorOn ? "on" : "off", sc[0], sc[1], sc[2], sc[3], fb, rfb, drawBuf,
		cm[0], cm[1], cm[2], cm[3], depthOn, stencilOn, screen->GetWidth(), screen->GetHeight(),
		static_cast<OpenGLFrameBuffer*>(screen)->GetTrueHeight());
}

sector_t * FGLRenderer::RenderViewpoint (AActor * camera, GL_IRECT * bounds, float fov, float ratio, float fovratio, bool mainview, bool toscreen)
{
	// [rc4l] features/frame sets the viewpoint up as a frame without GL does: which view the walks belong to, the camera, the view area and features/viewsetup's view through GL's lens.
	const FViewLens lens = { fov, ratio, fovratio };
	const bool savedMainview = zx::frame::BeginViewpoint(camera, lens, mainview, toscreen);
	sector_t * retval;
	const zx::viewsetup::ViewSetup &view = zx::viewsetup::CurrentView();
	mAngles.Pitch = view.pitch;
	mAngles.Roll = view.roll;

	// Scroll the sky
	mSky1Pos = view.sky1Pos;
	mSky2Pos = view.sky2Pos;


	mViewActor = static_cast<AActor *>(const_cast<void *>(view.viewActor));   // [rc4l] the view names who it is in

	// 'viewsector' will not survive the rendering so it cannot be used anymore below.
	retval = viewsector;

	// Render (potentially) multiple views for stereo 3d
	// [rc4l] This eye-loop replaces the [BB] GZ3Doom eyeShift stereo -- the s3d framework's
	// mono mode is a single eye with an identity shift, so it renders exactly as before until
	// the user selects a 3D mode.
	float viewShift[3];
	const s3d::Stereo3DMode& stereo3dMode = s3d::Stereo3DMode::getCurrentMode();
	stereo3dMode.SetUp();
	for (int eye_ix = 0; eye_ix < stereo3dMode.eye_count(); ++eye_ix)
	{
		const s3d::EyePose * eye = stereo3dMode.getEyePose(eye_ix);
		eye->SetUp();
		SetViewport(bounds);
		mCurrentFoV = fov;
		SetProjection( eye->GetProjection(fov, ratio, fovratio) );
		SetViewAngle(viewangle);
		eye->GetViewShift(GLRenderer->mAngles.Yaw, viewShift);
		s3d::ScopedViewShifter viewShifter(viewShift);
		SetViewMatrix(viewx, viewy, viewz, false, false);
		gl_RenderState.ApplyMatrices();
		if (mainview && toscreen) PrintViewStateIfArmed();

		clipper.Clear();
		angle_t a1 = FrustumAngle();
		clipper.SafeAddClipRangeRealAngles(viewangle+a1, viewangle-a1);

		ProcessScene(toscreen);
		if (mainview && toscreen) PrintViewErrorsIfArmed();
		if (mainview) EndDrawScene(retval);	// do not call this for camera textures.
		eye->TearDown();
	}
	stereo3dMode.TearDown();

	zx::frame::EndViewpoint(savedMainview);
	return retval;
}


//-----------------------------------------------------------------------------
//
// renders the view
//
//-----------------------------------------------------------------------------

#ifdef _WIN32 // [BB] Detect some kinds of glBegin hooking.
extern char myGlBeginCharArray[4];
int crashoutTic = 0;
#endif

void FGLRenderer::BeginFrame()
{
#ifdef _WIN32 // [BB] Detect some kinds of glBegin hooking.
	// [BB] Continuously make this check, otherwise a hack could bypass the check by activating
	// and deactivating itself at the right time interval.
	{
		if ( strncmp(reinterpret_cast<char *>(glBegin), myGlBeginCharArray, 4) )
		{
			I_FatalError ( "OpenGL malfunction encountered.\n" );
		}
		else
		{
			// [BB] Most GL wallhacks deactivate GL_DEPTH_TEST by manipulating glBegin.
			// Here we try check if this is done.
			GLboolean oldValue;
			glGetBooleanv ( GL_DEPTH_TEST, &oldValue );
			glEnable ( GL_DEPTH_TEST );
			glBegin( GL_TRIANGLE_STRIP );
			glEnd();
			GLboolean valueTrue;
			glGetBooleanv ( GL_DEPTH_TEST, &valueTrue );

			// [BB] Also check if glGetBooleanv simply always returns true.
			glDisable ( GL_DEPTH_TEST );
			GLboolean valueFalse;
			glGetBooleanv ( GL_DEPTH_TEST, &valueFalse );

			if ( ( valueTrue == GL_FALSE ) || ( valueFalse == GL_TRUE ) )
			{
				I_FatalError ( "OpenGL malfunction encountered.\n" );
			}

			if ( oldValue )
				glEnable ( GL_DEPTH_TEST );
			else
				glDisable ( GL_DEPTH_TEST );
		}
	}
#endif

	MCP_GLPerf_FrameBegin(); // [rc4l] earliest GL work of the frame -- open the GPU-time frame span

	checkBenchActive();

	gl_RenderState.SetVertexBuffer(mVBO);
	GLRenderer->mVBO->Reset();

	// reset statistics counters
	ResetProfilingData();

	// [rc4l] Cleared before the frame's camera textures are drawn, which light through it.
	GLRenderer->mLights->Clear();
}

// [rc4l] The player's view, once the frame is set up and its lens worked out (r_viewLens).
void FGLRenderer::RenderView (player_t* player)
{

	// I stopped using BaseRatioSizes here because the information there wasn't well presented.
	//							4:3				16:9		16:10		17:10		5:4

	// now render the main view
	float fovratio;
	// [rc4l] PROVENANCE: NO UPSTREAM COMMIT -- ours; this file is not in uzdoom@5b438d220f918e4d5b604e970f0f45f96963e8d1.
	//   SUPERSEDED BY: nothing. These GL files have no upstream counterpart -- upstream's
	//   equivalents live in a renderer we do not share -- so this stays ours indefinitely.
	//   ON PORT: leave alone; only revisit if the GL renderer itself is ever replaced.
	// [rc4l] `ratios[]` above was a bucket-to-ratio lookup, and WidescreenRatio now IS the ratio, so
	// the table is redundant. `!(WidescreenRatio & 4)` meant "not the 5:4 bucket", i.e. not taller
	// than wide; the float test says the same thing without needing a bucket to exist.
	//
	// No upstream counterpart -- these GL files are not in the commit being ported.
	float ratio = r_viewLens.ratio;
	fovratio = r_viewLens.fovRatio;

	gl_RenderState.SetFixedColormap(gl_fixedcolormap);

	// Check if there's some lights. If not some code can be skipped.
	TThinkerIterator<ADynamicLight> it(STAT_DLIGHT);
	GLRenderer->mLightCount = ((it.Next()) != NULL);

	// [rc4l] fov-interp: FieldOfView is only updated once per tic (D_Display -> R_SetFOV) and is
	// quantised to fineangles, so a zoom staircases at anything above 35Hz. Render the sub-tic
	// position of the very step P_PlayerThink is about to take instead. All the arithmetic lives
	// in features/fov-interp/computation/ so this stays a single line for the renderer staircase
	// to re-apply. Ported from Q-Zandronum d2475b676 + 390ea5ac2.
	sector_t * viewsector = RenderViewpoint(player->camera, NULL, r_viewLens.fov, ratio, fovratio, true, true);

	All.Unclock();
}

//===========================================================================
//
// Render the view to a savegame picture
//
//===========================================================================

void FGLRenderer::WriteSavePic (player_t *player, FILE *file, int width, int height)
{
	GL_IRECT bounds;

	bounds.left=0;
	bounds.top=0;
	bounds.width=width;
	bounds.height=height;
	glFlush();
	SetFixedColormap(player);
	gl_RenderState.SetVertexBuffer(mVBO);
	GLRenderer->mVBO->Reset();
	GLRenderer->mLights->Clear();

	// Check if there's some lights. If not some code can be skipped.
	TThinkerIterator<ADynamicLight> it(STAT_DLIGHT);
	GLRenderer->mLightCount = ((it.Next()) != NULL);

	sector_t *viewsector = RenderViewpoint(players[consoleplayer].camera, &bounds, 
								FieldOfView * 360.0f / FINEANGLES, 1.6f, 1.6f, true, false);
	glDisable(GL_STENCIL_TEST);
	gl_RenderState.SetFixedColormap(CM_DEFAULT);
	gl_RenderState.SetSoftLightLevel(-1);
	screen->Begin2D(false);
	DrawBlend(viewsector);
	glFlush();

	byte * scr = (byte *)M_Malloc(width * height * 3);
	glReadPixels(0,0,width, height,GL_RGB,GL_UNSIGNED_BYTE,scr);
	M_CreatePNG (file, scr + ((height-1) * width * 3), NULL, SS_RGB, width, height, -width*3);
	M_Free(scr);

	// [BC] In GZDoom, this is called every frame, regardless of whether or not
	// the view is active. In Skulltag, we don't so we have to call this here
	// to reset everything, such as the viewport, after rendering our view to
	// a canvas.
	FGLRenderer::EndDrawScene( viewsector );
}


//===========================================================================
//
//
//
//===========================================================================

// [rc4l] The level, lights, fog and pitch limits are features/frame's FLevelRenderer, shared with the backend's renderer.
struct FGLInterface : public zx::frame::FLevelRenderer
{
	void PrecacheTexture(FTexture *tex, int cache);
	void BeginFrame(player_t *player);
	void RenderView(player_t *player);
	void WriteSavePic (player_t *player, FILE *file, int width, int height);
	void RenderTextureView (FCanvasTexture *self, AActor *viewpoint, int fov);
	void ClearBuffer(int color);
};

//==========================================================================
//
// DFrameBuffer :: PrecacheTexture
//
//==========================================================================

void FGLInterface::PrecacheTexture(FTexture *tex, int cache)
{
	if (tex != NULL)
	{
		if (cache)
		{
			tex->PrecacheGL(cache);
		}
		// [ForkUnderA] cl_fua_caching: sprite materials stay resident across
		// level loads -- they are the hitch-prone ones, and evicting a warm
		// effect sprite just because it isn't placed on the next map recreates
		// the very stutter the precache exists to remove. Level geometry
		// still evicts per map as before.
		else if (FUA_CachingMode() > 0 && tex->UseType == FTexture::TEX_Sprite)
		{
		}
		else
		{
			tex->UncacheGL();
		}
	}
}

void gl_SerializeGlobals(FArchive &arc)
{
	arc << fogdensity << outsidefogdensity << skyfog;
}

//===========================================================================
//
// 
//
//===========================================================================

void FGLInterface::ClearBuffer(int color)
{
	PalEntry pe = GPalette.BaseColors[color];
	glClearColor(pe.r/255.f, pe.g/255.f, pe.b/255.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
}

//===========================================================================
//
// Render the view to a savegame picture
//
//===========================================================================

void FGLInterface::WriteSavePic (player_t *player, FILE *file, int width, int height)
{
	GLRenderer->WriteSavePic(player, file, width, height);
}

//===========================================================================
//
// 
//
//===========================================================================

void FGLInterface::BeginFrame(player_t *)
{
	GLRenderer->BeginFrame();
}

void FGLInterface::RenderView(player_t *player)
{
	GLRenderer->RenderView(player);
}


//===========================================================================
//
// Camera texture rendering
//
//===========================================================================
CVAR(Bool, gl_usefb, false , CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
extern TexFilter_s TexFilter[];

void FGLInterface::RenderTextureView (FCanvasTexture *tex, AActor *Viewpoint, int FOV)
{
	FMaterial * gltex = FMaterial::ValidateTexture(tex, false);

	int width = gltex->TextureWidth();
	int height = gltex->TextureHeight();

	gl_fixedcolormap=CM_DEFAULT;
	gl_RenderState.SetFixedColormap(CM_DEFAULT);

	bool usefb = gl_usefb || width > screen->GetWidth() || height > screen->GetHeight();
	if (!usefb)
	{
		glFlush();
	}
	else
	{
#if defined(_WIN32) && (defined(_MSC_VER) || defined(__INTEL_COMPILER))
		__try
#endif
		{
			GLRenderer->StartOffscreen();
			gl_BindMaterialToFrameBuffer(gltex);
		}
#if defined(_WIN32) && (defined(_MSC_VER) || defined(__INTEL_COMPILER))
		__except(1)
		{
			usefb = false;
			gl_usefb = false;
			GLRenderer->EndOffscreen();
			glFlush();
		}
#endif
	}

	GL_IRECT bounds;
	bounds.left=bounds.top=0;
	bounds.width=FHardwareTexture::GetTexDimension(gltex->GetWidth());
	bounds.height=FHardwareTexture::GetTexDimension(gltex->GetHeight());

	GLRenderer->RenderViewpoint(Viewpoint, &bounds, FOV, (float)width/height, (float)width/height, false, false);

	if (!usefb)
	{
		glFlush();
		gl_RenderState.SetMaterial(gltex, 0, 0, -1, false);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, bounds.width, bounds.height);
	}
	else
	{
		GLRenderer->EndOffscreen();
	}

	tex->SetUpdated();
}


//===========================================================================
//
// 
//
//===========================================================================

FRenderer *gl_CreateInterface()
{
	return new FGLInterface;
}


