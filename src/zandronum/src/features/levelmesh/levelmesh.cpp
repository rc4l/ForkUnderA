// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Level mesh P0 glue: walk the loaded map, collect the static budget inputs, report. No
// geometry is built and nothing renders from this yet -- see features/levelmesh/README.md.

#include "features/levelmesh/levelmesh.h"
#include "r_sky.h"   // skyflatnum, for fua_find_sky
#include "gl/data/gl_vertexbuffer.h"   // FFlatVertex, for fua_mesh_at
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


//==========================================================================
//
// fua_find_lines
//
// [rc4l] A general linedef query, so driving the engine for a test stops being guesswork.
//
//   fua_find_lines [key=value ...]
//     special=N    exact action special (see actionspecials.h)
//     door=1       any door special that does NOT need a key
//     use=1        only lines the player can activate by pressing use
//     cross=1      only lines triggered by walking over them
//     tag=N        exact tag
//     limit=N      how many to print (default 8)
//
// It prints a position to stand and a facing for each match, so the next two commands are
// player.setpos and a turn. Verifying that moving geometry reaches the backend needed a door, and
// finding one by walking the level with use held was hopeless: noclip skips doors entirely, and a
// mis-toggle walked the player out of the map.
//
// Two earlier versions of this got it wrong in instructive ways. Matching on geometry -- a two-sided
// line whose back sector has its ceiling on its floor -- is true of every closed door and also of
// every decorative alcove and computer bank, so it warped the player nose-first into a wall of
// panels. Matching on the door SPECIAL alone then found a door that is opened by walking over a
// trigger line, which no amount of pressing use will budge. Activation is the field that actually
// answers "can I open this by walking up to it and pressing use", so it is filterable here.
//
//==========================================================================

static int FindLinesArg( FCommandLine &argv, const char *key, int fallback )
{
	const size_t klen = strlen( key );
	for ( int i = 1; i < argv.argc( ); i++ )
	{
		const char *a = argv[i];
		if ( strncmp( a, key, klen ) == 0 && a[klen] == '=' ) return atoi( a + klen + 1 );
	}
	return fallback;
}

CCMD( fua_find_lines )
{
	if ( lines == NULL || numlines <= 0 )
	{
		Printf( "no level loaded.\n" );
		return;
	}

	const int wantSpecial = FindLinesArg( argv, "special", -1 );
	const int wantDoor    = FindLinesArg( argv, "door",    0 );
	const int wantUse     = FindLinesArg( argv, "use",     0 );
	const int wantCross   = FindLinesArg( argv, "cross",   0 );
	const int wantTag     = FindLinesArg( argv, "tag",     -1 );
	const int limit       = FindLinesArg( argv, "limit",   8 );

	int found = 0, scanned = 0;
	for ( int i = 0; i < numlines && found < limit; i++ )
	{
		const line_t *ln = &lines[i];
		if ( ln->special == 0 ) continue;
		scanned++;

		if ( wantSpecial >= 0 && ln->special != wantSpecial ) continue;
		if ( wantTag >= 0 && ln->id != wantTag ) continue;
		// Door_Close, Door_Open, Door_Raise, Door_Animated. Door_LockedRaise (13) is excluded on
		// purpose: it needs a key, so it will never open for an unattended test.
		if ( wantDoor && ln->special != 10 && ln->special != 11 && ln->special != 12 &&
			 ln->special != 14 ) continue;
		if ( wantUse && !( ln->activation & ( SPAC_Use | SPAC_UseThrough | SPAC_UseBack ))) continue;
		if ( wantCross && !( ln->activation & ( SPAC_Cross | SPAC_AnyCross ))) continue;

		const fixed_t mx = ( ln->v1->x + ln->v2->x ) / 2;
		const fixed_t my = ( ln->v1->y + ln->v2->y ) / 2;

		// A spot 48 units off the FRONT side -- close enough to press use, far enough to stand.
		const double dx = FIXED2FLOAT( ln->v2->x - ln->v1->x );
		const double dy = FIXED2FLOAT( ln->v2->y - ln->v1->y );
		const double len = sqrt( dx * dx + dy * dy );
		if ( len < 1.0 ) continue;
		const double sx = FIXED2FLOAT( mx ) + ( dy / len ) * 48.0;
		const double sy = FIXED2FLOAT( my ) - ( dx / len ) * 48.0;
		// Facing from that spot back at the line.
		int face = (int)( atan2( FIXED2FLOAT( my ) - sy, FIXED2FLOAT( mx ) - sx ) * 180.0 / 3.14159265 );
		if ( face < 0 ) face += 360;

		Printf( "line %d: special %d tag %d act 0x%x  mid (%d, %d)  stand (%d, %d) face %d%s\n",
				i, ln->special, ln->id, (unsigned)ln->activation,
				(int)FIXED2FLOAT( mx ), (int)FIXED2FLOAT( my ), (int)sx, (int)sy, face,
				ln->locknumber ? "  [LOCKED]" : "" );
		found++;
	}

	Printf( "fua_find_lines: %d shown, %d specialled lines scanned of %d\n", found, scanned, numlines );
}

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

//==========================================================================
//
// fua_mesh_at <x> <y> [radius]
//
// [rc4l] Every mesh piece standing near a map position, whoever registered it.
//
// fua_line_mesh answers "what does THIS seg hold", which was enough to prove the door's own piece
// updated correctly -- and not enough to explain why the shut door kept rendering anyway. The piece
// still painting it belongs to something else, and nothing in the per-seg view can see it. This walks
// the piece list itself, so a surface that no seg admits to owning still shows up.
//
//==========================================================================

