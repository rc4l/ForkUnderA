
#ifndef __GLC_DATA_H
#define __GLC_DATA_H

#include "doomtype.h"
#include "features/rendersettings/rendersettings.h"   // [rc4l] glset, getExtraLight, currentmapsection and gl_frameMS, which the backend's modules read too

#include "r_defs.h"
#include "a_sharedglobal.h"
#include "c_cvars.h"

void gl_RecalcVertexHeights(vertex_t * v);
#include "features/materials/spriteframe.h"   // [rc4l] gl_GetSpriteFrame

class AStackPoint;
#include "features/stackportals/stackportals.h"   // [rc4l] FPortal, portals, gl_InitPortals and gl_BuildPortalCoverage


#endif
