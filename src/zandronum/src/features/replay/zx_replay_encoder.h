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
#include <vector>

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
	// Top-down RGB24 of the CURRENT framebuffer (srcW x srcH, srcStride may be negative); tUs is a
	// monotonic capture timestamp in microseconds. srcW/srcH are per-frame so a mid-capture window or
	// render-scale resize is handled -- the frame is scaled to the fixed output size.
	void AddFrameTopDownRGB(const uint8_t *rgb, int srcW, int srcH, int srcStride, int64_t tUs);
	// Optional audio: set up an AAC stream at the given rate (stereo assumed). AddAudioInterleaved
	// takes interleaved float samples (nSamples = per-channel count) captured at time tUs; they are
	// framed, encoded, and ring-buffered alongside the video, then muxed as a 2nd stream by SaveClip.
	bool InitAudio(int sampleRate);
	void AddAudioInterleaved(const float *pcm, int nSamples, int64_t tUs);
	bool HasAudio() const { return aenc_ != nullptr; }

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
	void DrainAudio(AVFrame *f, int64_t tUs);
	void EvictAudio();

	int srcW_ = 0, srcH_ = 0, dstW_ = 0, dstH_ = 0, fps_ = 30, windowSecs_ = 15;
	// PTS is derived from real capture time (not a frame counter) so a clip's playback duration
	// always equals the wall-clock span it covers -- even if the game rendered below the capture
	// rate, the clip stays real-time (choppy) instead of fast-forwarding.
	int64_t firstUs_ = -1, lastPts_ = -1, lastUs_ = 0;
	AVCodecContext *enc_ = nullptr;
	AVFrame *frame_ = nullptr;
	SwsContext *sws_ = nullptr;
	std::deque<RingPkt> ring_;

	// Audio (optional): AAC encoder + a parallel packet ring. Incoming samples are accumulated into
	// full encoder frames; pts is a continuous sample counter (glitch-free), tUs tracks wall time
	// for windowing/sync.
	struct APkt { AVPacket *pkt; int64_t tUs; };
	AVCodecContext *aenc_ = nullptr;
	AVFrame *aframe_ = nullptr;
	std::deque<APkt> aring_;
	std::vector<float> aAccum_;   // leftover interleaved-stereo samples between calls
	int aSampleRate_ = 0, aFrameSamples_ = 0;
	int64_t aPts_ = 0, aAccumUs_ = 0;
};

} // namespace zx

#endif // ZX_ENABLE_REPLAY
#endif
