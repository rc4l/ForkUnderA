// [rc4l] See zx_replay_encoder.h. Ported from a standalone proof that was validated against ffmpeg
// 8.x (libx264 -> valid H.264/MP4, confirmed with ffprobe). No engine headers here on purpose.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/replay/zx_replay_encoder.h"

#ifdef ZX_ENABLE_REPLAY

#include <cstdio>
#include <cstring>
#include <vector>

#include "features/replay/computation/replay_compute.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/version.h>
}

// The new AVChannelLayout API (ch_layout / av_channel_layout_default) arrived in libavutil 57.24
// (FFmpeg 5.1). Older distro ffmpeg (e.g. Ubuntu) still uses channel_layout + channels.
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
#define ZX_NEW_CHLAYOUT 1
#endif

namespace zx {

bool ReplayEncoder::Init(int srcW, int srcH, int dstW, int dstH, int fps, int bitrateKbps,
						 const char *encName)
{
	srcW_ = srcW; srcH_ = srcH; dstW_ = dstW; dstH_ = dstH; fps_ = fps > 0 ? fps : 30;

	const AVCodec *codec = avcodec_find_encoder_by_name(encName);
	if (!codec) return false;
	enc_ = avcodec_alloc_context3(codec);
	if (!enc_) return false;
	enc_->width = dstW_;
	enc_->height = dstH_;
	enc_->time_base = AVRational{ 1, fps_ };
	enc_->framerate = AVRational{ fps_, 1 };
	enc_->pix_fmt = AV_PIX_FMT_YUV420P;
	enc_->bit_rate = (int64_t)bitrateKbps * 1000;
	enc_->gop_size = fps_;      // ~1 keyframe/sec so a clip can start on a clean GOP boundary
	enc_->max_b_frames = 0;     // no B-frames -> dts==pts, muxing stays trivial
	if (!std::strcmp(encName, "libx264"))
	{
		av_opt_set(enc_->priv_data, "preset", "ultrafast", 0);
		av_opt_set(enc_->priv_data, "tune", "zerolatency", 0);
		// VBV rate cap so the target bitrate is actually honoured (ultrafast otherwise overshoots by
		// several x, producing needlessly huge files). Hardware encoders manage their own rate
		// control from bit_rate, so this is scoped to x264. ~1 second buffer.
		enc_->rc_max_rate = enc_->bit_rate;
		enc_->rc_buffer_size = (int)enc_->bit_rate;
	}
	if (avcodec_open2(enc_, codec, nullptr) < 0)
	{
		avcodec_free_context(&enc_);
		return false;
	}

	frame_ = av_frame_alloc();
	frame_->format = AV_PIX_FMT_YUV420P;
	frame_->width = dstW_;
	frame_->height = dstH_;
	if (av_frame_get_buffer(frame_, 0) < 0) return false;

	// The swscale context is (re)built lazily per source size in AddFrameTopDownRGB, so a mid-capture
	// window/render-scale resize is handled rather than assumed away. Output size stays fixed.
	return true;
}

void ReplayEncoder::AddFrameTopDownRGB(const uint8_t *rgb, int srcW, int srcH, int srcStride, int64_t tUs)
{
	if (!enc_ || srcW <= 0 || srcH <= 0) return;
	// Reuse the scaler while the source size is unchanged; rebuild it (cached) only on a resize. The
	// destination stays dstW_ x dstH_, so every encoded packet is the same size and remains muxable.
	sws_ = sws_getCachedContext(sws_, srcW, srcH, AV_PIX_FMT_RGB24, dstW_, dstH_, AV_PIX_FMT_YUV420P,
							   SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (!sws_) return;
	const uint8_t *src[4] = { rgb, nullptr, nullptr, nullptr };
	int srcS[4] = { srcStride, 0, 0, 0 };
	if (av_frame_make_writable(frame_) < 0) return;
	sws_scale(sws_, src, srcS, 0, srcH, frame_->data, frame_->linesize);

	// Real-time PTS: map the capture timestamp onto the encoder's 1/fps timebase. At full capture
	// rate this yields 0,1,2,3,... (smooth); if the game lagged, PTS gaps keep playback real-time.
	if (firstUs_ < 0) firstUs_ = tUs;
	int64_t pts = ((tUs - firstUs_) * fps_ + 500000) / 1000000;
	if (pts <= lastPts_) pts = lastPts_ + 1;  // keep strictly monotonic
	lastPts_ = pts;
	frame_->pts = pts;
	Drain(frame_, tUs);
}

void ReplayEncoder::Drain(AVFrame *f, int64_t tUs)
{
	if (avcodec_send_frame(enc_, f) < 0) return;
	for (;;)
	{
		AVPacket *pkt = av_packet_alloc();
		int r = avcodec_receive_packet(enc_, pkt);
		if (r < 0) { av_packet_free(&pkt); break; }
		bool key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
		ring_.push_back(RingPkt{ pkt, tUs, key });
		lastUs_ = tUs;
		Evict();
	}
}

void ReplayEncoder::Evict()
{
	// Drop whole leading GOPs (keyframe-to-next-keyframe) that are entirely older than the keep
	// window, so the ring always still begins on a keyframe and a saved clip decodes from its first
	// packet. Keep windowSecs plus a small margin.
	const int64_t keep = (int64_t)(windowSecs_ + 2) * 1000000;
	for (;;)
	{
		// Find the start of the second GOP (the 2nd keyframe in the ring).
		size_t second = 0; int kf = 0;
		for (size_t i = 0; i < ring_.size(); ++i)
			if (ring_[i].key && ++kf == 2) { second = i; break; }
		if (kf < 2) break;   // fewer than two GOPs buffered -> keep everything

		// Only drop the first GOP if its last packet is already outside the keep window.
		if (lastUs_ - ring_[second - 1].tUs <= keep) break;
		for (size_t i = 0; i < second; ++i) { av_packet_free(&ring_.front().pkt); ring_.pop_front(); }
	}
}

bool ReplayEncoder::InitAudio(int sampleRate)
{
	const AVCodec *codec = avcodec_find_encoder_by_name("aac");
	if (!codec) return false;
	aenc_ = avcodec_alloc_context3(codec);
	if (!aenc_) return false;
	aenc_->sample_fmt = AV_SAMPLE_FMT_FLTP;
	aenc_->sample_rate = sampleRate;
	aenc_->bit_rate = 128000;
#ifdef ZX_NEW_CHLAYOUT
	av_channel_layout_default(&aenc_->ch_layout, 2);
#else
	aenc_->channel_layout = AV_CH_LAYOUT_STEREO;
	aenc_->channels = 2;
#endif
	aenc_->time_base = AVRational{ 1, sampleRate };
	if (avcodec_open2(aenc_, codec, nullptr) < 0) { avcodec_free_context(&aenc_); return false; }
	aSampleRate_ = sampleRate;
	aFrameSamples_ = aenc_->frame_size > 0 ? aenc_->frame_size : 1024;
	aframe_ = av_frame_alloc();
	aframe_->format = AV_SAMPLE_FMT_FLTP;
	aframe_->sample_rate = sampleRate;
#ifdef ZX_NEW_CHLAYOUT
	av_channel_layout_default(&aframe_->ch_layout, 2);
#else
	aframe_->channel_layout = AV_CH_LAYOUT_STEREO;
	aframe_->channels = 2;
#endif
	aframe_->nb_samples = aFrameSamples_;
	return av_frame_get_buffer(aframe_, 0) >= 0;
}

void ReplayEncoder::AddAudioInterleaved(const float *pcm, int nSamples, int64_t tUs)
{
	if (!aenc_ || pcm == nullptr || nSamples <= 0) return;
	if (aAccum_.empty()) aAccumUs_ = tUs;            // wall time of the accumulator's first sample
	aAccum_.insert(aAccum_.end(), pcm, pcm + (size_t)nSamples * 2);

	const size_t frameFloats = (size_t)aFrameSamples_ * 2;
	while (aAccum_.size() >= frameFloats)
	{
		if (av_frame_make_writable(aframe_) < 0) break;
		float *L = reinterpret_cast<float *>(aframe_->data[0]);
		float *R = reinterpret_cast<float *>(aframe_->data[1]);
		for (int i = 0; i < aFrameSamples_; ++i) { L[i] = aAccum_[(size_t)i * 2]; R[i] = aAccum_[(size_t)i * 2 + 1]; }
		aframe_->nb_samples = aFrameSamples_;
		aframe_->pts = aPts_;                        // continuous sample counter -> glitch-free AAC
		const int64_t frameUs = aAccumUs_;
		aPts_ += aFrameSamples_;
		aAccumUs_ += (int64_t)aFrameSamples_ * 1000000 / aSampleRate_;
		aAccum_.erase(aAccum_.begin(), aAccum_.begin() + frameFloats);
		DrainAudio(aframe_, frameUs);
	}
}

void ReplayEncoder::DrainAudio(AVFrame *f, int64_t tUs)
{
	if (avcodec_send_frame(aenc_, f) < 0) return;
	for (;;)
	{
		AVPacket *pkt = av_packet_alloc();
		int r = avcodec_receive_packet(aenc_, pkt);
		if (r < 0) { av_packet_free(&pkt); break; }
		aring_.push_back(APkt{ pkt, tUs });
	}
	EvictAudio();
}

void ReplayEncoder::EvictAudio()
{
	const int64_t keep = (int64_t)(windowSecs_ + 2) * 1000000;
	while (aring_.size() > 2 && (lastUs_ - aring_.front().tUs) > keep)
	{
		av_packet_free(&aring_.front().pkt);
		aring_.pop_front();
	}
}

bool ReplayEncoder::SaveClip(const char *path, int windowSecs)
{
	if (!enc_) return false;
	// [ZandroX] Do NOT send a terminal (nullptr) flush here. avcodec_send_frame(enc_, NULL) puts the
	// encoder into a permanent draining/EOF state, after which every later AddFrame's send_frame
	// returns AVERROR_EOF and no new packets ever reach the ring -- so the 2nd and later clips in a
	// session (e.g. one per wad_reload) freeze on the first clip's footage. The encoder runs with
	// max_b_frames=0 + zerolatency, so each AddFrame's receive loop already drains every emitted
	// packet into the ring; the ring is complete except for at most the encoder's in-flight pipeline
	// tail (~a couple of frames on VideoToolbox, zero on x264), which is imperceptible for a replay.
	if (ring_.empty()) return false;

	// Pick the start packet with the pure, unit-tested selector (handles the "session shorter than
	// the window" case by saving the whole buffer, and bails when nothing is decodable).
	std::vector<int64_t> times(ring_.size());
	std::vector<unsigned char> keys(ring_.size());
	for (size_t i = 0; i < ring_.size(); ++i) { times[i] = ring_[i].tUs; keys[i] = ring_[i].key ? 1 : 0; }
	const int startIdx = ComputeClipStartIndex(times.data(), keys.data(),
											   (int)ring_.size(), lastUs_, windowSecs);
	if (startIdx < 0) return false;
	const size_t start = (size_t)startIdx;

	const int64_t clipStartUs = ring_[start].tUs;

	// Flush the audio encoder and pick the first audio packet at/after the clip start, so audio and
	// video are aligned by capture time. Audio is optional -- a video-only clip is still valid.
	bool haveAudio = false;
	size_t aStart = 0;
	if (aenc_ != nullptr)
	{
		// Same reasoning as the video path: a terminal flush would EOF the audio encoder and freeze
		// all later clips. The audio ring already holds every emitted packet.
		for (size_t i = 0; i < aring_.size(); ++i)
			if (aring_[i].tUs >= clipStartUs) { aStart = i; haveAudio = true; break; }
	}

	AVFormatContext *oc = nullptr;
	avformat_alloc_output_context2(&oc, nullptr, nullptr, path);
	if (!oc) return false;
	AVStream *vst = avformat_new_stream(oc, nullptr);
	if (!vst) { avformat_free_context(oc); return false; }
	avcodec_parameters_from_context(vst->codecpar, enc_);
	vst->time_base = enc_->time_base;
	AVStream *ast = nullptr;
	if (haveAudio)
	{
		ast = avformat_new_stream(oc, nullptr);
		avcodec_parameters_from_context(ast->codecpar, aenc_);
		ast->time_base = aenc_->time_base;
	}
	if (avio_open(&oc->pb, path, AVIO_FLAG_WRITE) < 0) { avformat_free_context(oc); return false; }
	if (avformat_write_header(oc, nullptr) < 0) { avio_closep(&oc->pb); avformat_free_context(oc); return false; }

	const int64_t vbase = ring_[start].pkt->pts;
	const int64_t abase = haveAudio ? aring_[aStart].pkt->pts : 0;
	size_t vi = start, ai = aStart;
	int written = 0;
	// Interleave the two rings by presentation time so the muxer stays monotonic across streams.
	for (;;)
	{
		const bool haveV = vi < ring_.size();
		const bool haveA = haveAudio && ai < aring_.size();
		if (!haveV && !haveA) break;
		bool takeV;
		if (!haveA) takeV = true;
		else if (!haveV) takeV = false;
		else
		{
			const double vt = av_q2d(enc_->time_base)  * (ring_[vi].pkt->pts  - vbase);
			const double at = av_q2d(aenc_->time_base) * (aring_[ai].pkt->pts - abase);
			takeV = vt <= at;
		}
		AVPacket *p = av_packet_clone(takeV ? ring_[vi].pkt : aring_[ai].pkt);
		if (takeV) ++vi; else ++ai;
		if (!p) continue;
		if (takeV)
		{
			p->stream_index = 0; p->pts -= vbase; p->dts = p->pts;
			av_packet_rescale_ts(p, enc_->time_base, vst->time_base);
			++written;
		}
		else
		{
			p->stream_index = 1; p->pts -= abase; p->dts = p->pts;
			av_packet_rescale_ts(p, aenc_->time_base, ast->time_base);
		}
		av_interleaved_write_frame(oc, p);
		av_packet_free(&p);
	}
	av_write_trailer(oc);
	avio_closep(&oc->pb);
	avformat_free_context(oc);
	return written > 0;
}

void ReplayEncoder::Shutdown()
{
	for (auto &r : ring_) av_packet_free(&r.pkt);
	ring_.clear();
	if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
	if (frame_) av_frame_free(&frame_);
	if (enc_) avcodec_free_context(&enc_);

	for (auto &r : aring_) av_packet_free(&r.pkt);
	aring_.clear();
	if (aframe_) av_frame_free(&aframe_);
	if (aenc_) avcodec_free_context(&aenc_);
}

} // namespace zx

#endif // ZX_ENABLE_REPLAY
