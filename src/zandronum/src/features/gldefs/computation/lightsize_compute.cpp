// [Doruk-Aksoy] GLDEFS light sizes, clamped as uzdoom@62afe5f0 gldefs.cpp ParsePointLight and ParseSectorLight clamp them.
#include "features/gldefs/computation/lightsize_compute.h"

namespace zx { namespace gldefs {

namespace {

constexpr int kSmallest = 1;
constexpr int kLargest = 1024;

int Clamp(int v)
{
	return v < kSmallest ? kSmallest : v > kLargest ? kLargest : v;
}

}

int ComputeLightSize(int written)
{
	return Clamp(written);
}

int ComputeSectorLightScale(float written)
{
	// [Doruk-Aksoy] Clamped as a float before the int cast UZDoom makes, so a huge scale cannot overflow it and NaN lands on the smallest.
	const float scaled = written * 255.f;
	if (!(scaled > float(kSmallest))) return kSmallest;
	if (scaled > float(kLargest)) return kLargest;
	return int(scaled);
}

} }
