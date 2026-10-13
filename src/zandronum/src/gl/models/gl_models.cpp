/*
** gl_models.cpp
**
** General model handling code
**
**---------------------------------------------------------------------------
** Copyright 2005 Christoph Oelckers
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
#include "w_wad.h"
#include "cmdlib.h"
#include "sc_man.h"
#include "m_crc32.h"
#include "c_console.h"
#include "g_game.h"
#include "doomstat.h"
#include "g_level.h"
#include "r_state.h"
#include "d_player.h"
//#include "resources/voxels.h"
//#include "gl/gl_intern.h"

#include "gl/system/gl_interface.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/scene/gl_drawinfo.h"
#include "gl/models/gl_models.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/scene/gl_observer.h"
#include "features/sprites/modelbuild.h"   // [rc4l] the pose and frame blend the backend's derived models share
#include "features/sprites/computation/modelpose_compute.h"
#include "gl/textures/gl_material.h"
#include "gl/utility/gl_geometric.h"
#include "gl/utility/gl_convert.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/shaders/gl_shader.h"

// [BB] New #includes. 
#include "r_main.h"

#include <unordered_map>

CVAR(Bool, gl_light_models, true, CVAR_ARCHIVE)
// [BB] Allow the user disable the use of any kind of models.
EXTERN_CVAR(Int, gl_fogmode)

// [rc4l] Each model's buffer, made on first draw from the geometry features/models keeps.
static std::unordered_map<const FModel *, FModelVertexBuffer *> gl_modelBuffers;

static FModelVertexBuffer *gl_ModelBuffer(FModel *model)
{
	FModelVertexBuffer *&buffer = gl_modelBuffers[model];
	if (buffer == NULL)
	{
		model->BuildGeometry();
		buffer = new FModelVertexBuffer(model->mGeometry);
	}
	return buffer;
}

void gl_LoadModels()
{
	for (int i = ModelCount() - 1; i >= 0; i--) gl_ModelBuffer(ModelAt(i));
}

void gl_FlushModels()
{
	for (auto &entry : gl_modelBuffers) delete entry.second;
	gl_modelBuffers.clear();
}

//===========================================================================
//
//
//
//===========================================================================

FModelVertexBuffer::FModelVertexBuffer(const FModelGeometry &geometry)
{
	ibo_id = 0;
	if (vao_id == 0) return;   // [rc4l] no GL, so nothing to upload
	glBindVertexArray(vao_id);

	if (geometry.indexed)
	{
		glGenBuffers(1, &ibo_id);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_id);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, geometry.indices.Size() * sizeof(unsigned int),
			geometry.indices.Size() ? &geometry.indices[0] : NULL, GL_STATIC_DRAW);
	}

	glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
	glBufferData(GL_ARRAY_BUFFER, geometry.verts.Size() * sizeof(FModelVertex),
		geometry.verts.Size() ? &geometry.verts[0] : NULL, GL_STATIC_DRAW);
	glEnableVertexAttribArray(VATTR_VERTEX);
	glEnableVertexAttribArray(VATTR_TEXCOORD);
	glEnableVertexAttribArray(VATTR_VERTEX2);
	glBindVertexArray(0);
}

//===========================================================================
//
//
//
//===========================================================================

FModelVertexBuffer::~FModelVertexBuffer()
{
	if (ibo_id != 0)
	{
		glDeleteBuffers(1, &ibo_id);
	}
}

//===========================================================================
//
// Sets up the buffer starts for frame interpolation
// This must be called after gl_RenderState.Apply!
//
//===========================================================================

unsigned int FModelVertexBuffer::SetupFrame(unsigned int frame1, unsigned int frame2)
{
	glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
	glVertexAttribPointer(VATTR_VERTEX, 3, GL_FLOAT, false, sizeof(FModelVertex), &VMO[frame1].x);
	glVertexAttribPointer(VATTR_TEXCOORD, 2, GL_FLOAT, false, sizeof(FModelVertex), &VMO[frame1].u);
	glVertexAttribPointer(VATTR_VERTEX2, 3, GL_FLOAT, false, sizeof(FModelVertex), &VMO[frame2].x);
	return frame1;
}

//===========================================================================
//
// [rc4l] One frame of a model, its surfaces as features/models works them out: drawn as consecutive
// vertices (MD2) or by index (MD3, voxels), the keyframes blended unless the model has only one, and a
// voxel's palette unfiltered -- the three RenderFrame overrides this replaces, in one.
//
//===========================================================================

static void gl_RenderModelFrame(FModel *mdl, FModelVertexBuffer *buffer, const FModelSurface *surfs, int n, float blend, int translation)
{
	gl_RenderState.SetInterpolationFactor(blend);
	for (int i = 0; i < n; i++)
	{
		const FModelSurface &surf = surfs[i];
		FMaterial * tex = FMaterial::ValidateTexture(surf.skin, false);
		gl_RenderState.SetMaterial(tex, mdl->NoFilter() ? CLAMP_NOFILTER : CLAMP_NONE, translation, -1, false);

		// [rc4l] The surface, for a check against GL's models.
		if (gl_observer) gl_observer->ModelSurface(&mdl->mGeometry, surf.base1, surf.base2, blend, surf.firstIndex, surf.count, tex, translation);
		gl_RenderState.Apply();
		buffer->SetupFrame(surf.base1, surf.base2);
		if (mdl->mGeometry.indexed)
			glDrawElements(GL_TRIANGLES, surf.count, GL_UNSIGNED_INT, (void*)(intptr_t)(surf.firstIndex * sizeof(unsigned int)));
		else
			glDrawArrays(GL_TRIANGLES, 0, surf.count);
	}
	gl_RenderState.SetInterpolationFactor(0.f);
}

//===========================================================================
//
// gl_RenderModel
//
//===========================================================================

void gl_RenderFrameModels( const FSpriteModelFrame *smf,
						   const FState *curState,
						   const int curTics,
						   const PClass *ti,
						   Matrix3x4 *normaltransform,
						   int translation)
{
	// [BB] Frame interpolation: Find the FSpriteModelFrame smfNext which follows after smf in the animation
	// and the scalar value inter ( element of [0,1) ), both necessary to determine the interpolated frame.
	double inter = 0.;
	FSpriteModelFrame * smfNext = zx::sprites::NextModelFrame(smf, curState, curTics, ti, inter);   // [rc4l] features/sprites works these out for GL and the backend's derived models alike

	for(int i=0; i<MAX_MODELS_PER_FRAME; i++)
	{
		FModel * mdl = smf->models[i];

		if (mdl!=NULL)
		{
			mdl->PushSpriteMDLFrame(smf, i);

			FModelVertexBuffer *buffer = gl_ModelBuffer(mdl);
			gl_RenderState.SetVertexBuffer(buffer);

			// [rc4l] The slot's surfaces and blend, as the backend's derived models take them.
			FModelSurface surfs[MD3_MAX_SURFACES];
			float blend;
			const int n = zx::sprites::ModelSlotSurfaces(smf, i, smfNext, inter, surfs, blend);
			gl_RenderModelFrame(mdl, buffer, surfs, n, blend, translation);

			gl_RenderState.SetVertexBuffer(GLRenderer->mVBO);
		}
	}
}

void gl_RenderModel(GLSprite * spr)
{
	FSpriteModelFrame * smf = spr->modelframe;


	// Setup transformation.
	glDepthFunc(GL_LEQUAL);
	gl_RenderState.EnableTexture(true);
	// [BB] In case the model should be rendered translucent, do back face culling.
	// This solves a few of the problems caused by the lack of depth sorting.
	// TO-DO: Implement proper depth sorting.
	if (!( spr->actor->RenderStyle == LegacyRenderStyles[STYLE_Normal] ))
	{
		glEnable(GL_CULL_FACE);
		glFrontFace(GL_CW);
	}

	int translation = 0;
	if ( !(smf->flags & MDL_IGNORETRANSLATION) )
		translation = spr->actor->Translation;


	// [rc4l] Model space to world space (turn, pitch, roll, MODELDEF spin, scale, offsets, rotations and pixel stretch) comes from features/sprites so the backend's derived models stand exactly where GL's do.
	zx::sprites::ModelPose pose;
	zx::sprites::ModelPoseFor(spr->actor, smf, spr->x, spr->y, spr->z, pose);
	float modelToWorld[16];
	zx::sprites::ComputeModelMatrix(pose, modelToWorld);
	gl_RenderState.mModelMatrix.loadMatrix(modelToWorld);

	gl_RenderState.EnableModelMatrix(true);
	// [rc4l] The model's surfaces with the matrix above and the sprite's light, for a check against GL's models.
	if (gl_observer) gl_observer->ModelBegins(spr, gl_RenderState.mModelMatrix);
	gl_RenderFrameModels( smf, spr->actor->state, spr->actor->tics, RUNTIME_TYPE(spr->actor), NULL, translation );
	if (gl_observer) gl_observer->ModelEnds();
	gl_RenderState.EnableModelMatrix(false);

	glDepthFunc(GL_LESS);
	if (!( spr->actor->RenderStyle == LegacyRenderStyles[STYLE_Normal] ))
		glDisable(GL_CULL_FACE);
}


//===========================================================================
//
// gl_RenderHUDModel
//
//===========================================================================

void gl_RenderHUDModel(pspdef_t *psp, fixed_t ofsx, fixed_t ofsy)
{
	AActor * playermo=players[consoleplayer].camera;
	FSpriteModelFrame *smf = gl_FindModelFrame(playermo->player->ReadyWeapon->GetClass(), psp->state->sprite, psp->state->GetFrame(), false);

	// [BB] No model found for this sprite, so we can't render anything.
	if ( smf == NULL )
		return;

	glDepthFunc(GL_LEQUAL);

	// [BB] In case the model should be rendered translucent, do back face culling.
	// This solves a few of the problems caused by the lack of depth sorting.
	// TO-DO: Implement proper depth sorting.
	if (!( playermo->RenderStyle == LegacyRenderStyles[STYLE_Normal] ))
	{
		glEnable(GL_CULL_FACE);
		glFrontFace(GL_CCW);
	}

	// [BB] The model has to be drawn independently from the position of the player,
	// so we have to reset the view matrix.
	gl_RenderState.mViewMatrix.loadIdentity();

	// Scaling model (y scale for a sprite means height, i.e. z in the world!).
	gl_RenderState.mViewMatrix.scale(smf->xscale, smf->zscale, smf->yscale);
	
	// Aplying model offsets (model offsets do not depend on model scalings).
	gl_RenderState.mViewMatrix.translate(smf->xoffset / smf->xscale, smf->zoffset / smf->zscale, smf->yoffset / smf->yscale);

	// [BB] Weapon bob, very similar to the normal Doom weapon bob.
	gl_RenderState.mViewMatrix.rotate(FIXED2FLOAT(ofsx)/4, 0, 1, 0);
	gl_RenderState.mViewMatrix.rotate(-FIXED2FLOAT(ofsy-WEAPONTOP)/4, 1, 0, 0);

	// [BB] For some reason the jDoom models need to be rotated.
	gl_RenderState.mViewMatrix.rotate(90.f, 0, 1, 0);

	// Applying angleoffset, pitchoffset, rolloffset.
	gl_RenderState.mViewMatrix.rotate(-ANGLE_TO_FLOAT(smf->angleoffset), 0, 1, 0);
	gl_RenderState.mViewMatrix.rotate(smf->pitchoffset, 0, 0, 1);
	gl_RenderState.mViewMatrix.rotate(-smf->rolloffset, 1, 0, 0);
	gl_RenderState.ApplyMatrices();

	gl_RenderFrameModels( smf, psp->state, psp->tics, playermo->player->ReadyWeapon->GetClass(), NULL, 0 );

	glDepthFunc(GL_LESS);
	if (!( playermo->RenderStyle == LegacyRenderStyles[STYLE_Normal] ))
		glDisable(GL_CULL_FACE);
}

