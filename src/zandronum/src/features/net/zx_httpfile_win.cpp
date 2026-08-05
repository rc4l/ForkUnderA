// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The Windows backend for zx_httpfile.h, isolated in its own TU so <windows.h> (which
// redefines DWORD/BYTE and friends) never mixes with the engine's basictypes.h -- the same split
// zx_updater_net_win.cpp already uses for the update check. The whole file is empty off Windows so
// it can sit in the normal source list.
//
// WinHTTP rather than a bundled libcurl: it is the system HTTP stack, it brings the OS proxy
// settings and certificate store with it, and it means downloading costs the Windows build no new
// third-party dependency at all.

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include "features/net/zx_httpfile.h"

namespace
{

// A WAD arrives in chunks over minutes. The receive timeout is per-read, not per-transfer, so a slow
// but progressing download is never killed for taking a long time overall -- only a genuinely stalled
// one is.
const int kResolveTimeoutMs = 10000;
const int kConnectTimeoutMs = 15000;
const int kSendTimeoutMs    = 15000;
const int kReceiveTimeoutMs = 30000;

const DWORD kReadChunk = 64 * 1024;

bool ToWide(const char *utf8, wchar_t *out, int outChars)
{
	return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, outChars) > 0;
}

// A header WinHTTP hands back as a wide string, copied into a narrow buffer. Returns false if the
// header is absent.
bool QueryStringHeader(HINTERNET hRequest, DWORD which, char *out, int outSize)
{
	wchar_t buf[256];
	DWORD len = sizeof buf;
	if (!WinHttpQueryHeaders(hRequest, which, WINHTTP_HEADER_NAME_BY_INDEX, buf, &len,
			WINHTTP_NO_HEADER_INDEX))
	{
		return false;
	}
	return WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, outSize, NULL, NULL) > 0;
}

} // namespace

namespace zx
{

HttpFileResult Win_HttpGetToFile(const char *url, const char *destPath, long long maxBytes,
	HttpFileProgressProc onProgress, void *user)
{
	if (url == NULL || destPath == NULL)
		return HttpFileResult::NetworkError;

	wchar_t wurl[1024];
	if (!ToWide(url, wurl, 1024))
		return HttpFileResult::NetworkError;

	// Setting the length fields to -1 makes WinHttpCrackUrl return pointers INTO wurl with lengths,
	// rather than needing us to guess buffer sizes up front.
	URL_COMPONENTS parts;
	ZeroMemory(&parts, sizeof parts);
	parts.dwStructSize = sizeof parts;
	parts.dwSchemeLength = (DWORD)-1;
	parts.dwHostNameLength = (DWORD)-1;
	parts.dwUrlPathLength = (DWORD)-1;
	parts.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(wurl, 0, 0, &parts) || parts.dwHostNameLength == 0)
		return HttpFileResult::NetworkError;

	wchar_t host[512];
	const DWORD hostChars = parts.dwHostNameLength < 511 ? parts.dwHostNameLength : 511;
	wmemcpy(host, parts.lpszHostName, hostChars);
	host[hostChars] = L'\0';

	// Path and query rejoined: the caller already percent-escaped the filename, and WinHttpOpenRequest
	// leaves '%' alone by default, so what we built is what gets requested.
	wchar_t object[1024];
	object[0] = L'\0';
	{
		size_t n = 0;
		for (DWORD i = 0; i < parts.dwUrlPathLength && n < 1022; ++i)
			object[n++] = parts.lpszUrlPath[i];
		for (DWORD i = 0; i < parts.dwExtraInfoLength && n < 1022; ++i)
			object[n++] = parts.lpszExtraInfo[i];
		if (n == 0)
			object[n++] = L'/';
		object[n] = L'\0';
	}

	const bool secure = (parts.nScheme == INTERNET_SCHEME_HTTPS);

