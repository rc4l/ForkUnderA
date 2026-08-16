// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Level mesh P0 glue: walk the loaded map, collect the static budget inputs, report. No
// geometry is built and nothing renders from this yet -- see features/levelmesh/README.md.

#include "features/levelmesh/levelmesh.h"
#include "features/levelmesh/wallcache.h"
#include "features/levelmesh/flatmesh.h"

#include "c_dispatch.h"   // CCMD
#include "c_console.h"    // Printf
#include "doomtype.h"
#include "r_defs.h"
#include "r_state.h"      // sides, numsides, sectors, numsectors, subsectors, numsubsectors

namespace zx { namespace levelmesh {

// [rc4l] Which end of the linedef this sidedef starts from. Mirrors GetSideVertices in
// gl/data/gl_setup.cpp; side_t::V1/V2 are inline-private to po_man.cpp so they cannot be reused.
static void SideVertices(const side_t *side, const vertex_t *&v1, const vertex_t *&v2)
{
	const line_t *ln = side->linedef;
	if (ln->sidedef[0] == side)
	{
		v1 = ln->v1;
		v2 = ln->v2;
	}
	else
	{
		v1 = ln->v2;
		v2 = ln->v1;
	}
}

// [rc4l] 3D-floor blocks this side can present: rovers in the front sector plus rovers in the back,
// matching what DoFFloorBlocks walks in gl/scene/gl_walls.cpp.
static int FFloorBlocksForSide(const side_t *side)
{
	int blocks = 0;
	const line_t *ln = side->linedef;
	if (ln->frontsector && ln->frontsector->e) blocks += (int)ln->frontsector->e->XFloor.ffloors.Size();
	if (ln->backsector && ln->backsector->e) blocks += (int)ln->backsector->e->XFloor.ffloors.Size();
	return blocks;
}

void CollectSideBudgets(Collected &out)
{
	out.sides.Clear();
	out.preprocessed = false;
	out.flatVertices = 0;
	out.sectorsWithFFloors = 0;
	out.sectorsWithBands = 0;
	out.maxLightlistSize = 0;
	out.sidesTouchingBands = 0;

	if (sides == NULL || numsides <= 0 || lines == NULL) return;

	out.sides.Resize(numsides);
	for (int i = 0; i < numsides; i++)
	{
		const side_t *side = &sides[i];
		SideBudgetInput in;
		in.numSegs = side->numsegs;
		in.leftVertexSectors = 0;
		in.rightVertexSectors = 0;
		in.ffloorBlocks = 0;
		in.twoSided = false;

		if (side->linedef != NULL)
		{
			const vertex_t *v1 = NULL, *v2 = NULL;
			SideVertices(side, v1, v2);
			if (v1) in.leftVertexSectors = v1->numsectors;
			if (v2) in.rightVertexSectors = v2->numsectors;
			in.twoSided = (side->linedef->backsector != NULL);
			in.ffloorBlocks = FFloorBlocksForSide(side);
			// [rc4l] numsegs is only filled by PrepareSegs, so a nonzero one proves the GL level
			// data exists; a dedicated server skips gl_PreprocessLevel and leaves it at zero.
			if (side->numsegs > 0) out.preprocessed = true;

			// [rc4l] SplitWall keys off the FRONT sector's lightlist, so that is what decides
			// whether this side's vertex count can vary.
			const sector_t *fs = side->sector;
			if (fs != NULL && fs->e != NULL && (int)fs->e->XFloor.lightlist.Size() > 1)
				out.sidesTouchingBands++;
		}

		out.sides[i] = in;
	}

	for (int i = 0; i < numsectors; i++)
	{
		const sector_t *sec = &sectors[i];
		if (sec->e == NULL) continue;
		if (sec->e->XFloor.ffloors.Size() > 0) out.sectorsWithFFloors++;
		const int ll = (int)sec->e->XFloor.lightlist.Size();
		if (ll > out.maxLightlistSize) out.maxLightlistSize = ll;
		if (ll > 1) out.sectorsWithBands++;
	}

	// [rc4l] What FFlatVertexBuffer already reserves, so the report compares like with like: one
	// vertex per subsector edge, per plane, exactly as CreateSubsectorVertices emits them.
	if (subsectors != NULL && numsubsectors > 0)
	{
		long long perPlane = 0;
		for (int i = 0; i < numsubsectors; i++) perPlane += subsectors[i].numlines;
		out.flatVertices = perPlane * 2; // floor + ceiling
	}
}

// [rc4l] Full-level bake arming. One frame, consumed once -- see levelmesh.h.
static bool g_pendingFullBake = false;
void ArmFullBake() { g_pendingFullBake = true; }
bool TakePendingFullBake()
{
	const bool v = g_pendingFullBake;
	g_pendingFullBake = false;
	return v;
}

// [rc4l] Generation-stamped rather than cleared, so arming a second bake costs nothing.
static TArray<int> g_bakeSideStamp;
static int         g_bakeGen = 0;

void BeginBakePass()
{
	g_bakeGen++;
	if ((int)g_bakeSideStamp.Size() != numsides)
	{
		g_bakeSideStamp.Clear();
		g_bakeSideStamp.Resize(numsides > 0 ? numsides : 0);
		for (unsigned i = 0; i < g_bakeSideStamp.Size(); i++) g_bakeSideStamp[i] = 0;
	}
}

static bool g_inBakePass = false;
void SetBakePassActive(bool on) { g_inBakePass = on; }
bool IsBakePassActive() { return g_inBakePass; }

bool ClaimSideForBake(int sideIndex)
{
	if (sideIndex < 0 || (unsigned)sideIndex >= g_bakeSideStamp.Size()) return true;
	if (g_bakeSideStamp[sideIndex] == g_bakeGen) return false;
	g_bakeSideStamp[sideIndex] = g_bakeGen;
	return true;
}

}} // namespace zx::levelmesh

