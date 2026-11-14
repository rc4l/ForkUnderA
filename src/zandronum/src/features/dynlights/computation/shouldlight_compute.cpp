// [Doruk-Aksoy] Which actors a dynamic light lights, as uzdoom@62afe5f0 a_dynlight.h ShouldLightActor decides.
#include "features/dynlights/computation/shouldlight_compute.h"

namespace zx { namespace dynlights {

bool ComputeShouldLightActor(const ActorLightFlags &flags, bool carriesIt)
{
	if (flags.dontLightActors) return false;
	if (flags.dontLightSelf && carriesIt) return false;
	if (flags.dontLightOthers && !carriesIt) return false;
	return true;
}

} }
