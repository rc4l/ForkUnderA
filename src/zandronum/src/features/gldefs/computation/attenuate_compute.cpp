// [Doruk-Aksoy] uzdoom@1e950d75bdb73ea6fcc6cd614c0deb73369393ca (attenuate opt-in) and uzdoom@e77cba1fd8ad45809d207a7c281af3e6baabef36 (lightsizefactor), as uzdoom@62afe5f0 a_dynlightdata.cpp applies them.
#include "features/gldefs/computation/attenuate_compute.h"

namespace zx { namespace gldefs {

namespace {

// [Doruk-Aksoy] A scaled size is held here, far past any map, so a huge factor cannot overflow the int UZDoom casts it to.
constexpr double kLargestScaled = 1 << 20;

}

bool ComputeLightAttenuated(int attenuate, bool levelAttenuates)
{
	return attenuate == kAttenuateUnset ? levelAttenuates : attenuate != 0;
}

int ComputeLightSizeFactor(int size, int attenuate, double factor)
{
	// [Doruk-Aksoy] UZDoom tests its int8 setting through a uint8 getter, so an unset one (-1, read as 255) counts as attenuated here.
	if (attenuate == 0) return size;
	const double scaled = size * factor;
	if (!(scaled > 0.)) return 0;
	if (scaled > kLargestScaled) return int(kLargestScaled);
	return int(scaled);
}

} }
