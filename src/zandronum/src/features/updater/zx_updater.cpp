// [rc4l] See zx_updater.h. Update-notice state, the background GitHub-releases check, and the CCMDs.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/updater/zx_updater.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstring>

#include "zstring.h"
#include "c_dispatch.h"   // CCMD
#include "c_cvars.h"      // CVAR
#include "gitinfo.h"      // GIT_DESCRIPTION (the running build's tag)
#include "doomtype.h"     // Printf
#include "i_system.h"     // I_MSTime
#include "features/net/zx_httpsget.h"   // zx::HttpsGet (shared with the server registry list fetch)
#include "features/updater/computation/release_url_compute.h"

// [rc4l] On by default: the client checks GitHub once at startup for a newer release and shows the
// bottom-right main-menu notice if one exists. Turn off in Options > FUA Options to disable the check
// entirely (no network call). CVAR_GLOBALCONFIG so it's remembered across WADs/instances.
CVAR(Bool, cl_fua_update_notify, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

// The GitHub endpoint we check (matches the repo release_url_compute builds download URLs for).
#define ZX_UPDATE_API_HOST "api.github.com"
#define ZX_UPDATE_API_PATH "/repos/rc4l/ForkUnderA/releases/latest"

namespace zx { namespace updater {

namespace {

std::atomic<bool> g_available{ false };
std::mutex g_mtx;                  // guards g_tag reads/writes
char g_tag[64] = { 0 };

// The worker records its verdict here for the MAIN thread to log later (Printf is not thread-safe).
// -1 = not run yet; 0 = up to date; 1 = update available; 2 = no network; 3 = malformed response.
std::atomic<int> g_verdict{ -1 };

const char *CurrentDescribe() { return GIT_DESCRIPTION; }

// One check on the worker thread: fetch, decide, update the (thread-safe) notice state, and record a
// verdict code for the main thread to log. MUST NOT touch the console/CVARs/any engine global that
// isn't thread-safe -- Printf here crashed on first launch. StartCheck already gated on the cvar.
void RunCheckOnce()
{
	char body[65536];
	bool ok = HttpsGet(ZX_UPDATE_API_HOST, ZX_UPDATE_API_PATH, body, sizeof body);
	UpdateCheckResult r = ComputeUpdateCheckResult(ok, ok ? body : "", CurrentDescribe());
	int verdict = 2; // NoNetwork
	switch (r.status)
	{
	case UpdateCheckStatus::UpdateAvailable: SetLatestTag(r.tag); verdict = 1; break;
	case UpdateCheckStatus::UpToDate:        Clear();             verdict = 0; break;
	case UpdateCheckStatus::Malformed:                            verdict = 3; break;
	case UpdateCheckStatus::NoNetwork:                            verdict = 2; break;
	}
	g_verdict.store(verdict, std::memory_order_release);
}

} // namespace

void SetLatestTag(const char *tag)
{
	if (tag == NULL || tag[0] == '\0' || !zx::IsNewerVersion(CurrentDescribe(), tag))
	{
		Clear();
		return;
	}
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		std::snprintf(g_tag, sizeof g_tag, "%s", tag);
	}
	g_available.store(true, std::memory_order_release);
}

void Clear()
{
	g_available.store(false, std::memory_order_release);
	std::lock_guard<std::mutex> lk(g_mtx);
	g_tag[0] = '\0';
}

bool IsAvailable()
{
	return g_available.load(std::memory_order_acquire);
}

const char *Tag()
{
	// Copy under the lock into a per-thread buffer so a concurrent worker write can't tear the read.
	static thread_local char snap[64];
	std::lock_guard<std::mutex> lk(g_mtx);
	std::snprintf(snap, sizeof snap, "%s", g_tag);
	return snap;
}

void StartCheck()
{
	if (!cl_fua_update_notify)
		return;
	std::thread(RunCheckOnce).detach(); // fire-and-forget; never blocks startup
}

void DrainLog()
{
	static int logged = -1;                             // main-thread only; no locking needed
	int v = g_verdict.load(std::memory_order_acquire);
	if (v == -1 || v == logged)
		return;
	logged = v;
	switch (v)
	{
	case 0: Printf("update check: up to date\n"); break;
	case 1: Printf("update check: a newer ZandroX release is available (%s)\n", Tag()); break;
	case 2: Printf("update check: could not reach the update server\n"); break;
	case 3: Printf("update check: could not read the release info\n"); break;
	}
}

void Tick()
{
	// Defer the check to a bit after the main loop starts (idle at the title screen), so the worker
	// never runs during the heavy first-launch init -- that contention crashed on a fresh first launch.
	static bool started = false;
	static unsigned firstMs = 0;
	if (!started)
	{
		unsigned now = I_MSTime();
		if (firstMs == 0)
			firstMs = now == 0 ? 1 : now; // remember the first frame's time (avoid 0 = "unset")
		else if (now - firstMs >= 1500)
		{
			started = true;
			StartCheck();
		}
	}
	DrainLog();
}

} } // namespace zx::updater

// [rc4l] Run the update check now (handy for testing without a restart; it also runs once at startup).
CCMD(fua_check_update)
{
	Printf("running update check...\n");
	zx::updater::StartCheck();
}

// [rc4l] Force the notice to a specific tag (testing/manual). Real arming comes from the background
// check above; kept for driving the menu without a live network.
CCMD(fua_update_notify)
{
	if (argv.argc() < 2)
	{
		Printf("usage: fua_update_notify <tag>   (e.g. fua_update_notify v0.1.19)\n");
		return;
	}
	zx::updater::SetLatestTag(argv[1]);
	if (zx::updater::IsAvailable())
		Printf("update notice armed for %s\n", zx::updater::Tag());
	else
		Printf("no notice: %s is not newer than this build\n", argv[1]);
}

CCMD(fua_update_notify_clear)
{
	zx::updater::Clear();
	Printf("update notice cleared\n");
}
