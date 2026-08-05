// [rc4l] FUA instant replay -- press a key (default comma) to save the last N seconds of gameplay
// as a shareable H.264/MP4 clip, ShadowPlay-style. Full design: docs/instant-replay-PLAN.md.
//
// Pipeline: the GL present path (OpenGLFrameBuffer::Swap) calls WantsFrame() cheaply every frame;
// when a frame is due at the configured rate it reads back the framebuffer and calls SubmitFrame().
// SubmitFrame copies the RGB frame onto a bounded queue; a worker thread scales+encodes it into a
// rolling ring of encoded packets (ReplayEncoder). fua_clip asks the worker to mux the last N
// seconds to an .mp4. The encoder lives only on the worker thread; the queue/save-request/result
// are the only shared state. Fragile arithmetic is in computation/replay_compute.* (unit-tested).
//
// Naming: cl_fua_* CVARs (client prefix + FUA), bindable command fua_clip. If FFmpeg is unavailable
// (ZX_ENABLE_REPLAY undefined) the feature degrades to a no-capture stub so the build still works.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include <ctime>

#include "c_cvars.h"
#include "c_console.h"    // Printf
#include "c_dispatch.h"   // CCMD
#include "features/replay/computation/replay_compute.h"

// [rc4l] Master on/off, default ON: an instant replay is only useful if it was already running when
// the thing worth keeping happened. A recorder you have to switch on first cannot catch the shot you
// did not know you were about to make, which is the whole point of a rolling buffer -- so this is
// opt-OUT (`cl_fua_replay 0`), not opt-in.
//
// The cost is bounded and configurable below: a rolling buffer of cl_fua_replay_duration seconds at
// cl_fua_replay_fps, capped at cl_fua_replay_maxheight, encoded on a worker thread off the render
// path. Nothing reaches disk until the player asks for a clip -- the buffer is overwritten in place.
CVAR(Bool, cl_fua_replay,           true,  CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// Seconds of history kept in the rolling buffer (the "last N seconds").
CVAR(Int,  cl_fua_replay_duration,  15,    CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// Captured frames per second, independent of the game's frame rate.
CVAR(Int,  cl_fua_replay_fps,       30,    CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// Max captured height; width scales to aspect (0 = native).
CVAR(Int,  cl_fua_replay_maxheight, 720,   CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// Target bitrate in Mbit/s.
CVAR(Int,  cl_fua_replay_bitrate,   12,    CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// Encoder preference: 0 auto (hardware then software), 1 force software (x264), 2 force hardware.
CVAR(Int,  cl_fua_replay_encoder,   0,     CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// Experimental: capture game audio into clips. Routes OpenAL through a loopback device at startup,
// so it only takes effect on the next launch. Default off.
CVAR(Bool, cl_fua_replay_audio,     false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

#ifdef ZX_ENABLE_REPLAY

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#ifdef _WIN32
#include <direct.h>   // _mkdir
#else
#include <sys/stat.h> // mkdir
#endif
#include <thread>
#include <vector>

#include "features/replay/zx_replay.h"
#include "features/replay/zx_replay_encoder.h"
#include "i_system.h"   // atterm(): clean-shutdown hook. Join the capture worker before the process
                        // exits, else the global std::thread is destroyed while joinable ->
                        // std::terminate (and an in-flight clip mux is truncated).

namespace {

void ReplayAtTerm(); // joins the worker on engine shutdown (defined below)


struct RawFrame  { std::vector<unsigned char> rgb; int w; int h; int64_t tUs; };
struct AudioChunk { std::vector<float> pcm; int rate; int64_t tUs; };  // interleaved stereo

std::thread            g_worker;
std::mutex             g_mtx;
std::condition_variable g_cv;
std::deque<RawFrame>   g_queue;          // raw frames awaiting encode (bounded)
std::deque<AudioChunk> g_audioQueue;     // captured audio awaiting encode (bounded)
std::string            g_saveReq;        // non-empty => worker should mux a clip to this path
bool                   g_stop = false;
bool                   g_running = false; // game-thread view of worker liveness

// worker -> game-thread result, flushed to the console on the game thread
std::mutex             g_resMtx;
bool                   g_resPending = false;
bool                   g_resOk = false;
std::string            g_resPath;
std::string            g_pendingInfo;   // one-shot status line (e.g. which encoder was selected)

int64_t                g_lastCaptureUs = 0; // game thread only

int64_t NowUs()
{
	using namespace std::chrono;
	return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// Ordered list of H.264 encoders to try. The platform hardware encoder is tried first (near-zero
// CPU); an encoder that isn't built into this ffmpeg, or can't open (no GPU), simply fails and we
// fall through to the next. cl_fua_replay_encoder: 0 = auto (hardware then software), 1 = software
// only, 2 = hardware only.
std::vector<const char *> EncoderCandidates()
{
	std::vector<const char *> hw;
#if defined(__APPLE__)
	hw = { "h264_videotoolbox" };
#elif defined(_WIN32)
	hw = { "h264_nvenc", "h264_amf", "h264_qsv" };
#else
	// Linux VAAPI needs a hw-frames context (a separate effort); use software there for now.
	hw = {};
#endif
	if (cl_fua_replay_encoder == 2) return hw;                 // hardware only
	if (cl_fua_replay_encoder == 1) return { "libx264" };      // software only
	std::vector<const char *> out = hw;                        // auto: hardware first...
	out.push_back("libx264");                                  // ...then software fallback
	return out;
}

// Clips land in the platform's standard video folder, under a ZandroX subfolder.
std::string ClipsDir()
{
#ifdef _WIN32
	const char *base = getenv("USERPROFILE");
	std::string parent = (base ? std::string(base) : std::string(".")) + "\\Videos";
	std::string dir = parent + "\\ZandroX";
	_mkdir(parent.c_str());   // ensure the video folder exists (no-op if it already does)
	_mkdir(dir.c_str());
#else
	const char *home = getenv("HOME");
	std::string base = home ? std::string(home) : std::string(".");
#ifdef __APPLE__
	std::string parent = base + "/Movies";   // standard macOS video location
#else
	std::string parent = base + "/Videos";   // XDG-style video location on Linux
#endif
	std::string dir = parent + "/ZandroX";
	mkdir(parent.c_str(), 0755);   // ensure the video folder exists (no-op if it already does)
	mkdir(dir.c_str(), 0755);
#endif
	return dir;
}

void WorkerLoop()
{
	zx::ReplayEncoder enc;
	bool inited = false;
	bool triedInit = false;   // pick the encoder once, on the first frame's dimensions
	bool audioInited = false;

	for (;;)
	{
		RawFrame frame;
		AudioChunk audio;
		std::string saveReq;
		bool haveFrame = false, haveAudio = false;
		{
			std::unique_lock<std::mutex> lk(g_mtx);
			g_cv.wait(lk, [] { return g_stop || !g_queue.empty() || !g_audioQueue.empty() || !g_saveReq.empty(); });
			if (g_stop && g_queue.empty() && g_audioQueue.empty() && g_saveReq.empty()) break;
			if (!g_queue.empty()) { frame = std::move(g_queue.front()); g_queue.pop_front(); haveFrame = true; }
			if (!g_audioQueue.empty()) { audio = std::move(g_audioQueue.front()); g_audioQueue.pop_front(); haveAudio = true; }
			saveReq.swap(g_saveReq);
		}

		// Audio needs the video encoder initialised first (it sets up alongside it).
		if (haveAudio && inited)
		{
			if (!audioInited) audioInited = enc.InitAudio(audio.rate);
			if (audioInited) enc.AddAudioInterleaved(audio.pcm.data(), (int)(audio.pcm.size() / 2), audio.tUs);
		}

		if (haveFrame)
		{
			if (!inited && !triedInit)
			{
				triedInit = true;
				zx::ScaledDims d = zx::ComputeScaledDims(frame.w, frame.h, cl_fua_replay_maxheight);
				for (const char *name : EncoderCandidates())
				{
					if (enc.Init(frame.w, frame.h, d.w, d.h, cl_fua_replay_fps,
								 cl_fua_replay_bitrate * 1000, name))
					{
						inited = true;
						std::lock_guard<std::mutex> lk(g_resMtx);
						g_pendingInfo = std::string("Instant replay recording (") + name + ").";
						break;
					}
				}
				if (!inited)
				{
					std::lock_guard<std::mutex> lk(g_resMtx);
					g_pendingInfo = "Instant replay: no usable H.264 encoder found.";
				}
			}
			enc.SetWindow(cl_fua_replay_duration);
			if (inited)
				enc.AddFrameTopDownRGB(frame.rgb.data(), frame.w, frame.h, frame.w * 3, frame.tUs);
		}

		if (!saveReq.empty())
		{
			bool ok = inited && enc.SaveClip(saveReq.c_str(), cl_fua_replay_duration);
			std::lock_guard<std::mutex> lk(g_resMtx);
			g_resPending = true; g_resOk = ok; g_resPath = saveReq;
		}
	}

	enc.Shutdown();
}

void StartCapture()
{
	// Register the shutdown hook once so the worker is always joined before the process exits.
	static bool s_termHook = false;
	if (!s_termHook) { atterm(ReplayAtTerm); s_termHook = true; }

	{
		std::lock_guard<std::mutex> lk(g_mtx);
		g_stop = false;
		g_queue.clear();
		g_audioQueue.clear();
		g_saveReq.clear();
	}
	g_worker = std::thread(WorkerLoop);
	g_running = true;
	g_lastCaptureUs = 0;
}

void StopCapture()
{
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		g_stop = true;
	}
	g_cv.notify_all();
	if (g_worker.joinable()) g_worker.join();
	g_running = false;
}

// atterm hook: runs on clean engine shutdown. Idempotent (StopCapture no-ops if already stopped),
// and because a pending save request is drained before the worker honours g_stop, an in-flight clip
// finishes writing rather than being truncated on quit.
void ReplayAtTerm()
{
	if (g_running) StopCapture();
}

void FlushMessages()
{
	std::string path, info; bool ok = false, pending = false;
	{
		std::lock_guard<std::mutex> lk(g_resMtx);
		if (g_resPending) { pending = true; ok = g_resOk; path = g_resPath; g_resPending = false; }
		if (!g_pendingInfo.empty()) { info.swap(g_pendingInfo); }
	}
	if (!info.empty()) Printf("%s\n", info.c_str());
	if (!pending) return;
	if (ok) Printf("Saved replay clip: %s\n", path.c_str());
	else Printf("Replay clip failed (no footage buffered yet, or encoder error).\n");
}

} // namespace

namespace zx {
namespace replay {

bool WantsFrame()
{
	FlushMessages();

	if (!cl_fua_replay)
	{
		if (g_running) StopCapture();
		return false;
	}
	if (!g_running) StartCapture();

	const int fps = int(cl_fua_replay_fps) > 0 ? int(cl_fua_replay_fps) : 30;
	return zx::ComputeFrameDue(g_lastCaptureUs, NowUs(), fps);
}

void SubmitFrame(const unsigned char *rgbTopRow, int w, int h, int pitch)
{
	if (!g_running || rgbTopRow == nullptr || w <= 0 || h <= 0) return;

	RawFrame f;
	f.w = w; f.h = h; f.tUs = NowUs();
	f.rgb.resize((size_t)w * h * 3);
	// Copy to a top-down tightly-packed buffer (pitch may be negative for GL bottom-up readback).
	for (int y = 0; y < h; ++y)
		std::memcpy(f.rgb.data() + (size_t)y * w * 3, rgbTopRow + (ptrdiff_t)y * pitch, (size_t)w * 3);

	g_lastCaptureUs = f.tUs;
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		if (g_queue.size() < 8) g_queue.push_back(std::move(f)); // bounded: drop if worker is behind
	}
	g_cv.notify_one();
}

bool AudioCaptureEnabled()
{
	return cl_fua_replay_audio;
}

void SubmitAudio(const float *interleavedStereo, int nSamples, int sampleRate, long long tUs)
{
	if (!g_running || interleavedStereo == nullptr || nSamples <= 0) return;
	(void)tUs;   // timestamp on the capture clock (same as video) so A/V align in SaveClip
	AudioChunk c;
	c.rate = sampleRate;
	c.tUs = NowUs();
	c.pcm.assign(interleavedStereo, interleavedStereo + (size_t)nSamples * 2);
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		if (g_audioQueue.size() < 64) g_audioQueue.push_back(std::move(c)); // bounded
	}
	g_cv.notify_one();
}

} // namespace replay
} // namespace zx

// Save the last cl_fua_replay_duration seconds. Bound to comma by default (c_bind.cpp), rebindable
// under Options > FUA Options and in Customize Controls.
CCMD(fua_clip)
{
	if (!cl_fua_replay)
	{
		Printf("Instant replay is off. Turn it on in Options > FUA Options, or 'cl_fua_replay 1'.\n");
		return;
	}
	if (!g_running)
	{
		Printf("Instant replay is warming up -- play for a moment, then try again.\n");
		return;
	}

	time_t now = time(nullptr);
	struct tm lt;
#ifdef _WIN32
	localtime_s(&lt, &now);
#else
	localtime_r(&now, &lt);
#endif
	zx::ClipStamp stamp{ lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec };
	char name[64];
	zx::ComputeClipFilename(name, sizeof(name), stamp);
	std::string path = ClipsDir() + "/" + name;

	{
		std::lock_guard<std::mutex> lk(g_mtx);
		g_saveReq = path;
	}
	g_cv.notify_one();
	Printf("Saving replay clip...\n");
}

#else // !ZX_ENABLE_REPLAY -- FFmpeg not available; keep the command as a no-capture stub.

CCMD(fua_clip)
{
	Printf("Instant replay was not built into this binary (no FFmpeg at build time).\n");
}

#endif // ZX_ENABLE_REPLAY
