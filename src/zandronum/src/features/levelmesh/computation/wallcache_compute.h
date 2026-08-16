// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Wall geometry caching (plan phase P2b) -- the rule deciding when a seg's generated wall
// geometry can be reused instead of regenerated.
//
// GLWall::Process runs per visible wall per frame: plane evaluations at both vertices, texture
// lookups, pegging arithmetic, then a GLWall copied into a draw list. On Sunder MAP10 that is 8251
// walls a frame for a level whose topology never changes. Level topology being immutable at runtime
// (only positions, materials and transforms move) is exactly what makes the output reusable.
//
// This header holds the validity rule and nothing else, because the rule is where the bugs live: too
// strict and the cache never hits, too loose and walls render with last frame's heights after a door
// opens. Every input is a value the caller samples from the seg's own sectors, so the decision is
// pure and testable without an engine.

#ifndef ZX_WALLCACHE_COMPUTE_H
#define ZX_WALLCACHE_COMPUTE_H

#include <stdint.h>

namespace zx { namespace levelmesh {

// What a cached seg was generated against.
//
// Three version counters, not a content hash. The first version of this hashed ~18 fields including
// twelve int64 multiplies over the side's texture offsets, and measured (via `stat rendertimes`) to
// cost about as much as the GLWall::Process it was meant to avoid -- 0.979 ms of wall setup against
// a stamp of roughly the same size, which is why the cache came out neutral. The engine now bumps
// sector_t::fua_dirty and side_t::fua_dirty at the few places that mutate anything a wall is
// generated from, so validity is three integer compares.
struct WallCacheStamp
{
	int frontDirty;
	int backDirty;   // 0 when one-sided
	int sideDirty;
};

// Are these two stamps the same generation input?
bool ComputeStampsEqual(const WallCacheStamp &a, const WallCacheStamp &b);

// Reasons a seg can never be cached, independent of what changed this frame. Kept separate from the
// stamp because these are properties of the seg, checked once, not per-frame comparisons.
struct WallCacheEligibility
{
	bool isPolyobject;       // polyobject vertices move in place every tic
	bool hasHeightsec;       // gl_FakeFlat substitutes a different sector depending on the viewpoint
	bool hasFFloors;         // 3D floors rebuild every frame for moving sectors (see plan P2a)
	bool producesPortal;     // sky/mirror/horizon/stack walls route to the portal manager, not a list
	bool inArea;             // the seg's area resolution is view-dependent (in_area == area_default)
};

// May this seg's geometry be cached at all?
bool ComputeIsCacheable(const WallCacheEligibility &e);

// Whether a cached entry may be reused: it must be eligible, previously filled, and stamp-identical.
bool ComputeCanReuse(const WallCacheEligibility &e, bool haveCached,
                     const WallCacheStamp &cached, const WallCacheStamp &current);


}} // namespace zx::levelmesh

#endif // ZX_WALLCACHE_COMPUTE_H
