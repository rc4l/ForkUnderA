// [Doruk-Aksoy] Whether a state change leaves an actor's dynamic lights alone, decided from its class alone.
#ifndef ZX_ACTORLIGHTS_COMPUTE_H
#define ZX_ACTORLIGHTS_COMPUTE_H

namespace zx { namespace gldefs {

// [Doruk-Aksoy] True when the class has no light associations, the new state names no light, and no actor of the class was ever given a light, so it holds none to put out.
bool ComputeActorLightsIdle(bool classHasAssociations, int stateLight, bool classEverLit);

} }

#endif
