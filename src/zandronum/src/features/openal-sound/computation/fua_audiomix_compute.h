// [rc4l] The pure parts of the instant-replay audio pump.
//
// Instant replay is ours, not upstream's: we open OpenAL's ALC_SOFT_loopback device so the recorder
// can capture the master mix. A loopback device does not drive its own clock, so something has to
// pull at the output rate and play the rendered mix to a real device. Upstream has no equivalent --
// its oalsound uses a normal, self-driving device and contains no SDL at all.
//
// The pull itself is platform work (AudioQueue on macOS, SDL elsewhere). Everything here is not:
// how many frames a byte-count is worth, how many fit the scratch buffer, and the float-to-S16
// conversion. Keeping it separate is what makes it testable without an audio device.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_FUA_AUDIOMIX_COMPUTE_H
#define ZX_FUA_AUDIOMIX_COMPUTE_H

#include <cstddef>

namespace zx
{

// [rc4l] The pump's fixed contract, in one place so the sinks cannot disagree about it.
enum
{
	kFuaAudioRate     = 44100,
	kFuaAudioChannels = 2,
	kFuaAudioFrames   = 1024,  // frames per pull
	kFuaAudioScratch  = 8192   // floats in the render scratch buffer
};

// [rc4l] Interleaved stereo frames in `bytes` of S16 output. Returns 0 for a negative or
// sub-frame count rather than a partial frame, since a partial frame has no meaning downstream.
int FuaFramesForBytes(int bytes);

// [rc4l] Frames that actually fit the scratch buffer, which holds interleaved stereo floats.
// Never returns more than `wanted`, never more than the buffer holds, never negative.
int FuaClampFramesToScratch(int wanted, int scratchFloats);

// [rc4l] Convert `frames` interleaved stereo float samples to S16, clamping to [-1, 1] first.
// Out-of-range input is the normal case, not an error: a loopback mix can exceed unity when several
// loud sources land together, and letting that wrap would produce a loud click rather than
// clipping. Does nothing if either pointer is null or frames <= 0.
void FuaFloatToS16(const float *in, short *out, int frames);

} // namespace zx

#endif // ZX_FUA_AUDIOMIX_COMPUTE_H
