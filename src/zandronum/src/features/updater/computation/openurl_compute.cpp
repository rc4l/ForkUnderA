// [rc4l] See openurl_compute.h. Pure logic only — unit-tested at 100% line coverage off-engine.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/updater/computation/openurl_compute.h"

namespace zx {

namespace {

char AsciiLower(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive match of the leading scheme. Returns the length consumed on match, else 0.
int MatchScheme(const char *url, const char *scheme)
{
	int i = 0;
	for (; scheme[i] != '\0'; ++i)
	{
		if (url[i] == '\0' || AsciiLower(url[i]) != scheme[i])
			return 0;
	}
	return i;
}

} // namespace

bool IsOpenableURL(const char *url)
{
	if (url == nullptr || url[0] == '\0')
		return false;

	// Scheme allowlist: http:// or https:// (try the longer one first is unnecessary; they diverge
	// at char 4). A host character must follow the "://".
	int schemeLen = MatchScheme(url, "https://");
	if (schemeLen == 0)
		schemeLen = MatchScheme(url, "http://");
	if (schemeLen == 0)
		return false;
	if (url[schemeLen] == '\0')
		return false; // scheme only, no host

	// Length + character allowlist over the whole string (printable 7-bit ASCII, no space/DEL).
	int len = 0;
	for (const char *p = url; *p != '\0'; ++p)
	{
		if (len >= kMaxOpenableUrlLen)
			return false;
		const unsigned char c = static_cast<unsigned char>(*p);
		if (c <= 0x20 || c >= 0x7f)
			return false;
		++len;
	}
	return true;
}

} // namespace zx
