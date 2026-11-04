// [Doruk-Aksoy] Whether a moving light keeps its last relink's surfaces.
#include "features/dynlights/computation/relinkslack_compute.h"

namespace zx { namespace dynlights {

bool ComputeLightKeepsLinks(const LinkSpot &last, int64_t x, int64_t y, int64_t slack)
{
	if (!last.valid || slack <= 0) return false;
	const int64_t dx = x - last.x, dy = y - last.y;
	return (dx < 0 ? -dx : dx) <= slack && (dy < 0 ? -dy : dy) <= slack;
}

} }
