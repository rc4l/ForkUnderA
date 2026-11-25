// [Doruk-Aksoy] uzdoom@7ab7fc9a57f8d329f53e9643586eb1ed958afb51 and uzdoom@bb5c8d8124181180f505e34093fc133616f9cd17: a light lifted off the floor or lowered off the ceiling, except a +NOINTERACTION actor's.
#include "features/dynlights/computation/lightheight_compute.h"

namespace zx { namespace dynlights {

int64_t ComputeLightZ(int64_t z, int64_t floorz, int64_t ceilingz, int64_t margin, bool noInteraction)
{
	if (noInteraction) return z;
	if (z < floorz + margin) return floorz + margin;
	if (z > ceilingz - margin) return ceilingz - margin;
	return z;
}

} }
