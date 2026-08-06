// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-serve/computation/httpreq_compute.h"

namespace zx
{

namespace
{

bool HexValue(char c, int &out)
{
	if ((c >= '0') && (c <= '9'))
	{
		out = c - '0';
		return true;
	}
	if ((c >= 'a') && (c <= 'f'))
	{
		out = c - 'a' + 10;
		return true;
	}
	if ((c >= 'A') && (c <= 'F'))
	{
		out = c - 'A' + 10;
		return true;
	}
	return false;
}

// Compare `a` against a LOWERCASE ASCII literal, ignoring case in `a`. Only one side is folded on
// purpose: folding both would leave a branch no caller can reach, and every call site here passes a
// literal we wrote.
bool EqualsIgnoreCaseAscii(const std::string &a, const char *lowerB)
{
	size_t i = 0;
	for (; (i < a.size()) && (lowerB[i] != '\0'); ++i)
	{
		char ca = a[i];
		if ((ca >= 'A') && (ca <= 'Z'))
			ca = static_cast<char>(ca - 'A' + 'a');
		if (ca != lowerB[i])
			return false;
	}
	return (i == a.size()) && (lowerB[i] == '\0');
}

std::string TrimSpaces(const std::string &s)
{
	size_t begin = 0;
	while ((begin < s.size()) && ((s[begin] == ' ') || (s[begin] == '\t')))
		++begin;

	size_t end = s.size();
	while ((end > begin) && ((s[end - 1] == ' ') || (s[end - 1] == '\t')))
		--end;

	return s.substr(begin, end - begin);
}

// Strictly digits, and short enough that the accumulation cannot overflow. A file offset arriving
// from the network is exactly the kind of number that should not be handed to atoll and hoped about.
bool ParseDecimal(const std::string &s, long long &out)
{
	if (s.empty() || (s.size() > 18))
		return false;

	long long value = 0;
	for (size_t i = 0; i < s.size(); ++i)
	{
		if ((s[i] < '0') || (s[i] > '9'))
			return false;
		value = (value * 10) + (s[i] - '0');
	}

	out = value;
	return true;
}

} // namespace

bool PercentDecode(const std::string &in, std::string &out)
{
	std::string result;
	result.reserve(in.size());

	for (size_t i = 0; i < in.size(); ++i)
	{
		if (in[i] != '%')
		{
			result.push_back(in[i]);
			continue;
		}

		if ((i + 2) >= in.size())
			return false;

		int hi = 0;
		int lo = 0;
		if (!HexValue(in[i + 1], hi) || !HexValue(in[i + 2], lo))
			return false;

		result.push_back(static_cast<char>((hi << 4) | lo));
		i += 2;
	}

	out = result;
	return true;
}

bool ParseRangeHeader(const std::string &value, HttpRange &out)
{
	const std::string trimmed = TrimSpaces(value);
	if (trimmed.size() <= 6)
		return false;
	if (!EqualsIgnoreCaseAscii(trimmed.substr(0, 6), "bytes="))
		return false;

	const std::string spec = TrimSpaces(trimmed.substr(6));

	// Multi-range would mean multipart/byteranges responses. We serve one WAD at a time to our own
	// downloader; the whole-file fallback is a better answer than a second response encoding.
	if (spec.find(',') != std::string::npos)
		return false;

	const size_t dash = spec.find('-');
	if (dash == std::string::npos)
		return false;

	const std::string firstPart = TrimSpaces(spec.substr(0, dash));
	const std::string lastPart = TrimSpaces(spec.substr(dash + 1));

	HttpRange parsed;
	parsed.present = true;

	if (firstPart.empty())
	{
		// "bytes=-500" -- the last 500 bytes, not everything from 500.
		long long count = 0;
		if (!ParseDecimal(lastPart, count))
			return false;
		parsed.suffix = true;
		parsed.first = 0;
		parsed.last = count;
	}
	else
	{
		long long first = 0;
		if (!ParseDecimal(firstPart, first))
			return false;
		parsed.first = first;

		if (lastPart.empty())
		{
			parsed.last = -1;
		}
		else
		{
			long long last = 0;
			if (!ParseDecimal(lastPart, last))
				return false;
			parsed.last = last;
		}
	}

	out = parsed;
	return true;
}

bool ResolveRange(const HttpRange &range, long long fileSize, long long &outOffset,
	long long &outLength)
{
	if (fileSize < 0)
		return false;

	if (!range.present)
	{
		outOffset = 0;
		outLength = fileSize;
		return true;
	}

	if (range.suffix)
	{
		if (range.last <= 0)
			return false;					// "bytes=-0" asks for nothing at all

		if (range.last >= fileSize)
		{
			// Asking for more trailing bytes than exist is satisfied by the whole file, unless
			// there is no file to send.
			outOffset = 0;
			outLength = fileSize;
			return fileSize > 0;
		}

		outOffset = fileSize - range.last;
		outLength = range.last;
		return true;
	}

	// Starting past the end means the client's idea of the file is stale. Say so with a 416 rather
	// than handing back a truncated 200 it will treat as the real thing.
	if (range.first >= fileSize)
		return false;

	long long last = range.last;
	if ((last < 0) || (last >= fileSize))
		last = fileSize - 1;
	if (last < range.first)
		return false;

	outOffset = range.first;
	outLength = (last - range.first) + 1;
	return true;
}

HttpParse ParseHttpRequest(const std::string &buffer, size_t maxHeaderBytes, HttpRequest &out)
{
	const std::string terminator = "\r\n\r\n";
	const size_t end = buffer.find(terminator);
	if (end == std::string::npos)
	{
		// Nothing complete yet. The cap is what stops a peer dribbling headers forever to pin a
		// transfer slot -- slowloris is cheap against a server with a handful of them.
		if (buffer.size() > maxHeaderBytes)
			return HttpParse::TooLarge;
		return HttpParse::NeedMore;
	}
	if ((end + terminator.size()) > maxHeaderBytes)
		return HttpParse::TooLarge;

	const std::string head = buffer.substr(0, end);

	const size_t lineEnd = head.find("\r\n");
	const std::string requestLine = (lineEnd == std::string::npos) ? head : head.substr(0, lineEnd);

	const size_t firstSpace = requestLine.find(' ');
	if (firstSpace == std::string::npos)
		return HttpParse::BadRequest;
	const size_t secondSpace = requestLine.find(' ', firstSpace + 1);
	if (secondSpace == std::string::npos)
		return HttpParse::BadRequest;

	const std::string method = requestLine.substr(0, firstSpace);
	const std::string target = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
	const std::string version = requestLine.substr(secondSpace + 1);

	if (version.compare(0, 7, "HTTP/1.") != 0)
		return HttpParse::BadRequest;

	HttpRequest parsed;
	if (method == "GET")
		parsed.headOnly = false;
	else if (method == "HEAD")
		parsed.headOnly = true;
	else
		return HttpParse::Unsupported;
	parsed.method = method;

	if (target.empty() || (target[0] != '/'))
		return HttpParse::BadRequest;

	std::string path = target.substr(1);
	const size_t query = path.find('?');
	if (query != std::string::npos)
		path = path.substr(0, query);
	if (path.empty())
		return HttpParse::BadRequest;

	// One segment, never a path. A request that cannot name a directory cannot name a parent one.
	if (path.find('/') != std::string::npos)
		return HttpParse::BadRequest;

	// Decoding cannot empty a non-empty segment, so the emptiness check above covers the result too.
	std::string name;
	if (!PercentDecode(path, name))
		return HttpParse::BadRequest;

	// The decode happens after the segment split, so this is where "%2f" gets caught trying to put a
	// separator back. Control characters go too -- nothing we serve has one in its name.
	for (size_t i = 0; i < name.size(); ++i)
	{
		const unsigned char c = static_cast<unsigned char>(name[i]);
		if ((c < 0x20) || (c == 0x7f) || (c == '/') || (c == '\\'))
			return HttpParse::BadRequest;
	}
	parsed.filename = name;

	size_t pos = (lineEnd == std::string::npos) ? head.size() : (lineEnd + 2);
	while (pos < head.size())
	{
		size_t lineBreak = head.find("\r\n", pos);
		if (lineBreak == std::string::npos)
			lineBreak = head.size();

		const std::string line = head.substr(pos, lineBreak - pos);
		pos = lineBreak + 2;

		const size_t colon = line.find(':');
		if (colon != std::string::npos)
		{
			if (EqualsIgnoreCaseAscii(line.substr(0, colon), "range"))
			{
				// A Range we cannot parse is ignored rather than fatal: sending the whole file is
				// always a correct answer, and a download that succeeds beats one that 400s.
				HttpRange range;
				if (ParseRangeHeader(line.substr(colon + 1), range))
					parsed.range = range;
			}
		}
	}

	out = parsed;
	return HttpParse::Ok;
}

} // namespace zx