CCMD( fua_mesh_at )
{
	if ( argv.argc( ) < 3 )
	{
		Printf( "usage: fua_mesh_at <x> <y> [radius]\n" );
		return;
	}
	const float px = (float)atof( argv[1] );
	const float py = (float)atof( argv[2] );
	const float rad = ( argv.argc( ) > 3 ) ? (float)atof( argv[3] ) : 64.f;

	int nv = 0, np = 0;
	const FFlatVertex *verts = zx::levelmesh::MeshVertexData( nv );
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces( np );
	if ( verts == NULL || pieces == NULL ) { Printf( "no mesh\n" ); return; }

	int shown = 0;
	for ( int i = 0; i < np; i++ )
	{
		const zx::levelmesh::MeshPiece &p = pieces[i];
		if ( p.range.count == 0 ) continue;
		float xlo = 1e9f, xhi = -1e9f, ylo = 1e9f, yhi = -1e9f, zlo = 1e9f, zhi = -1e9f;
		bool near = false;
		for ( unsigned v = 0; v < p.range.count && p.range.offset + v < (unsigned)nv; v++ )
		{
			const FFlatVertex &fv = verts[p.range.offset + v];
			if ( fv.x < xlo ) xlo = fv.x;  if ( fv.x > xhi ) xhi = fv.x;
			if ( fv.y < ylo ) ylo = fv.y;  if ( fv.y > yhi ) yhi = fv.y;
			if ( fv.z < zlo ) zlo = fv.z;  if ( fv.z > zhi ) zhi = fv.z;
			if ( fabsf( fv.x - px ) <= rad && fabsf( fv.y - py ) <= rad ) near = true;
		}
		if ( !near ) continue;
		Printf( "piece %d: range %u+%u  x %.0f..%.0f  y %.0f..%.0f  height %.0f..%.0f  tex %d\n",
				i, p.range.offset, p.range.count, xlo, xhi, ylo, yhi, zlo, zhi,
				p.baseTex ? 1 : 0 );
		shown++;
	}
	Printf( "fua_mesh_at: %d of %d pieces within %.0f of (%.0f, %.0f)\n", shown, np, rad, px, py );
}

//==========================================================================
//
// fua_find_sky [limit=N]
//
// [rc4l] The biggest open-air sectors in the level, largest first.
//
// Checking the sky meant spawning in, turning on noclip, running forwards for a few seconds and
// hoping to end up outdoors. That is not a test -- it depends on which way the spawn faces and what
// is in front of it, and on these maps the spawn is INDOORS, so it repeatedly compared two ceilings,
// agreed to within a point, and reported the sky as fixed while it was a black disc. This finds
// somewhere the sky is actually visible and prints a spot to stand.
//
//==========================================================================

CCMD( fua_find_sky )
{
	if ( sectors == NULL || numsectors <= 0 )
	{
		Printf( "no level loaded.\n" );
		return;
	}
	const int limit = FindLinesArg( argv, "limit", 5 );

	struct Cand { int sec; double area; double cx, cy; };
	TArray<Cand> cands;
	for ( int i = 0; i < numsectors; i++ )
	{
		const sector_t *sec = &sectors[i];
		if ( sec->GetTexture( sector_t::ceiling ) != skyflatnum ) continue;
		if ( sec->linecount <= 0 ) continue;

		double minx = 1e30, maxx = -1e30, miny = 1e30, maxy = -1e30;
		for ( int k = 0; k < sec->linecount; k++ )
		{
			const line_t *ln = sec->lines[k];
			const double xs[2] = { FIXED2FLOAT( ln->v1->x ), FIXED2FLOAT( ln->v2->x ) };
			const double ys[2] = { FIXED2FLOAT( ln->v1->y ), FIXED2FLOAT( ln->v2->y ) };
			for ( int q = 0; q < 2; q++ )
			{
				if ( xs[q] < minx ) minx = xs[q];
				if ( xs[q] > maxx ) maxx = xs[q];
				if ( ys[q] < miny ) miny = ys[q];
				if ( ys[q] > maxy ) maxy = ys[q];
			}
		}
		Cand c;
		c.sec = i;
		c.area = ( maxx - minx ) * ( maxy - miny );
		c.cx = ( minx + maxx ) * 0.5;
		c.cy = ( miny + maxy ) * 0.5;
		cands.Push( c );
	}

	// Largest first: the biggest opening is the one where the sky fills the most of the view.
	for ( unsigned a = 0; a + 1 < cands.Size( ); a++ )
		for ( unsigned b = a + 1; b < cands.Size( ); b++ )
			if ( cands[b].area > cands[a].area ) { Cand t = cands[a]; cands[a] = cands[b]; cands[b] = t; }

	int shown = 0;
	for ( unsigned i = 0; i < cands.Size( ) && shown < limit; i++, shown++ )
	{
		Printf( "sky sector %d: area %.0f  stand (%d, %d)  floor %d\n",
				cands[i].sec, cands[i].area, (int)cands[i].cx, (int)cands[i].cy,
				(int)FIXED2FLOAT( sectors[cands[i].sec].floorplane.ZatPoint(
					FLOAT2FIXED( cands[i].cx ), FLOAT2FIXED( cands[i].cy ) ) ) );
	}
	Printf( "fua_find_sky: %d of %d sky sectors, %d sectors total\n",
			shown, (int)cands.Size( ), numsectors );
}
