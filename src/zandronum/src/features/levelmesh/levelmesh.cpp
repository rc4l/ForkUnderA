// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Level mesh P0 glue: walk the loaded map, collect the static budget inputs, report. No
// geometry is built and nothing renders from this yet -- see features/levelmesh/README.md.

#include "features/levelmesh/levelmesh.h"
#include "features/levelmesh/computation/flatmesh_compute.h"
#include "r_sky.h"   // skyflatnum, for fua_find_sky
#include "gl/data/gl_vertexbuffer.h"   // FFlatVertex, for fua_mesh_at
#include "features/levelmesh/wallcache.h"
#include "features/levelmesh/flatmesh.h"

#include "c_dispatch.h"   // CCMD
#include "c_console.h"    // Printf
#include "doomtype.h"
#include "r_defs.h"
#include "r_state.h"      // sides, numsides, sectors, numsectors, subsectors, numsubsectors
#include "r_utility.h"    // viewx/viewy/viewz, for fua_look
#include "p_trace.h"      // Trace, so fua_look can say what the ENGINE thinks is there
#include "d_player.h"     // players, consoleplayer
#include "tables.h"       // finesine/finecosine
#include "textures/textures.h"
#include "gl/textures/gl_material.h"   // FMaterial::GetTransparent, for fua_find_lines transparent=1

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
	// [rc4l] transparent=1 finds lines whose middle texture has partial alpha -- panes of glass,
	// grates, force fields. Those are exactly the surfaces the mesh handles differently from the
	// solid ones, and finding one meant asking someone to walk the level and take a screenshot.
	const int wantTrans   = FindLinesArg( argv, "transparent", 0 );

	int found = 0, scanned = 0;
	for ( int i = 0; i < numlines && found < limit; i++ )
	{
		const line_t *ln = &lines[i];
		// A transparent midtexture usually carries no special at all, so that filter replaces the
		// has-a-special requirement rather than adding to it.
		if ( wantTrans )
		{
			bool anyTrans = false;
			for ( int sd = 0; sd < 2 && !anyTrans; sd++ )
			{
				if ( ln->sidedef[sd] == NULL ) continue;
				FTextureID mid = ln->sidedef[sd]->GetTexture( side_t::mid );
				if ( !mid.isValid( ) ) continue;
				FMaterial *m = FMaterial::ValidateTexture( mid, false, true );
				if ( m != NULL && m->GetTransparent( ) ) anyTrans = true;
			}
			if ( !anyTrans ) continue;
		}
		else if ( ln->special == 0 ) continue;
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
	// [rc4l] How much 3D-floor geometry the level has, and how much of it the mesh could ever hold.
	//
	// A seg touching a 3D floor is marked uncacheable, and 3D floor PLANES are drawn by a per-frame,
	// per-subsector, view-dependent walk in GLFlat::ProcessSector -- so none of it is baked. Counting
	// it is the difference between "3D floors are missing" and knowing how much is missing.
	{
		int ffSectors = 0, ffPlanes = 0, ffSegs = 0;
		for ( int i = 0; i < numsectors; i++ )
		{
			if ( sectors[i].e == NULL ) continue;
			const unsigned n = sectors[i].e->XFloor.ffloors.Size( );
			if ( n == 0 ) continue;
			ffSectors++;
			ffPlanes += (int)n * 2;   // each 3D floor contributes a top and a bottom plane
		}
		for ( int i = 0; i < numsegs; i++ )
		{
			const sector_t *fs = segs[i].frontsector, *bs = segs[i].backsector;
			if (( fs != NULL && fs->e != NULL && fs->e->XFloor.ffloors.Size( ) > 0 ) ||
				( bs != NULL && bs->e != NULL && bs->e->XFloor.ffloors.Size( ) > 0 ))
				ffSegs++;
		}
		int flatOwn = 0, flat3D = 0;
		GetFlatStats( flatOwn, flat3D );
		Printf( "  3d floors: %d sectors, %d planes, %d segs touching them; "
				"flat registrations %d own / %d from 3d floors\n",
				ffSectors, ffPlanes, ffSegs, flatOwn, flat3D );
	}

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
		// [rc4l] The SHADING as well as the geometry. "The lava is dimmer than GL and does not
		// animate" are both answered by what was BAKED rather than by what the screen shows: no
		// baseTex means the animation pass skips this piece entirely, and the light level and colour
		// say whether a fullbright texture was captured as fullbright.
		Printf( "piece %d: range %u+%u  x %.0f..%.0f  y %.0f..%.0f  height %.0f..%.0f\n"
				"          light %d  rgb %.2f,%.2f,%.2f  fog %.2f mode %d  baseTex %s  face %d\n",
				i, p.range.offset, p.range.count, xlo, xhi, ylo, yhi, zlo, zhi,
				p.lightLevel, p.colorR, p.colorG, p.colorB, p.fogDensity, p.fogMode,
				p.baseTex ? ((FTexture *)p.baseTex)->Name.GetChars() : "NONE",
				p.facesDown ? 1 : 0 );
		// [rc4l] Whether the engine considers this texture fullbright or glowing, alongside what we
		// baked for it. GLDEFS `glow { flats { ... } }` sets BOTH flags, and GLFlat::Process turns
		// isFullbright() into lightlevel 255 -- so a glow flat baked at the sector's own light level
		// is a captured value that disagrees with the engine, not a shading formula that differs.
		if ( p.baseTex != NULL )
		{
			FTexture *bt = (FTexture *)p.baseTex;
			if ( bt->isFullbright() || bt->isGlowing() )
				Printf( "          ENGINE SAYS: fullbright %d glowing %d\n",
						bt->isFullbright() ? 1 : 0, bt->isGlowing() ? 1 : 0 );
		}
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

//==========================================================================
//
// fua_mesh_dupes
//
// [rc4l] Does the mesh hold the same surface twice?
//
// Z-fighting the GL renderer does not have is either a depth-precision problem or a duplicate-
// geometry one, and those want opposite fixes. The depth buffer here is D32_FLOAT against a
// 5..65536 frustum, which is better precision than GL's, so this asks the other question: how many
// pieces have byte-identical vertices to another piece. One surface drawn twice from two ranges is
// two sets of triangles the rasteriser has to break a tie between.
//
//==========================================================================

CCMD( fua_mesh_dupes )
{
	int nv = 0, np = 0;
	const FFlatVertex *verts = zx::levelmesh::MeshVertexData( nv );
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces( np );
	if ( verts == NULL || pieces == NULL ) { Printf( "no mesh\n" ); return; }

	// Key on the vertex data itself, quantised to 1/16 of a map unit so a re-bake that lands on a
	// slightly different float still counts as the same surface -- which is the case that fights.
	TMap<QWORD, int> seen;
	int dupes = 0, live = 0;
	int firstA = -1, firstB = -1;
	for ( int i = 0; i < np; i++ )
	{
		const zx::levelmesh::MeshPiece &p = pieces[i];
		if ( p.range.count == 0 ) continue;
		live++;
		QWORD h = 1469598103934665603ULL;
		for ( unsigned v = 0; v < p.range.count && p.range.offset + v < (unsigned)nv; v++ )
		{
			const FFlatVertex &fv = verts[p.range.offset + v];
			const int q[3] = { (int)(fv.x * 16.f), (int)(fv.y * 16.f), (int)(fv.z * 16.f) };
			for ( int k = 0; k < 3; k++ ) { h ^= (QWORD)q[k]; h *= 1099511628211ULL; }
		}
		int *prev = seen.CheckKey( h );
		if ( prev != NULL )
		{
			dupes++;
			if ( firstA < 0 ) { firstA = *prev; firstB = i; }
		}
		else seen.Insert( h, i );
	}
	Printf( "fua_mesh_dupes: %d of %d live pieces duplicate another piece's geometry (%.1f%%)\n",
			dupes, live, live ? 100.0 * dupes / live : 0.0 );
	if ( firstA >= 0 )
		Printf( "  first pair: pieces %d and %d, ranges %u+%u and %u+%u%s\n",
				firstA, firstB, pieces[firstA].range.offset, pieces[firstA].range.count,
				pieces[firstB].range.offset, pieces[firstB].range.count,
				pieces[firstA].material == pieces[firstB].material ? ", SAME material" :
					", different materials" );

	// [rc4l] Exact duplicates are the easy case. What actually fights is two pieces sharing a PLANE
	// and overlapping in it -- a 3D floor's side wall over the sector wall behind it, or two segs
	// that both claim the same span. Neither is a byte-identical copy, so the hash above cannot see
	// them. Bounding boxes, degenerate on the axis the surface is flat in, catch both.
	int overlaps = 0, ovA = -1, ovB = -1;
	struct Box { float x0, x1, y0, y1, z0, z1; };
	TArray<Box> box;
	TArray<int> idx;
	for ( int i = 0; i < np; i++ )
	{
		const zx::levelmesh::MeshPiece &p = pieces[i];
		if ( p.range.count == 0 ) continue;
		Box b = { 1e9f, -1e9f, 1e9f, -1e9f, 1e9f, -1e9f };
		for ( unsigned v = 0; v < p.range.count && p.range.offset + v < (unsigned)nv; v++ )
		{
			const FFlatVertex &fv = verts[p.range.offset + v];
			if ( fv.x < b.x0 ) b.x0 = fv.x;  if ( fv.x > b.x1 ) b.x1 = fv.x;
			if ( fv.y < b.y0 ) b.y0 = fv.y;  if ( fv.y > b.y1 ) b.y1 = fv.y;
			if ( fv.z < b.z0 ) b.z0 = fv.z;  if ( fv.z > b.z1 ) b.z1 = fv.z;
		}
		box.Push( b ); idx.Push( i );
	}
	const float kEps = 0.05f;
	for ( unsigned i = 0; i < box.Size( ); i++ )
		for ( unsigned j = i + 1; j < box.Size( ); j++ )
		{
			const Box &a = box[i], &b = box[j];
			// Must overlap in all three axes, allowing for the flat axis being a single value.
			if ( a.x1 < b.x0 - kEps || b.x1 < a.x0 - kEps ) continue;
			if ( a.y1 < b.y0 - kEps || b.y1 < a.y0 - kEps ) continue;
			if ( a.z1 < b.z0 - kEps || b.z1 < a.z0 - kEps ) continue;
			// And both must be flat in the SAME axis, with the same value there: that is coplanar.
			const bool ax = (a.x1 - a.x0) < kEps && (b.x1 - b.x0) < kEps;
			const bool ay = (a.y1 - a.y0) < kEps && (b.y1 - b.y0) < kEps;
			const bool az = (a.z1 - a.z0) < kEps && (b.z1 - b.z0) < kEps;
			if ( !ax && !ay && !az ) continue;
			// Zero-area overlap in the remaining axes is just two surfaces meeting at an edge.
			const float ox = MIN( a.x1, b.x1 ) - MAX( a.x0, b.x0 );
			const float oy = MIN( a.y1, b.y1 ) - MAX( a.y0, b.y0 );
			const float oz = MIN( a.z1, b.z1 ) - MAX( a.z0, b.z0 );
			int wide = 0;
			if ( ox > kEps ) wide++;
			if ( oy > kEps ) wide++;
			if ( oz > kEps ) wide++;
			if ( wide < 2 ) continue;
			overlaps++;
			if ( ovA < 0 ) { ovA = idx[i]; ovB = idx[j]; }
			// Print the first few with their actual extents. A count on its own is only as good as
			// the predicate that produced it, and this predicate is easy to get subtly wrong -- two
			// surfaces meeting at an edge are not an overlap, and a detector that says they are would
			// report most of a level.
			if ( overlaps <= 6 )
				Printf( "    #%d: piece %d [%.0f..%.0f, %.0f..%.0f, %.0f..%.0f] vs %d "
						"[%.0f..%.0f, %.0f..%.0f, %.0f..%.0f]  overlap %.1f x %.1f x %.1f  %s\n",
						overlaps, idx[i], a.x0, a.x1, a.y0, a.y1, a.z0, a.z1,
						idx[j], b.x0, b.x1, b.y0, b.y1, b.z0, b.z1, ox, oy, oz,
						pieces[idx[i]].material == pieces[idx[j]].material ? "same mat" : "diff mat" );
		}
	Printf( "  coplanar overlapping pairs: %d\n", overlaps );
	if ( ovA >= 0 )
	{
		const Box &a = box[0];
		(void)a;
		Printf( "  first overlap: pieces %d and %d, %s material, %s\n", ovA, ovB,
				pieces[ovA].material == pieces[ovB].material ? "same" : "different",
				pieces[ovA].facesDown || pieces[ovB].facesDown ? "a downward-facing surface involved"
																   : "both seen from above" );
	}
}

//==========================================================================
//
// fua_mesh_verify
//
// [rc4l] Assert what must be TRUE of the mesh, rather than compare what it looks like.
//
// Every rendering fault found on dbab02 violated a property of the baked data that is checkable
// without a camera, a GPU or a human: a texture that resolved to the null texture, a flat wound the
// wrong way for the side it is drawn from, two coplanar surfaces claiming the same space. Each was
// found by computing exactly such a property AFTER someone pointed at a screenshot. This computes
// them first, and because it needs no rendering it can be run over every map in the catalogue
// instead of the handful anyone walks through.
//
// Prints one line per violated invariant and a final PASS/FAIL that a script can grep for.
//
//==========================================================================

//==========================================================================
//
// fua_look
//
// [rc4l] What is the surface I am looking at, and what does the mesh hold for it?
//
// Written after the fourth round of "here is a screenshot of a wall that looks wrong" -> guess at a
// cause -> rebuild -> ask the user to walk back there. The question every one of those rounds was
// really asking is "what does the mesh say about THIS surface", and nothing could answer it: the
// mesh only holds what has been walked past, so a fresh instance at the spawn cannot be interrogated
// about a pane of glass somewhere across the level, and the aggregate counters cannot single one
// surface out of four thousand.
//
// So: cast the crosshair ray, find the nearest mesh triangle it hits, and print everything the mesh
// knows about that piece -- material, base texture by NAME, blend mode, alpha, light, fog, winding
// side. Also runs the ENGINE's own trace alongside, because "the engine says there is a linedef here
// and the mesh has nothing" and "both agree and the blend mode is wrong" are different bugs that
// look identical from the outside.
//
//==========================================================================

CCMD( fua_look )
{
	if ( players[consoleplayer].mo == NULL ) { Printf( "no player\n" ); return; }
	AActor *mo = players[consoleplayer].mo;

	// The direction a shot would take: the engine's own convention, so this cannot disagree with
	// where the player is actually aiming.
	const angle_t ang = mo->angle;
	const angle_t pit = (angle_t)mo->pitch;
	const float ca = FIXED2FLOAT( finecosine[pit >> ANGLETOFINESHIFT] );
	const float dx = ca * FIXED2FLOAT( finecosine[ang >> ANGLETOFINESHIFT] );
	const float dy = ca * FIXED2FLOAT( finesine[ang >> ANGLETOFINESHIFT] );
	const float dz = -FIXED2FLOAT( finesine[pit >> ANGLETOFINESHIFT] );

	const float ox = FIXED2FLOAT( viewx ), oy = FIXED2FLOAT( viewy ), oz = FIXED2FLOAT( viewz );
	Printf( "fua_look: from (%.0f, %.0f, %.0f) dir (%.2f, %.2f, %.2f)\n", ox, oy, oz, dx, dy, dz );

	// --- what the engine says is there ---------------------------------------------------------
	{
		FTraceResults res;
		if ( Trace( viewx, viewy, viewz, mo->Sector,
					FLOAT2FIXED( dx ), FLOAT2FIXED( dy ), FLOAT2FIXED( dz ),
					8192 * FRACUNIT, 0, ML_BLOCKEVERYTHING, mo, res ) )
		{
			const char *what = ( res.HitType == TRACE_HitWall ) ? "wall" :
							   ( res.HitType == TRACE_HitFloor ) ? "floor" :
							   ( res.HitType == TRACE_HitCeiling ) ? "ceiling" :
							   ( res.HitType == TRACE_HitActor ) ? "actor" : "nothing";
			Printf( "  engine: %s at (%.0f, %.0f, %.0f), %.0f away", what,
					FIXED2FLOAT( res.X ), FIXED2FLOAT( res.Y ), FIXED2FLOAT( res.Z ),
					FIXED2FLOAT( res.Distance ) );
			if ( res.Line != NULL )
				Printf( ", linedef %d side %d tier %d", (int)( res.Line - lines ), (int)res.Side,
						(int)res.Tier );
			if ( res.ffloor != NULL ) Printf( ", 3D FLOOR" );
			Printf( "\n" );
		}
		else Printf( "  engine: trace hit nothing\n" );
	}

	// --- what the mesh holds there --------------------------------------------------------------
	int nv = 0, np = 0;
	const FFlatVertex *verts = zx::levelmesh::MeshVertexData( nv );
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces( np );
	if ( verts == NULL || pieces == NULL ) { Printf( "  mesh: EMPTY\n" ); return; }

	int bestPiece = -1;
	float bestT = 1e30f;
	for ( int i = 0; i < np; i++ )
	{
		const zx::levelmesh::MeshPiece &p = pieces[i];
		if ( p.range.count < 3 ) continue;
		for ( unsigned v = 0; v + 2 < p.range.count; v += 3 )
		{
			if ( p.range.offset + v + 2 >= (unsigned)nv ) break;
			const FFlatVertex &a = verts[p.range.offset + v];
			const FFlatVertex &b = verts[p.range.offset + v + 1];
			const FFlatVertex &c = verts[p.range.offset + v + 2];
			// Moller-Trumbore. Two-sided on purpose: a surface facing away is still a surface, and
			// "the piece is there but wound the wrong way" is exactly one of the bugs this is for.
			const float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
			const float e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
			const float px = dy * e2z - dz * e2y;
			const float py = dz * e2x - dx * e2z;
			const float pz = dx * e2y - dy * e2x;
			const float det = e1x * px + e1y * py + e1z * pz;
			if ( det > -1e-6f && det < 1e-6f ) continue;
			const float inv = 1.f / det;
			const float tx = ox - a.x, ty = oy - a.y, tz = oz - a.z;
			const float u = ( tx * px + ty * py + tz * pz ) * inv;
			if ( u < 0.f || u > 1.f ) continue;
			const float qx = ty * e1z - tz * e1y;
			const float qy = tz * e1x - tx * e1z;
			const float qz = tx * e1y - ty * e1x;
			const float vv = ( dx * qx + dy * qy + dz * qz ) * inv;
			if ( vv < 0.f || u + vv > 1.f ) continue;
			const float t = ( e2x * qx + e2y * qy + e2z * qz ) * inv;
			if ( t > 0.5f && t < bestT ) { bestT = t; bestPiece = i; }
		}
	}

	if ( bestPiece < 0 )
	{
		Printf( "  mesh: NOTHING along that ray -- the surface is not baked. Walk to it, or it is "
				"refused by the wall cache (fua_wallcache_census).\n" );
		return;
	}

	const zx::levelmesh::MeshPiece &p = pieces[bestPiece];
	static const char *kBlend[4] = { "opaque/alpha-tested", "translucent", "additive", "fuzz" };
	FTexture *bt = (FTexture *)p.baseTex;
	Printf( "  mesh: piece %d at %.0f away, range %u+%u\n"
			"        blend %s, alpha %.3f, seen-from-%s\n"
			"        light %d, rgb %.2f,%.2f,%.2f, fog %.2f mode %d\n"
			"        baseTex %s%s\n",
			bestPiece, bestT, p.range.offset, p.range.count,
			kBlend[( p.blendMode >= 0 && p.blendMode < 4 ) ? p.blendMode : 0], p.alpha,
			p.facesDown ? "below" : "above",
			p.lightLevel, p.colorR, p.colorG, p.colorB, p.fogDensity, p.fogMode,
			bt ? ( bt->Name.Len( ) ? bt->Name.GetChars( ) : "(THE NULL TEXTURE)" ) : "(none)",
			bt && ( bt->isFullbright( ) || bt->isGlowing( ) ) ? "  [fullbright/glowing]" : "" );
	// [rc4l] Whether this surface has a brightmap, and whether the backend is therefore adding one.
	// "The feature is ported" and "this particular texture uses it" are different claims, and only
	// the second one can be checked by looking at a screenshot of this wall.
	if ( p.material != NULL )
	{
		FMaterial *m = (FMaterial *)p.material;
		Printf( "        brightmap: %s\n",
				( m->tex != NULL && m->tex->gl_info.Brightmap != NULL )
					? m->tex->gl_info.Brightmap->Name.GetChars( ) : "none (adds black)" );
	}
}

//==========================================================================
//
// fua_spritestyles
//
// [rc4l] Which render styles the sprites in this session actually use.
//
// A sprite whose style is not handled is drawn with ordinary alpha blending, and an additive or
// subtractive effect then paints its dark texels dark -- black holes where a plasma impact's bright
// core should be. Which styles a given mod reaches for is a question about content, not about the
// renderer, so it is counted rather than assumed.
//
//==========================================================================

CCMD( fua_spritestyles )
{
	static const char *kOp[16] = { "None", "Add", "Sub", "RevSub", "Fuzz", "FuzzOrAdd",
	                               "FuzzOrSub", "FuzzOrRevSub", "?8", "Shadow", "?10", "?11",
	                               "?12", "?13", "?14", "?15" };
	int ops[16], flags = 0;
	zx::levelmesh::GetSpriteStyleStats( ops, flags );
	Printf( "sprite render styles since load:\n" );
	for ( int i = 0; i < 16; i++ )
		if ( ops[i] > 0 ) Printf( "  %-12s %d\n", kOp[i], ops[i] );
	Printf( "  style flags seen: 0x%x\n", flags );
}

// [rc4l] Every sprite piece registered on the current frame.
//
// A sprite is not always one quad -- GLSprite::SplitSprite cuts it wherever a 3D floor's light band
// begins and gives each piece that band's light and colormap. When two pieces of one explosion come
// out visibly different in the backend and identical in GL, this says which input diverged instead
// of leaving it to be inferred from a screenshot.
//
// Pause first: the list is rebuilt every frame and an explosion lasts about a quarter of a second.
CCMD( fua_sprites )
{
	zx::levelmesh::DumpSpriteNotes();
}

CCMD( fua_mesh_verify )
{
	int nv = 0, np = 0;
	const FFlatVertex *verts = zx::levelmesh::MeshVertexData( nv );
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces( np );
	if ( verts == NULL || pieces == NULL || np == 0 )
	{
		Printf( "fua_mesh_verify: FAIL (no mesh -- set gl_wallmesh 1 and bake first)\n" );
		return;
	}

	int failures = 0, live = 0;

	// --- 1. Every piece must name a base texture ------------------------------------------------
	//
	// A null baseTex is not merely "does not animate": the pointer is non-null and points at the
	// engine's null texture, whose id translates to itself, so the animation pass re-resolves it
	// every frame to no change and the surface silently freezes. It printed as "baseTex yes" for a
	// week. The NAME is what makes it visible, so the name is what gets asserted.
	int noBase = 0, nullBase = 0;
	for ( int i = 0; i < np; i++ )
	{
		if ( pieces[i].range.count == 0 ) continue;
		live++;
		FTexture *bt = (FTexture *)pieces[i].baseTex;
		if ( bt == NULL ) { noBase++; continue; }
		if ( bt->Name.Len( ) == 0 ) nullBase++;
	}
	if ( nullBase > 0 )
	{
		Printf( "  FAIL base-texture: %d of %d pieces resolve to the NULL texture "
				"(they will render once and never animate)\n", nullBase, live );
		failures++;
	}

	// --- 2. Flats must wind consistently for the side they are viewed from ----------------------
	//
	// Back-face culling deletes a flat wound the wrong way, silently and completely -- every ceiling
	// in the level, or every 3D floor top. The convention itself does not matter here, only that it
	// is applied consistently: all pieces seen from above must wind one way and all pieces seen from
	// below the other. A mixture is the bug, whichever way round the convention happens to be.
	int upPos = 0, upNeg = 0, downPos = 0, downNeg = 0;
	for ( int i = 0; i < np; i++ )
	{
		const zx::levelmesh::MeshPiece &p = pieces[i];
		if ( p.range.count < 3 ) continue;
		// Only near-horizontal surfaces: a wall's winding says nothing about up and down.
		if ( p.normY < 0.9f && p.normY > -0.9f ) continue;
		const FFlatVertex &a = verts[p.range.offset];
		const FFlatVertex &b = verts[p.range.offset + 1];
		const FFlatVertex &c = verts[p.range.offset + 2];
		// z component of (b-a) x (c-a): the winding's own idea of which way the triangle faces.
		const float nz = zx::levelmesh::ComputeTriangleWindingZ( a.x, a.y, b.x, b.y, c.x, c.y );
		if ( fabsf( nz ) < 0.0001f ) continue;   // degenerate, e.g. a squashed piece
		if ( p.facesDown ) { if ( nz > 0 ) downPos++; else downNeg++; }
		else               { if ( nz > 0 ) upPos++;   else upNeg++; }
	}
	const int upMix = MIN( upPos, upNeg ), downMix = MIN( downPos, downNeg );
	if ( upMix > 0 || downMix > 0 )
	{
		Printf( "  FAIL winding: seen-from-above %d/%d split, seen-from-below %d/%d split "
				"(a consistent convention would be 0 on one side of each)\n",
				upPos, upNeg, downPos, downNeg );
		failures++;
	}
	// And the two groups must be OPPOSITE, or culling keeps one and drops the other.
	const bool upIsPos = upPos >= upNeg, downIsPos = downPos >= downNeg;
	if ( ( upPos + upNeg ) > 0 && ( downPos + downNeg ) > 0 && upIsPos == downIsPos )
	{
		Printf( "  FAIL winding: surfaces seen from above and from below wind the SAME way, "
				"so back-face culling cannot keep both\n" );
		failures++;
	}

	// --- 3. No two coplanar pieces may claim the same area --------------------------------------
	//
	// Coplanar quads built from different vertices do not agree on depth to the last bit, so the
	// rasteriser stipples between them. This is how the mesh holding both sides of every two-sided
	// line was found; the count was 1799 pairs on dbab02 before back-face culling.
	int overlaps = 0;
	{
		typedef zx::levelmesh::MeshBox Box;
		TArray<Box> box;
		for ( int i = 0; i < np; i++ )
		{
			const zx::levelmesh::MeshPiece &p = pieces[i];
			if ( p.range.count == 0 ) continue;
			Box b = { 1e9f, -1e9f, 1e9f, -1e9f, 1e9f, -1e9f };
			for ( unsigned v = 0; v < p.range.count && p.range.offset + v < (unsigned)nv; v++ )
			{
				const FFlatVertex &fv = verts[p.range.offset + v];
				if ( fv.x < b.x0 ) b.x0 = fv.x;  if ( fv.x > b.x1 ) b.x1 = fv.x;
				if ( fv.y < b.y0 ) b.y0 = fv.y;  if ( fv.y > b.y1 ) b.y1 = fv.y;
				if ( fv.z < b.z0 ) b.z0 = fv.z;  if ( fv.z > b.z1 ) b.z1 = fv.z;
			}
			box.Push( b );
		}
		const float kEps = 0.05f;
		for ( unsigned i = 0; i < box.Size( ); i++ )
			for ( unsigned j = i + 1; j < box.Size( ); j++ )
			{
				if ( zx::levelmesh::ComputeCoplanarOverlap( box[i], box[j], kEps ) ) overlaps++;
			}
	}

	// --- 4. Blend mode and alpha must agree -----------------------------------------------------
	//
	// A piece marked opaque while carrying alpha < 1 renders solid; the reverse renders a solid
	// surface through the sorted translucent pass for nothing. Both were live: flats were baked
	// unconditionally opaque while a translucent grate hung over a lava pit.
	int blendMismatch = 0;
	for ( int i = 0; i < np; i++ )
	{
		const zx::levelmesh::MeshPiece &p = pieces[i];
		if ( p.range.count == 0 ) continue;
		const bool seeThrough = p.alpha < 1.f - 1.f / 256.f;
		if ( seeThrough && p.blendMode == 0 ) blendMismatch++;
	}
	if ( blendMismatch > 0 )
	{
		Printf( "  FAIL blend: %d pieces carry alpha < 1 but are marked opaque\n", blendMismatch );
		failures++;
	}

	Printf( "fua_mesh_verify: %s  (%d live pieces, %d without a base texture, "
			"%d coplanar overlapping pairs)\n",
			failures ? "FAIL" : "PASS", live, noBase, overlaps );
}

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
