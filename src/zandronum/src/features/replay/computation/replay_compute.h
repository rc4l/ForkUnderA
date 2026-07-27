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

} // namespace zx

#endif
