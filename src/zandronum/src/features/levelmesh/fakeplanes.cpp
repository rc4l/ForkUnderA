// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/fakeflat/fakeflat.h"   // [rc4l] ViewDependent
#include "features/levelmesh/fakeplanes.h"
#include "r_defs.h"
#include "r_state.h"
#include "r_sky.h"        // skyflatnum
#include "p_lnspec.h"     // Line_Horizon
#include "p_3dfloors.h"   // FF_FIX, for AddLowerMissingTexture's exception
#include "features/materials/materials.h"   // [rc4l] MaterialForTextureId, the test Process makes for a tier's texture
#include "features/levelmesh/flatmesh.h"   // RefreshFakePlanes, for fua_fakeplanes
#include "c_dispatch.h"   // CCMD
#include "stats.h"        // cycle_t
#include "d_player.h"     // players, for the sample move
#include "doomstat.h"     // consoleplayer

namespace zx { namespace levelmesh {

static TArray<FillSeg> g_fillSegs;
static TArray<int> g_fillFirst, g_fillCount;
static std::vector<FakePlane> g_planes;              // [rc4l] by subsector, subsector i's from g_planeStart[i] to g_planeStart[i + 1]
static std::vector<int> g_planeStart;
static std::vector<int> g_modelSubs, g_modelStart;   // [rc4l] the same by model sector, the subsectors carrying a plane modelled on it


static bool IsSky(const sector_t *s, int plane) { return s->rc.GetTexture(plane) == skyflatnum; }

static bool Sloped(const secplane_core_t &p) { return (p.a | p.b) != 0; }

static bool ClosedAtFloor(const sector_t *s)
{
	return ComputeClosedAtFloor(Sloped(s->rc.floorplane) || Sloped(s->rc.ceilingplane), s->rc.GetPlaneTexZ(sector_t::floor).Raw(),
		s->rc.GetPlaneTexZ(sector_t::ceiling).Raw());
}

// [rc4l] DoOneSectorUpper's test for a texture on the edge it stops at.
static bool HasTexture(const side_t *side, int part)
{
	if (side == NULL) return false;
	FTexture *tex = TexMan[side->rc.GetTexture(part)];
	return tex != NULL && tex->UseType != FTexture::TEX_Null;
}

// [rc4l] A seg GL might leave a missing upper or lower on: what holds still while the level runs, and what it filed as the heights last stood.
struct Candidate
{
	int seg;
	bool topTextured, bottomTextured;   // [rc4l] Process's own test, FMaterial::ValidateTexture
	bool up, down;
	long long upZ, downZ;
};
static std::vector<Candidate> g_candidates;
static std::vector<int> g_edgeSegs;                  // [rc4l] segs onto another sector, whose far heights a move changes
static std::vector<int> g_candStart, g_candOrder;   // [rc4l] by sector, the candidates it fronts or backs (item i is candidate i / 2)
static std::vector<int> g_edgeStart, g_edgeOrder;   // [rc4l] by sector, the edges it is the far side of
static std::vector<int> g_candOfSeg;                 // [rc4l] each seg's candidate, -1 for none
static TArray<int> g_sideTop, g_sideBottom;          // [rc4l] each side's upper and lower texture as last seen
static bool g_staticBuilt = false;
static double g_stageMs[4];   // [rc4l] the last recompute's stages: candidates, filing, fills, grouping
static int g_lastEvaluated = 0, g_lastEntries = 0;

static bool Validates(const side_t *side, int part) { return zx::materials::MaterialForTextureId(side->rc.GetTexture(part)) != NULL; }

// [rc4l] What the fill reads of the level that holds still: which segs join which subsectors, which could leave a gap at all, and which sectors each depends on, with textures read again when a side's change (RefreshSide).
static void BuildStatic()
{
	g_fillSegs.Resize((unsigned)numsegs);
	g_fillFirst.Resize((unsigned)numsubsectors);
	g_fillCount.Resize((unsigned)numsubsectors);
	g_edgeSegs.clear();
	g_candidates.clear();
	g_candOfSeg.assign((size_t)numsegs, -1);
	for (int i = 0; i < numsubsectors; i++)
	{
		g_fillFirst[i] = int(subsectors[i].firstline - segs);
		g_fillCount[i] = (int)subsectors[i].numlines;
	}
	std::vector<int> candKeys, edgeKeys;
	for (int i = 0; i < numsegs; i++)
	{
		const seg_t &sg = segs[i];
		FillSeg &f = g_fillSegs[i];
		f = FillSeg();
		const sector_t *front = sg.frontsector, *back = sg.backsector;
		f.closed = back == NULL || sg.PartnerSeg == NULL || zx::fakeflat::ViewDependent(back);
		if (f.closed) continue;
		f.backSub = int(sg.PartnerSeg->Subsector - subsectors);
		f.sectorEdge = front != back && sg.linedef != NULL;
		f.topTextured = HasTexture(sg.sidedef, side_t::top);
		f.bottomTextured = HasTexture(sg.sidedef, side_t::bottom);
		if (f.sectorEdge) { g_edgeSegs.push_back(i); edgeKeys.push_back(back->sectornum); }

		const subsector_t *sub = sg.Subsector;
		const bool hasSideAndLine = sg.sidedef != NULL && sg.linedef != NULL && front != NULL && sub != NULL;
		if (!ComputeIsFillCandidate(hasSideAndLine, hasSideAndLine && (sg.linedef->rc.flags & (ML_TWOSIDED | ML_3DMIDTEX)) != 0,
				hasSideAndLine && sg.linedef->rc.special == Line_Horizon, hasSideAndLine && sub->render_sector == sub->sector,
				hasSideAndLine && front == sub->sector, hasSideAndLine && zx::fakeflat::ViewDependent(front)))
			continue;
		Candidate c = {};
		c.seg = i;
		c.topTextured = Validates(sg.sidedef, side_t::top);
		c.bottomTextured = Validates(sg.sidedef, side_t::bottom);
		g_candOfSeg[(size_t)i] = (int)g_candidates.size();
		g_candidates.push_back(c);
		candKeys.push_back(front->sectornum);
		candKeys.push_back(back->sectornum);
	}
	ComputeIndexByKey(candKeys, numsectors, g_candStart, g_candOrder);
	ComputeIndexByKey(edgeKeys, numsectors, g_edgeStart, g_edgeOrder);
	g_sideTop.Resize((unsigned)numsides);
	g_sideBottom.Resize((unsigned)numsides);
	for (int i = 0; i < numsides; i++)
	{
		g_sideTop[i] = sides[i].rc.GetTexture(side_t::top).GetIndex();
		g_sideBottom[i] = sides[i].rc.GetTexture(side_t::bottom).GetIndex();
	}
	g_staticBuilt = true;
}

// [rc4l] An edge's far planes as gl_FakeFlat gives them.
static void RefreshEdge(int seg)
{
	FillSeg &f = g_fillSegs[seg];
	const sector_t *back = segs[seg].backsector;
	f.farFloor = back->rc.GetPlaneTexZ(sector_t::floor).Raw();
	f.farCeil = ClosedAtFloor(back) ? f.farFloor : back->rc.GetPlaneTexZ(sector_t::ceiling).Raw();
	f.farCeilSloped = Sloped(back->rc.ceilingplane);
	f.farFloorSloped = Sloped(back->rc.floorplane);
}

// [rc4l] Whether GLWall::Process leaves a missing upper or lower on the candidate's wall as the heights stand, and at what height.
static void Evaluate(Candidate &c)
{
	const seg_t &sg = segs[c.seg];
	const sector_t *front = sg.frontsector, *back = sg.backsector;
	MissingTierInput in = {};
	const vertex_t *ends[2] = { sg.v1, sg.v2 };
	const bool closed = ClosedAtFloor(back);
	for (int k = 0; k < 2; k++)
	{
		in.ffh[k] = front->rc.floorplane.ZatPoint(ends[k]).Raw();
		in.fch[k] = front->rc.ceilingplane.ZatPoint(ends[k]).Raw();
		in.bfh[k] = back->rc.floorplane.ZatPoint(ends[k]).Raw();
		in.bch[k] = closed ? in.bfh[k] : back->rc.ceilingplane.ZatPoint(ends[k]).Raw();
	}
	in.frontFloorSky = IsSky(front, sector_t::floor);
	in.frontCeilSky = IsSky(front, sector_t::ceiling);
	in.backFloorSky = IsSky(back, sector_t::floor);
	in.backCeilSky = IsSky(back, sector_t::ceiling);
	in.polyobj = !!(sg.sidedef->rc.Flags & WALLF_POLYOBJ);
	in.partnerOk = !(sg.PartnerSeg->Subsector->hacked & 4);

	in.textured = c.topTextured;
	in.sloped = Sloped(front->rc.ceilingplane) || Sloped(back->rc.ceilingplane);
	c.up = ComputeMissingUpper(in, c.upZ);

	// [rc4l] AddLowerMissingTexture leaves a transparent door's lowered floor and an FF_FIX 3D floor's deliberate gap alone.
	const bool doorHack = back->transdoor && back->transdoorheight == back->rc.GetPlaneTexZ(sector_t::floor);
	const bool fixHack = back->e != NULL && back->e->XFloor.ffloors.Size() > 0 &&
		(back->e->XFloor.ffloors[0]->flags & (FF_FIX | FF_SEETHROUGH)) == FF_FIX;
	in.textured = c.bottomTextured;
	in.sloped = Sloped(front->rc.floorplane) || Sloped(back->rc.floorplane);
	c.down = !doorHack && !fixHack && ComputeMissingLower(in, c.downZ);
}

// [rc4l] A side's segs with its new upper and lower textures, the edges the fill stops at and the candidates it files from.
static int RefreshSide(int side)
{
	int evaluated = 0;
	for (int k = 0; k < sides[side].numsegs; k++)
	{
		const int seg = int(sides[side].segs[k] - segs);
		FillSeg &f = g_fillSegs[seg];
		f.topTextured = HasTexture(&sides[side], side_t::top);
		f.bottomTextured = HasTexture(&sides[side], side_t::bottom);
		const int c = g_candOfSeg[(size_t)seg];
		if (c < 0) continue;
		g_candidates[(size_t)c].topTextured = Validates(&sides[side], side_t::top);
		g_candidates[(size_t)c].bottomTextured = Validates(&sides[side], side_t::bottom);
		Evaluate(g_candidates[(size_t)c]);
		evaluated++;
	}
	return evaluated;
}

// [rc4l] The edges and candidates the moved sectors touch read again, every one of them with no list; returns how many candidates were.
static int RefreshFor(const TArray<int> *moved)
{
	if (moved == NULL)
	{
		for (int seg : g_edgeSegs) RefreshEdge(seg);
		for (Candidate &c : g_candidates) Evaluate(c);
		return (int)g_candidates.size();
	}
	int evaluated = 0;
	for (unsigned m = 0; m < moved->Size(); m++)
	{
		const int s = (*moved)[m];
		if (s < 0 || s >= numsectors) continue;
		for (int k = g_edgeStart[s]; k < g_edgeStart[s + 1]; k++) RefreshEdge(g_edgeSegs[g_edgeOrder[k]]);
		for (int k = g_candStart[s]; k < g_candStart[s + 1]; k++) { Evaluate(g_candidates[g_candOrder[k] / 2]); evaluated++; }
	}
	return evaluated;
}

bool SideTexturesChanged(int side)
{
	if (side < 0 || (unsigned)side >= g_sideTop.Size()) return false;
	const int top = sides[side].rc.GetTexture(side_t::top).GetIndex(), bottom = sides[side].rc.GetTexture(side_t::bottom).GetIndex();
	if (top == g_sideTop[side] && bottom == g_sideBottom[side]) return false;
	g_sideTop[side] = top;
	g_sideBottom[side] = bottom;
	return true;
}

// [rc4l] Recompute's body: moved and sides NULL read every candidate again, else only what they touch.
static void Recompute(TArray<int> &changedSubs, const TArray<int> *moved, const TArray<int> *sides)
{
	if (segs == NULL || subsectors == NULL || numsubsectors <= 0) return;
	cycle_t stage[4];
	for (int i = 0; i < 4; i++) stage[i].Reset();
	const bool rebuild = !g_staticBuilt || g_fillSegs.Size() != (unsigned)numsegs;
	if (rebuild) BuildStatic();
	stage[0].Clock();
	g_lastEvaluated = RefreshFor(rebuild ? NULL : moved);
	for (unsigned i = 0; !rebuild && sides != NULL && i < sides->Size(); i++)
		if ((*sides)[i] >= 0 && (*sides)[i] < numsides) g_lastEvaluated += RefreshSide((*sides)[i]);
	stage[0].Unclock();
	stage[1].Clock();
	std::vector<MissingEntry> up, down;
	std::vector<int> upOf((size_t)numsubsectors, -1), downOf((size_t)numsubsectors, -1);
	for (const Candidate &c : g_candidates)
	{
		if (!c.up && !c.down) continue;
		const seg_t &sg = segs[c.seg];
		const int sub = int(sg.Subsector - subsectors);
		if (c.up) ComputeFileMissing(up, upOf, true, sub, c.upZ, sg.backsector->sectornum);
		if (c.down) ComputeFileMissing(down, downOf, false, sub, c.downZ, sg.backsector->sectornum);
	}
	stage[1].Unclock();
	stage[2].Clock();
	std::vector<FakePlane> planes = ComputeFakePlanes(true, up, &g_fillSegs[0], &g_fillFirst[0], &g_fillCount[0], numsubsectors);
	const std::vector<FakePlane> lower = ComputeFakePlanes(false, down, &g_fillSegs[0], &g_fillFirst[0], &g_fillCount[0], numsubsectors);
	planes.insert(planes.end(), lower.begin(), lower.end());
	stage[2].Unclock();
	stage[3].Clock();

	std::vector<FakePlane> now;
	std::vector<int> nowStart;
	ComputeGroupPlanes(planes, numsubsectors, false, now, nowStart);
	for (int sub : ComputeChangedSubs(g_planes, g_planeStart, now, nowStart, numsubsectors)) changedSubs.Push(sub);
	g_planes.swap(now);
	g_planeStart.swap(nowStart);

	std::vector<FakePlane> byModel;
	ComputeGroupPlanes(g_planes, numsectors, true, byModel, g_modelStart);
	g_modelSubs.resize(byModel.size());
	for (size_t i = 0; i < byModel.size(); i++) g_modelSubs[i] = byModel[i].sub;
	stage[3].Unclock();
	for (int i = 0; i < 4; i++) g_stageMs[i] = stage[i].TimeMS();
	g_lastEntries = (int)(up.size() + down.size());
}

void RecomputeAllFakePlanes(TArray<int> &changedSubs) { Recompute(changedSubs, NULL, NULL); }

void RecomputeFakePlanesFor(TArray<int> &changedSubs, const TArray<int> &movedSectors, const TArray<int> &changedSides)
{
	Recompute(changedSubs, &movedSectors, &changedSides);
}

const FakePlane *FakePlanesFor(int sub, int &count)
{
	count = 0;
	if (sub < 0 || (size_t)sub + 1 >= g_planeStart.size()) return NULL;
	count = g_planeStart[sub + 1] - g_planeStart[sub];
	return count > 0 ? &g_planes[g_planeStart[sub]] : NULL;
}

const int *FakeSubsModelledOn(int sector, int &count)
{
	count = 0;
	if (sector < 0 || (size_t)sector + 1 >= g_modelStart.size()) return NULL;
	count = g_modelStart[sector + 1] - g_modelStart[sector];
	return count > 0 ? &g_modelSubs[g_modelStart[sector]] : NULL;
}

void ClearFakePlanes()
{
	g_planes.clear();
	g_planeStart.clear();
	g_modelSubs.clear();
	g_modelStart.clear();
	g_staticBuilt = false;
}

}} // namespace zx::levelmesh

