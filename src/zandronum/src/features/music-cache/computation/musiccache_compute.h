// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure rules for features/music-cache: how much memory the level's songs may take, and which of them it keeps.
#ifndef ZX_MUSICCACHE_COMPUTE_H
#define ZX_MUSICCACHE_COMPUTE_H

#include <vector>

// [Doruk-Aksoy] snd_fua_musiccache_mb's bounds, the menu slider's ends.
enum
{
	ZX_MUSICCACHE_MIN_MB = 16,
	ZX_MUSICCACHE_MAX_MB = 256,
};

// [Doruk-Aksoy] A song the level may play: its lump (-1 when nothing by that name is one), its length, and whether it is stored compressed and so inflated whole to play.
struct ZxMusicCandidate
{
	int lump;
	long long size;
	bool compressed;
};

// [Doruk-Aksoy] The name S_ChangeMusic looks a song's lump up by, past a leading "file://", or null for one that never names a lump (none, "*" or a CD track).
const char *ComputeMusicLumpName(const char *name);

// [Doruk-Aksoy] The bytes the cache may hold: none when turned off, else the size in MB held to the slider's range.
long long ComputeMusicCacheBudget(bool enabled, int megabytes);

// [Doruk-Aksoy] The lumps the cache keeps, in the candidates' priority order: each compressed, non-empty song once, as many as fit the budget, a song too big for what is left skipped for smaller ones after it.
std::vector<int> ComputeMusicCachePlan(const std::vector<ZxMusicCandidate> &candidates, long long budget);

#endif // ZX_MUSICCACHE_COMPUTE_H
