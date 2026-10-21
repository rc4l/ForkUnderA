#ifndef __GL_MODELS_H_
#define __GL_MODELS_H_

#include "gl/utility/gl_geometric.h"
#include "gl/data/gl_vertexbuffer.h"
#include "features/models/models.h"   // [rc4l] the models themselves, which the backend reads too

class GLSprite;

void gl_RenderModel(GLSprite * spr);
// [BB] HUD weapon model rendering functions.
void gl_RenderHUDModel(pspdef_t *psp, fixed_t ofsx, fixed_t ofsy);

#endif
