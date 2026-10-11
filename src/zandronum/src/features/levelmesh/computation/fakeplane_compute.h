// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's fill for a missing upper or lower texture (FDrawInfo::HandleMissingTextures, its hole case), engine-free, so the map bake can draw the fake planes GL draws.

#ifndef ZX_FAKEPLANE_COMPUTE_H
#define ZX_FAKEPLANE_COMPUTE_H

#include <vector>

namespace zx { namespace levelmesh {

// [rc4l] One two-sided tier as GLWall::Process decides it, heights raw fixed at the seg's two ends (index 0 and 1).
struct MissingTierInput
{
	long long ffh[2], fch[2], bfh[2], bch[2];   // [rc4l] front floor and ceiling, back floor and ceiling
	bool frontFloorSky, frontCeilSky, backFloorSky, backCeilSky;
	bool textured;    // [rc4l] the tier's own texture (top for an upper, bottom for a lower) exists
	bool sloped;      // [rc4l] either side's plane for this tier is sloped
	bool polyobj;     // [rc4l] a polyobject's side, which GL never fills
	bool partnerOk;   // [rc4l] the seg has a partner whose subsector the nodebuilder did not mark malformed
};

// [rc4l] Whether Process files a missing upper on this tier, and the far ceiling height it files (bch1a): the back ceiling cuts below the front, nothing textures the gap, and it is not a sloped pair GL paints with the ceiling's own texture.
bool ComputeMissingUpper(const MissingTierInput &in, long long &planez);

// [rc4l] The same for a missing lower and its far floor height (bfh1), which GL never files against a sky floor.
bool ComputeMissingLower(const MissingTierInput &in, long long &planez);

// [rc4l] A subsector's filed missing texture: the height of the plane that would close it and the sector that plane belongs to.
struct MissingEntry
{
	int sub;
	long long planez;
	int model;
};

// [rc4l] Files a missing tier under its subsector as AddUpper/LowerMissingTexture do: one entry a subsector, found through entryOfSub (-1 for none, one slot a subsector), keeping the lowest far ceiling (highest far floor) and that seg's sector.
void ComputeFileMissing(std::vector<MissingEntry> &entries, std::vector<int> &entryOfSub, bool upper, int sub, long long planez, int model);

// [rc4l] One seg of a subsector as the fill crosses it, both planes kept so one table serves upper and lower fills.
struct FillSeg
{
	bool closed;       // [rc4l] no back sector or no partner seg: a wall no fill crosses
	int  backSub;      // [rc4l] the partner seg's subsector
	bool sectorEdge;   // [rc4l] a linedef between two different sectors, whose far planes bound the fill
	long long farCeil, farFloor;           // [rc4l] the far sector's plane heights, raw fixed
	bool farCeilSloped, farFloorSloped;
	bool topTextured, bottomTextured;      // [rc4l] this side's upper and lower textures exist
};

// [rc4l] The subsectors DoOneSectorUpper (or Lower) covers from subsector start at height planez, in the order it pushes them, or none when a closed seg, a sloped far plane, one past planez, or a textured edge at it stops the fill; subsector i's segs are segs[first[i]] up to segs[first[i] + count[i] - 1], and seen (one mark a subsector) is GL's validcount, a fresh stamp a fill.
std::vector<int> ComputeFakePlaneFill(bool upper, int start, long long planez, const FillSeg *segs, const int *first, const int *count, int numSubs,
	std::vector<int> &seen, int stamp);

// [rc4l] gl_FakeFlat's rule for a back sector whose floor is above its ceiling: GL closes it at the floor, unless either plane is sloped.
bool ComputeClosedAtFloor(bool sloped, long long floorZ, long long ceilingZ);

// [rc4l] Whether a seg can ever leave a missing texture GL fills: Process's gate for a two-sided wall that is no horizon line, and AddUpperMissingTexture's refusal of a subsector drawn as another sector or a front sector GL resolves per viewer.
bool ComputeIsFillCandidate(bool hasSideAndLine, bool twoSided, bool horizonLine, bool subDrawnAsItsSector, bool frontIsSubSector, bool frontViewDependent);

// [rc4l] A plane GL draws over a subsector that is not its own: the model sector's ceiling (upper) or floor (lower).
struct FakePlane
{
	int sub;
	int model;
	bool upper;
};

// [rc4l] HandleMissingTextures' hole case over every filed entry in order: each fill that succeeds puts its entry's plane over every subsector it covers, and settles the other entries of those subsectors when it covers more than one.
std::vector<FakePlane> ComputeFakePlanes(bool upper, const std::vector<MissingEntry> &entries, const FillSeg *segs, const int *first,
	const int *count, int numSubs);

// [rc4l] A counting sort's index: start gets numKeys + 1 offsets, order the item indices grouped by key (keys[i] is item i's), each key's in the order given.
void ComputeIndexByKey(const std::vector<int> &keys, int numKeys, std::vector<int> &start, std::vector<int> &order);

// [rc4l] Fake planes grouped by subsector, or by model sector with byModel (ComputeIndexByKey): start gets keys + 1 offsets, out the planes in key order.
void ComputeGroupPlanes(const std::vector<FakePlane> &planes, int keys, bool byModel, std::vector<FakePlane> &out, std::vector<int> &start);

// [rc4l] The subsectors whose run of fake planes differs between two groupings by subsector, an empty before counting as none anywhere.
std::vector<int> ComputeChangedSubs(const std::vector<FakePlane> &before, const std::vector<int> &beforeStart,
	const std::vector<FakePlane> &now, const std::vector<int> &nowStart, int numSubs);

}} // namespace zx::levelmesh

#endif
