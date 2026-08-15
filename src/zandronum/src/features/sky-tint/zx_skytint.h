// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Sky-derived outdoor light, applied at DRAW TIME.
//
// The tint is never written into sector_t::ColorMap. That matters for a reason the earlier trial
// found the hard way: sector colours are serialised into savegames (p_saveg.cpp), so baking the
// tint means a save taken while it is on carries it forever -- and on reload those sectors are no
// longer "default white", so the code that politely defers to a mapper's own colour would defer to
// its own past output. There is no way back from that. Here the world is left alone and the colour
// is substituted into the render copy, so turning the option off restores the map exactly and a
// savegame never learns the feature exists.
//
// The per-sector strengths are computed once per level (and again when a cvar changes), not per
// frame. The algorithm itself is in computation/skytint_compute.

#ifndef ZX_SKYTINT_H
#define ZX_SKYTINT_H

struct sector_t;
struct FColormap;

namespace zx
{

// Recompute the whole table: which sectors see sky, how far the light bleeds, and what colour the
// current sky is. Called on level load and whenever one of the cl_fua_skytint_* cvars moves.
void SkyTint_Rebuild();

// Forget everything. Called when a level ends so no index outlives the sector array it refers to.
void SkyTint_Clear();

// Substitute the tinted light colour for `sec`, if the feature is on and that sector has one.
// Anchored where the renderer reads a sector's colormap; a no-op otherwise.
void SkyTint_Apply(const sector_t *sec, FColormap &cm);

} // namespace zx

#endif
