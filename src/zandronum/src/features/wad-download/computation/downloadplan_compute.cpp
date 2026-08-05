// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-download/computation/downloadplan_compute.h"

namespace
{

char LowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

char UpperAscii(char c)
{
	return (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
}

std::string ToLower(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i)
		out.push_back(LowerAscii(s[i]));
	return out;
}

std::string ToUpper(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i)
		out.push_back(UpperAscii(s[i]));
	return out;
}

bool StartsWith(const std::string &s, const char *prefix)
{
	size_t i = 0;
	for (; prefix[i] != '\0'; ++i)
	{
		if (i >= s.size() || LowerAscii(s[i]) != LowerAscii(prefix[i]))
			return false;
	}
	return true;
}

bool IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// The extensions the engine will actually load as a resource. Anything else has no business being
// created by a remote instruction, however harmless the bytes turn out to be.
bool HasLoadableExtension(const std::string &lowered)
{
	static const char *const kExts[] = { ".wad", ".pk3", ".pk7", ".pke", ".zip", ".deh", ".bex" };
	for (size_t e = 0; e < sizeof kExts / sizeof kExts[0]; ++e)
	{
		const std::string ext(kExts[e]);
		if (lowered.size() > ext.size() &&
			lowered.compare(lowered.size() - ext.size(), ext.size(), ext) == 0)
		{
			return true;
		}
	}
	return false;
}

// Win32 resolves these stems to devices no matter what extension follows, so "con.wad" is not a file.
// Checked on every platform: the refusal should not depend on which machine the player is on.
bool IsReservedDeviceStem(const std::string &lowered)
{
	std::string stem = lowered;
	const size_t dot = stem.find('.');
	if (dot != std::string::npos)
		stem = stem.substr(0, dot);

	static const char *const kNames[] = { "con", "prn", "aux", "nul" };
	for (size_t i = 0; i < sizeof kNames / sizeof kNames[0]; ++i)
	{
		if (stem == kNames[i])
			return true;
	}
	// COM1..COM9 and LPT1..LPT9.
	if (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9')
	{
		if (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0)
			return true;
	}
	return false;
}

void PushUnique(std::vector<std::string> &out, const std::string &value)
{
	for (size_t i = 0; i < out.size(); ++i)
	{
		if (out[i] == value)
			return;
	}
	out.push_back(value);
}

} // namespace

namespace zx
{

std::vector<std::string> SplitOnWhitespace(const std::string &value)
{
	std::vector<std::string> out;
	size_t i = 0;
	while (i < value.size())
	{
		while (i < value.size() && IsSpace(value[i]))
			++i;
		const size_t start = i;
		while (i < value.size() && !IsSpace(value[i]))
			++i;
		if (i > start)
			out.push_back(value.substr(start, i - start));
	}
	return out;
}

std::vector<std::string> NormalizeDownloadSites(const std::vector<std::string> &raw)
{
	std::vector<std::string> out;
	for (size_t i = 0; i < raw.size(); ++i)
	{
		const std::string &site = raw[i];
		if (!StartsWith(site, "http://") && !StartsWith(site, "https://"))
			continue;					// not a URL we can fetch; silently skipped, not an error

		// A scheme with nothing after it ("https://") would build URLs against no host.
		const size_t schemeEnd = site.find("//");
		if (schemeEnd == std::string::npos || schemeEnd + 2 >= site.size())
			continue;

		std::string normalized = site;
		const char last = normalized[normalized.size() - 1];
		if (last != '/' && last != '=')
			normalized += '/';

		PushUnique(out, normalized);	// first listing wins; the list is in preference order
	}
	return out;
}

std::string UrlEscapeFileName(const std::string &name)
{
	static const char kHex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(name.size());
	for (size_t i = 0; i < name.size(); ++i)
	{
		const unsigned char c = static_cast<unsigned char>(name[i]);
		const bool unreserved =
			(c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~';
		if (unreserved)
		{
			out.push_back(static_cast<char>(c));
		}
		else
		{
			out.push_back('%');
			out.push_back(kHex[(c >> 4) & 0xF]);
			out.push_back(kHex[c & 0xF]);
		}
	}
	return out;
}

bool IsSafeDownloadName(const std::string &name)
{
	if (name.empty() || name.size() > 96)
		return false;

	// A leading dot hides the file on POSIX and makes ".." reachable; neither is a WAD.
	if (name[0] == '.')
		return false;

	for (size_t i = 0; i < name.size(); ++i)
	{
		const unsigned char c = static_cast<unsigned char>(name[i]);
		if (c < 0x20 || c == 0x7F)
			return false;				// control characters, including an embedded NUL or newline
		if (c == '/' || c == '\\' || c == ':')
			return false;				// path separators and the Windows drive/stream separator
	}

	if (name.find("..") != std::string::npos)
		return false;

	// A trailing space or dot is stripped by Win32 on create, so the file we validated and the file
	// we end up with would be different names.
	const char last = name[name.size() - 1];
	if (last == ' ' || last == '.')
		return false;

	const std::string lowered = ToLower(name);
	if (IsReservedDeviceStem(lowered))
		return false;
	if (!HasLoadableExtension(lowered))
		return false;

	return true;
}

std::vector<std::string> BuildCandidateUrls(const std::vector<std::string> &sites,
	const std::string &filename)
{
	std::vector<std::string> out;
	if (!IsSafeDownloadName(filename))
		return out;

	const std::vector<std::string> bases = NormalizeDownloadSites(sites);

	for (size_t s = 0; s < bases.size(); ++s)
	{
		// Exhaust one mirror's spellings before moving on: a mirror that has the file at all almost
		// certainly has it under one of these three, and walking every site per spelling instead would
		// triple the round trips to the sites we like least.
		std::vector<std::string> spellings;
		PushUnique(spellings, filename);
		PushUnique(spellings, ToLower(filename));
		PushUnique(spellings, ToUpper(filename));

		for (size_t v = 0; v < spellings.size(); ++v)
			PushUnique(out, bases[s] + UrlEscapeFileName(spellings[v]));
	}

	return out;
}

} // namespace zx
