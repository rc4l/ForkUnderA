// [rc4l] Windows HTTPS GET for the auto-update check, isolated in its own TU so <windows.h> (which
// redefines DWORD/BYTE etc.) never mixes with the engine's basictypes.h. zx_updater.cpp just declares
// and calls Zx_Win_HttpsGet. The whole file is empty off Windows so it can sit in the normal source
// list. Uses WinHTTP (system TLS + CA store); fail-safe (false on any error/timeout).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

extern "C" bool Zx_Win_HttpsGet(const char *host, const char *path, char *out, int outSize)
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
		WinHttpSetTimeouts(hSession, 8000, 8000, 8000, 8000); // resolve/connect/send/receive
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

#endif // _WIN32
