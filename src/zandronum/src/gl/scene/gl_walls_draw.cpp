/*
** gl_walls_draw.cpp
** Wall rendering
**
**---------------------------------------------------------------------------
** Copyright 2000-2005 Christoph Oelckers
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
#include "p_local.h"
#include "p_lnspec.h"
#include "a_sharedglobal.h"
#include "gl/gl_functions.h"

#include "gl/system/gl_interface.h"
#include "gl/system/gl_cvars.h"
#include "gl/renderer/gl_lightdata.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/data/gl_data.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/dynlights/gl_dynlight.h"
#include "gl/dynlights/gl_glow.h"
#include "gl/dynlights/gl_lightbuffer.h"
#include "gl/scene/gl_drawinfo.h"
#include "gl/scene/gl_portal.h"
#include "gl/shaders/gl_shader.h"
#include "gl/textures/gl_material.h"
#include "gl/utility/gl_clock.h"
#include "gl/utility/gl_templates.h"
#include "features/hwrender/computation/walllight_compute.h"

EXTERN_CVAR(Bool, gl_seamless)

//==========================================================================
//
// Collect lights for shader
//
//==========================================================================
FDynLightData lightdata;


void GLWall::SetupLights()
{
	// [rc4l] Nothing this wall was holding survives into this frame.
	//
	// A wall replayed from the cache is the SAME object every frame, so its light index is whatever
	// was last written into it -- and every return below leaves it exactly as it was. Stock GL never
	// had to think about that, because PutWall built a fresh wall each frame and cleared it there.
	// Carried over, a wall goes on reading light data belonging to a light that has since died: a
	// dead plasma bolt lighting the wall it hit, indefinitely. See walllight_compute.h.
	dynlightindex = zx::hwrender::kNoWallLightIndex;

	// check for wall types which cannot have dynamic lights on them (portal types never get here so they don't need to be checked.)
	switch (type)
	{
	case RENDERWALL_FOGBOUNDARY:
	case RENDERWALL_MIRRORSURFACE:
	case RENDERWALL_COLOR:
	case RENDERWALL_COLORLAYER:
		return;
	}

	// [AK] Take care of gl_lights_size and ZADF_FORCE_VIDEO_DEFAULTS.
	OVERRIDE_LIGHTS_SIZE_IF_NECESSARY

	float vtx[]={glseg.x1,zbottom[0],glseg.y1, glseg.x1,ztop[0],glseg.y1, glseg.x2,ztop[1],glseg.y2, glseg.x2,zbottom[1],glseg.y2};
	Plane p;

	lightdata.Clear();
	p.Init(vtx,4);

	if (!p.ValidNormal()) 
	{
		return;
	}
	FLightNode *node;
	if (seg->sidedef == NULL)
	{
		node = NULL;
	}
	else if (!(seg->sidedef->Flags & WALLF_POLYOBJ))
	{
		node = seg->sidedef->lighthead;
	}
	else if (sub)
	{
		// Polobject segs cannot be checked per sidedef so use the subsector instead.
		node = sub->lighthead;
	}
	else node = NULL;

	// Iterate through all dynamic lights which touch this wall and render them
	while (node)
	{
		if (!(node->lightsource->flags2&MF2_DORMANT))
		{
			iter_dlight++;

			Vector fn, pos;

			float x = FIXED2FLOAT(node->lightsource->x);
			float y = FIXED2FLOAT(node->lightsource->y);
			float z = FIXED2FLOAT(node->lightsource->z);
			float dist = fabsf(p.DistToPoint(x, z, y));
			float radius = (node->lightsource->GetRadius() * gl_lights_size);
			float scale = 1.0f / ((2.f * radius) - dist);

			if (radius > 0.f && dist < radius)
			{
				Vector nearPt, up, right;

				pos.Set(x,z,y);
				fn=p.Normal();
				fn.GetRightUp(right, up);

				Vector tmpVec = fn * dist;
				nearPt = pos + tmpVec;

				Vector t1;
				int outcnt[4]={0,0,0,0};
				texcoord tcs[4];

				// do a quick check whether the light touches this polygon
				for(int i=0;i<4;i++)
				{
					t1.Set(&vtx[i*3]);
					Vector nearToVert = t1 - nearPt;
					tcs[i].u = (nearToVert.Dot(right) * scale) + 0.5f;
					tcs[i].v = (nearToVert.Dot(up) * scale) + 0.5f;

					if (tcs[i].u<0) outcnt[0]++;
					if (tcs[i].u>1) outcnt[1]++;
					if (tcs[i].v<0) outcnt[2]++;
					if (tcs[i].v>1) outcnt[3]++;

				}
				if (outcnt[0]!=4 && outcnt[1]!=4 && outcnt[2]!=4 && outcnt[3]!=4) 
				{
					gl_GetLight(p, node->lightsource, true, false, lightdata);
				}
			}
		}
		node = node->nextLight;
	}

	// The pass ran for this wall, so what it produced is the answer -- including the -1 an empty
	// upload returns, which is how a wall that receives no light says so.
	dynlightindex = zx::hwrender::ComputeWallLightIndex(true,
		GLRenderer->mLights->UploadLights(lightdata), dynlightindex);
}


//==========================================================================
//
// General purpose wall rendering function
// everything goes through here
//
//==========================================================================

void GLWall::RenderWall(int textured, unsigned int *store)
{
	static texcoord tcs[4]; // making this variable static saves us a relatively costly stack integrity check.
	bool split = (gl_seamless && !(textured&RWF_NOSPLIT) && seg->sidedef != NULL && !(seg->sidedef->Flags & WALLF_POLYOBJ) && !(flags & GLWF_NOSPLIT));

	tcs[0]=lolft;
	tcs[1]=uplft;
	tcs[2]=uprgt;
	tcs[3]=lorgt;
	if ((flags&GLWF_GLOW) && (textured & RWF_GLOW))
	{
		gl_RenderState.SetGlowPlanes(topplane, bottomplane);
		gl_RenderState.SetGlowParams(topglowcolor, bottomglowcolor);
	}

	if (!(textured & RWF_NORENDER))
	{
		gl_RenderState.Apply();
		gl_RenderState.ApplyLightIndex(dynlightindex);
	}

	// the rest of the code is identical for textured rendering and lights
	FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
	unsigned int count, offset;

	ptr->Set(glseg.x1, zbottom[0], glseg.y1, tcs[0].u, tcs[0].v);
	ptr++;
	if (split && glseg.fracleft == 0) SplitLeftEdge(tcs, ptr);
	ptr->Set(glseg.x1, ztop[0], glseg.y1, tcs[1].u, tcs[1].v);
	ptr++;
	if (split && !(flags & GLWF_NOSPLITUPPER)) SplitUpperEdge(tcs, ptr);
	ptr->Set(glseg.x2, ztop[1], glseg.y2, tcs[2].u, tcs[2].v);
	ptr++;
	if (split && glseg.fracright == 1) SplitRightEdge(tcs, ptr);
	ptr->Set(glseg.x2, zbottom[1], glseg.y2, tcs[3].u, tcs[3].v);
	ptr++;
	if (split && !(flags & GLWF_NOSPLITLOWER)) SplitLowerEdge(tcs, ptr);
	count = GLRenderer->mVBO->GetCount(ptr, &offset);
	if (!(textured & RWF_NORENDER))
	{
		GLRenderer->mVBO->RenderArray(GL_TRIANGLE_FAN, offset, count);
		vertexcount += count;
	}
	if (store != NULL)
	{
		store[0] = offset;
		store[1] = count;
	}
}

//==========================================================================
//
// [rc4l] features/levelmesh wall batching -- build the same fan RenderWall would emit, but into a
// caller-supplied array instead of straight into the mapped buffer, so a run of walls can be
// fan-expanded into one triangle-list draw. Returns the vertex count, or 0 if the fan would not fit
// (the caller then draws that wall on its own, exactly as before).
//
//==========================================================================

int GLWall::BuildFanVertices(FFlatVertex *out, int maxOut)
{
	texcoord tcs[4];
	const bool split = (gl_seamless && seg->sidedef != NULL &&
		!(seg->sidedef->Flags & WALLF_POLYOBJ) && !(flags & GLWF_NOSPLIT));

	tcs[0] = lolft;
	tcs[1] = uplft;
	tcs[2] = uprgt;
	tcs[3] = lorgt;

	// [rc4l] The split helpers advance the pointer by an amount bounded only by map data, so give
	// them the whole array and bail if they overran rather than trusting a computed bound.
	FFlatVertex *ptr = out;
	if (maxOut < 4) return 0;

	ptr->Set(glseg.x1, zbottom[0], glseg.y1, tcs[0].u, tcs[0].v);
	ptr++;
	if (split && glseg.fracleft == 0) SplitLeftEdge(tcs, ptr);
	ptr->Set(glseg.x1, ztop[0], glseg.y1, tcs[1].u, tcs[1].v);
	ptr++;
	if (split && !(flags & GLWF_NOSPLITUPPER)) SplitUpperEdge(tcs, ptr);
	ptr->Set(glseg.x2, ztop[1], glseg.y2, tcs[2].u, tcs[2].v);
	ptr++;
	if (split && glseg.fracright == 1) SplitRightEdge(tcs, ptr);
	ptr->Set(glseg.x2, zbottom[1], glseg.y2, tcs[3].u, tcs[3].v);
	ptr++;
	if (split && !(flags & GLWF_NOSPLITLOWER)) SplitLowerEdge(tcs, ptr);

	const int count = (int)(ptr - out);
	return (count <= maxOut) ? count : 0;
}

//==========================================================================
//
// [rc4l] The state GLWall::Draw's GLPASS_PLAIN/GLPASS_ALL arm sets before RenderWall, without the
// draw. Kept beside that arm so the two cannot drift; anything added there must be added here.
//
//==========================================================================

void GLWall::SetupBatchState(int pass)
{
	const int rel = rellight + getExtraLight();
	gl_SetColor(lightlevel, rel, Colormap, 1.0f);
	if (type != RENDERWALL_M2SNF) gl_SetFog(lightlevel, rel, &Colormap, false);
	else gl_SetFog(255, 0, NULL, false);
	gl_RenderState.EnableGlow(false);	// batched walls are never glowing; ComputeCanBatch refuses them
	gl_RenderState.SetMaterial(gltexture, flags & 3, false, -1, false);
	gl_RenderState.Apply();
	gl_RenderState.ApplyLightIndex(dynlightindex);
}

//==========================================================================
//
// [rc4l] Everything that decides this wall's draw state, for the batching predicate.
//
//==========================================================================

void GLWall::FillBatchKey(zx::levelmesh::WallBatchKey &out) const
{
	out.material      = gltexture;
	out.clampFlags    = flags & 3;
	out.lightLevel    = lightlevel;
	out.relLight      = rellight;
	out.type          = type;
	out.lightColor    = Colormap.LightColor.d;
	out.fadeColor     = Colormap.FadeColor.d;
	out.desaturation  = Colormap.desaturation;
	out.blendFactor   = Colormap.blendfactor;
	out.dynLightIndex = dynlightindex;
	out.glowing       = !!(flags & GLWF_GLOW);
	// [rc4l] Mirrors the RENDERWALL_M2SNF arm of GLWall::Draw, which swaps in TM_CLAMPY for the
	// duration of its own draw. SetupBatchState cannot reproduce a per-wall mode, so refuse it.
	out.ownTextureMode = (type == RENDERWALL_M2SNF) && !!(flags & GLT_CLAMPY);
}

//==========================================================================
//
// 
//
//==========================================================================

void GLWall::RenderFogBoundary()
{
	if (gl_fogmode && gl_fixedcolormap == 0)
	{
		int rel = rellight + getExtraLight();
		gl_SetFog(lightlevel, rel, &Colormap, false);
		gl_RenderState.SetEffect(EFF_FOGBOUNDARY);
		gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -128.0f);
		RenderWall(RWF_BLANK);
		glPolygonOffset(0.0f, 0.0f);
		glDisable(GL_POLYGON_OFFSET_FILL);
		gl_RenderState.SetEffect(EFF_NONE);
	}
}


//==========================================================================
//
// 
//
//==========================================================================
void GLWall::RenderMirrorSurface()
{
	if (GLRenderer->mirrortexture == NULL) return;

	// For the sphere map effect we need a normal of the mirror surface,
	Vector v(glseg.y2-glseg.y1, 0 ,-glseg.x2+glseg.x1);
	v.Normalize();

	// we use texture coordinates and texture matrix to pass the normal stuff to the shader so that the default vertex buffer format can be used as is.
	lolft.u = lorgt.u = uplft.u = uprgt.u = v.X();
	lolft.v = lorgt.v = uplft.v = uprgt.v = v.Z();

	gl_RenderState.EnableTextureMatrix(true);
	gl_RenderState.mTextureMatrix.computeNormalMatrix(gl_RenderState.mViewMatrix);

	// Use sphere mapping for this
	gl_RenderState.SetEffect(EFF_SPHEREMAP);

	gl_SetColor(lightlevel, 0, Colormap ,0.1f);
	gl_SetFog(lightlevel, 0, &Colormap, true);
	gl_RenderState.BlendFunc(GL_SRC_ALPHA,GL_ONE);
	gl_RenderState.AlphaFunc(GL_GREATER,0);
	glDepthFunc(GL_LEQUAL);

	FMaterial * pat=FMaterial::ValidateTexture(GLRenderer->mirrortexture, false);
	gl_RenderState.SetMaterial(pat, CLAMP_NONE, 0, -1, false);

	flags &= ~GLWF_GLOW;
	RenderWall(RWF_BLANK);

	gl_RenderState.EnableTextureMatrix(false);
	gl_RenderState.SetEffect(EFF_NONE);

	// Restore the defaults for the translucent pass
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_RenderState.AlphaFunc(GL_GEQUAL, gl_mask_sprite_threshold);
	glDepthFunc(GL_LESS);

	// This is drawn in the translucent pass which is done after the decal pass
	// As a result the decals have to be drawn here.
	if (seg->sidedef->AttachedDecals)
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -128.0f);
		glDepthMask(false);
		DoDrawDecals();
		glDepthMask(true);
		glPolygonOffset(0.0f, 0.0f);
		glDisable(GL_POLYGON_OFFSET_FILL);
		gl_RenderState.SetTextureMode(TM_MODULATE);
		gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}


//==========================================================================
//
// 
//
//==========================================================================

void GLWall::RenderTranslucentWall()
{
	bool transparent = gltexture? gltexture->GetTransparent() : false;
	
	// currently the only modes possible are solid, additive or translucent
	// and until that changes I won't fix this code for the new blending modes!
	bool isadditive = RenderStyle == STYLE_Add;

	if (gl_fixedcolormap == CM_DEFAULT && gl_lights && (gl.flags & RFL_BUFFER_STORAGE))
	{
		SetupLights();
	}

	if (!transparent) gl_RenderState.AlphaFunc(GL_GEQUAL, gl_mask_threshold);
	else gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
	if (isadditive) gl_RenderState.BlendFunc(GL_SRC_ALPHA,GL_ONE);

	int extra;
	if (gltexture) 
	{
		gl_RenderState.EnableGlow(!!(flags & GLWF_GLOW));
		gl_RenderState.SetMaterial(gltexture, flags & 3, 0, -1, false);
		extra = getExtraLight();
	}
	else 
	{
		gl_RenderState.EnableTexture(false);
		extra = 0;
	}
	int tmode = gl_RenderState.GetTextureMode();

	gl_SetColor(lightlevel, extra, Colormap, fabsf(alpha));
	if (type!=RENDERWALL_M2SNF) gl_SetFog(lightlevel, extra, &Colormap, isadditive);
	else
	{
		if (flags & GLT_CLAMPY)
		{
			if (tmode == TM_MODULATE) gl_RenderState.SetTextureMode(TM_CLAMPY);
		}
		gl_SetFog(255, 0, NULL, false);
	}

	RenderWall(RWF_TEXTURED|RWF_NOSPLIT);

	// restore default settings
	if (isadditive) gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (!gltexture)	
	{
		gl_RenderState.EnableTexture(true);
	}
	gl_RenderState.EnableGlow(false);
	gl_RenderState.SetTextureMode(tmode);
}

//==========================================================================
//
// 
//
//==========================================================================
void GLWall::Draw(int pass)
{
	int rel;
	int tmode;

#ifdef _DEBUG
	if (seg->linedef-lines==879)
	{
		int a = 0;
	}
#endif


	switch (pass)
	{
	case GLPASS_LIGHTSONLY:
		SetupLights();
		break;

	case GLPASS_ALL:
		SetupLights();
		// fall through
	case GLPASS_PLAIN:
		// [rc4l] Reached with pass == GLPASS_ALL as well, by the fall-through above -- so only the
		// plain pass clears, or this would throw away the index SetupLights just computed.
		//
		// The plain pass is what runs once the last dynamic light dies, and a wall replayed from the
		// cache still holds the index it had while one was alive. Left alone it keeps reading that
		// light out of the buffer for as long as nothing else spawns one.
		if (pass == GLPASS_PLAIN)
			dynlightindex = zx::hwrender::ComputeWallLightIndex(false,
				zx::hwrender::kNoWallLightIndex, dynlightindex);
		rel = rellight + getExtraLight();
		gl_SetColor(lightlevel, rel, Colormap,1.0f);
		tmode = gl_RenderState.GetTextureMode();
		if (type!=RENDERWALL_M2SNF) gl_SetFog(lightlevel, rel, &Colormap, false);
		else
		{
			if (flags & GLT_CLAMPY)
			{
				if (tmode == TM_MODULATE) gl_RenderState.SetTextureMode(TM_CLAMPY);
			}
			gl_SetFog(255, 0, NULL, false);
		}
		gl_RenderState.EnableGlow(!!(flags & GLWF_GLOW));
		gl_RenderState.SetMaterial(gltexture, flags & 3, false, -1, false);
		RenderWall(RWF_TEXTURED|RWF_GLOW);
		gl_RenderState.EnableGlow(false);
		gl_RenderState.SetTextureMode(tmode);
		break;

	case GLPASS_TRANSLUCENT:


		switch (type)
		{
		case RENDERWALL_MIRRORSURFACE:
			RenderMirrorSurface();
			break;

		case RENDERWALL_FOGBOUNDARY:
			RenderFogBoundary();
			break;

		case RENDERWALL_COLORLAYER:
			glEnable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(-1.0f, -128.0f);
			RenderTranslucentWall();
			glDisable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(0, 0);

		default:
			RenderTranslucentWall();
			break;
		}
	}
}
