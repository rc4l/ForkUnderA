// [rc4l] Implementation of the gamma-ramp helpers.
#include "features/hwrender/computation/gammaramp_compute.h"
#include <cmath>

namespace hwrender
{

void ComputeIdentityRamp(unsigned short out[256])
{
	for (int i = 0; i < 256; i++)
		out[i] = (unsigned short)(i * 257); // 255*257 == 65535
}

void ComputeGammaRamp(float gamma, unsigned short out[256])
{
	if (gamma <= 0.0f)
	{
		ComputeIdentityRamp(out);
		return;
	}

	const float inv = 1.0f / gamma;
	for (int i = 0; i < 256; i++)
	{
		// [rc4l] pow(i/255, inv) is in [0,1] for i in [0,255], so v is always >= 0; only the upper end can round past 65535 (at i==255), so a single top clamp is all that's needed.
		float v = std::pow(i / 255.0f, inv) * 65535.0f + 0.5f;
		if (v > 65535.0f)
			v = 65535.0f;
		out[i] = (unsigned short)v;
	}
}

} // namespace hwrender
