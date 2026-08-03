// [rc4l] See fua_audiomix_compute.h.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fua_audiomix_compute.h"

namespace zx
{

int FuaFramesForBytes(int bytes)
{
	if (bytes <= 0)
		return 0;
	const int bytesPerFrame = kFuaAudioChannels * 2; // S16
	return bytes / bytesPerFrame;
}

int FuaClampFramesToScratch(int wanted, int scratchFloats)
{
	if (wanted <= 0 || scratchFloats <= 0)
		return 0;
	const int maxFrames = scratchFloats / kFuaAudioChannels;
	return wanted < maxFrames ? wanted : maxFrames;
}

void FuaFloatToS16(const float *in, short *out, int frames)
{
	if (in == nullptr || out == nullptr || frames <= 0)
		return;

	const int samples = frames * kFuaAudioChannels;
	for (int i = 0; i < samples; ++i)
	{
		float v = in[i];
		// Clamp before scaling. A loopback mix genuinely can exceed unity when several loud sources
		// coincide; letting that wrap turns a moment of loudness into a click.
		if (v > 1.0f)  v = 1.0f;
		if (v < -1.0f) v = -1.0f;
		out[i] = static_cast<short>(v * 32767.0f);
	}
}

} // namespace zx
