// [rc4l] The stacked sectors' portals (a plane that shows another sector, displaced) and the subsectors each covers: moved here from gl/data, which GL's portals and the backend's walks both read.

#ifndef ZX_STACKPORTALS_H
#define ZX_STACKPORTALS_H

#include "r_defs.h"
#include "tarray.h"

struct GLSectorStackPortal;

struct FPortal
{
	fixed_t xDisplacement;
	fixed_t yDisplacement;
	int plane;
	GLSectorStackPortal *glportal;	// for quick access to the render data. This is only valid during BSP traversal!

	// [rc4l] GL's render data for this portal, made on first ask; GL's portal code defines it.
	GLSectorStackPortal *GetGLPortal();
};

extern TArray<FPortal *> portals;

void gl_InitPortals();
void gl_BuildPortalCoverage(FPortalCoverage *coverage, subsector_t *subsector, FPortal *portal);

// [rc4l] The portals some sector's floor or ceiling looks through, which is how many stacked views the level can show at once; gl_InitPortals also makes ones no sector takes.
int StackPortalsInUse();

#endif // ZX_STACKPORTALS_H
