// [rc4l] Pure, engine-free logic for the FUA instant-replay feature. No engine headers — only the
// standard library — so it is unit-tested off-engine and the coverage gate can enforce 100% on the
// matching *_compute.cpp. See features/replay/ and docs/instant-replay-PLAN.md.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_REPLAY_COMPUTE_H
#define ZX_REPLAY_COMPUTE_H

#include <cstdint>

namespace zx {

// Capture cadence: with a fixed target FPS independent of game FPS, decide whether enough time has
// elapsed since the last captured frame to grab another. targetFps <= 0 disables capture.
bool ComputeFrameDue(int64_t lastCaptureUs, int64_t nowUs, int targetFps);

// Downscale target: cap the height to maxH (0 or a source already within the cap = keep native),
// preserving aspect ratio. Both dimensions are forced even (H.264/NV12 wants even dims) with a floor
// of 2 so a valid encode surface always results.
struct ScaledDims { int w; int h; };
ScaledDims ComputeScaledDims(int srcW, int srcH, int maxH);

// Ring-buffer capacity in frames for a rolling window of `durationSecs` at `fps`, plus one guard
// frame so the newest write never evicts the frame a save is about to read.
int ComputeRingCapacity(int durationSecs, int fps);

// Format a clip filename from broken-down local time into a caller buffer (no allocation), e.g.
// "clip-20260727-143005.mp4". Safe against a null/zero-size buffer.
struct ClipStamp { int year; int month; int day; int hour; int min; int sec; };
void ComputeClipFilename(char *out, int outSize, const ClipStamp &s);

// Choose which buffered packet a saved clip starts from. `tUs`/`key` describe the ring's packets
// oldest-first (key[i] != 0 marks a keyframe); `lastUs` is the newest capture time; `windowSecs` is
// the requested clip length. Returns the latest keyframe at/before (lastUs - windowSecs) so the clip
// is keyframe-aligned and covers the last N seconds; if the session is SHORTER than the window it
// falls back to the first keyframe (save the whole buffer); returns -1 when nothing is saveable
// (empty / no keyframe).
int ComputeClipStartIndex(const int64_t *tUs, const unsigned char *key, int count,
						  int64_t lastUs, int windowSecs);

// ---- async framebuffer readback (PBO) ------------------------------------------------------------
// The instant-replay capture reads the back buffer into pixel-pack buffers. The GL objects are owned
// by the framebuffer (destroyed with its GL context on a windowed<->fullscreen switch), so `haveBuffers`
// is false whenever we hold no live buffers -- a fresh framebuffer, or the frame right after a context
// recreate. These pure helpers make the size/lifecycle decisions unit-testable off-engine.

// Whether the readback PBOs must be (re)allocated. Allocate when we own no buffers yet (haveBuffers
// false) OR the framebuffer size changed since they were sized; otherwise reuse. Allocating on
// !haveBuffers -- even when the requested size equals the last-seen size -- is what prevents a buffer
// name from a destroyed GL context ever being reused (the Apple-driver crash this fixes).
enum class PboAction { Allocate, Reuse };
PboAction ComputePboAction(bool haveBuffers, int curW, int curH, int reqW, int reqH);

// Byte size of a tightly-packed (GL_PACK_ALIGNMENT 1) RGB / 8-bit-per-channel readback of reqW x reqH.
// Returns 0 for any non-positive dimension (nothing to allocate or read). int64 so large windows can't
// overflow the size computation.
int64_t ComputeRgbReadbackBytes(int reqW, int reqH);

// glReadPixels returns rows bottom-up; to hand a top-down image to the encoder we point at the LAST
// row and walk with a negative stride. Returns that first (bottom) row's byte offset from the buffer
// start and the negative row stride. Both are 0 when either dimension is non-positive.
struct BottomUpView { int64_t firstRowOffset; int rowStride; };
BottomUpView ComputeBottomUpView(int reqW, int reqH);

// [ZandroX] Regression guard for the "every clip after the first freezes" bug in
// ReplayEncoder::SaveClip. The FFmpeg video encoder is a one-way pipeline: avcodec_send_frame(enc,
// NULL) is a *terminal* flush that moves it to EOF, after which every further send_frame returns
// AVERROR_EOF and the frame is dropped. SaveClip used to terminally-flush the LIVE encoder to grab
// queued packets, permanently killing capture -- so the 2nd+ clips in a session (e.g. one per
// wad_reload) froze on the first clip's footage.
//
// This models that pipeline. `ops` is a sequence: 0 = add a frame, 1 = save a clip.
// `terminalFlushOnSave` picks the buggy (true) vs fixed (false) SaveClip behaviour. Returns the
// number of frames that reach the packet ring (are encodable). The invariant a correct SaveClip must
// keep: saving is non-destructive -- every added frame stays encodable regardless of intervening saves.
int ComputeEncodableFramesAcrossSaves(const int *ops, int count, bool terminalFlushOnSave);

} // namespace zx

#endif
