#include "features/gldefs/computation/actorlights_compute.h"

namespace zx { namespace gldefs {

bool ComputeActorLightsIdle(bool classHasAssociations, int stateLight, bool classEverLit)
{
	return !classHasAssociations && stateLight <= 0 && !classEverLit;
}

} }