// [rc4l] fua_fakeplanes [subsector] -- works GL's missing-texture fills out again from scratch and after a move, how long each takes, how many fake planes they put where, and one subsector's.
CCMD( fua_fakeplanes )
{
	cycle_t full, heights; full.Reset(); heights.Reset();
	full.Clock();
	zx::levelmesh::RefreshAllFakePlanes();
	full.Unclock();
	// [rc4l] A move of the sector under the player stands for a typical one.
	TArray<int> moved, noSides;
	if ( players[consoleplayer].mo != NULL ) moved.Push( players[consoleplayer].mo->Sector->sectornum );
	heights.Clock();
	zx::levelmesh::RefreshFakePlanes(moved, noSides);
	heights.Unclock();
	int subs = 0, planes = 0;
	for ( int i = 0; i < numsubsectors; i++ )
	{
		int n = 0;
		if ( zx::levelmesh::FakePlanesFor( i, n ) != NULL ) { subs++; planes += n; }
	}
	Printf( "fake planes: %d over %d subsectors, worked out in %.3f ms for every candidate and %.3f ms after a move\n", planes, subs,
		full.TimeMS( ), heights.TimeMS( ) );
	Printf( "  after a move: %d of %u candidates %.3f, filing %.3f, %d fills %.3f, grouping %.3f ms\n", zx::levelmesh::g_lastEvaluated,
		(unsigned)zx::levelmesh::g_candidates.size(), zx::levelmesh::g_stageMs[0], zx::levelmesh::g_stageMs[1], zx::levelmesh::g_lastEntries,
		zx::levelmesh::g_stageMs[2], zx::levelmesh::g_stageMs[3] );
	if ( argv.argc( ) < 2 ) return;
	const int sub = atoi( argv[1] );
	int n = 0;
	const zx::levelmesh::FakePlane *p = zx::levelmesh::FakePlanesFor( sub, n );
	if ( n == 0 ) Printf( "  subsector %d: none\n", sub );
	for ( int i = 0; i < n; i++ ) Printf( "  subsector %d: sector %d's %s\n", sub, p[i].model, p[i].upper ? "ceiling" : "floor" );
}
