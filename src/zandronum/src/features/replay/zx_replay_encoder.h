// [rc4l] FFmpeg H.264/MP4 encoder for the instant-replay ring buffer. Deliberately engine-free
// (FFmpeg + STL only, no engine headers) so its FFmpeg/Windows types never clash with the engine's
// -- zx_replay.cpp owns all engine glue and calls this. Design: docs/instant-replay-PLAN.md.
//
// Frames are encoded continuously into a rolling ring of encoded packets (a few MB), NOT stored raw.
// SaveClip() muxes the last N seconds from a keyframe boundary to an .mp4. Single-threaded use:
// all methods run on the replay worker thread (see zx_replay.cpp).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_REPLAY_ENCODER_H
#define ZX_REPLAY_ENCODER_H

#ifdef ZX_ENABLE_REPLAY

#include <cstdint>
#include <deque>

struct AVCodecContext;
struct AVFrame;
struct SwsContext;
struct AVPacket;

namespace zx {

class ReplayEncoder
{
public:
	// srcW/srcH: incoming framebuffer size; dstW/dstH: encoded (downscaled) size. encName e.g.
	// "libx264" (software) or "h264_videotoolbox" (hardware). Returns false if unavailable.
	bool Init(int srcW, int srcH, int dstW, int dstH, int fps, int bitrateKbps, const char *encName);
	// Top-down tightly-or-strided RGB24; tUs is a monotonic capture timestamp in microseconds.
	void AddFrameTopDownRGB(const uint8_t *rgb, int srcStride, int64_t tUs);
	// Mux the last windowSecs of buffered packets (from a keyframe) to an .mp4. False on failure.
	bool SaveClip(const char *path, int windowSecs);
	void Shutdown();

	void SetWindow(int secs) { windowSecs_ = secs; }
	int SrcW() const { return srcW_; }
	int SrcH() const { return srcH_; }
	bool Ready() const { return enc_ != nullptr; }

private:
	struct RingPkt { AVPacket *pkt; int64_t tUs; bool key; };
	void Drain(AVFrame *f, int64_t tUs);
	void Evict();

	int srcW_ = 0, srcH_ = 0, dstW_ = 0, dstH_ = 0, fps_ = 30, windowSecs_ = 15;
	// PTS is derived from real capture time (not a frame counter) so a clip's playback duration
	// always equals the wall-clock span it covers -- even if the game rendered below the capture
	// rate, the clip stays real-time (choppy) instead of fast-forwarding.
	int64_t firstUs_ = -1, lastPts_ = -1, lastUs_ = 0;
	AVCodecContext *enc_ = nullptr;
	AVFrame *frame_ = nullptr;
	SwsContext *sws_ = nullptr;
	std::deque<RingPkt> ring_;
};

} // namespace zx

#endif // ZX_ENABLE_REPLAY
#endif
