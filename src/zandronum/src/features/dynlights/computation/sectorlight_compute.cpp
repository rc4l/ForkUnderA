// [Doruk-Aksoy] A sector light's radius, as uzdoom@62afe5f0 a_dynlight.cpp FDynamicLight::Tick computes it for a SectorLight.
#include "features/dynlights/computation/sectorlight_compute.h"

namespace zx { namespace dynlights {

float ComputeSectorLightRadius(int intensityArg, int lightLevel)
{
	float scale = intensityArg / 8.f;
	if (scale == 0.f) scale = 1.f;
	const float radius = lightLevel * scale;
	return radius < 0.f ? 0.f : radius > 255.f ? 255.f : radius;
}

} }
