// [rc4l] Implementation of the pure OpenAL playback math. No engine or OpenAL dependencies,
// so both the engine and the standalone test build compile this TU.
#include "features/openal-sound/computation/oal_playback_compute.h"
#include <cmath>

namespace
{
// [rc4l] Mirror of the engine's Scale(a, srate, 1000): a * srate / 1000 via 64-bit math.
unsigned int ScaleMs(unsigned int value, int srate)
{
	return (unsigned int)(int)(((long long)(int)value * srate) / 1000);
}
}

float ComputeRolloff(int rolloffType, float minDistance, float maxDistanceOrFactor,
	float distance, const unsigned char *soundCurve, int soundCurveSize)
{
	if (distance <= minDistance)
		return 1.f;

	// Logarithmic rolloff has no max distance where it goes silent.
	if (rolloffType == ZX_ROLLOFF_LOG)
		return minDistance /
			(minDistance + maxDistanceOrFactor * (distance - minDistance));

	if (distance >= maxDistanceOrFactor)
		return 0.f;

	float volume = (maxDistanceOrFactor - distance) / (maxDistanceOrFactor - minDistance);
	if (rolloffType == ZX_ROLLOFF_LINEAR)
		return volume;

	if (rolloffType == ZX_ROLLOFF_CUSTOM && soundCurve != nullptr)
		return soundCurve[int(soundCurveSize * (1.f - volume))] / 127.f;

	return (powf(10.f, volume) - 1.f) / 9.f;
}

ZxLoopPoints ComputeLoopPoints(unsigned int start, unsigned int end,
	bool startAssigned, bool endAssigned, int srate, unsigned int samples)
{
	if (!startAssigned)
		start = ScaleMs(start, srate);
	if (!endAssigned && end != ~0u)
		end = ScaleMs(end, srate);

	if (start > samples)
		start = 0;
	if (end > samples)
		end = samples;

	ZxLoopPoints points;
	points.start = start;
	points.end = end;
	return points;
}

bool ComputeShouldSetLoopPoints(unsigned int start, unsigned int end, bool softLoopPoints)
{
	return (start > 0 || end > 0) && end > start && softLoopPoints;
}
