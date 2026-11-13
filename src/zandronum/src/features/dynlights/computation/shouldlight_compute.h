// [Doruk-Aksoy] Whether a dynamic light lights an actor or particle, UZDoom's FDynamicLight::ShouldLightActor without its active and visible test.
#ifndef ZX_SHOULDLIGHT_COMPUTE_H
#define ZX_SHOULDLIGHT_COMPUTE_H

namespace zx { namespace dynlights {

// [Doruk-Aksoy] A light's GLDEFS flags that pick which actors it lights.
struct ActorLightFlags
{
	bool dontLightSelf, dontLightOthers, dontLightActors;
};

// [Doruk-Aksoy] carriesIt is whether the light's target is the actor lit (a particle, being no actor, is NULL).
bool ComputeShouldLightActor(const ActorLightFlags &flags, bool carriesIt);

} }

#endif