CCMD( fua_levelmesh_bakeall )
{
	zx::levelmesh::ArmFullBake( );
	Printf( "levelmesh: full-level bake armed -- it runs on the next rendered frame.\n" );
}

//==========================================================================
//
// fua_levelmesh_stats
//
//==========================================================================

CCMD( fua_levelmesh_stats )
{
	using namespace zx::levelmesh;

	Collected c;
	CollectSideBudgets( c );

	if ( c.sides.Size( ) == 0 )
	{
		Printf( "levelmesh: no level loaded.\n" );
		return;
	}

	const LevelBudget b = ComputeLevelBudget( &c.sides[0], (int)c.sides.Size( ) );
	const long long wallBytes = ComputeBufferBytes( b.totalVertices, kVertexStrideBytes );
	const long long flatBytes = ComputeBufferBytes( c.flatVertices, kVertexStrideBytes );

	Printf( "levelmesh budget (worst-case fixed ranges)\n" );
	Printf( "  sides %d, sectors %d, subsectors %d\n", numsides, numsectors, numsubsectors );
	if ( !c.preprocessed )
	{
		Printf( "  WARNING: GL level data absent (server build or pre-load) -- seg and vertex\n"
				"           sector counts read zero, so these numbers are a floor, not a budget.\n" );
	}
	Printf( "  wall vertices %lld  (%.2f MB)\n", b.totalVertices, wallBytes / ( 1024.0 * 1024.0 ) );
	Printf( "  flat vertices %lld  (%.2f MB, what FFlatVertexBuffer reserves today)\n",
			c.flatVertices, flatBytes / ( 1024.0 * 1024.0 ) );
	Printf( "  total %.2f MB at %d bytes/vertex\n",
			( wallBytes + flatBytes ) / ( 1024.0 * 1024.0 ), kVertexStrideBytes );
	Printf( "  mean %.1f vertices/side, worst %d (side %d)\n",
			b.sides ? (double)b.totalVertices / b.sides : 0.0, b.maxPerSide, b.worstSideIndex );
	Printf( "  3D floors: %d/%d sectors; light-banded (lightlist>1): %d sectors, %d/%d sides (%.2f%%), deepest stack %d\n",
			c.sectorsWithFFloors, numsectors, c.sectorsWithBands,
			c.sidesTouchingBands, numsides,
			numsides ? 100.0 * c.sidesTouchingBands / numsides : 0.0,
			c.maxLightlistSize );

	// [rc4l] What the mesh actually holds right now, next to what the level could give it. The gap
	// between the two is the honest ceiling on a draw-the-whole-level renderer.
	CoverageStats cov;
	GetCoverage( cov );
	Printf( "  coverage: %d/%d drawable segs baked (%.1f%%; %d segs total incl. minisegs), "
			"%d uncacheable, %d wall pieces; %d flat pieces of %d planes, %d sprites\n",
			cov.baked, cov.drawableSegs,
			cov.drawableSegs ? 100.0 * cov.baked / cov.drawableSegs : 0.0, cov.segs,
			cov.uncacheable, cov.pieces,
			FlatPieceCount( ), numsubsectors * 2, SpritePieceCount( ) );

	int rp, rpoly, rff, ra, ro, ok;
	GetRejects( rp, rpoly, rff, ra, ro, ok );
	Printf( "  captures: %d ok, refused %d portal / %d polyobj / %d 3D-floor / %d area / %d other\n",
			ok, rp, rpoly, rff, ra, ro );
}
