// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Does the derivation agree with what GL actually produced?
//
// features/surfaces works out what a sidedef looks like from the map. The capture path transcribes
// what GLWall::Process made of the same sidedef. Until those two agree on real maps, the derivation
// is a claim; once they agree everywhere, the capture is redundant and can go -- and with it the
// wall/flat split, which only exists because there are two transcriptions of GL's two shapes.
//
// This is that comparison, run on demand over a loaded level. It reads the capture and never writes
// it. What it prints is not a pass/fail: it is a coverage figure and the first handful of
// disagreements with their numbers, because the way this gets finished is by picking off the largest
// remaining category of disagreement, not by waiting for a green light.
//
// The alternative -- wiring the derivation in and looking at the screen -- is how a thousand lines of
// special cases get re-learned one screenshot at a time. A sidedef that comes out 8 units too short
// is invisible in a corridor and obvious only in the one room nobody walks through.

#include "doomtype.h"
#include "c_dispatch.h"
#include "r_defs.h"
#include "r_state.h"
#include "g_level.h"

#include "textures/textures.h"
#include "gl/scene/gl_wall.h"

#include "features/levelmesh/wallcache.h"
#include "features/surfaces/computation/wallgeom_compute.h"

namespace zx { namespace surfaces {

namespace {

// [rc4l] The heights of the sectors either side of this seg, as the derivation wants them.
//
// Read at the seg's MIDPOINT rather than at a vertex, because a sloped plane has no single height
// and the two ends disagree -- taking one end would compare the derivation against half of what the
// capture drew. Slopes are skipped entirely below; this is what makes the skip detectable rather
// than a silent wrong answer.
bool HeightsForSeg(const seg_t *seg, WallHeights &out, bool &sloped)
{
	if (seg == NULL || seg->frontsector == NULL || seg->sidedef == NULL) return false;

	const sector_t *front = seg->frontsector;
	const sector_t *back = seg->backsector;
	sloped = (front->floorplane.a | front->floorplane.b | front->ceilingplane.a | front->ceilingplane.b) != 0;
	if (back != NULL)
		sloped = sloped || ((back->floorplane.a | back->floorplane.b |
		                     back->ceilingplane.a | back->ceilingplane.b) != 0);

	const fixed_t mx = seg->v1->x / 2 + seg->v2->x / 2;
	const fixed_t my = seg->v1->y / 2 + seg->v2->y / 2;

	out.frontFloor   = FIXED2FLOAT(front->floorplane.ZatPoint(mx, my));
	out.frontCeiling = FIXED2FLOAT(front->ceilingplane.ZatPoint(mx, my));
	out.twoSided = (back != NULL);
	if (back != NULL)
	{
		out.backFloor   = FIXED2FLOAT(back->floorplane.ZatPoint(mx, my));
		out.backCeiling = FIXED2FLOAT(back->ceilingplane.ZatPoint(mx, my));
	}
	else
	{
		out.backFloor = out.frontFloor;
		out.backCeiling = out.frontCeiling;
	}
	return true;
}

// What the derivation says this part should span, or absent.
// [rc4l] The seg's middle texture, for the rule that needs it: a two-sided middle hangs by its own
// height rather than filling the opening. Null texture means the line carries none, which is most
// two-sided lines in a Doom map.
bool MiddleTextureOf(const seg_t *seg, float &height, bool &pegBottom, float &rowOffset)
{
	if (seg == NULL || seg->sidedef == NULL || seg->linedef == NULL) return false;
	FTexture *tex = TexMan(seg->sidedef->GetTexture(side_t::mid));
	if (tex == NULL || tex->UseType == FTexture::TEX_Null) return false;
	// [rc4l] The SIDEDEF's scale as well as the texture's own.
	//
	// GetScaledHeight applies the texture's scale; the sidedef can scale it again, and a middle
	// texture scaled 3.2x covers 40 map units of a 128-unit graphic. Sunder MAP16 does exactly that
	// on thousands of lines, and without this the derivation hangs the full 128 and disagrees with
	// the capture by 88 units on every one of them -- 3640 pieces, which is the entire remaining gap
	// on that map.
	const fixed_t yscale = seg->sidedef->GetTextureYScale(side_t::mid);
	height = (float)tex->GetScaledHeight();
	if (yscale != 0) height /= FIXED2FLOAT(yscale);
	if (height <= 0.f) return false;
	pegBottom = !!(seg->linedef->flags & ML_DONTPEGBOTTOM);
	rowOffset = FIXED2FLOAT(seg->sidedef->GetTextureYOffset(side_t::mid));
	return true;
}

bool ExpectedSpan(const seg_t *seg, const WallHeights &h, int type, float &bottom, float &top)
{
	WallPart p;
	switch (type)
	{
	case RENDERWALL_TOP:    p = ComputeUpperPart(h); break;
	case RENDERWALL_BOTTOM: p = ComputeLowerPart(h); break;
	case RENDERWALL_M1S:    p = ComputeMiddlePart(h); break;
	case RENDERWALL_M2S:
	case RENDERWALL_M2SNF:
	{
		float texH = 0.f, rowOfs = 0.f;
		bool pegBottom = false;
		if (MiddleTextureOf(seg, texH, pegBottom, rowOfs))
			p = ComputeMiddleTexturePart(h, texH, pegBottom, rowOfs);
		else p = ComputeMiddlePart(h);
		break;
	}
	default: return false;   // sky, portals, fog boundaries: not this question
	}
	if (!p.present) return false;
	bottom = p.bottom;
	top = p.top;
	return true;
}

const char *TypeName(int type)
{
	switch (type)
	{
	case RENDERWALL_TOP:    return "upper";
	case RENDERWALL_BOTTOM: return "lower";
	case RENDERWALL_M1S:    return "middle (one-sided)";
	case RENDERWALL_M2S:    return "middle";
	case RENDERWALL_M2SNF:  return "middle (no fog)";
	default: return "other";
	}
}

} // namespace

}} // namespace zx::surfaces

