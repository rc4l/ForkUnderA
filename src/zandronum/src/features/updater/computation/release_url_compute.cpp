// [rc4l] See release_url_compute.h. Pure logic only — unit-tested at 100% line coverage off-engine.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/updater/computation/release_url_compute.h"

#include <cstdio>

namespace zx
{

// The per-platform asset suffix that follows "ZandroX-<tag>". Null for Unknown (no single asset).
static const char *AssetSuffix(ReleasePlatform p)
{
	switch (p)
	{
	case ReleasePlatform::MacOS:   return "-macos-arm64.zip";
	case ReleasePlatform::Windows: return "-windows-x64.zip";
	case ReleasePlatform::Linux:   return "-linux-x86_64.tar.gz";
	case ReleasePlatform::Unknown: break; // no single-file asset -> fall through
	}
	return nullptr;
}

bool ComputeReleaseDownloadURL(char *out, int outSize, const char *repoBase, const char *tag,
	ReleasePlatform p)
{
	if (out == nullptr || outSize <= 0)
		return false;
	out[0] = '\0';
	if (repoBase == nullptr || repoBase[0] == '\0' || tag == nullptr || tag[0] == '\0')
		return false;

	const char *suffix = AssetSuffix(p);
	int written;
	if (suffix == nullptr)
	{
		// Unknown platform: the release page for this tag lists all assets.
		written = std::snprintf(out, static_cast<size_t>(outSize),
			"%s/releases/tag/%s", repoBase, tag);
	}
	else
	{
		written = std::snprintf(out, static_cast<size_t>(outSize),
			"%s/releases/download/%s/ZandroX-%s%s", repoBase, tag, tag, suffix);
	}

	// snprintf returns the length it WANTED to write; a value >= outSize means it was truncated.
	if (written < 0 || written >= outSize)
	{
		out[0] = '\0';
		return false;
	}
	return true;
}

bool ExtractVersionTag(const char *gitDescribe, char *out, int outSize)
{
	if (out == nullptr || outSize <= 0)
		return false;
	out[0] = '\0';
	if (gitDescribe == nullptr || gitDescribe[0] == '\0')
		return false;

	int n = 0;
	for (; gitDescribe[n] != '\0' && gitDescribe[n] != '-' && gitDescribe[n] != '+'; ++n)
	{
		if (n >= outSize - 1)
		{
			out[0] = '\0'; // wouldn't fit -> refuse rather than return a half tag
			return false;
		}
		out[n] = gitDescribe[n];
	}
	out[n] = '\0';
	return n > 0;
}

// Parse up to three dot-separated integers from a version tag into v[0..2] (major, minor, patch),
// skipping a single optional leading 'v'/'V'. Missing or non-numeric components stay 0.
static void ParseVersion(const char *tag, int v[3])
{
	v[0] = v[1] = v[2] = 0;
	if (tag == nullptr)
		return;
	const char *p = tag;
	if (*p == 'v' || *p == 'V')
		++p;
	for (int i = 0; i < 3 && *p != '\0'; ++i)
	{
		int n = 0;
		bool any = false;
		while (*p >= '0' && *p <= '9')
		{
			n = n * 10 + (*p - '0');
			any = true;
			++p;
		}
		if (any)
			v[i] = n;
		// Advance to the next component: skip to just past the next '.', or stop at end/garbage.
		while (*p != '\0' && *p != '.')
			++p;
		if (*p == '.')
			++p;
	}
}

bool IsNewerVersion(const char *current, const char *candidate)
{
	int c[3], n[3];
	ParseVersion(current, c);
	ParseVersion(candidate, n);
	for (int i = 0; i < 3; ++i)
	{
		if (n[i] != c[i])
			return n[i] > c[i];
	}
	return false; // equal -> not newer
}

} // namespace zx
