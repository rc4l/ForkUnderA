#ifndef __GL_TEXTURE_H
#define __GL_TEXTURE_H

#include "m_fixed.h"
#include "textures/textures.h"
#include "features/materials/material.h"   // [rc4l] FMaterial and FTexturePixels, which any renderer reads; GL binds them here
#include "gl/textures/gl_hwtexture.h"
#include "gl/renderer/gl_colormap.h"
#include "i_system.h"

EXTERN_CVAR(Bool, gl_precache)

enum
{
	CLAMP_NONE = 0,
	CLAMP_X = 1,
	CLAMP_Y = 2,
	CLAMP_XY = 3,
	CLAMP_XY_NOMIP = 4,
	CLAMP_NOFILTER = 5,
	CLAMP_CAMTEX = 6,
};

// [rc4l] GL's side of a material: binding its layers to texture units, uploading their pixels the first time.
void gl_BindMaterial(FMaterial *mat, int clampmode, int translation);
void gl_PrecacheMaterial(FMaterial *mat);
// [rc4l] Render into a camera texture's material.
void gl_BindMaterialToFrameBuffer(FMaterial *mat);
// [rc4l] Forget which material is bound, so the next bind binds again.
void gl_ClearLastMaterial();

#endif