//==========================================================================
//
// fua_surface_verify
//
// [rc4l] Compare every captured wall piece against what the derivation says it should be.
//
// usage: fua_surface_verify [tolerance]   -- in map units, default 0.05
//
//==========================================================================

CCMD( fua_surface_verify )
{
	using namespace zx::surfaces;

	if ( segs == NULL || numsegs <= 0 ) { Printf( "no level loaded.\n" ); return; }

	const float tol = ( argv.argc( ) > 1 ) ? (float)atof( argv[1] ) : 0.05f;

	int checked = 0, agreed = 0, skippedSloped = 0, skippedType = 0, shown = 0;
	// [rc4l] A piece the derivation says should not exist is its own category, and the worse one: a
	// disagreement about a HEIGHT is a wall that is the wrong size, while a disagreement about
	// EXISTENCE is a wall that is missing or a wall that is not there at all.
	int missing = 0;

	const int segCount = zx::levelmesh::CachedSegCount( );
	for ( int s = 0; s < segCount; s++ )
	{
		const int pieces = zx::levelmesh::CachedPieceCount( s );
		if ( pieces <= 0 ) continue;
		if ( (unsigned)s >= (unsigned)numsegs ) continue;

		WallHeights h;
		bool sloped = false;
		if ( !HeightsForSeg( &segs[s], h, sloped ) ) continue;

		// [rc4l] Compare the UNION of the pieces of a type, not each piece on its own.
		//
		// One sidedef part does not always arrive as one piece. A sector with 3D floors above it has a
		// light list, and GL splits a wall into a band per entry so each band can take its own light --
		// so a 128-unit middle texture comes back as three or four pieces of 40 units. Compared one at
		// a time every band but one disagrees, which is what made Sunder MAP16 read 93.9% while the
		// derivation was right: 3640 pieces, all of them bands of walls it had placed correctly.
		//
		// The union is the honest comparison: the derivation answers "where is this part", not "how did
		// GL choose to slice it".
		struct Span { float bottom, top; int count; };
		Span byType[8];
		for ( int t = 0; t < 8; t++ ) { byType[t].bottom = 1e30f; byType[t].top = -1e30f; byType[t].count = 0; }

		for ( int p = 0; p < pieces; p++ )
		{
			const GLWall *w = zx::levelmesh::CachedPiece( s, p );
			if ( w == NULL ) continue;
			int slot;
			switch ( w->type )
			{
			case RENDERWALL_TOP:    slot = 0; break;
			case RENDERWALL_BOTTOM: slot = 1; break;
			case RENDERWALL_M1S:    slot = 2; break;
			case RENDERWALL_M2S:    slot = 3; break;
			case RENDERWALL_M2SNF:  slot = 4; break;
			default: skippedType++; continue;
			}
			const float b = MIN( w->zbottom[0], w->zbottom[1] );
			const float t = MAX( w->ztop[0], w->ztop[1] );
			if ( b < byType[slot].bottom ) byType[slot].bottom = b;
			if ( t > byType[slot].top ) byType[slot].top = t;
			byType[slot].count++;
		}

		static const int kTypeOf[5] = { RENDERWALL_TOP, RENDERWALL_BOTTOM, RENDERWALL_M1S,
			RENDERWALL_M2S, RENDERWALL_M2SNF };
		for ( int slot = 0; slot < 5; slot++ )
		{
			if ( byType[slot].count == 0 ) continue;
			if ( sloped ) { skippedSloped++; continue; }

			float wantBottom = 0.f, wantTop = 0.f;
			if ( !ExpectedSpan( &segs[s], h, kTypeOf[slot], wantBottom, wantTop ) )
			{
				missing++;
				if ( shown < 8 )
				{
					Printf( "  seg %d %s: capture drew %.1f..%.1f in %d piece(s), derivation places nothing\n",
						s, TypeName( kTypeOf[slot] ), byType[slot].bottom, byType[slot].top, byType[slot].count );
					shown++;
				}
				continue;
			}

			checked++;
			if ( fabsf( byType[slot].bottom - wantBottom ) <= tol &&
			     fabsf( byType[slot].top - wantTop ) <= tol )
			{
				agreed++;
				continue;
			}
			if ( shown < 8 )
			{
				Printf( "  seg %d %s: capture %.2f..%.2f (%d piece(s)), derived %.2f..%.2f\n",
					s, TypeName( kTypeOf[slot] ), byType[slot].bottom, byType[slot].top,
					byType[slot].count, wantBottom, wantTop );
				float texH = 0.f, rowOfs = 0.f;
				bool pegBottom = false;
				const bool haveTex = MiddleTextureOf( &segs[s], texH, pegBottom, rowOfs );
				const WallPart opening = ComputeMiddlePart( h );
				Printf( "      line %d: front %.0f..%.0f back %.0f..%.0f | opening %.0f..%.0f | "
					"tex %s h %.1f peg %d rowofs %.1f | flags %08x\n",
					segs[s].linedef ? (int)( segs[s].linedef - lines ) : -1,
					h.frontFloor, h.frontCeiling, h.backFloor, h.backCeiling,
					opening.bottom, opening.top, haveTex ? "yes" : "none", texH, (int)pegBottom, rowOfs,
					segs[s].linedef ? (unsigned)segs[s].linedef->flags : 0u );
				shown++;
			}
		}
	}

	Printf( "fua_surface_verify on %s: %d of %d captured pieces agree (%.1f%%)\n",
		level.MapName.GetChars( ), agreed, checked,
		checked ? 100.0 * agreed / checked : 0.0 );
	Printf( "  %d skipped as sloped, %d skipped as another surface type, %d the derivation does not place at all\n",
		skippedSloped, skippedType, missing );
}
