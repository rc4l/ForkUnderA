// [Doruk-Aksoy] Whether a light can keep the subsector it has instead of a BSP lookup, which most of a light's updates repeat at the point it last looked up.
#ifndef ZX_LIGHTSPOT_COMPUTE_H
#define ZX_LIGHTSPOT_COMPUTE_H

#include <cstdint>

namespace zx { namespace dynlights {

// [Doruk-Aksoy] The point a light last looked its subsector up for, and the subsector found.
struct LightSpot
{
	int64_t x, y;
	const void *subsector;
};

// [Doruk-Aksoy] True when the light is at that very point and still holds that subsector, so a lookup would find it again.
bool ComputeLightKeepsSubsector(const LightSpot &last, int64_t x, int64_t y, const void *subsector);

} }

#endif
