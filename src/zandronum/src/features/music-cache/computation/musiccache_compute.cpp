// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Implementation of the music cache's rules; no engine dependencies.
#include "features/music-cache/computation/musiccache_compute.h"
#include <algorithm>
#include <cctype>
#include <cstring>

// [Doruk-Aksoy] Whether a name starts ",CD,", in any case, as S_ChangeMusic's strnicmp reads it.
static bool IsCDTrack(const char *name)
{
	static const char cd[] = ",CD,";
	for (int i = 0; i < 4; ++i)
		if (toupper((unsigned char)name[i]) != cd[i])
			return false;
	return true;
}

const char *ComputeMusicLumpName(const char *name)
{
	if (name == nullptr || name[0] == 0 || strcmp(name, "*") == 0 || IsCDTrack(name))
		return nullptr;
	if (strncmp(name, "file://", 7) == 0)
		return name + 7;
	return name;
}

long long ComputeMusicCacheBudget(bool enabled, int megabytes)
{
	if (!enabled)
		return 0;
	const int mb = std::min(std::max(megabytes, (int)ZX_MUSICCACHE_MIN_MB), (int)ZX_MUSICCACHE_MAX_MB);
	return (long long)mb * 1024 * 1024;
}

std::vector<int> ComputeMusicCachePlan(const std::vector<ZxMusicCandidate> &candidates, long long budget)
{
	std::vector<int> plan;
	long long used = 0;
	for (const ZxMusicCandidate &c : candidates)
	{
		if (c.lump < 0 || !c.compressed || c.size <= 0)
			continue;
		if (std::find(plan.begin(), plan.end(), c.lump) != plan.end())
			continue;
		if (used + c.size > budget)
			continue;
		plan.push_back(c.lump);
		used += c.size;
	}
	return plan;
}
