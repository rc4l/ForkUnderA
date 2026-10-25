// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure model of how OpenAL Soft's basic stereo output pans a mono source, and the gain that gives it FMOD Ex's constant power, so mono plays at Zandronum's level from every direction.
#ifndef ZX_OAL_PAN_COMPUTE_H
#define ZX_OAL_PAN_COMPUTE_H

// [Doruk-Aksoy] ALC_SOFT_output_mode's query and its plain stereo speakers value, mirrored here and static_asserted in oalsound.cpp where the headers have them.
enum
{
	ZX_ALC_OUTPUT_MODE_SOFT  = 0x19AC,
	ZX_ALC_STEREO_BASIC_SOFT = 0x19AE,
};

// [Doruk-Aksoy] An offset in OpenAL's listener frame (x right, y up, z back), the frame OpenAL Soft pans in.
struct ZxPanVector
{
	float x, y, z;
};

// [Doruk-Aksoy] What a source sends to the left and right speakers.
struct ZxStereoGains
{
	float left, right;
};

// [Doruk-Aksoy] An offset in the engine's sound space (X east, Y up, Z north) in the frame of a listener facing angle radians, upright as UpdateListener orients it.
ZxPanVector ComputeListenerFrameOffset(float dx, float dy, float dz, float listenerAngle);

// [Doruk-Aksoy] OpenAL Soft's angular spread for a source of radius at distance (alc/alu.cpp), 0 for a point source.
float ComputeOalSpread(float distance, float radius);

// [Doruk-Aksoy] The speaker gains OpenAL Soft 1.25's basic stereo output gives a mono source at offset with spread: its front azimuth widening, then its StereoConfig decoder, with a source on the listener at front centre.
ZxStereoGains ComputeOalMonoStereoPan(ZxPanVector offset, float spread);

// [Doruk-Aksoy] The gain that lifts a mono source at offset with radius to constant total power, FMOD Ex's equal-power level in every direction; 1 unless the output is basic stereo and the buffer mono.
float ComputeMonoPanGain(bool basicStereo, int bufferChannels, ZxPanVector offset, float radius);

// [Doruk-Aksoy] Whether the device pans as ComputeOalMonoStereoPan models: it reports ALC_SOFT_output_mode and plays basic stereo, not HRTF, UHJ or surround.
bool ComputeBasicStereoOutput(bool hasOutputMode, int outputMode);

// [Doruk-Aksoy] A sound source's AL_MAX_GAIN, room for a sound at maxVolume with the largest ComputeMonoPanGain, or 1 where the implementation refuses more (Emscripten's).
float ComputeSourceMaxGain(bool aboveOneAccepted, float maxVolume);

// [Doruk-Aksoy] A source's AL_SOURCE_RADIUS: the area radius for an area sound, its stand-in at the OpenAL distance under manual rolloff, and 0 otherwise or without AL_EXT_SOURCE_RADIUS.
float ComputeSourceRadius(bool hasRadiusExt, bool area, bool manualRolloff, float areaRadius, float rolloffAtArea);

#endif // ZX_OAL_PAN_COMPUTE_H
