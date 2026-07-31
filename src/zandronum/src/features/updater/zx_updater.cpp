// [rc4l] See zx_updater.h. Update-notice state, the background GitHub-releases check, and the CCMDs.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/updater/zx_updater.h"

// Platform networking headers at file scope (before engine headers), so no namespace juggling.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#elif !defined(__APPLE__)
#include <dlfcn.h>
#endif

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
#include "features/updater/computation/release_url_compute.h"

// [rc4l] On by default: the client checks GitHub once at startup for a newer release and shows the
// bottom-right main-menu notice if one exists. Turn off in Options > FUA Options to disable the check
// entirely (no network call). CVAR_GLOBALCONFIG so it's remembered across WADs/instances.
CVAR(Bool, cl_fua_update_notify, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

// The GitHub endpoint we check (matches the repo release_url_compute builds download URLs for).
#define ZX_UPDATE_API_HOST "api.github.com"
#define ZX_UPDATE_API_PATH "/repos/rc4l/ZandroX/releases/latest"

namespace zx { namespace updater {

namespace {

std::atomic<bool> g_available{ false };
std::mutex g_mtx;                  // guards g_tag reads/writes
char g_tag[64] = { 0 };

const char *CurrentDescribe() { return GIT_DESCRIPTION; }

// ---- platform HTTPS GET ------------------------------------------------------------------------
// Fill `out` with the body of GET https://<host><path> on a 2xx; return false (out emptied) on any
// error/timeout. Fail-safe: false means "we don't know", so the notice stays hidden.

#if defined(__APPLE__)

extern "C" bool Mac_HttpsGet(const char *urlStr, char *out, int outSize, int timeoutSecs);
bool HttpsGet(const char *host, const char *path, char *out, int outSize)
{
	char url[512];
	std::snprintf(url, sizeof url, "https://%s%s", host, path);
	return Mac_HttpsGet(url, out, outSize, 8);
}

#elif defined(_WIN32)

bool HttpsGet(const char *host, const char *path, char *out, int outSize)
{
	if (out == nullptr || outSize <= 0)
		return false;
	out[0] = '\0';

	wchar_t whost[256], wpath[512];
	MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 256);
	MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);

	bool ok = false;
	HINTERNET hSession = WinHttpOpen(L"ZandroX-updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (hSession != nullptr)
	{
		WinHttpSetTimeouts(hSession, 8000, 8000, 8000, 8000);
		HINTERNET hConnect = WinHttpConnect(hSession, whost, INTERNET_DEFAULT_HTTPS_PORT, 0);
		if (hConnect != nullptr)
		{
			HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath, nullptr,
				WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
			if (hRequest != nullptr)
			{
				if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
						WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
					WinHttpReceiveResponse(hRequest, nullptr))
				{
					DWORD status = 0, len = sizeof(status);
					WinHttpQueryHeaders(hRequest,
						WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
						WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
					if (status >= 200 && status < 300)
					{
						int total = 0;
						DWORD avail = 0;
						while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0 &&
							   total < outSize - 1)
						{
							DWORD want = avail;
							if ((int)want > outSize - 1 - total) want = (DWORD)(outSize - 1 - total);
							DWORD got = 0;
							if (!WinHttpReadData(hRequest, out + total, want, &got) || got == 0)
								break;
							total += (int)got;
						}
						out[total] = '\0';
						ok = true;
					}
				}
				WinHttpCloseHandle(hRequest);
			}
			WinHttpCloseHandle(hConnect);
		}
		WinHttpCloseHandle(hSession);
	}
	if (!ok) out[0] = '\0';
	return ok;
}

#else // Linux/other: libcurl loaded at runtime (dlopen), so it's not a hard build dependency.

struct CurlBuf { char *out; int cap; int len; };
size_t CurlWrite(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	CurlBuf *b = static_cast<CurlBuf *>(userdata);
	size_t n = size * nmemb;
	for (size_t i = 0; i < n && b->len < b->cap - 1; ++i)
		b->out[b->len++] = ptr[i];
	return n; // report full consumption even if we stopped storing, so curl doesn't abort
}
bool HttpsGet(const char *host, const char *path, char *out, int outSize)
{
	if (out == nullptr || outSize <= 0)
		return false;
	out[0] = '\0';

	void *h = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
	if (h == nullptr) h = dlopen("libcurl.so", RTLD_NOW | RTLD_LOCAL);
	if (h == nullptr) return false; // no libcurl present -> skip the check (fail-safe)

	typedef void *(*easy_init_t)();
	typedef int (*easy_setopt_t)(void *, int, ...);
	typedef int (*easy_perform_t)(void *);
	typedef void (*easy_cleanup_t)(void *);
	easy_init_t easy_init = (easy_init_t)dlsym(h, "curl_easy_init");
	easy_setopt_t easy_setopt = (easy_setopt_t)dlsym(h, "curl_easy_setopt");
	easy_perform_t easy_perform = (easy_perform_t)dlsym(h, "curl_easy_perform");
	easy_cleanup_t easy_cleanup = (easy_cleanup_t)dlsym(h, "curl_easy_cleanup");
	if (!easy_init || !easy_setopt || !easy_perform || !easy_cleanup)
	{
		dlclose(h);
		return false;
	}

	// Stable CURLOPT constants: URL=10002, WRITEFUNCTION=20011, WRITEDATA=10001, TIMEOUT=13,
	// USERAGENT=10018, FOLLOWLOCATION=52.
	char url[512];
	std::snprintf(url, sizeof url, "https://%s%s", host, path);
	CurlBuf buf{ out, outSize, 0 };
	bool ok = false;
	void *c = easy_init();
	if (c != nullptr)
	{
		easy_setopt(c, 10002, url);
		easy_setopt(c, 20011, (void *)CurlWrite);
		easy_setopt(c, 10001, &buf);
		easy_setopt(c, 13, (long)8);
		easy_setopt(c, 10018, "ZandroX-updater");
		easy_setopt(c, 52, (long)1);
		if (easy_perform(c) == 0) // CURLE_OK
		{
			out[buf.len] = '\0';
			ok = true;
		}
		easy_cleanup(c);
	}
	dlclose(h);
	if (!ok) out[0] = '\0';
	return ok;
}

#endif

// One check on the worker thread: fetch, decide, update the notice, log the verdict.
void RunCheckOnce()
{
	if (!cl_fua_update_notify)
		return;

	char body[65536];
	bool ok = HttpsGet(ZX_UPDATE_API_HOST, ZX_UPDATE_API_PATH, body, sizeof body);
	UpdateCheckResult r = ComputeUpdateCheckResult(ok, ok ? body : "", CurrentDescribe());
	switch (r.status)
	{
	case UpdateCheckStatus::UpdateAvailable:
		SetLatestTag(r.tag);
		Printf("update check: a newer ZandroX release is available (%s)\n", r.tag);
		break;
	case UpdateCheckStatus::UpToDate:
		Clear();
		Printf("update check: up to date\n");
		break;
	case UpdateCheckStatus::Malformed:
		Printf("update check: could not read the release info\n");
		break;
	case UpdateCheckStatus::NoNetwork:
		Printf("update check: could not reach the update server\n");
		break;
	}
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

} } // namespace zx::updater

// [rc4l] Run the update check now (handy for testing without a restart; it also runs once at startup).
CCMD(fua_check_update)
{
	Printf("running update check...\n");
	zx::updater::StartCheck();
}

// [rc4l] Force the notice to a specific tag (testing/manual). Real arming comes from the background
// check above; kept for driving the menu without a live network.
CCMD(update_notify)
{
	if (argv.argc() < 2)
	{
		Printf("usage: update_notify <tag>   (e.g. update_notify v0.1.19)\n");
		return;
	}
	zx::updater::SetLatestTag(argv[1]);
	if (zx::updater::IsAvailable())
		Printf("update notice armed for %s\n", zx::updater::Tag());
	else
		Printf("no notice: %s is not newer than this build\n", argv[1]);
}

CCMD(update_notify_clear)
{
	zx::updater::Clear();
	Printf("update notice cleared\n");
}
