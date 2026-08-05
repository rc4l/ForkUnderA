// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Reading an HTTP request off a socket that anyone on the internet can connect to.
//
// This is the attack surface of the whole feature. Everything else in features/wad-serve acts on
// what comes out of here, so it is a pure function with tests rather than parsing scattered through
// the transfer loop -- "did we remember to reject %2e%2e%2f?" should be answerable without running a
// server. Same reasoning as downloadplan_compute.h on the client side, and the same conclusion.
//
// The strongest decision here is structural rather than a check: THE TARGET IS ONE PATH SEGMENT AND
// NEVER A PATH. "/dwango5.wad" parses; "/wads/dwango5.wad" is rejected outright, not normalised.
// A request never names a directory, so it cannot name a parent one either, and the server then looks
// the name up in a table of already-loaded WADs instead of joining it onto a filesystem root.
// Traversal stops being a check we have to get right and becomes a shape the code cannot express.
// Percent-decoding still rejects a decoded '/', '\' or NUL, because the decode happens after that
// segment split and "%2f" must not smuggle one back in.
//
// Requests are also capped by total header bytes. Without it a peer can open a connection, dribble
// headers forever and pin a transfer slot -- the classic slowloris, and cheap to do to a game server
// with a handful of slots. The cap plus a read timeout in the driver is what bounds it.
//
// Deliberately narrow: GET and HEAD, HTTP/1.x, one Range header, no chunked bodies, no keep-alive
// negotiation. The client this serves is our own downloader plus curl for debugging, and every
// feature not implemented is a class of bug not possible.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_HTTPREQ_COMPUTE_H
#define ZX_HTTPREQ_COMPUTE_H

#include <cstddef>
#include <string>

namespace zx
{

enum class HttpParse
{
	NeedMore,			// the header block is not complete yet -- read more and call again
	Ok,
	BadRequest,			// 400: malformed, or a target we will not turn into a filename
	TooLarge,			// 431: header block past the cap before it ever terminated
	Unsupported,		// 501: a method other than GET or HEAD
};

// A parsed Range header. `suffix` is the "bytes=-500" form, meaning the LAST 500 bytes rather than
// everything from offset 500 -- a genuinely different request that looks almost identical, and worth
// a named field rather than a sign convention nobody remembers.
struct HttpRange
{
	bool present;
	bool suffix;
	long long first;
	long long last;			// -1 means "through the end of the file"

	HttpRange() : present(false), suffix(false), first(0), last(-1) {}
};

struct HttpRequest
{
	std::string method;
	std::string filename;	// the single path segment, percent-decoded, no query string
	HttpRange range;
	bool headOnly;			// HEAD: send the headers and stop

	HttpRequest() : headOnly(false) {}
};

// Percent-decode `in`. False -- leaving `out` unspecified -- on a '%' not followed by two hex digits.
// Rejecting rather than passing the '%' through matters: a lenient decoder here and a strict one
// somewhere else is how the same bytes come to mean two different filenames.
bool PercentDecode(const std::string &in, std::string &out);

// Parse a Range header value ("bytes=0-499", "bytes=500-", "bytes=-500"). False on anything else,
// including the multi-range form, which we do not serve. A false return means the caller should send
// the whole file rather than fail -- ignoring a Range it cannot honour is allowed, and a download
// that succeeds unrangeed beats one that 400s.
bool ParseRangeHeader(const std::string &value, HttpRange &out);

// Turn a parsed range into a concrete offset and length against a known file size. False when the
// range cannot be satisfied at all, which is a 416 rather than a truncated 200 -- a client asking
// past the end of the file has a stale idea of it and should be told, not handed the wrong bytes.
bool ResolveRange(const HttpRange &range, long long fileSize, long long &outOffset,
	long long &outLength);

// Parse a complete request out of whatever has been read so far. `maxHeaderBytes` bounds the header
// block; NeedMore means the caller should read more into the same buffer and try again.
HttpParse ParseHttpRequest(const std::string &buffer, size_t maxHeaderBytes, HttpRequest &out);

} // namespace zx

#endif // ZX_HTTPREQ_COMPUTE_H
