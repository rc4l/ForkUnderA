// [Doruk-Aksoy] A sector light's radius from its sector's light level, UZDoom's FDynamicLight::Tick SectorLight case.
#ifndef ZX_SECTORLIGHT_COMPUTE_H
#define ZX_SECTORLIGHT_COMPUTE_H

namespace zx { namespace dynlights {

// [Doruk-Aksoy] intensityArg is the light's intensity argument (its args[3], a GLDEFS scale times 255 or a map thing's own), eighths of the light level, 0 meaning the level itself.
float ComputeSectorLightRadius(int intensityArg, int lightLevel);

} }

#endif
