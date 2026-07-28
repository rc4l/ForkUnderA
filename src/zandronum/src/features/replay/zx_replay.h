// [rc4l] Frame-capture hooks for the FUA instant-replay feature, called from the GL present path
// (OpenGLFrameBuffer::Swap). Kept free of engine/GL/FFmpeg types so gl_framebuffer.cpp can call in
// without header clashes. Implementation + all engine glue live in zx_replay.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_REPLAY_H
#define ZX_REPLAY_H

namespace zx {
namespace replay {

// Cheap per-frame check (game thread): true when capture is enabled AND a new frame is due at the
// configured capture rate. Also drives lazy start/stop and flushes any pending user messages.
bool WantsFrame();

// Hand the just-rendered frame to the capture pipeline. rgbTopRow points at the top row of tightly
// packed RGB24; pitch may be negative (OpenGL bottom-up readback). Copies out immediately.
void SubmitFrame(const unsigned char *rgbTopRow, int w, int h, int pitch);

// Experimental audio capture (cl_fua_replay_audio). AudioCaptureEnabled() is checked once by the
// OpenAL backend at startup to decide whether to run in loopback mode; SubmitAudio() receives the
// mixed master as interleaved stereo float (nSamples per channel) captured at time tUs.
bool AudioCaptureEnabled();
void SubmitAudio(const float *interleavedStereo, int nSamples, int sampleRate, long long tUs);

} // namespace replay
} // namespace zx

#endif
