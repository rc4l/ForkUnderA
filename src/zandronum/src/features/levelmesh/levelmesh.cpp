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
#include "p_lnspec.h"    // Line_Mirror, for fua_make_mirror
#include "gl/dynlights/gl_dynlight.h"   // ADynamicLight, for fua_light
#include "a_sharedglobal.h"                // DBaseDecal, for fua_decals
#include "a_sharedglobal.h"                // DBaseDecal, for fua_decals
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

//==========================================================================
//
// fua_line / fua_lines_at
//
// [rc4l] Everything about a linedef, and which linedefs are near a point.
//
// Written after an argument about a wall that took bullets and refused plasma. The crosshair
// reported one linedef and the exploding missile reported another, and there was no way from a
// running game to ask what either of them WAS -- whether a line blocks projectiles but not
// hitscans, which of its tiers carry a texture, or what span of height each tier actually covers.
// All three decide whether a mark can exist, and all three were being guessed at.
//
// fua_lines_at is the one that settles "these are two different lines in the same place", which
// is invisible from a screenshot and obvious the moment both are printed side by side.
//
//==========================================================================

static void PrintOneLine( int idx )
{
	const line_t *ln = &lines[idx];
	const sector_t *fs = ln->frontsector, *bs = ln->backsector;

	Printf( "line %d: (%.0f, %.0f) -> (%.0f, %.0f)  special %d tag %d\n", idx,
		FIXED2FLOAT( ln->v1->x ), FIXED2FLOAT( ln->v1->y ),
		FIXED2FLOAT( ln->v2->x ), FIXED2FLOAT( ln->v2->y ), ln->special, ln->args[0] );

	// [rc4l] The flags that decide what gets through. BLOCKPROJECTILE is the one that makes a line
	// stop a rocket and pass a bullet, which looks exactly like a wall refusing one weapon.
	FString f;
	if ( ln->flags & ML_BLOCKING )        f += "BLOCKING ";
	if ( ln->flags & ML_BLOCKEVERYTHING ) f += "BLOCKEVERYTHING ";
	if ( ln->flags & ML_BLOCKPROJECTILE ) f += "BLOCKPROJECTILE ";
	if ( ln->flags & ML_BLOCK_PLAYERS )   f += "BLOCK_PLAYERS ";
	if ( ln->flags & ML_TWOSIDED )        f += "TWOSIDED ";
	if ( ln->flags & ML_RAILING )         f += "RAILING ";
	if ( ln->flags & ML_3DMIDTEX )        f += "3DMIDTEX ";
	Printf( "  flags 0x%08x  %s\n", (unsigned)ln->flags, f.Len( ) ? f.GetChars( ) : "(none)" );

	if ( fs != NULL )
		Printf( "  front sector %d: floor %.0f ceiling %.0f\n", (int)( fs - sectors ),
			FIXED2FLOAT( fs->GetPlaneTexZ( sector_t::floor ) ),
			FIXED2FLOAT( fs->GetPlaneTexZ( sector_t::ceiling ) ) );
	if ( bs != NULL )
		Printf( "  back  sector %d: floor %.0f ceiling %.0f\n", (int)( bs - sectors ),
			FIXED2FLOAT( bs->GetPlaneTexZ( sector_t::floor ) ),
			FIXED2FLOAT( bs->GetPlaneTexZ( sector_t::ceiling ) ) );
	else
		Printf( "  back  sector: NONE (one-sided)\n" );

	// [rc4l] What each tier covers IN WORLD HEIGHT, not just whether a texture is named.
	//
	// A decal is glued to a tier and stored relative to that tier's texture, so a height with no
	// tier over it is a height where no mark can exist. Printing the spans says immediately whether
	// a given impact height had anywhere to go.
	for ( int sd = 0; sd < 2; sd++ )
	{
		const side_t *side = ln->sidedef[sd];
		if ( side == NULL ) continue;
		const char *names[3] = { "top", "mid", "bottom" };
		const int tiers[3] = { side_t::top, side_t::mid, side_t::bottom };
		Printf( "  side %d (sector %d):\n", sd, (int)( side->sector - sectors ) );
		for ( int t = 0; t < 3; t++ )
		{
			FTextureID tid = side->GetTexture( tiers[t] );
			FTexture *tx = tid.isValid( ) ? TexMan[tid] : NULL;
			Printf( "    %-6s %-10s", names[t], tx ? tx->Name.GetChars( ) : "-" );
			if ( fs != NULL && bs != NULL )
			{
				const float ff = FIXED2FLOAT( fs->GetPlaneTexZ( sector_t::floor ) );
				const float fc = FIXED2FLOAT( fs->GetPlaneTexZ( sector_t::ceiling ) );
				const float bf = FIXED2FLOAT( bs->GetPlaneTexZ( sector_t::floor ) );
				const float bc = FIXED2FLOAT( bs->GetPlaneTexZ( sector_t::ceiling ) );
				if ( t == 0 )      Printf( "  covers %.0f..%.0f", bc < fc ? bc : fc, bc < fc ? fc : bc );
				else if ( t == 2 ) Printf( "  covers %.0f..%.0f", ff < bf ? ff : bf, ff < bf ? bf : ff );
				else               Printf( "  open span %.0f..%.0f", ff > bf ? ff : bf, fc < bc ? fc : bc );
			}
			Printf( "\n" );
		}
	}
}

