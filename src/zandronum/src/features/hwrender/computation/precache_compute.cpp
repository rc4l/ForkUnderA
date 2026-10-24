// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "precache_compute.h"

#include <algorithm>
#include <cstdint>

namespace zx { namespace hwrender {

PrecacheAsk ComputePrecacheAsk(int hits, int cachingMode)
{
	PrecacheAsk ask = { false, false };
	if (cachingMode <= 0) return ask;
	ask.plain = (hits & (kHitWall | kHitFlat | kHitSky)) != 0;
	ask.expanded = (hits & kHitSprite) != 0;
	return ask;
}

void ComputePrecacheQueueAdd(PrecacheQueue &q, const void *texture, int hits)
{
	if (texture == nullptr || hits == 0) return;
	const auto found = q.at.find(texture);
	if (found != q.at.end()) { q.entries[found->second].hits |= hits; return; }
	q.at[texture] = q.entries.size();
	q.entries.push_back({ texture, hits });
}

std::vector<PrecacheEntry> ComputePrecacheQueueTake(PrecacheQueue &q)
{
	std::vector<PrecacheEntry> out;
	out.swap(q.entries);
	q.at.clear();
	std::stable_sort(out.begin(), out.end(), [](const PrecacheEntry &a, const PrecacheEntry &b)
	{
		const bool aWorld = (a.hits & (kHitWall | kHitFlat | kHitSky)) != 0;
		const bool bWorld = (b.hits & (kHitWall | kHitFlat | kHitSky)) != 0;
		return aWorld && !bWorld;
	});
	return out;
}

void ComputePrecacheQueueAddDecal(PrecacheQueue &q, const void *texture, int translation, bool redToAlpha)
{
	if (texture == nullptr) return;
	if (!q.decalsSeen.insert(std::make_tuple(texture, translation, redToAlpha)).second) return;
	q.decals.push_back({ texture, translation, redToAlpha });
}

std::vector<DecalPrecache> ComputePrecacheQueueTakeDecals(PrecacheQueue &q)
{
	std::vector<DecalPrecache> out;
	out.swap(q.decals);
	q.decalsSeen.clear();
	return out;
}

int ComputeDecalPassTranslation(int translation, bool redToAlpha, bool basePalette, int maskTranslation)
{
	return redToAlpha && basePalette ? maskTranslation : translation;
}

size_t ComputePrecacheBudget(bool webgpu)
{
	return webgpu ? kBrowserPrecacheBytes : SIZE_MAX;
}

size_t ComputeMaterialBytes(int width, int height, bool mips)
{
	if (width <= 0 || height <= 0) return 0;
	const size_t base = (size_t)width * (size_t)height * 4;
	return mips ? base + base / 3 : base;
}

bool ComputePrecacheSpend(size_t &spent, size_t bytes, size_t budget)
{
	if (spent > budget || bytes > budget - spent) return false;
	spent += bytes;
	return true;
}

}} // namespace zx::hwrender
