// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Pure OpenAL playback math, extracted from oalsound.cpp so it can be unit-tested
// without linking OpenAL or the engine: distance rolloff attenuation and loop-point
// scaling/clamping. The ROLLOFF_* values are mirrored here and static_asserted in the
// caller; the SNDCURVE table is passed in as a plain array so the custom curve is testable.
// Implementation in oal_playback_compute.cpp.
#ifndef ZX_OAL_PLAYBACK_COMPUTE_H
#define ZX_OAL_PLAYBACK_COMPUTE_H

// [rc4l] Rolloff models, mirroring the anonymous enum in s_sound.h.
enum
{
	ZX_ROLLOFF_DOOM   = 0,
	ZX_ROLLOFF_LINEAR = 1,
	ZX_ROLLOFF_LOG    = 2,
	ZX_ROLLOFF_CUSTOM = 3,
};

// [rc4l] Attenuation volume in [0,1] for a sound at distance under the given rolloff model.
// maxDistanceOrFactor is FRolloffInfo's union field: the rolloff factor for log rolloff,
// otherwise the max distance. soundCurve/soundCurveSize back the custom SNDCURVE lookup
// (a null curve falls through to the Doom-style logarithmic scale).
float ComputeRolloff(int rolloffType, float minDistance, float maxDistanceOrFactor,
	float distance, const unsigned char *soundCurve, int soundCurveSize);

// [rc4l] Resolved sample-space loop points.
struct ZxLoopPoints
{
	unsigned int start;
	unsigned int end;
};

// [rc4l] Convert unassigned (millisecond-tagged) loop points to samples at the given rate
// and clamp both into [0, samples]; an unset end (~0u) is left for the caller's clamp.
ZxLoopPoints ComputeLoopPoints(unsigned int start, unsigned int end,
	bool startAssigned, bool endAssigned, int srate, unsigned int samples);

// [rc4l] Whether a buffer's loop points are worth programming into an AL_SOFT loop buffer.
bool ComputeShouldSetLoopPoints(unsigned int start, unsigned int end, bool softLoopPoints);

// [rc4l] How many sources to make: the device's mono and stereo counts summed, or `channels` when it reports none, at most `channels`; a count past int's range (Emscripten's OpenAL reports INT_MAX for each) is just large, never negative.
int ComputeSourceCount(int numMono, int numStereo, int channels);

// [rc4l] A source's play position as the channels read it: 0 once it has stopped, as OpenAL Soft reports, since a browser's OpenAL reports a finished sound's whole length, often a sample short, and the sound counted as cut off and played again.
unsigned int ComputeSourcePosition(bool stopped, int sampleOffset);

#endif // ZX_OAL_PLAYBACK_COMPUTE_H
