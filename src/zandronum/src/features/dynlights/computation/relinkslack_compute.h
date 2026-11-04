// [Doruk-Aksoy] Whether a moving light can keep the surfaces its last relink found instead of walking again, which a few units of movement never change by more than the faint edge of its reach.
#ifndef ZX_RELINKSLACK_COMPUTE_H
#define ZX_RELINKSLACK_COMPUTE_H

#include <cstdint>

namespace zx { namespace dynlights {

// [Doruk-Aksoy] Where a light's last relink put it, in fixed point; not valid before its first.
struct LinkSpot
{
	int64_t x, y;
	bool valid;
};

// [Doruk-Aksoy] True when the light is within slack (fixed point, 0 or less for none) of its last relink on both axes, so that relink's surfaces stand.
bool ComputeLightKeepsLinks(const LinkSpot &last, int64_t x, int64_t y, int64_t slack);

} }

#endif
