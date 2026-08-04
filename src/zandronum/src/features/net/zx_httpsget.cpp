// [rc4l] See zx_httpsget.h. Moved here verbatim from features/updater/zx_updater.cpp once a second
// caller appeared; the backends are unchanged.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// Platform networking headers at file scope, before anything else. NOTE: <windows.h> is NOT included
// here -- it redefines DWORD/BYTE and clashes with the engine's basictypes.h, so the Windows fetch
// lives in its own TU (features/updater/zx_updater_net_win.cpp) behind Zx_Win_HttpsGet.
#if !defined(_WIN32) && !defined(__APPLE__)
#include <dlfcn.h>
#endif

#include <cstdio>

#include "features/net/zx_httpsget.h"

namespace zx
{

#if defined(__APPLE__)

extern "C" bool Mac_HttpsGet(const char *urlStr, char *out, int outSize, int timeoutSecs);
bool HttpsGet(const char *host, const char *path, char *out, int outSize)
{
	char url[512];
	std::snprintf(url, sizeof url, "https://%s%s", host, path);
	return Mac_HttpsGet(url, out, outSize, 8);
}

#elif defined(_WIN32)

// Defined in features/updater/zx_updater_net_win.cpp (isolated so <windows.h> doesn't clash with
// basictypes.h).
extern "C" bool Zx_Win_HttpsGet(const char *host, const char *path, char *out, int outSize);
bool HttpsGet(const char *host, const char *path, char *out, int outSize)
{
	return Zx_Win_HttpsGet(host, path, out, outSize);
}

#else // Linux/other: libcurl loaded at runtime (dlopen), so it's not a hard build dependency.

namespace
{
struct CurlBuf { char *out; int cap; int len; };
size_t CurlWrite(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	CurlBuf *b = static_cast<CurlBuf *>(userdata);
	size_t n = size * nmemb;
	for (size_t i = 0; i < n && b->len < b->cap - 1; ++i)
		b->out[b->len++] = ptr[i];
	return n; // report full consumption even if we stopped storing, so curl doesn't abort
}
} // namespace

bool HttpsGet(const char *host, const char *path, char *out, int outSize)
{
	if (out == nullptr || outSize <= 0)
		return false;
	out[0] = '\0';

	void *h = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
	if (h == nullptr) h = dlopen("libcurl.so", RTLD_NOW | RTLD_LOCAL);
	if (h == nullptr) return false; // no libcurl present -> skip the fetch (fail-safe)

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
		easy_setopt(c, 10018, "ZandroX");
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

} // namespace zx
