// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The render copy's decals: each side's wall decals and every floor and ceiling mark as values, taken between tics, since a tic spawns, fades and destroys decals while a frame draws.

#ifndef ZX_GAMECOPY_DECALS_H
#define ZX_GAMECOPY_DECALS_H

#include "g_shared/a_sharedglobal.h"   // [rc4l] DBaseDecal
#include "features/decals/flatdecal.h"   // [rc4l] DFlatDecal

#include <vector>

namespace zx { namespace gamecopy {

// [rc4l] A wall decal as the renderer reads it, its members declared once in fields.h.
struct DecalRecord
{
#define ZX_DECAL_FIELD(member) decltype(DBaseDecal::member) member;
#include "features/gamecopy/fields.h"
#undef ZX_DECAL_FIELD
};

// [rc4l] A floor or ceiling mark (features/decals' DFlatDecal) as the renderer reads it: a decal's members and its own, declared once in fields.h, and which mark it is.
struct FlatDecalRecord
{
	const void *who;   // [rc4l] identity only, for a cache of what is worked out from it
#define ZX_DECAL_FIELD(member) decltype(DBaseDecal::member) member;
#define ZX_FLATDECAL_FIELD(member) decltype(DFlatDecal::member) member;
#include "features/gamecopy/fields.h"
#undef ZX_FLATDECAL_FIELD
#undef ZX_DECAL_FIELD
};

// [rc4l] Every floor and ceiling mark as the last fill took them, oldest first.
const std::vector<FlatDecalRecord> &FlatDecals();

// [rc4l] A side's decals in its list's order, NULL when it has none.
const DecalRecord *SideDecals(int side, int &count);
// [rc4l] DBaseDecal::GetXY on the copy: where along its side a decal sits.
void DecalXY(const DecalRecord &decal, const side_t *side, fixed_t &x, fixed_t &y);

// [rc4l] As a tic ends: the sides holding decals found, for the next fill to take alone.
void ScanDecalSides();
// [rc4l] The sides holding decals now that the tic's end did not find, for fua_copy_prescan_check.
int DecalSidesMissed();
// [rc4l] Takes every side's decals, or with scanned those of the sides the tic's end found; part of the fill.
void FillDecals(bool scanned);

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_DECALS_H
