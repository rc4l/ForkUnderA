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
#include "c_cvars.h"
#include "r_defs.h"
#include "r_state.h"
#include "g_level.h"
#include "r_sky.h"   // skyflatnum, for the lower texture's sky reference

#include "textures/textures.h"
#include "gl/textures/gl_material.h"
#include "gl/scene/gl_wall.h"

#include "features/levelmesh/wallcache.h"
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/staticmesh.h"
#include "gl/data/gl_vertexbuffer.h"   // FFlatVertex, whose layout the mesh stores
#include "features/surfaces/computation/wallgeom_compute.h"
#include "features/surfaces/computation/walluv_compute.h"
#include "features/surfaces/computation/planegeom_compute.h"

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
bool MiddleTextureOf(const seg_t *seg, float &height, bool &pegBottom, float &rowOffset,
	float &pegFloor, float &pegCeiling)
{
	if (seg == NULL || seg->sidedef == NULL || seg->linedef == NULL) return false;
	FMaterial *mat = FMaterial::ValidateTexture(seg->sidedef->GetTexture(side_t::mid), false, true);
	if (mat == NULL) return false;

	// [rc4l] The material's RENDER height, which is not the texture's scaled height.
	//
	// GetScaledHeight applies the texture's own scale and stops there; GetTexCoordInfo folds in the
	// sidedef scales and whatever the texture definition did to it, and mRenderHeight is what GL
	// measures the middle texture with. On Sunder MAP16 the two differ by more than three times --
	// 128 against 40 -- which is 3640 pieces of disagreement from one wrong input, and no amount of
	// staring at the map would have said which input it was.
	FTexCoordInfo tci;
	mat->GetTexCoordInfo(&tci, seg->sidedef->GetTextureXScale(side_t::mid),
		seg->sidedef->GetTextureYScale(side_t::mid));
	// [rc4l] expand=false. The expanded material carries a two-pixel border for filtering, and
	// asking it for a render height gives 130 where the wall is 128 -- which reads as the derivation
	// being two units too tall on every middle texture in the level.
	int renderHeight = tci.mRenderHeight;
	if (renderHeight < 0) renderHeight = -renderHeight;
	if (renderHeight <= 0) return false;
	height = (float)renderHeight;

	pegBottom = !!(seg->linedef->flags & ML_DONTPEGBOTTOM);
	rowOffset = FIXED2FLOAT(tci.RowOffset(seg->sidedef->GetTextureYOffset(side_t::mid)));

	// [rc4l] Pegged from the plane's TEXTURE Z, not from where the plane currently is.
	//
	// A sector that has moved keeps a separate reference height for its textures, so a lift under a
	// hanging middle texture slides the geometry without sliding the picture. Pegging from the live
	// plane instead is a mark that swims up and down with the lift.
	const sector_t *front = seg->frontsector, *back = seg->backsector;
	if (front == NULL || back == NULL) return false;
	pegFloor = FIXED2FLOAT(MAX(front->GetPlaneTexZ(sector_t::floor), back->GetPlaneTexZ(sector_t::floor)));
	pegCeiling = FIXED2FLOAT(MIN(front->GetPlaneTexZ(sector_t::ceiling), back->GetPlaneTexZ(sector_t::ceiling)));
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
		float texH = 0.f, rowOfs = 0.f, pegFloor = 0.f, pegCeil = 0.f;
		bool pegBottom = false;
		if (MiddleTextureOf(seg, texH, pegBottom, rowOfs, pegFloor, pegCeil))
		{
			// The pegging reference is the texture Z, the CLIP is the live opening.
			WallHeights peg = h;
			peg.frontFloor = peg.backFloor = pegFloor;
			peg.frontCeiling = peg.backCeiling = pegCeil;
			const WallPart hung = ComputeMiddleTexturePart(peg, texH, pegBottom, rowOfs);
			const WallPart opening = ComputeMiddlePart(h);
			float b = hung.bottom, t = hung.top;
			if (b < opening.bottom) b = opening.bottom;
			if (t > opening.top) t = opening.top;
			p.bottom = b; p.top = t; p.present = (t > b);
		}
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

// [rc4l] A switch for the rule under test, so it is a measurement and not a belief.
// [rc4l] GL's CheckTexturePosition, modelled -- and off by default because it is not yet a win.
//
// It fixes 39 pieces on dbab01 and breaks 77, so applying it unconditionally would trade one wrong
// number for a worse one. The switch is here so the claim stays measurable in one run rather than
// remembered wrongly: see features/surfaces/README.md for what is known and what is not.
CVAR( Bool, fua_surface_vshift, false, 0 )

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
	int uvChecked = 0, uvAgreed = 0, uvShown = 0;
	// Which PART the peg flip explains, because "the flag is inverted" is only actionable once it
	// says for which of the three it is inverted.
	int uvFlipByType[5] = { 0, 0, 0, 0, 0 };
	// [rc4l] What the disagreement IS, in the terms DoTexture is written in.
	//
	// A v that is wrong is not a fault until you know by how much. GL's own reference is recoverable
	// from the coordinate it produced -- texTop = z + v * texHeight -- so the difference can be
	// measured against the terms of the formula instead of guessed at: the peg shift, a whole texture,
	// or something that is neither and deserves its own look.
	int uvDeltaPeg = 0, uvDeltaUnpeg = 0, uvDeltaWholeTex = 0, uvDeltaOther = 0, lastOther = 0;
	// [rc4l] A whole texture off is the same picture ONLY where the wall wraps.
	// GLT_CLAMPY is set on the parts that do not -- a hanging midtexture, a sky-clipped upper -- and on
	// those the offset is a real fault wearing the harmless class's clothes. Counted apart so it can
	// never be waved through.
	int uvWholeTexClamped = 0;
	// [rc4l] The shift scored on its own: how many pieces it fixed and how many it broke.
	int uvAgreedRaw = 0, uvShiftFixed = 0, uvShiftBroke = 0, uvShiftShown = 0;
	// [rc4l] Did GL shift THIS wall? Asked of the capture alone, with no derivation involved.
	//
	// CheckTexturePosition leaves min(uplft.v, uprgt.v) in [0,1) -- that is what it is for. So a
	// captured wall whose top v sits outside that range was never put through it, whatever the call
	// graph says, and counting those separates a wrong formula from a step that did not run.
	int uvPostOk[5] = { 0, 0, 0, 0, 0 }, uvPostNo[5] = { 0, 0, 0, 0, 0 };
	int uvPegByType[5] = { 0, 0, 0, 0, 0 }, uvUnpegByType[5] = { 0, 0, 0, 0, 0 };

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

			// [rc4l] The alignment as well as the height, on the piece that agrees about height.
			//
			// Two different questions with two different failure modes, so two scores. A wall the right
			// size with the picture in the wrong place is a texture that looks misaligned; a wall the
			// wrong size with the picture right is a hole or an overlap. Reported apart because a single
			// combined number cannot say which of them is happening.
			{
				const GLWall *first = NULL;
				for ( int q = 0; q < pieces && first == NULL; q++ )
				{
					const GLWall *pw = zx::levelmesh::CachedPiece( s, q );
					if ( pw != NULL && pw->type == kTypeOf[slot] ) first = pw;
				}
				// [rc4l] A cached piece is only comparable while everything it points at is still there.
				//
				// The cache outlives a single frame by design, and a piece captured moments before a level
				// change holds an FMaterial that the new level's precache has already thrown away. Reading
				// through it takes the process down with no log line at all, which reads as a tool fault for
				// an hour. Minisegs have no sidedef and no linedef either, so both are checked here rather
				// than at each of the dozen places below that assume them.
				if ( first != NULL && ( first->gltexture == NULL || first->gltexture->tex == NULL ||
					segs[s].sidedef == NULL || segs[s].linedef == NULL ) ) first = NULL;
				if ( first != NULL && byType[slot].count == 1 )
				{
					FMaterial *mat = FMaterial::ValidateTexture( first->gltexture ?
						first->gltexture->tex->id : FNullTextureID( ), false, true );
					if ( mat != NULL )
					{
						// [rc4l] The scales of the PART, not of nothing.
						//
						// Asking for texture coordinates with unit scales gives the answer for a wall that
						// nobody scaled, which is most of them and not all of them -- and the ones that are
						// scaled then disagree by exactly the scale factor, which reads as a pegging fault.
						const int texposEarly = ( kTypeOf[slot] == RENDERWALL_TOP ) ? side_t::top :
							( kTypeOf[slot] == RENDERWALL_BOTTOM ) ? side_t::bottom : side_t::mid;
						FTexCoordInfo tci;
						mat->GetTexCoordInfo( &tci, segs[s].sidedef->GetTextureXScale( texposEarly ),
							segs[s].sidedef->GetTextureYScale( texposEarly ) );
						int th = tci.mRenderHeight;
						if ( th < 0 ) th = -th;
						if ( th > 0 )
						{
							// The capture's own v at the top-left corner, against the derived one for the
							// same height. Whatever pegging the line asked for is already in both.
							// [rc4l] The reference pair and the peg flag each part actually uses, taken from
							// DoTexture's own call sites rather than inferred:
							//
							//   upper   front ceiling / back ceiling, pegged when DONTPEGTOP is CLEAR
							//   lower   back floor / front floor, pegged when DONTPEGBOTTOM is SET
							//   middle  front ceiling / front floor, pegged when DONTPEGBOTTOM is SET
							//
							// All texture Z, not live plane heights. Two rounds of inferring this from
							// pictures produced 91.9% and then 55.7%; reading the caller produced the rule.
							const int lineFlags = segs[s].linedef->flags;
							// [rc4l] The ORIGINAL sectors, which is what GL aligns against.
							//
							// GLWall::Process works from `realfront`/`realback` -- &sectors[sectornum] -- and
							// not from the frontsector/backsector it was handed, because those can be the
							// substituted copies a fake floor or a transfer-heights sector puts in their place.
							// Their PLANES agree; their texture Z does not have to, and texture Z is the whole
							// of alignment.
							const sector_t *rf = segs[s].frontsector ? &sectors[segs[s].frontsector->sectornum] : NULL;
							const sector_t *rb = segs[s].backsector ? &sectors[segs[s].backsector->sectornum] : NULL;
							if ( rf == NULL ) { uvChecked++; continue; }
							bool pegged = false;
							float refCeil = 0.f, refFloor = 0.f, vOffset = 0.f;
							int texpos = side_t::mid;
							if ( kTypeOf[slot] == RENDERWALL_TOP && rb != NULL )
							{
								texpos = side_t::top;
								pegged = ( lineFlags & ML_DONTPEGTOP ) == 0;
								refCeil = FIXED2FLOAT( rf->GetPlaneTexZ( sector_t::ceiling ) );
								refFloor = FIXED2FLOAT( rb->GetPlaneTexZ( sector_t::ceiling ) );
							}
							else if ( kTypeOf[slot] == RENDERWALL_BOTTOM && rb != NULL )
							{
								texpos = side_t::bottom;
								pegged = ( lineFlags & ML_DONTPEGBOTTOM ) != 0;
								refCeil = FIXED2FLOAT( rb->GetPlaneTexZ( sector_t::floor ) );
								refFloor = FIXED2FLOAT( rf->GetPlaneTexZ( sector_t::floor ) );
								// [rc4l] The lower texture's extra term, which no other part has.
								//
								// A pegged lower continues the picture down from the wall above it, so its
								// reference reaches to the front sector's ceiling -- and under sky on both sides,
								// to the back sector's. Applied against the ORIGINAL sectors; against the
								// substituted ones it moved the answer the wrong way, which is what sent the
								// third guess at this rule to 84.1%.
								const bool bothSky = rf->GetTexture( sector_t::ceiling ) == skyflatnum &&
									rb->GetTexture( sector_t::ceiling ) == skyflatnum;
								vOffset = FIXED2FLOAT( rf->GetPlaneTexZ( sector_t::floor ) -
									( bothSky ? rb->GetPlaneTexZ( sector_t::ceiling )
									          : rf->GetPlaneTexZ( sector_t::ceiling ) ) );
							}
							else if ( rb != NULL )
							{
								// [rc4l] A two-sided middle is not pegged the way the other parts are: it is
								// placed directly, from the higher floor upward when unpegged-bottom and from
								// the lower ceiling downward otherwise, and clipped to the opening. DoMidTexture
								// writes it as a texturebottom/texturetop pair rather than through DoTexture.
								pegged = false;
								if ( lineFlags & ML_DONTPEGBOTTOM )
								{
									const float bottomRef = FIXED2FLOAT( MAX( rf->GetPlaneTexZ( sector_t::floor ),
										rb->GetPlaneTexZ( sector_t::floor ) ) );
									refCeil = bottomRef + (float)th;
									refFloor = bottomRef;
								}
								else
								{
									refCeil = FIXED2FLOAT( MIN( rf->GetPlaneTexZ( sector_t::ceiling ),
										rb->GetPlaneTexZ( sector_t::ceiling ) ) );
									refFloor = refCeil - (float)th;
								}
							}
							else
							{
								pegged = ( lineFlags & ML_DONTPEGBOTTOM ) != 0;
								refCeil = FIXED2FLOAT( rf->GetPlaneTexZ( sector_t::ceiling ) );
								refFloor = FIXED2FLOAT( rf->GetPlaneTexZ( sector_t::floor ) );
							}
							const float rowOfs = FIXED2FLOAT( tci.RowOffset(
								segs[s].sidedef->GetTextureYOffset( texpos ) ) );
							const float texTop = ComputeTextureTop( refCeil, refFloor, (float)th, pegged,
								rowOfs, vOffset );
							// [rc4l] And then GL slides the wall back into the first copy of its texture.
							//
							// CheckTexturePosition runs on every wall DoTexture makes, and until it was modelled
							// here every wall it moved read as an alignment fault. That is what the peg-condition
							// hunt was chasing: a step after the formula, not a term inside it.
							const float wantVRaw = ComputeWallV( first->ztop[0], texTop, (float)th );
							const float wantVRight = ComputeWallV( first->ztop[1], texTop, (float)th );
							// [rc4l] And only the parts DoTexture makes.
							//
							// CheckTexturePosition is called from DoTexture and from BuildFFBlock, and from
							// nowhere else -- DoMidTexture writes its four v values and leaves them. So an upper,
							// a lower and a one-sided middle slide back into the first copy of their texture and a
							// two-sided middle does not. Applied to all five, the shift fixed 39 pieces and broke
							// 77; the 77 were the hanging midtextures.
							const bool glShifts = kTypeOf[slot] == RENDERWALL_TOP ||
								kTypeOf[slot] == RENDERWALL_BOTTOM || kTypeOf[slot] == RENDERWALL_M1S;
							// [rc4l] The shift belongs to the WHOLE wall, not to the piece of it that survived.
							//
							// CheckTexturePosition runs once, on the wall DoTexture built, and SplitWall then cuts
							// that wall up and interpolates v across the cuts. A fragment therefore inherits a
							// shift computed from corners it no longer has, and taking floor() of its OWN top v
							// moves it again -- which is what made the shift fix 39 pieces and break 77. So the
							// shift is computed from the derived span of the whole part, which is what the
							// unsplit wall's top was.
							const float wholeV = ComputeWallV( wantTop, texTop, (float)th );
							const float vShift = ( !fua_surface_vshift || !glShifts ||
								first->gltexture->tex->bHasCanvas ) ? 0.f
								: ComputeVShift( wholeV, wholeV );
							const float wantV = wantVRaw - vShift;
							// [rc4l] Both answers are scored, because a step that fixes more than it breaks is
							// still breaking something, and the count of each is the only way to see that.
							const bool okRaw = fabsf( wantVRaw - first->uplft.v ) <= 0.01f;
							const bool okShifted = fabsf( wantV - first->uplft.v ) <= 0.01f;
							if ( okRaw ) uvAgreedRaw++;
							if ( okRaw && !okShifted ) uvShiftBroke++;
							if ( !okRaw && okShifted ) uvShiftFixed++;
							uvChecked++;
							{
								const float topV = ( first->uplft.v < first->uprgt.v ) ? first->uplft.v : first->uprgt.v;
								if ( topV >= 0.f && topV < 1.f ) uvPostOk[slot]++; else uvPostNo[slot]++;
							}
							if ( okRaw && !okShifted && uvShiftShown < 6 )
							{
								uvShiftShown++;
								Printf( "  shift broke seg %d %s: capture v %.3f, derived %.3f, shift %.1f, ztop %.1f/%.1f, clampy %d\n",
									s, TypeName( kTypeOf[slot] ), first->uplft.v, wantVRaw, vShift,
									first->ztop[0], first->ztop[1], ( first->flags & GLT_CLAMPY ) ? 1 : 0 );
								Printf( "      line %d flags 0x%x, drawn with %s, sidedef says %s, canvas %d, th %d, lolft.v %.3f\n",
									(int)( segs[s].linedef - lines ), (unsigned)segs[s].linedef->flags,
									first->gltexture->tex->Name.GetChars( ),
									TexMan[segs[s].sidedef->GetTexture( (side_t::ETexpart)texposEarly )] ?
										TexMan[segs[s].sidedef->GetTexture( (side_t::ETexpart)texposEarly )]->Name.GetChars( ) : "-",
									first->gltexture->tex->bHasCanvas ? 1 : 0, tci.mRenderHeight, first->lolft.v );
							}
							if ( okShifted ) uvAgreed++;
							else
							{
								// [rc4l] Name the term, do not guess the rule.
								//
								// Two rounds of inferring a peg CONDITION from which pieces failed produced a rule
								// that correlated perfectly and halved the score when applied. So this asks a
								// different question: not "which pieces are wrong" but "by exactly how much", in
								// the units of the one line of DoTexture that can differ --
								//
								//     if (peg) floatceilingref += mRenderHeight - (lh + v_offset)
								//
								// A difference of exactly that shift says the peg flag disagrees and nothing else
								// does. A difference of a whole texture says the picture lands in the same place
								// on a wrapped wall and the two answers are visually identical. Anything else is a
								// real gap and is the only part worth reasoning about.
								const float glTop = first->ztop[0] + first->uplft.v * (float)th;
								const float delta = glTop - texTop;
								const float pegShift = (float)th - ( ( refCeil - refFloor ) + vOffset );
								const float texturesOff = delta / (float)th;
								if ( fabsf( delta - pegShift ) <= 0.01f ) { uvDeltaPeg++; uvPegByType[slot]++; }
								else if ( fabsf( delta + pegShift ) <= 0.01f ) { uvDeltaUnpeg++; uvUnpegByType[slot]++; }
								else if ( fabsf( texturesOff - floorf( texturesOff + 0.5f ) ) <= 0.001f )
								{
									uvDeltaWholeTex++;
									if ( first->flags & GLT_CLAMPY ) uvWholeTexClamped++;
								}
								else { uvDeltaOther++; uvFlipByType[slot]++; }
								// Only the something-else cases get printed: the other three classes are already
								// explained by their name, and four lines of a solved class crowd out the one
								// unsolved piece worth looking at.
								// The two classes with a term still unnamed -- the peg shift the other way, and the ones
								// that fit no term at all -- are the only ones printed in full.
								if ( uvShown < 8 && ( uvDeltaOther + uvDeltaUnpeg ) != lastOther )
								{
									// [rc4l] GL's own reference, recovered from the coordinates it produced.
									//
									// v = (texTop - z) / texHeight, so texTop = z + v * texHeight. Printed beside the
									// derived one, "off by 1.0" becomes "GL referenced THIS height", which names the
									// term instead of inviting another guess at the formula.
									const float glTexTop = first->ztop[0] + first->uplft.v * (float)th;
									Printf( "  seg %d %s: capture v %.3f (texTop %.1f), derived v %.3f (texTop %.1f)\n",
										s, TypeName( kTypeOf[slot] ), first->uplft.v, glTexTop, wantV, texTop );
									Printf( "      refs %.1f / %.1f th %d peg %d rowofs %.1f ztop %.1f | sec %d/%d ceilZ %.1f/%.1f floorZ %.1f/%.1f vofs %.1f\n",
										refCeil, refFloor, th, (int)pegged, rowOfs, first->ztop[0],
										(int)( rf - sectors ), rb ? (int)( rb - sectors ) : -1,
										FIXED2FLOAT( rf->GetPlaneTexZ( sector_t::ceiling ) ),
										rb ? FIXED2FLOAT( rb->GetPlaneTexZ( sector_t::ceiling ) ) : 0.f,
										FIXED2FLOAT( rf->GetPlaneTexZ( sector_t::floor ) ),
										rb ? FIXED2FLOAT( rb->GetPlaneTexZ( sector_t::floor ) ) : 0.f, vOffset );
									Printf( "      off by %.2f textures; peg shift would be %.1f\n",
										( glTexTop - texTop ) / (float)th,
										(float)th - ( ( refCeil - refFloor ) + vOffset ) );
									Printf( "      line %d flags 0x%x, side %d of it, tex %s, clampy %d\n",
										(int)( segs[s].linedef - lines ), (unsigned)segs[s].linedef->flags,
										segs[s].sidedef == segs[s].linedef->sidedef[0] ? 0 : 1,
										first->gltexture ? first->gltexture->tex->Name.GetChars( ) : "?",
										( first->flags & GLT_CLAMPY ) ? 1 : 0 );
									lastOther = uvDeltaOther + uvDeltaUnpeg;
									uvShown++;
								}
							}
						}
					}
				}
			}
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
				float texH = 0.f, rowOfs = 0.f, pegF = 0.f, pegC = 0.f;
				bool pegBottom = false;
				const bool haveTex = MiddleTextureOf( &segs[s], texH, pegBottom, rowOfs, pegF, pegC );
				const WallPart opening = ComputeMiddlePart( h );
				Printf( "      line %d: front %.0f..%.0f back %.0f..%.0f | opening %.0f..%.0f | "
					"tex %s (%s %dx%d) h %.1f peg %d rowofs %.1f | pegZ %.1f/%.1f | scale %.3f | flags %08x\n",
					segs[s].linedef ? (int)( segs[s].linedef - lines ) : -1,
					h.frontFloor, h.frontCeiling, h.backFloor, h.backCeiling,
					opening.bottom, opening.top, haveTex ? "yes" : "none",
					TexMan( segs[s].sidedef->GetTexture( side_t::mid ) ) ?
						TexMan( segs[s].sidedef->GetTexture( side_t::mid ) )->Name.GetChars( ) : "?",
					TexMan( segs[s].sidedef->GetTexture( side_t::mid ) ) ?
						TexMan( segs[s].sidedef->GetTexture( side_t::mid ) )->GetWidth( ) : 0,
					TexMan( segs[s].sidedef->GetTexture( side_t::mid ) ) ?
						TexMan( segs[s].sidedef->GetTexture( side_t::mid ) )->GetHeight( ) : 0,
					texH, (int)pegBottom, rowOfs, pegF, pegC,
					FIXED2FLOAT( segs[s].sidedef->GetTextureYScale( side_t::mid ) ),
					segs[s].linedef ? (unsigned)segs[s].linedef->flags : 0u );
				// [rc4l] Every piece the capture holds for this seg, raw.
				//
				// A span that disagrees with what DoMidTexture reads like means the piece is not the quad
				// that function produced -- so the next question is what the capture actually stored, and
				// that is answerable only by printing it rather than by reasoning about the code that was
				// supposed to have produced it.
				// [rc4l] A fake floor or ceiling -- Transfer_Heights, which is how Doom does deep water --
				// clips wall geometry to itself, and nothing in the sidedef or the sector planes says so.
				{
					const sector_t *hf = segs[s].frontsector ? segs[s].frontsector->GetHeightSec() : NULL;
					const sector_t *hb = segs[s].backsector ? segs[s].backsector->GetHeightSec() : NULL;
					Printf( "      heightsec: front %s%s back %s%s\n",
						hf ? "yes " : "no", hf ? "" : "", hb ? "yes" : "no", "" );
					if ( hf ) Printf( "        front fake floor %.1f ceiling %.1f\n",
						FIXED2FLOAT( hf->floorplane.ZatPoint( segs[s].v1->x, segs[s].v1->y ) ),
						FIXED2FLOAT( hf->ceilingplane.ZatPoint( segs[s].v1->x, segs[s].v1->y ) ) );
					if ( hb ) Printf( "        back fake floor %.1f ceiling %.1f\n",
						FIXED2FLOAT( hb->floorplane.ZatPoint( segs[s].v1->x, segs[s].v1->y ) ),
						FIXED2FLOAT( hb->ceilingplane.ZatPoint( segs[s].v1->x, segs[s].v1->y ) ) );
				}
				// [rc4l] The texture coordinate info both ways, because the disagreement is a height and
				// every candidate for it lives in this struct.
				{
					FMaterial *m1 = FMaterial::ValidateTexture( segs[s].sidedef->GetTexture( side_t::mid ), false, true );
					FMaterial *m2 = FMaterial::ValidateTexture( segs[s].sidedef->GetTexture( side_t::mid ), true, true );
					FTexCoordInfo t1, t2;
					if ( m1 ) m1->GetTexCoordInfo( &t1, segs[s].sidedef->GetTextureXScale( side_t::mid ),
						segs[s].sidedef->GetTextureYScale( side_t::mid ) );
					if ( m2 ) m2->GetTexCoordInfo( &t2, segs[s].sidedef->GetTextureXScale( side_t::mid ),
						segs[s].sidedef->GetTextureYScale( side_t::mid ) );
					Printf( "      tci: plain h %d scaleY %.3f | expanded h %d scaleY %.3f | worldpan %d\n",
						m1 ? t1.mRenderHeight : -1, m1 ? FIXED2FLOAT( t1.mScaleY ) : 0.f,
						m2 ? t2.mRenderHeight : -1, m2 ? FIXED2FLOAT( t2.mScaleY ) : 0.f,
						m1 ? (int)t1.mWorldPanning : -1,
						FIXED2FLOAT( segs[s].v1->x / 2 + segs[s].v2->x / 2 ),
						FIXED2FLOAT( segs[s].v1->y / 2 + segs[s].v2->y / 2 ) );
				}
				for ( int q = 0; q < pieces; q++ )
				{
					const GLWall *pw = zx::levelmesh::CachedPiece( s, q );
					if ( pw == NULL ) continue;
					Printf( "      piece %d: type %d z %.1f..%.1f / %.1f..%.1f flags %02x alpha %.2f\n",
						q, (int)pw->type, pw->zbottom[0], pw->ztop[0], pw->zbottom[1], pw->ztop[1],
						(unsigned)pw->flags, pw->alpha );
				}
				shown++;
			}
		}
	}

	// [rc4l] And the planes, on the same terms.
	//
	// A floor or ceiling that was captured has its vertices in the mesh; the derivation says where
	// that plane is at each of those points. Every vertex should sit on it. This is a stronger check
	// than the wall one -- it tests every vertex rather than a span -- and it is the one that will
	// catch a slope derived with the wrong sign, which a height comparison at one point cannot.
	int flatChecked = 0, flatAgreed = 0, flatShown = 0, flatSkipped = 0;
	{
		int vertCount = 0;
		const FFlatVertex *verts = zx::levelmesh::MeshVertexData( vertCount );
		for ( int i = 0; verts != NULL; i++ )
		{
			const subsector_t *sub = NULL;
			const sector_t *model = NULL;
			bool ceiling = false;
			int whichPlane = 0;
			zx::levelmesh::MeshRange range;
			if ( !zx::levelmesh::CachedFlat( i, &sub, &ceiling, &model, &whichPlane, &range ) ) break;
			if ( range.count == 0 || sub == NULL ) { flatSkipped++; continue; }

			// [rc4l] Only a subsector's OWN planes. A 3D floor piece takes its geometry from a control
			// sector and its own plane index, which the derivation has not been taught yet -- counted
			// as skipped rather than failed, because "not yet answered" and "answered wrongly" are
			// different states and only one of them is a bug.
			const sector_t *sec = ( model != NULL ) ? model : sub->sector;
			if ( sec == NULL || ( model != NULL && model != sub->sector ) ) { flatSkipped++; continue; }

			const secplane_t &sp = ceiling ? sec->ceilingplane : sec->floorplane;
			SurfacePlane plane;
			plane.a = FIXED2FLOAT( sp.a ); plane.b = FIXED2FLOAT( sp.b );
			plane.c = FIXED2FLOAT( sp.c ); plane.d = FIXED2FLOAT( sp.d );

			bool ok = true;
			for ( unsigned int v = 0; v < range.count && ok; v++ )
			{
				if ( (int)( range.offset + v ) >= vertCount ) { ok = false; break; }
				const FFlatVertex &fv = verts[range.offset + v];
				// The mesh stores (x, z-up, y); the plane is asked in map (x, y).
				const float want = ComputePlaneHeightAt( plane, fv.x, fv.y );
				if ( fabsf( want - fv.z ) > 0.05f ) ok = false;
			}
			flatChecked++;
			if ( ok ) flatAgreed++;
			else if ( flatShown < 4 )
			{
				const FFlatVertex &fv = verts[range.offset];
				Printf( "  flat %d (sub %d, %s): vertex at (%.0f, %.0f) is z %.2f, plane says %.2f\n",
					i, (int)( sub - subsectors ), ceiling ? "ceiling" : "floor", fv.x, fv.y, fv.z,
					ComputePlaneHeightAt( plane, fv.x, fv.y ) );
				flatShown++;
			}
		}
	}
	Printf( "  planes: %d of %d captured flats sit on their derived plane (%.1f%%), %d skipped\n",
		flatAgreed, flatChecked, flatChecked ? 100.0 * flatAgreed / flatChecked : 0.0, flatSkipped );

	Printf( "fua_surface_verify on %s: %d of %d captured pieces agree (%.1f%%)\n",
		level.MapName.GetChars( ), agreed, checked,
		checked ? 100.0 * agreed / checked : 0.0 );
	Printf( "  alignment: %d of %d agree (%.1f%%)\n",
		uvAgreed, uvChecked, uvChecked ? 100.0 * uvAgreed / uvChecked : 0.0 );
	Printf( "    of the %d that do not, by how much: %d off by the peg shift, %d off by the peg shift the other way,\n",
		uvChecked - uvAgreed, uvDeltaPeg, uvDeltaUnpeg );
	Printf( "      %d off by a whole texture (%d of them on a wall that CLAMPS, where it is a real fault)\n",
		uvDeltaWholeTex, uvWholeTexClamped );
	Printf( "      %d off by something else\n", uvDeltaOther );
	Printf( "    without the CheckTexturePosition shift: %d of %d (%.1f%%); it fixed %d and broke %d\n",
		uvAgreedRaw, uvChecked, uvChecked ? 100.0 * uvAgreedRaw / uvChecked : 0.0, uvShiftFixed, uvShiftBroke );
	Printf( "    captured walls already inside their first texture copy (so GL shifted them):\n" );
	Printf( "      upper %d/%d, lower %d/%d, middle-1s %d/%d, middle %d/%d, middle-nofog %d/%d\n",
		uvPostOk[0], uvPostOk[0] + uvPostNo[0], uvPostOk[1], uvPostOk[1] + uvPostNo[1],
		uvPostOk[2], uvPostOk[2] + uvPostNo[2], uvPostOk[3], uvPostOk[3] + uvPostNo[3],
		uvPostOk[4], uvPostOk[4] + uvPostNo[4] );
	Printf( "    the same question asked AT CAPTURE, before anything could rewrite it:\n" );
	for ( int t = 0; t < 5; t++ )
	{
		int inRange = 0, outOfRange = 0;
		static const int kReportTypes[5] = { RENDERWALL_TOP, RENDERWALL_BOTTOM, RENDERWALL_M1S,
			RENDERWALL_M2S, RENDERWALL_M2SNF };
		zx::levelmesh::GetCaptureVRangeStats( kReportTypes[t], inRange, outOfRange );
		Printf( "      %-14s %d in range, %d outside\n", TypeName( kReportTypes[t] ), inRange, outOfRange );
	}
	Printf( "      of the outside ones, %d were fragments SplitWall made\n",
		zx::levelmesh::CaptureVOutOfRangeSplits( ) );
	Printf( "    the something-else by part: upper %d, lower %d, middle-1s %d, middle %d, middle-nofog %d\n",
		uvFlipByType[0], uvFlipByType[1], uvFlipByType[2], uvFlipByType[3], uvFlipByType[4] );
	Printf( "  %d skipped as sloped, %d skipped as another surface type, %d the derivation does not place at all\n",
		skippedSloped, skippedType, missing );
}
