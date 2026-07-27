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

// Master on/off -- default off so the feature costs nothing until the player opts in.
CVAR(Bool, cl_fua_replay,           false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
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


struct RawFrame { std::vector<unsigned char> rgb; int w; int h; int64_t tUs; };

std::thread            g_worker;
std::mutex             g_mtx;
std::condition_variable g_cv;
std::deque<RawFrame>   g_queue;          // raw frames awaiting encode (bounded)
std::string            g_saveReq;        // non-empty => worker should mux a clip to this path
bool                   g_stop = false;
bool                   g_running = false; // game-thread view of worker liveness

// worker -> game-thread result, flushed to the console on the game thread
std::mutex             g_resMtx;
bool                   g_resPending = false;
bool                   g_resOk = false;
std::string            g_resPath;

int64_t                g_lastCaptureUs = 0; // game thread only

int64_t NowUs()
{
	using namespace std::chrono;
	return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

const char *EncoderName()
{
	// Phase 2 ships the software path; 2 = force hardware (VideoToolbox on macOS). Auto stays
	// software here and becomes hardware-first in phase 3.
	return (cl_fua_replay_encoder == 2) ? "h264_videotoolbox" : "libx264";
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

	for (;;)
	{
		RawFrame frame;
		std::string saveReq;
		bool haveFrame = false;
		{
			std::unique_lock<std::mutex> lk(g_mtx);
			g_cv.wait(lk, [] { return g_stop || !g_queue.empty() || !g_saveReq.empty(); });
			if (g_stop && g_queue.empty() && g_saveReq.empty()) break;
			if (!g_queue.empty()) { frame = std::move(g_queue.front()); g_queue.pop_front(); haveFrame = true; }
			saveReq.swap(g_saveReq);
		}

		if (haveFrame)
		{
			if (!inited)
			{
				zx::ScaledDims d = zx::ComputeScaledDims(frame.w, frame.h, cl_fua_replay_maxheight);
				inited = enc.Init(frame.w, frame.h, d.w, d.h,
								  cl_fua_replay_fps, cl_fua_replay_bitrate * 1000, EncoderName());
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
	std::string path; bool ok = false, pending = false;
	{
		std::lock_guard<std::mutex> lk(g_resMtx);
		if (g_resPending) { pending = true; ok = g_resOk; path = g_resPath; g_resPending = false; }
	}
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
