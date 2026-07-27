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
}

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
	// VBV rate cap so the target bitrate is actually honoured (ultrafast otherwise overshoots by
	// several x, producing needlessly huge, hard-to-share files). ~1 second buffer.
	enc_->rc_max_rate = enc_->bit_rate;
	enc_->rc_buffer_size = (int)enc_->bit_rate;
	if (!std::strcmp(encName, "libx264"))
	{
		av_opt_set(enc_->priv_data, "preset", "ultrafast", 0);
		av_opt_set(enc_->priv_data, "tune", "zerolatency", 0);
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

	sws_ = sws_getContext(srcW_, srcH_, AV_PIX_FMT_RGB24, dstW_, dstH_, AV_PIX_FMT_YUV420P,
						  SWS_BILINEAR, nullptr, nullptr, nullptr);
	return sws_ != nullptr;
}

void ReplayEncoder::AddFrameTopDownRGB(const uint8_t *rgb, int srcStride, int64_t tUs)
{
	if (!enc_ || !sws_) return;
	const uint8_t *src[4] = { rgb, nullptr, nullptr, nullptr };
	int srcS[4] = { srcStride, 0, 0, 0 };
	if (av_frame_make_writable(frame_) < 0) return;
	sws_scale(sws_, src, srcS, 0, srcH_, frame_->data, frame_->linesize);

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

bool ReplayEncoder::SaveClip(const char *path, int windowSecs)
{
	if (!enc_) return false;
	Drain(nullptr, lastUs_); // flush any queued frames
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

	AVFormatContext *oc = nullptr;
	avformat_alloc_output_context2(&oc, nullptr, nullptr, path);
	if (!oc) return false;
	AVStream *st = avformat_new_stream(oc, nullptr);
	if (!st) { avformat_free_context(oc); return false; }
	avcodec_parameters_from_context(st->codecpar, enc_);
	st->time_base = enc_->time_base;
	if (avio_open(&oc->pb, path, AVIO_FLAG_WRITE) < 0) { avformat_free_context(oc); return false; }
	if (avformat_write_header(oc, nullptr) < 0) { avio_closep(&oc->pb); avformat_free_context(oc); return false; }

	const int64_t basePts = ring_[start].pkt->pts;
	int written = 0;
	for (size_t i = start; i < ring_.size(); ++i)
	{
		AVPacket *p = av_packet_clone(ring_[i].pkt);
		if (!p) continue;
		p->stream_index = 0;
		p->pts -= basePts;
		p->dts = p->pts;
		av_packet_rescale_ts(p, enc_->time_base, st->time_base);
		av_interleaved_write_frame(oc, p);
		av_packet_free(&p);
		++written;
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
}

} // namespace zx

#endif // ZX_ENABLE_REPLAY
