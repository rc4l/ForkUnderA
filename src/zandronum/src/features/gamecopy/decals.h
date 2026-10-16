// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The render copy's wall decals: each side's decals as values, taken between tics, since a tic spawns, fades and destroys decals while a frame draws.

#ifndef ZX_GAMECOPY_DECALS_H
#define ZX_GAMECOPY_DECALS_H

#include "g_shared/a_sharedglobal.h"   // [rc4l] DBaseDecal

namespace zx { namespace gamecopy {

// [rc4l] A wall decal as the renderer reads it, its members declared once in fields.h.
struct DecalRecord
{
#define ZX_DECAL_FIELD(member) decltype(DBaseDecal::member) member;
#include "features/gamecopy/fields.h"
#undef ZX_DECAL_FIELD
};

// [rc4l] A side's decals in its list's order, NULL when it has none.
const DecalRecord *SideDecals(int side, int &count);
// [rc4l] DBaseDecal::GetXY on the copy: where along its side a decal sits.
void DecalXY(const DecalRecord &decal, const side_t *side, fixed_t &x, fixed_t &y);

// [rc4l] Takes every side's decals; part of the fill.
void FillDecals();

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_DECALS_H