	HINTERNET hSession = WinHttpOpen(L"ZandroX", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (hSession == NULL)
		return HttpFileResult::NetworkError;
	WinHttpSetTimeouts(hSession, kResolveTimeoutMs, kConnectTimeoutMs, kSendTimeoutMs,
		kReceiveTimeoutMs);

	HttpFileResult result = HttpFileResult::NetworkError;
	FILE *fp = NULL;

	HINTERNET hConnect = WinHttpConnect(hSession, host, parts.nPort, 0);
	if (hConnect != NULL)
	{
		HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", object, NULL,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
		if (hRequest != NULL)
		{
			if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
					WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
				WinHttpReceiveResponse(hRequest, NULL))
			{
				DWORD status = 0, statusLen = sizeof(status);
				WinHttpQueryHeaders(hRequest,
					WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
					WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusLen, WINHTTP_NO_HEADER_INDEX);

				if (status == 404 || status == 410)
				{
					result = HttpFileResult::NotFound;
				}
				else if (status < 200 || status >= 300)
				{
					result = HttpFileResult::HttpError;
				}
				else
				{
					// A mirror that answers every path with its own search page returns 200 and HTML.
					// Caught before a byte is written, so it costs nothing to check.
					char contentType[256];
					if (QueryStringHeader(hRequest, WINHTTP_QUERY_CONTENT_TYPE, contentType,
							sizeof contentType) &&
						_strnicmp(contentType, "text/html", 9) == 0)
					{
						result = HttpFileResult::NotFound;
					}
					else
					{
						// Content-Length read as a string, not WINHTTP_QUERY_FLAG_NUMBER: that form
						// is a DWORD and would wrap on anything past 4 GB.
						long long total = -1;
						char lenStr[64];
						if (QueryStringHeader(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, lenStr,
								sizeof lenStr))
						{
							total = _atoi64(lenStr);
							if (total <= 0)
								total = -1;
						}

						if (maxBytes > 0 && total > maxBytes)
						{
							result = HttpFileResult::TooLarge;	// refused before it starts
						}
						else
						{
							fp = fopen(destPath, "wb");
							if (fp == NULL)
							{
								result = HttpFileResult::WriteFailed;
							}
							else
							{
								result = HttpFileResult::Ok;
								long long received = 0;
								char *buf = (char *)malloc(kReadChunk);
								if (buf == NULL)
								{
									result = HttpFileResult::WriteFailed;
								}
								else
								{
									for (;;)
									{
										DWORD avail = 0;
										if (!WinHttpQueryDataAvailable(hRequest, &avail))
										{
											result = HttpFileResult::NetworkError;
											break;
										}
										if (avail == 0)
											break;				// body complete

										DWORD want = avail < kReadChunk ? avail : kReadChunk;
										DWORD got = 0;
										if (!WinHttpReadData(hRequest, buf, want, &got) || got == 0)
										{
											result = HttpFileResult::NetworkError;
											break;
										}

										if (maxBytes > 0 && received + (long long)got > maxBytes)
										{
											// Enforced here as well as against Content-Length: a
											// chunked response, or a lying one, has no length to
											// check up front.
											result = HttpFileResult::TooLarge;
											break;
										}
										if (fwrite(buf, 1, got, fp) != got)
										{
											result = HttpFileResult::WriteFailed;
											break;
										}
										received += (long long)got;

										if (onProgress != NULL && !onProgress(user, received, total))
										{
											result = HttpFileResult::Cancelled;
											break;
										}
									}

									// A server that closed early leaves a short file that is still a
									// valid-looking one. Treat a truncated body as a failure rather
									// than keeping a WAD that is missing its tail.
									if (result == HttpFileResult::Ok && total > 0 && received < total)
										result = HttpFileResult::NetworkError;

									free(buf);
								}
							}
						}
					}
				}
			}
			WinHttpCloseHandle(hRequest);
		}
		WinHttpCloseHandle(hConnect);
	}
	WinHttpCloseHandle(hSession);

	if (fp != NULL)
		fclose(fp);
	if (result != HttpFileResult::Ok)
		remove(destPath);		// never leave a partial file to be mistaken for a complete one

	return result;
}

} // namespace zx

#endif // _WIN32
