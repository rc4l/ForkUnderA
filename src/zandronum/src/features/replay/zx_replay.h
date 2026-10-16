// [rc4l] Frame-capture hooks for the FUA instant-replay feature, called from the GL present path
// (OpenGLFrameBuffer::Swap). Kept free of engine/GL/FFmpeg types so gl_framebuffer.cpp can call in
// without header clashes. Implementation + all engine glue live in zx_replay.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_REPLAY_H
#define ZX_REPLAY_H

#include "features/replay/computation/replay_compute.h"   // [rc4l] ReplayPixels

namespace zx {
namespace replay {

// Cheap per-frame check (main thread): true when capture is enabled AND a new frame is due at the
// configured capture rate. Also drives lazy start/stop and flushes any pending user messages.
bool WantsFrame();

// [rc4l] Hands the just-rendered frame to the capture pipeline, which copies it out at once: `topRow` is its top row in `format` (a ReplayPixels), and `pitch` is negative for GL's bottom-up readback.
void SubmitFrame(const unsigned char *topRow, int w, int h, int pitch, int format = kReplayRGB24);

// [rc4l] The same, from the Vulkan backend's swapchain, recorded as its OWN stream.
//
// fua_clip then writes one clip per live renderer -- <name>.mp4 and <name>_vk.mp4 -- so a
// side-by-side session yields a matched pair of the same seconds of play. The Vulkan stream starts
// lazily on its first frame, so a GL-only session never spins up a second encoder.
//
// The Vulkan stream keeps its OWN capture clock -- WantsFrameVulkan, not WantsFrame. Sharing one
// does not work: WantsFrame reports a frame due and SubmitFrame consumes it, so whichever renderer
// presents first takes it and the other captures nothing.
bool WantsFrameVulkan();
void SubmitFrameVulkan(const unsigned char *topRow, int w, int h, int pitch, int format);

// Experimental audio capture (cl_fua_replay_audio). AudioCaptureEnabled() is checked once by the
// OpenAL backend at startup to decide whether to run in loopback mode; SubmitAudio() receives the
// mixed master as interleaved stereo float (nSamples per channel) captured at time tUs.
bool AudioCaptureEnabled();
void SubmitAudio(const float *interleavedStereo, int nSamples, int sampleRate, long long tUs);

} // namespace replay
} // namespace zx

#endif
