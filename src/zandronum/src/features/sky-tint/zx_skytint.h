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

#include <cstddef>
#include <string>

struct sector_t;
struct subsector_t;
struct FColormap;

namespace zx
{

// Recompute the whole table: which sectors see sky, how far the light bleeds, and what colour the
// current sky is. Called on level load and whenever one of the cl_fua_skytint_* cvars moves.
void SkyTint_Rebuild();

// Forget everything. Called when a level ends so no index outlives the sector array it refers to.
void SkyTint_Clear();

// [rc4l] The sky can change WITHOUT the level changing: ACS ChangeSky, the changesky console
// command, and -- on every client, for every map -- the server's SetMapSky arriving after the level
// has already loaded. The table is derived from that texture, so a sky swapped underneath it leaves
// the old sky's colour on the walls with nothing to trigger a recompute. Rebuilds only when the sky
// really differs from the one the table was built for, because the call sites also fire on view
// size changes, where nothing about the light has moved.
void SkyTint_SkyChanged();

// [rc4l] Light is computed and stored per SUBSECTOR -- the BSP leaf the renderer actually draws --
// rather than per sector. A whole sector taking one value is what made the effect look like a flood
// rather than light: a room next to a lit yard came up evenly bright to its far corner. Leaves are
// small, so the same room now fades across itself.
void SkyTint_ApplySub(const subsector_t *sub, FColormap &cm);

// Is there any tint at all this level? Asked once per drawn leaf, so it has to be a plain bool read
// rather than anything that touches the table.
bool SkyTint_Active();

// TEMPORARY, for the fua_skytintinfo diagnostic.
size_t SkyTintTableSize();

// [rc4l] Once per frame, straight after FCanvasTextureInfo::UpdateAll.
//
// A 3D skybox is a camera, not an image, so the only way to know its colour is to render it and
// look. This drives that: it registers a small canvas texture against a SkyViewpoint, lets the
// engine's own camera-texture pass render it, reads the result back and rebuilds the table with it.
//
// It has to be here rather than at level load because P_SetupLevel runs outside a frame, with no
// usable GL state. Costs nothing once every skybox in the level has been sampled, which is normally
// within the first frame or two, and nothing at all on a level without one.
void SkyTint_FrameHook();

// TEMPORARY, for fua_skytintinfo: the colour each sampled skybox resolved to. std::string rather
// than FString so this header stays free of engine string headers; the caller converts.
void SkyTintSkyboxSamples(std::string &out);

// How many sky-seeing leaves render a 3D skybox instead of a texture. Those are not lit yet, and
// this is how you find out a map has them rather than wondering why part of it stayed grey.
int SkyTintSkyboxLeaves();

// TEMPORARY, for fua_skytintinfo: what the last rebuild derived from each sky, raw and shaped, with
// the shaped colour's saturation. The honest way to compare two maps -- screenshotting each measures
// where the camera was standing as much as it measures the tint.
void SkyTintDerived(std::string &out);

// TEMPORARY, for fua_skytintinfo: what the table holds for the leaf the player is standing in, and
// the sector-wide answer for the same spot. "No tint here" has two causes that are indistinguishable
// from inside the game -- nothing in the table (propagation), or something in it the renderer never
// draws -- and telling them apart by guessing cost two builds on the 3D floor case.
void SkyTintHere(std::string &out);

// [rc4l] The centre of the largest sky-lit BSP leaf on this level, for `warp`. False when the level
// has no outdoor area at all.
//
// Exists because judging an outdoor effect from wherever the player happens to spawn is unreliable
// and quietly so: a comparison of two maps taken at their player starts measured a dark room at
// R12 G9 B7 on one of them and was nearly reported as evidence about the sky.
bool SkyTintOutdoorSpot(double &x, double &y);

// The sector form, for the few places the renderer knows only which sector it is drawing (sprites
// away from a subsector, horizon portals). Takes the brightest leaf of that sector, so it errs
// toward the lit side rather than picking an arbitrary one.
void SkyTint_Apply(const sector_t *sec, FColormap &cm);

} // namespace zx

#endif
