// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_httpfile.h. Windows forwards to the WinHTTP backend in its own TU (windows.h must not
// meet the engine's basictypes.h); macOS and Linux share one libcurl path, loaded with dlopen so
// libcurl stays a runtime nicety rather than a build dependency -- the same trick zx_httpsget.cpp
// already uses on Linux, extended to macOS because it ships libcurl in /usr/lib too.

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

#include <cstdio>
#include <cstring>

#include "features/net/zx_httpfile.h"

namespace zx
{

#if defined(_WIN32)

// Defined in features/net/zx_httpfile_win.cpp.
HttpFileResult Win_HttpGetToFile(const char *url, const char *destPath, long long maxBytes,
	HttpFileProgressProc onProgress, void *user);

HttpFileResult HttpGetToFile(const char *url, const char *destPath, long long maxBytes,
	HttpFileProgressProc onProgress, void *user)
{
	return Win_HttpGetToFile(url, destPath, maxBytes, onProgress, user);
}

#else // macOS / Linux: libcurl via dlopen

namespace
{

// Stable CURLOPT / CURLINFO / CURLE constants, spelled out rather than #included so there is no
// build-time libcurl dependency. These values are ABI and have not moved since curl 7.x.
const int OPT_URL             = 10002;
const int OPT_WRITEFUNCTION   = 20011;
const int OPT_WRITEDATA       = 10001;
const int OPT_USERAGENT       = 10018;
const int OPT_FOLLOWLOCATION  = 52;
const int OPT_MAXREDIRS       = 68;
const int OPT_FAILONERROR     = 45;
const int OPT_CONNECTTIMEOUT  = 78;
const int OPT_LOW_SPEED_LIMIT = 19;
const int OPT_LOW_SPEED_TIME  = 20;
const int OPT_NOPROGRESS      = 43;
const int OPT_XFERINFOFUNCTION = 20219;
const int OPT_XFERINFODATA    = 10057;
const int INFO_RESPONSE_CODE  = 0x200002;
const int INFO_CONTENT_TYPE   = 0x100012;
const int CURLE_OK            = 0;
const int CURLE_WRITE_ERROR   = 23;
const int CURLE_ABORTED       = 42;

struct Sink
{
	FILE *fp;
	long long written;
	long long maxBytes;
	long long total;
	HttpFileProgressProc onProgress;
	void *user;
	bool overCap;
	bool writeFailed;
};

size_t OnWrite(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	Sink *s = static_cast<Sink *>(userdata);
	const size_t n = size * nmemb;

	// The cap is enforced here rather than via CURLOPT_MAXFILESIZE because that option only acts on a
	// Content-Length the server chose to send -- a chunked response, or a lying one, walks past it.
	if (s->maxBytes > 0 && s->written + static_cast<long long>(n) > s->maxBytes)
	{
		s->overCap = true;
		return 0;					// short write -> CURLE_WRITE_ERROR, transfer stops
	}
	if (n > 0 && std::fwrite(ptr, 1, n, s->fp) != n)
	{
		s->writeFailed = true;
		return 0;
	}
	s->written += static_cast<long long>(n);
	return n;
}

// curl_off_t is int64 on every platform we build for.
int OnXferInfo(void *clientp, long long dltotal, long long dlnow, long long, long long)
{
	Sink *s = static_cast<Sink *>(clientp);
	s->total = dltotal > 0 ? dltotal : -1;
	if (s->onProgress != NULL && !s->onProgress(s->user, dlnow, s->total))
		return 1;					// -> CURLE_ABORTED_BY_CALLBACK
	return 0;
}

void *OpenCurl()
{
	static const char *const kNames[] = {
		"libcurl.4.dylib", "libcurl.dylib",		// macOS ships this in /usr/lib
		"libcurl.so.4", "libcurl.so",
	};
	for (size_t i = 0; i < sizeof kNames / sizeof kNames[0]; ++i)
	{
		void *h = dlopen(kNames[i], RTLD_NOW | RTLD_LOCAL);
		if (h != NULL)
			return h;
	}
	return NULL;
}

} // namespace

HttpFileResult HttpGetToFile(const char *url, const char *destPath, long long maxBytes,
	HttpFileProgressProc onProgress, void *user)
{
	if (url == NULL || destPath == NULL)
		return HttpFileResult::NetworkError;

	void *h = OpenCurl();
	if (h == NULL)
		return HttpFileResult::NetworkError;	// no libcurl -> we simply cannot download here

	typedef void *(*easy_init_t)();
	typedef int (*easy_setopt_t)(void *, int, ...);
	typedef int (*easy_perform_t)(void *);
	typedef int (*easy_getinfo_t)(void *, int, ...);
	typedef void (*easy_cleanup_t)(void *);
	easy_init_t easy_init = (easy_init_t)dlsym(h, "curl_easy_init");
	easy_setopt_t easy_setopt = (easy_setopt_t)dlsym(h, "curl_easy_setopt");
	easy_perform_t easy_perform = (easy_perform_t)dlsym(h, "curl_easy_perform");
	easy_getinfo_t easy_getinfo = (easy_getinfo_t)dlsym(h, "curl_easy_getinfo");
	easy_cleanup_t easy_cleanup = (easy_cleanup_t)dlsym(h, "curl_easy_cleanup");
	if (!easy_init || !easy_setopt || !easy_perform || !easy_getinfo || !easy_cleanup)
	{
		dlclose(h);
		return HttpFileResult::NetworkError;
	}

	FILE *fp = std::fopen(destPath, "wb");
	if (fp == NULL)
	{
		dlclose(h);
		return HttpFileResult::WriteFailed;
	}

	Sink sink;
	sink.fp = fp;
	sink.written = 0;
	sink.maxBytes = maxBytes;
	sink.total = -1;
	sink.onProgress = onProgress;
	sink.user = user;
	sink.overCap = false;
	sink.writeFailed = false;

	HttpFileResult result = HttpFileResult::NetworkError;
	void *c = easy_init();
	if (c != NULL)
	{
		easy_setopt(c, OPT_URL, url);
		easy_setopt(c, OPT_WRITEFUNCTION, (void *)OnWrite);
		easy_setopt(c, OPT_WRITEDATA, &sink);
		easy_setopt(c, OPT_USERAGENT, "ZandroX");
		easy_setopt(c, OPT_FOLLOWLOCATION, (long)1);
		easy_setopt(c, OPT_MAXREDIRS, (long)5);
		// Abort before a body is written on a non-2xx, so a 404 page never lands in the file. The
		// exact status still comes back via getinfo below.
		easy_setopt(c, OPT_FAILONERROR, (long)1);
		easy_setopt(c, OPT_CONNECTTIMEOUT, (long)15);
		// A stall timeout rather than a total one: a large WAD on a slow line legitimately takes
		// minutes, and a total timeout would kill exactly the transfers most worth finishing.
		easy_setopt(c, OPT_LOW_SPEED_LIMIT, (long)512);
		easy_setopt(c, OPT_LOW_SPEED_TIME, (long)30);
		easy_setopt(c, OPT_NOPROGRESS, (long)0);
		easy_setopt(c, OPT_XFERINFOFUNCTION, (void *)OnXferInfo);
		easy_setopt(c, OPT_XFERINFODATA, &sink);

		const int rc = easy_perform(c);

		long status = 0;
		easy_getinfo(c, INFO_RESPONSE_CODE, &status);
		const char *contentType = NULL;
		easy_getinfo(c, INFO_CONTENT_TYPE, &contentType);

		if (sink.overCap)
			result = HttpFileResult::TooLarge;
		else if (sink.writeFailed)
			result = HttpFileResult::WriteFailed;
		else if (rc == CURLE_ABORTED)
			result = HttpFileResult::Cancelled;
		else if (rc == CURLE_OK && status >= 200 && status < 300)
		{
			// A mirror that answers every path with its own search page returns 200 and HTML. Treat
			// that as "this site does not have it" rather than saving a web page as a WAD.
			if (contentType != NULL && std::strncmp(contentType, "text/html", 9) == 0)
				result = HttpFileResult::NotFound;
			else
				result = HttpFileResult::Ok;
		}
		else if (status == 404 || status == 410)
			result = HttpFileResult::NotFound;
		else if (status >= 400)
			result = HttpFileResult::HttpError;
		else if (rc == CURLE_WRITE_ERROR)
			result = HttpFileResult::WriteFailed;
		else
			result = HttpFileResult::NetworkError;

		easy_cleanup(c);
	}

	std::fclose(fp);
	if (result != HttpFileResult::Ok)
		std::remove(destPath);		// never leave a partial file to be mistaken for a complete one
	dlclose(h);
	return result;
}

#endif

} // namespace zx
