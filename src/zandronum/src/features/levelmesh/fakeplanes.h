// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's missing-texture fill for the map bake: the subsectors GL draws another sector's ceiling or floor over (FDrawInfo::HandleMissingTextures, its hole case), worked out from the map with fakeplane_compute.

#ifndef ZX_LEVELMESH_FAKEPLANES_H
#define ZX_LEVELMESH_FAKEPLANES_H

#include "tarray.h"
#include "features/levelmesh/computation/fakeplane_compute.h"

struct sector_t;

namespace zx { namespace levelmesh {

// [rc4l] Works the fills out from the map as it stands, every candidate read again, and adds to changedSubs every subsector whose fake planes differ from last time.
void RecomputeAllFakePlanes(TArray<int> &changedSubs);

// [rc4l] The same reading again only what the moved sectors and the sides whose textures changed touch.
void RecomputeFakePlanesFor(TArray<int> &changedSubs, const TArray<int> &movedSectors, const TArray<int> &changedSides);

// [rc4l] Whether side's upper or lower texture changed since the fills last read it, taking the new ones.
bool SideTexturesChanged(int side);

// [rc4l] The fake planes over subsector sub, NULL when it has none.
const FakePlane *FakePlanesFor(int sub, int &count);

// [rc4l] The subsectors that carry a fake plane modelled on sector, whose light or texture they take, NULL when there are none.
const int *FakeSubsModelledOn(int sector, int &count);

// [rc4l] Forgets every fill, for a new level.
void ClearFakePlanes();

// [rc4l] Whether a sector takes another's heights (Transfer_Heights), whose planes then depend on where the viewer stands.
bool ViewDependent(const sector_t *s);

}} // namespace zx::levelmesh

#endif
