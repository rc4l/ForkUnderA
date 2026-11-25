// [Doruk-Aksoy] How high a dynamic light sits, kept off its actor's floor and ceiling as UZDoom's UpdateLocation keeps it.
#ifndef ZX_LIGHTHEIGHT_COMPUTE_H
#define ZX_LIGHTHEIGHT_COMPUTE_H

#include <cstdint>

namespace zx { namespace dynlights {

// [Doruk-Aksoy] z at least margin above floorz, else at most margin below ceilingz, all raw fixed point; unchanged for a +NOINTERACTION actor, which may pass through either.
int64_t ComputeLightZ(int64_t z, int64_t floorz, int64_t ceilingz, int64_t margin, bool noInteraction);

} }

#endif
