// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] One HTTP(S) GET streamed to a file, three platform backends behind one call. The download
// counterpart to zx_httpsget.h, which fetches into a fixed buffer -- a WAD is tens of megabytes and
// arrives over minutes, so it needs a different shape: streamed to disk, size-capped, reporting
// progress, and interruptible.
//
// Plain http:// is accepted as well as https://, which zx_httpsget deliberately is not. The update
// check fetches something we act on, so it must be authenticated; a WAD mirror is a file host, most
// of the established ones are http-only, and what actually protects us is that the file is validated
// after it lands (see features/wad-download) rather than that the pipe was encrypted.
//
// Blocking: call it from a worker thread, never the game thread.

#ifndef ZX_HTTPFILE_H
#define ZX_HTTPFILE_H

namespace zx
{

enum class HttpFileResult
{
	Ok,
	NotFound,		// 404/410, or a 200 that was plainly a web page -- try the next mirror
	HttpError,		// any other non-2xx status
	NetworkError,	// DNS, connect, TLS, timeout, truncated body
	TooLarge,		// exceeded the caller's byte cap; the partial file is removed
	WriteFailed,	// could not create or write the destination
	Cancelled,		// the caller's progress callback asked to stop
};

// Called as bytes arrive, from the transfer thread. `total` is -1 when the server sent no
// Content-Length. Return false to abort the transfer (-> Cancelled). May be NULL.
typedef bool (*HttpFileProgressProc)(void *user, long long received, long long total);

// GET `url` into `destPath`, which is created/truncated. On anything but Ok the destination is
// removed, so a failed transfer never leaves a partial file behind for the caller to mistake for a
// complete one. `maxBytes` <= 0 means no cap.
//
// Note this writes exactly where it is told: the caller owns deciding that `destPath` is somewhere it
// should be writing (features/wad-download/computation/downloadplan_compute.h::IsSafeDownloadName).
HttpFileResult HttpGetToFile(const char *url, const char *destPath, long long maxBytes,
	HttpFileProgressProc onProgress, void *user);

} // namespace zx

#endif // ZX_HTTPFILE_H
