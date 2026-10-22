// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The level's textures the engine marks for precache (TexMan.PrecacheLevel, cl_fua_caching), made on the backend when the level is set up instead of on the frame that first draws each.

#ifndef ZX_PRECACHE_COMPUTE_H
#define ZX_PRECACHE_COMPUTE_H

#include <cstddef>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace zx { namespace hwrender {

// [rc4l] FTextureManager's hit bits, as GetHitlist marks them.
const int kHitWall = 1, kHitFlat = 2, kHitSky = 4, kHitSprite = 8;

// [rc4l] The bytes a browser's backend makes ahead, world textures first, past which the rest load as they are first drawn.
const size_t kBrowserPrecacheBytes = (size_t)256 << 20;

struct PrecacheEntry
{
	const void *texture;   // [rc4l] the FTexture
	int hits;              // [rc4l] kHit* bits, every mark it had
};

// [rc4l] A decal template's graphic, made as its marks ask for it.
struct DecalPrecache
{
	const void *texture;   // [rc4l] the FTexture
	int translation;       // [rc4l] the template's own
	bool redToAlpha;       // [rc4l] a shaded decal, whose red is its alpha
};

struct PrecacheQueue
{
	std::vector<PrecacheEntry> entries;
	std::unordered_map<const void *, size_t> at;   // [rc4l] each texture's entry, so a texture marked again only gains bits
	std::vector<DecalPrecache> decals;
	std::set<std::tuple<const void *, int, bool>> decalsSeen;   // [rc4l] so templates sharing a graphic, and later levels, queue it once
};

// [rc4l] Which of a texture's materials the backend draws for its hits: the plain one for a wall, flat or sky, the expanded one for a sprite; none with cl_fua_caching off, as GL makes none then.
struct PrecacheAsk { bool plain; bool expanded; };
PrecacheAsk ComputePrecacheAsk(int hits, int cachingMode);

// [rc4l] Queues a texture the engine marked, merging its bits with an earlier mark of the same one, and ignoring a texture with none.
void ComputePrecacheQueueAdd(PrecacheQueue &q, const void *texture, int hits);

// [rc4l] The queue in the order it is made, world textures before sprites and otherwise as marked, leaving it empty.
std::vector<PrecacheEntry> ComputePrecacheQueueTake(PrecacheQueue &q);

// [rc4l] Queues a decal template's graphic once per translation and shading, ignoring one without a graphic.
void ComputePrecacheQueueAddDecal(PrecacheQueue &q, const void *texture, int translation, bool redToAlpha);

// [rc4l] The decals queued, in the order queued, leaving none.
std::vector<DecalPrecache> ComputePrecacheQueueTakeDecals(PrecacheQueue &q);

// [rc4l] The translation the floor mark pass asks a decal's graphic for: maskTranslation for a shaded decal on a base-palette graphic, whose palette index is then its alpha as GL's alpha texture mode reads it, else its own.
int ComputeDecalPassTranslation(int translation, bool redToAlpha, bool basePalette, int maskTranslation);

// [rc4l] How many bytes may be made ahead: kBrowserPrecacheBytes on a browser's device, every texture elsewhere, as GL makes them.
size_t ComputePrecacheBudget(bool webgpu);

// [rc4l] A material's bytes on the GPU, a third more with its mip chain.
size_t ComputeMaterialBytes(int width, int height, bool mips);

// [rc4l] Whether a material of `bytes` fits what is left of budget, counting it as spent when it does.
bool ComputePrecacheSpend(size_t &spent, size_t bytes, size_t budget);

}} // namespace zx::hwrender

#endif // ZX_PRECACHE_COMPUTE_H
