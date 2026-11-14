// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad: p_mobj.cpp's spot arguments, hw_dynlightdata.cpp's spot direction and main.fp's spotLightAttenuation.
#include "features/dynlights/computation/spotlight_compute.h"

#include <cmath>

namespace zx { namespace dynlights {

namespace {

constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;

}

SpotMapArgs ComputeSpotMapArgs(int arg0, int arg1, int arg2)
{
	return { (arg0 >> 16) & 0xff, (arg0 >> 8) & 0xff, arg0 & 0xff, float(arg1), float(arg2) };
}

void ComputeSpotDirection(double yawDegrees, double pitchDegrees, float out[3])
{
	const double yaw = yawDegrees * kRadiansPerDegree;
	const double pitch = pitchDegrees * kRadiansPerDegree;
	const double flat = std::cos(pitch);
	out[0] = float(-std::cos(yaw) * flat);
	out[1] = float(-std::sin(yaw) * flat);
	out[2] = float(std::sin(pitch));
}

float ComputeSpotFactor(float toLightX, float toLightY, float toLightZ, const float direction[3], float cosInner, float cosOuter)
{
	const float length = std::sqrt(toLightX * toLightX + toLightY * toLightY + toLightZ * toLightZ);
	// [Doruk-Aksoy] A point at the light itself has no direction, and is lit fully.
	if (length <= 0.f) return 1.f;
	const float cosDir = (toLightX * direction[0] + toLightY * direction[1] + toLightZ * direction[2]) / length;
	if (cosInner <= cosOuter) return cosDir >= cosOuter ? 1.f : 0.f;
	float t = (cosDir - cosOuter) / (cosInner - cosOuter);
	t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
	return t * t * (3.f - 2.f * t);
}

} }
