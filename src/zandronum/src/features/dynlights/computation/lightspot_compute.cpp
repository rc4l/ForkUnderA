// [Doruk-Aksoy] Whether a light keeps its subsector without a BSP lookup.
#include "features/dynlights/computation/lightspot_compute.h"

namespace zx { namespace dynlights {

bool ComputeLightKeepsSubsector(const LightSpot &last, int64_t x, int64_t y, const void *subsector)
{
	return last.subsector != nullptr && last.subsector == subsector && last.x == x && last.y == y;
}

} }
