
#ifndef __GLC_DATA_H
#define __GLC_DATA_H

#include "doomtype.h"
#include "features/rendersettings/rendersettings.h"   // [rc4l] glset, getExtraLight, currentmapsection and gl_frameMS, which the backend's modules read too

#include "r_defs.h"
#include "a_sharedglobal.h"
#include "c_cvars.h"

void gl_RecalcVertexHeights(vertex_t * v);
FTextureID gl_GetSpriteFrame(unsigned sprite, int frame, int rot, angle_t ang, bool *mirror);

class AStackPoint;
struct GLSectorStackPortal;

struct FPortal
{
	fixed_t xDisplacement;
	fixed_t yDisplacement;
	int plane;
	GLSectorStackPortal *glportal;	// for quick access to the render data. This is only valid during BSP traversal!

	GLSectorStackPortal *GetGLPortal();
};

extern TArray<FPortal *> portals;

void gl_InitPortals();
void gl_BuildPortalCoverage(FPortalCoverage *coverage, subsector_t *subsector, FPortal *portal);


#endif