CCMD( fua_line )
{
	if ( lines == NULL || numlines <= 0 ) { Printf( "no level loaded.\n" ); return; }
	if ( argv.argc( ) < 2 ) { Printf( "usage: fua_line <index>\n" ); return; }
	const int idx = atoi( argv[1] );
	if ( idx < 0 || idx >= numlines ) { Printf( "line %d out of range (0..%d)\n", idx, numlines - 1 ); return; }
	PrintOneLine( idx );
}

CCMD( fua_lines_at )
{
	if ( lines == NULL || numlines <= 0 ) { Printf( "no level loaded.\n" ); return; }
	if ( argv.argc( ) < 3 ) { Printf( "usage: fua_lines_at <x> <y> [radius]\n" ); return; }
	const float px = (float)atof( argv[1] ), py = (float)atof( argv[2] );
	const float rad = ( argv.argc( ) > 3 ) ? (float)atof( argv[3] ) : 32.f;

	int shown = 0;
	for ( int i = 0; i < numlines; i++ )
	{
		const line_t *ln = &lines[i];
		// distance from the point to the SEGMENT, so a long line counts only where it passes near
		const float x1 = FIXED2FLOAT( ln->v1->x ), y1 = FIXED2FLOAT( ln->v1->y );
		const float x2 = FIXED2FLOAT( ln->v2->x ), y2 = FIXED2FLOAT( ln->v2->y );
		const float dx = x2 - x1, dy = y2 - y1;
		const float len2 = dx * dx + dy * dy;
		float t = ( len2 > 0.0001f ) ? ( ( px - x1 ) * dx + ( py - y1 ) * dy ) / len2 : 0.f;
		if ( t < 0.f ) t = 0.f; else if ( t > 1.f ) t = 1.f;
		const float qx = x1 + t * dx - px, qy = y1 + t * dy - py;
		if ( qx * qx + qy * qy > rad * rad ) continue;
		PrintOneLine( i );
		shown++;
	}
	Printf( "fua_lines_at: %d line(s) within %.0f of (%.0f, %.0f)\n", shown, rad, px, py );
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
	int dupes = 0, live = 0, squashed = 0;
	int firstA = -1, firstB = -1;
	for ( int i = 0; i < np; i++ )
	{
		const zx::levelmesh::MeshPiece &p = pieces[i];
		if ( p.range.count == 0 ) continue;
		// [rc4l] A SQUASHED piece is not a duplicate, however identical it looks.
		//
		// BakeSeg collapses a piece a seg no longer produces by rewriting its vertices to all zeros --
		// it cannot free the range, so it makes it rasterise nothing. Every squashed piece is therefore
		// byte-identical to every other, and hashing the vertex data counts them all as duplicates of
		// each other. They draw nothing and fight nothing; counting them made this number useless and
		// sent a hunt for duplicate GEOMETRY after what is really just retired space.
		{
			bool degenerate = true;
			for ( unsigned v = 0; v < p.range.count && p.range.offset + v < (unsigned)nv; v++ )
			{
				const FFlatVertex &fv = verts[p.range.offset + v];
				if ( fv.x != 0.f || fv.y != 0.f || fv.z != 0.f ) { degenerate = false; break; }
			}
			if ( degenerate ) { squashed++; continue; }
		}
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
	Printf( "fua_mesh_dupes: %d of %d live pieces duplicate another piece's geometry (%.1f%%), %d squashed\n",
			dupes, live, live ? 100.0 * dupes / live : 0.0, squashed );
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

//==========================================================================
//
// fua_make_mirror
//
// [rc4l] Turn the wall under the crosshair into a mirror.
//
// The traced reflection path had no way to be exercised: a mirror is linedef special 182 and no
// stock Doom map contains one, so testing it meant authoring a wad. A renderer feature that cannot
// be reached from a running game does not get tested, and this one was not -- its texture lookup
// had been left sampling white with nobody able to look at it.
//
// Single-sided walls only, which is what GLWall::Process requires: RENDERWALL_M1S is the only type
// it promotes to RENDERWALL_MIRROR. Pointing at a window and getting nothing would otherwise read
// as the mirror code failing rather than as the wall being the wrong kind.
//
//==========================================================================

EXTERN_CVAR( Bool, gl_mirrors )

CCMD( fua_make_mirror )
{
	if ( players[consoleplayer].mo == NULL ) { Printf( "no player\n" ); return; }
	AActor *mo = players[consoleplayer].mo;

	const angle_t ang = mo->angle;
	const angle_t pit = (angle_t)mo->pitch;
	const float ca = FIXED2FLOAT( finecosine[pit >> ANGLETOFINESHIFT] );
	const float dx = ca * FIXED2FLOAT( finecosine[ang >> ANGLETOFINESHIFT] );
	const float dy = ca * FIXED2FLOAT( finesine[ang >> ANGLETOFINESHIFT] );
	const float dz = -FIXED2FLOAT( finesine[pit >> ANGLETOFINESHIFT] );

	FTraceResults res;
	if ( !Trace( viewx, viewy, viewz, mo->Sector,
				FLOAT2FIXED( dx ), FLOAT2FIXED( dy ), FLOAT2FIXED( dz ),
				8192 * FRACUNIT, 0, ML_BLOCKEVERYTHING, mo, res )
		|| res.HitType != TRACE_HitWall || res.Line == NULL )
	{
		Printf( "fua_make_mirror: not looking at a wall\n" );
		return;
	}

	line_t *ln = res.Line;
	const int idx = (int)( ln - lines );
	if ( ln->sidedef[1] != NULL )
	{
		Printf( "fua_make_mirror: linedef %d is two-sided -- only single-sided walls mirror\n", idx );
		return;
	}

	ln->special = Line_Mirror;
	// [rc4l] The wall cache has to be told, because a linedef special is not part of its stamp.
	//
	// A cached seg is replayed without running GLWall::Process again, so the wall keeps the type it
	// was captured with and never becomes RENDERWALL_MIRROR -- and its baked geometry keeps sitting
	// in front of the mirror surface, which is why the reflection appeared in GL and not in the
	// backend. No map needs this: a real mirror carries its special at load, before anything is
	// captured. Only changing one at runtime does, which is this command.
	zx::levelmesh::InvalidateAll( );
	if ( !gl_mirrors ) gl_mirrors = true;
	Printf( "fua_make_mirror: linedef %d is now a mirror (gl_mirrors %d)\n"
			"  GL reflects in it immediately; run fua_dg_mirrors for the Vulkan backend to see it\n",
			idx, gl_mirrors ? 1 : 0 );
}
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
	// The camera itself is NOT printed here. It belongs to the bridge, not to a mesh diagnostic:
	// see the player.camera RPC and `fuactl here`, which answer it in the units setpos takes and to
	// enough decimals to replay onto the same texel.

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
			// [rc4l] The normal is what the dynamic-light SIDE TEST reads, and it is invisible in a
			// screenshot: a surface facing the wrong way is simply never lit, which looks exactly like
			// a light that is out of range. Mesh space is (x, z-up, y), so the middle component is the
			// vertical one -- +1 for a surface seen from above, -1 from below.
			"        normal (%.2f, %.2f, %.2f)%s\n"
			"        baseTex %s%s\n",
			bestPiece, bestT, p.range.offset, p.range.count,
			kBlend[( p.blendMode >= 0 && p.blendMode < 4 ) ? p.blendMode : 0], p.alpha,
			p.facesDown ? "below" : "above",
			p.lightLevel, p.colorR, p.colorG, p.colorB, p.fogDensity, p.fogMode,
			p.normX, p.normY, p.normZ,
			// A surface whose normal disagrees with the side it is SEEN from can never take a dynamic
			// light, because every light in the room is behind it.
			( ( p.facesDown && p.normY > 0.01f ) || ( !p.facesDown && p.normY < -0.01f ) )
				? "   <-- points AWAY from the side it is seen from" : "",
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
		// [rc4l] Box index is NOT piece index: empty pieces are skipped. Keep the mapping, or every
		// detail line below describes a different piece than the one that overlapped.
		TArray<int> pieceOf;
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
			box.Push( b ); pieceOf.Push( i );
		}
		const float kEps = 0.05f;
		// [rc4l] Say WHICH pairs, not just how many.
		//
		// A count cannot be acted on: two coplanar pieces are a real fight if they are the same surface
		// twice and harmless if they are neighbours whose bounding boxes merely touch, and the count
		// does not say which. What distinguishes them is printed instead -- the material, the side each
		// is seen from, and above all whether they carry the SAME dynamic light. Two copies shaded
		// identically are invisible; it is the pair that disagrees about its light that appears the
		// moment a dynamic light reaches the surface, which is exactly when the stripes were reported.
		int shown = 0;
		for ( unsigned i = 0; i < box.Size( ); i++ )
			for ( unsigned j = i + 1; j < box.Size( ); j++ )
			{
				if ( !zx::levelmesh::ComputeCoplanarOverlap( box[i], box[j], kEps ) ) continue;
				overlaps++;
				if ( shown < 8 )
				{
					const zx::levelmesh::MeshPiece &pa = pieces[pieceOf[i]];
					const zx::levelmesh::MeshPiece &pb = pieces[pieceOf[j]];
					Printf( "  overlap %d: pieces %u/%u  %s material  light %d/%d  facesDown %d/%d  verts %u/%u  at y %.1f/%.1f  x %.0f..%.0f  z %.0f..%.0f\n",
						shown, pieceOf[i], pieceOf[j], pa.material == pb.material ? "same" : "different",
						pa.dynLightIndex, pb.dynLightIndex, (int)pa.facesDown, (int)pb.facesDown,
						pa.range.count, pb.range.count,
							box[i].y0, box[j].y0, box[i].x0, box[i].x1, box[i].z0, box[i].z1 );
					shown++;
				}
			}
	}

	// --- 4. A world surface must carry a normal -------------------------------------------------
	//
	// [rc4l] A zero normal is not "no data", it is a MESSAGE, and only a sprite is entitled to send
	// it. The backend reads it as "the CPU has already done this surface's dynamic lighting, take
	// none from the light loop" -- correct for a billboard, which has no side for a light to be in
	// front of, and catastrophic for a wall or a floor, which then takes NO dynamic light at all.
	//
	// It is worth a check of its own because of how it fails: the surface still has its texture and
	// its sector light, so it looks completely normal until a dynamic light arrives, and then it is
	// the one patch of floor the plasma does not reach -- a hard straight edge along the piece's own
	// boundary, in Vulkan only, which is precisely what a side test being wrong looks like. Hunting
	// that difference through screenshots cost hours; the mesh can simply say so.
	int zeroNorm = 0, zeroSprite = 0;
	for ( int i = 0; i < np; i++ )
	{
		const zx::levelmesh::MeshPiece &p = pieces[i];
		if ( p.range.count == 0 ) continue;
		const float len2 = p.normX * p.normX + p.normY * p.normY + p.normZ * p.normZ;
		if ( len2 > 0.0001f ) continue;
		// A sprite is the one piece allowed to have none: it is a billboard and has no fixed side.
		if ( p.blendMode != 0 || p.translation != 0 ) { zeroSprite++; continue; }
		if ( zeroNorm < 6 )
			Printf( "  no-normal piece %d: %u verts, facesDown %d, depthBias %d, light %d\n",
				i, p.range.count, (int)p.facesDown, (int)p.depthBias, p.dynLightIndex );
		zeroNorm++;
	}
	if ( zeroNorm > 0 )
	{
		Printf( "  FAIL normals: %d of %d pieces carry no normal, so they take no dynamic light\n",
			zeroNorm, live );
		failures++;
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

//==========================================================================
//
// fua_find_3dfloors
//
// [rc4l] Where in this level is there a 3D floor to stand on?
//
// Searching linedefs for Sector_Set3DFloor does not work: P_Spawn3DFloors consumes the special at
// level load and clears it, so a map full of them reports none. The 3D floors themselves live on
// the sector, so this asks the sector.
//
// Prints a spot to stand ON each one, because that is what a test needs -- a floor decal is placed
// at the actor's floorz, and the whole question about 3D floors is whether floorz lands on the
// surface you are standing on or on the real sector floor below it.
//
//==========================================================================

//==========================================================================
//
// fua_move_sector
//
// [rc4l] Move a sector plane, so behaviour that only shows on MOVING geometry can be tested.
//
// Lifts and doors are the only things that move a floor or ceiling, and a map that happens to
// contain neither cannot exercise any of it -- dbab02 has 1652 linedefs and two specials, and
// doom2 MAP01 has no lift at all. So there was no way to check that a decal rides a floor, that
// the wall cache notices a plane changing, or that a 3D floor updates, short of finding a map
// with the right special in the right place and hoping.
//
// Moves it directly rather than triggering a special: a special needs a tag, an activation type
// and a player standing in the right spot, none of which a test wants. This is the state a lift
// would put the sector in, arrived at in one step.
//
//==========================================================================

//==========================================================================
//
// fua_light / fua_light_clear
//
// [rc4l] A dynamic light that HOLDS STILL, so two renderers can be compared on one.
//
// Every light a map hands you is attached to something that moves or expires: a plasma ball flies,
// a rocket explodes, a muzzle flash lasts two tics. Comparing GL against Vulkan on one of those
// means comparing two instances that fired at slightly different moments, from slightly different
// places, with the light somewhere else in each -- and every difference in the picture is then
// arguably just that. Chasing a reported hard edge in the Vulkan dynamic lighting stalled on
// exactly this: the pair never showed the same light twice.
//
// So: spawn a plain ADynamicLight at a stated point and leave it there. Same position, same radius,
// same colour, in both instances, for as many frames as the test needs.
//
// usage: fua_light <radius> [r g b] [dz]      -- at the player, dz above the floor (default 16)
//        fua_light <radius> <r> <g> <b> <x> <y> <z>
//        fua_light_clear
//
//==========================================================================

//==========================================================================
//
// fua_decals
//
// [rc4l] Every decal the ENGINE is holding, which is not the same as what either renderer draws.
//
// Both renderers filter: GL skips a decal flagged invisible, the mesh path skips it too and also
// reads its alpha off the engine's own object. So counting what a renderer registered answers
// "what got drawn", and the question that keeps coming up is the other one -- what is still THERE.
// A mark that should have faded and been destroyed, but has not been, is invisible to every
// instrument that starts from the draw.
//
// Walks the sidedefs, because that is where a glued decal lives: side_t::AttachedDecals, threaded
// on DBaseDecal::WallNext.
//
//==========================================================================

CCMD( fua_decals )
{
	if ( sides == NULL || numsides <= 0 ) { Printf( "no level loaded.\n" ); return; }

	int total = 0, invisible = 0, additive = 0, zeroAlpha = 0;
	int shown = 0;
	for ( int i = 0; i < numsides; i++ )
	{
		for ( DBaseDecal *d = sides[i].AttachedDecals; d != NULL; d = d->WallNext )
		{
			total++;
			const bool inv = !!( d->RenderFlags & RF_INVISIBLE );
			const bool add = ( d->RenderStyle.BlendOp == STYLEOP_Add &&
			                   d->RenderStyle.DestAlpha == STYLEALPHA_One );
			if ( inv ) invisible++;
			if ( add ) additive++;
			if ( d->Alpha <= 0 ) zeroAlpha++;
			if ( shown < 8 && add )
			{
				FTexture *t = TexMan[d->PicNum];
				Printf( "  additive decal on side %d: %s alpha %.3f%s\n", i,
					( t != NULL && t->Name != NULL ) ? t->Name : "?",
					FIXED2FLOAT( d->Alpha ), inv ? " INVISIBLE" : "" );
				shown++;
			}
		}
	}
	Printf( "fua_decals: %d attached to sides -- %d additive, %d invisible, %d at zero alpha\n",
		total, additive, invisible, zeroAlpha );
}

//==========================================================================
//
// fua_lightnodes
//
// [rc4l] What is LINKED to the surfaces, which is not the same as what is alive.
//
// GL lights a wall by walking side_t::lighthead, a list of nodes each pointing at a light. The
// light itself is a thinker and dies on its own schedule; the node is unlinked separately, by
// ADynamicLight::UnlinkLight and by LinkLight's mark-and-sweep. Those two can disagree, and when
// they do the symptom is a surface still being lit by something that no longer exists -- while
// every instrument that starts from the thinker list reports nothing at all, because there IS
// nothing at all. fua_dg_lights says "0 active" and the wall stays blue.
//
// So this walks the lists instead, and says which nodes have no light behind them.
//
//==========================================================================

CCMD( fua_lightnodes )
{
	if ( sides == NULL || numsides <= 0 ) { Printf( "no level loaded.\n" ); return; }

	int sideNodes = 0, sideDead = 0, sideDormant = 0, subNodes = 0, subDead = 0, shown = 0;
	for ( int i = 0; i < numsides; i++ )
	{
		for ( FLightNode *n = sides[i].lighthead; n != NULL; n = n->nextLight )
		{
			sideNodes++;
			// [rc4l] ORPHANED is the fault; dormant and zero-radius are not.
			//
			// A dormant light keeps its nodes on purpose -- Deactivate does not unlink, and both
			// draw paths skip it explicitly (gl_flats.cpp and gl_walls_draw.cpp both test
			// MF2_DORMANT). Counting those as broken reports 64 leaked nodes on a perfectly healthy
			// level and sends the next reader after the wrong thing, which it duly did.
			//
			// A node with no light behind it is different: nothing skips it, both paths dereference
			// lightsource without checking, so it is a crash waiting rather than a tint.
			const bool dead = ( n->lightsource == NULL );
			if ( n->lightsource != NULL && !n->lightsource->IsActive( ) ) sideDormant++;
			if ( !dead ) continue;
			sideDead++;
			if ( shown < 8 )
			{
				Printf( "  side %d: node with NO light behind it\n", i );
				shown++;
			}
		}
	}
	for ( int i = 0; i < numsubsectors; i++ )
	{
		for ( FLightNode *n = subsectors[i].lighthead; n != NULL; n = n->nextLight )
		{
			subNodes++;
			if ( n->lightsource == NULL ) subDead++;
		}
	}
	Printf( "fua_lightnodes: sides %d linked (%d orphaned, %d dormant), subsectors %d linked (%d orphaned)\n",
		sideNodes, sideDead, sideDormant, subNodes, subDead );
}

//==========================================================================
//
// fua_decals
//
// [rc4l] Every decal the ENGINE is holding, which is not the same as what either renderer draws.
//
// Both renderers filter: GL skips a decal flagged invisible, the mesh path skips it too and also
// reads its alpha off the engine's own object. So counting what a renderer registered answers
// "what got drawn", and the question that keeps coming up is the other one -- what is still THERE.
// A mark that should have faded and been destroyed, but has not been, is invisible to every
// instrument that starts from the draw.
//
// Walks the sidedefs, because that is where a glued decal lives: side_t::AttachedDecals, threaded
// on DBaseDecal::WallNext.
//
CCMD( fua_light )
{
	if ( sectors == NULL || numsectors <= 0 ) { Printf( "no level loaded.\n" ); return; }
	if ( argv.argc( ) < 2 )
	{
		Printf( "usage: fua_light <radius> [r g b] [dz]  |  fua_light <radius> <r> <g> <b> <x> <y> <z>\n" );
		return;
	}

	const int radius = atoi( argv[1] );
	const int r = ( argv.argc( ) > 4 ) ? atoi( argv[2] ) : 255;
	const int g = ( argv.argc( ) > 4 ) ? atoi( argv[3] ) : 255;
	const int b = ( argv.argc( ) > 4 ) ? atoi( argv[4] ) : 255;

	fixed_t x, y, z;
	if ( argv.argc( ) >= 8 )
	{
		x = FLOAT2FIXED( (float)atof( argv[5] ) );
		y = FLOAT2FIXED( (float)atof( argv[6] ) );
		z = FLOAT2FIXED( (float)atof( argv[7] ) );
	}
	else
	{
		AActor *pmo = players[consoleplayer].mo;
		if ( pmo == NULL ) { Printf( "no player to place the light at.\n" ); return; }
		const float dz = ( argv.argc( ) == 6 ) ? (float)atof( argv[5] ) : 16.f;
		x = pmo->x;
		y = pmo->y;
		z = pmo->Sector->floorplane.ZatPoint( x, y ) + FLOAT2FIXED( dz );
	}

	ADynamicLight *lt = Spawn<ADynamicLight>( x, y, z, NO_REPLACE );
	if ( lt == NULL ) { Printf( "could not spawn the light.\n" ); return; }
	// [rc4l] BeginPlay has already run by the time Spawn returns and it reads args, so the intensity
	// is written to m_intensity as well as to args. Tick copies m_intensity[0] into the current
	// intensity and GetRadius doubles it, which is why the radius asked for is halved going in.
	lt->args[LIGHT_RED]   = clamp<int>( r, 0, 255 );
	lt->args[LIGHT_GREEN] = clamp<int>( g, 0, 255 );
	lt->args[LIGHT_BLUE]  = clamp<int>( b, 0, 255 );
	lt->args[LIGHT_INTENSITY] = radius / 2;
	lt->args[LIGHT_SECONDARY_INTENSITY] = radius / 2;
	lt->m_intensity[0] = radius / 2;
	lt->m_intensity[1] = radius / 2;
	lt->lighttype = PointLight;   // steady, with no cycler to make it breathe
	lt->Activate( NULL );
	lt->UpdateLocation( );
	Printf( "fua_light: radius %d rgb %d,%d,%d at (%.0f, %.0f, %.0f)\n", radius,
		lt->args[LIGHT_RED], lt->args[LIGHT_GREEN], lt->args[LIGHT_BLUE],
		FIXED2FLOAT( x ), FIXED2FLOAT( y ), FIXED2FLOAT( z ) );
}

CCMD( fua_light_clear )
{
	TThinkerIterator<ADynamicLight> it( STAT_DLIGHT );
	ADynamicLight *lt;
	TArray<ADynamicLight *> doomed;
	while ( ( lt = it.Next( ) ) != NULL )
	{
		// Only the ones standing on their own: a light OWNED by an actor belongs to that actor, and
		// taking it out from under its owner is not this command's business.
		if ( !lt->IsOwned( ) ) doomed.Push( lt );
	}
	for ( unsigned k = 0; k < doomed.Size( ); k++ ) doomed[k]->Destroy( );
	Printf( "fua_light_clear: removed %d free-standing light(s)\n", doomed.Size( ) );
}

CCMD( fua_move_sector )
{
	if ( sectors == NULL || numsectors <= 0 ) { Printf( "no level loaded.\n" ); return; }
	if ( argv.argc( ) < 4 )
	{
		Printf( "usage: fua_move_sector <index> <floor|ceiling> <delta>\n" );
		return;
	}
	const int idx = atoi( argv[1] );
	if ( idx < 0 || idx >= numsectors ) { Printf( "sector %d out of range (0..%d)\n", idx, numsectors - 1 ); return; }
	const bool ceiling = ( argv[2][0] == 'c' || argv[2][0] == 'C' );
	const fixed_t delta = FLOAT2FIXED( (float)atof( argv[3] ) );

	sector_t *sec = &sectors[idx];
	const int part = ceiling ? (int)sector_t::ceiling : (int)sector_t::floor;
	const fixed_t before = sec->GetPlaneTexZ( part );
	// [rc4l] Both the plane and its texture Z, which is what every reader actually consults --
	// moving one without the other leaves the surface drawn at the old height.
	sec->SetPlaneTexZ( part, before + delta );
	if ( ceiling ) sec->ceilingplane.ChangeHeight( delta );
	else           sec->floorplane.ChangeHeight( delta );
	Printf( "fua_move_sector: sector %d %s %.0f -> %.0f\n", idx, ceiling ? "ceiling" : "floor",
		FIXED2FLOAT( before ), FIXED2FLOAT( sec->GetPlaneTexZ( part ) ) );
}

CCMD( fua_find_3dfloors )
{
	if ( sectors == NULL || numsectors <= 0 ) { Printf( "no level loaded.\n" ); return; }
	const int limit = FindLinesArg( argv, "limit", 6 );

	int shown = 0, total = 0;
	for ( int i = 0; i < numsectors && shown < limit; i++ )
	{
		sector_t *sec = &sectors[i];
		if ( sec->e == NULL ) continue;
		const unsigned n = sec->e->XFloor.ffloors.Size( );
		if ( n == 0 ) continue;
		total++;

		double minx = 1e30, maxx = -1e30, miny = 1e30, maxy = -1e30;
		for ( int k = 0; k < sec->linecount; k++ )
		{
			const line_t *ln = sec->lines[k];
			const double xs[2] = { FIXED2FLOAT( ln->v1->x ), FIXED2FLOAT( ln->v2->x ) };
			const double ys[2] = { FIXED2FLOAT( ln->v1->y ), FIXED2FLOAT( ln->v2->y ) };
			for ( int q = 0; q < 2; q++ )
			{
				if ( xs[q] < minx ) minx = xs[q];  if ( xs[q] > maxx ) maxx = xs[q];
				if ( ys[q] < miny ) miny = ys[q];  if ( ys[q] > maxy ) maxy = ys[q];
			}
		}
		const double cx = ( minx + maxx ) * 0.5, cy = ( miny + maxy ) * 0.5;

		for ( unsigned f = 0; f < n; f++ )
		{
			F3DFloor *r = sec->e->XFloor.ffloors[f];
			if ( r == NULL || r->top.plane == NULL ) continue;
			// ::top is the CONTROL sector's ceiling plane, which is the surface walked on -- a control
			// sector is modelled upside down. Getting that backwards is what once left decals floating.
			// secplane_t has a double overload, and cx/cy are doubles, so take the double answer
			// rather than round-tripping through fixed point to print it.
			const double topz = r->top.plane->ZatPoint( cx, cy );
			const double botz = r->bottom.plane != NULL ? r->bottom.plane->ZatPoint( cx, cy ) : 0.0;
			Printf( "sector %d 3D floor %u/%u: top %.0f bottom %.0f  flags 0x%x  stand (%.0f, %.0f, %.0f)\n",
				i, f + 1, n, topz, botz, (unsigned)r->flags,
				cx, cy, topz );
		}
		shown++;
	}
	Printf( "fua_find_3dfloors: %d sector(s) shown, %d with 3D floors, of %d\n", shown, total, numsectors );
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
