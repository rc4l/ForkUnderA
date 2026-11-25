// [Doruk-Aksoy] uzdoom@a4e527979d51066b748b94184dfda57c8a6fc2e6 (a light dimmed by its carrier's alpha) and uzdoom@c0d17bbda34015b214d5c96cb141ad02d8b0ddb1 (only with +LIGHTMULTALPHA).
#include "features/dynlights/computation/lightintensity_compute.h"

namespace zx { namespace dynlights {

float ComputeLightIntensity(float gldefsIntensity, bool multAlpha, float carrierAlpha)
{
	return multAlpha ? gldefsIntensity * carrierAlpha : gldefsIntensity;
}

} }
