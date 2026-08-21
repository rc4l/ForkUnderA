// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "doomtype.h"
#include "r_defs.h"
#include "r_state.h"
#include "r_sky.h"   // skyflatnum, for the lower texture's sky reference
#include "g_level.h"   // level.flags, for the fog test GLWall::Process makes
#include "gl/renderer/gl_lightdata.h"   // gl_ClampLight, gl_isBlack

#include "textures/textures.h"
#include "gl/textures/gl_material.h"
#include "gl/scene/gl_wall.h"

#include <math.h>

#include "features/surfaces/surfacebuild.h"
#include "features/surfaces/computation/wallgeom_compute.h"
#include "features/surfaces/computation/walluv_compute.h"

// [rc4l] Declared at global scope: EXTERN_CVAR inside the namespace would name a different symbol
// than the CVAR defined in surfaceverify.cpp, and only the linker would notice.
EXTERN_CVAR(Bool, fua_surface_vshift)

namespace zx { namespace surfaces {

namespace {

int g_derived = 0, g_fellBack = 0;
// [rc4l] WHY a piece fell back, because "1073 fell back" does not say what to build next.
int g_fbMiddle = 0, g_fbSpecial = 0, g_fbNoTexture = 0, g_fbNoSpan = 0, g_fbSeam = 0;

// [rc4l] The sector heights AT A POINT, not at the seg's midpoint.
//
// The scorer reads them at the midpoint because it compares one number per piece and a slope has no
// single height. A surface that is going to be DRAWN needs both ends, or every sloped wall in the
// level comes out flat -- and it comes out flat quietly, since the midpoint is the average and the
// error is zero exactly where anyone would think to check.
void HeightsAt(const seg_t *seg, fixed_t x, fixed_t y, WallHeights &out)
{
	const sector_t *front = seg->frontsector;
	const sector_t *back = seg->backsector;
	out.frontFloor   = FIXED2FLOAT(front->floorplane.ZatPoint(x, y));
	out.frontCeiling = FIXED2FLOAT(front->ceilingplane.ZatPoint(x, y));
	out.twoSided = (back != NULL);
	if (back != NULL)
	{
		out.backFloor   = FIXED2FLOAT(back->floorplane.ZatPoint(x, y));
		out.backCeiling = FIXED2FLOAT(back->ceilingplane.ZatPoint(x, y));
	}
	else
	{
		out.backFloor = out.frontFloor;
		out.backCeiling = out.frontCeiling;
	}
}

// GL's own test for "the sidedef carries a texture here": missing and TEX_Null both mean no.
bool NonNullTexture(FTextureID id)
{
	FTexture *t = TexMan(id);
	return t != NULL && t->UseType != FTexture::TEX_Null;
}

// The part this render type is, and which sidedef texture slot it uses.
bool PartOf(int renderType, int &texpos)
{
	switch (renderType)
	{
	case RENDERWALL_TOP:    texpos = side_t::top; return true;
	case RENDERWALL_BOTTOM: texpos = side_t::bottom; return true;
	case RENDERWALL_M1S:
	case RENDERWALL_M2S:
	case RENDERWALL_M2SNF: texpos = side_t::mid; return true;
	default: return false;
	}
}

// [rc4l] The vertical span this part occupies, from the map.
// [rc4l] The span of a part at BOTH ends, because two of the three clamps are two-ended.
//
// The numbers come back whether or not the part has area at either end. A sloped wall can exist at
// one end and not the other, and GL draws it if EITHER end has area -- `if (topleft<=bottomleft &&
// topright<=bottomright) return;` is the whole of its test. Requiring both ends is what left 133
// uppers and lowers on dbab04, the map with 337 sloped pieces, unaccounted for: every one of them a
// wall that pinches out at one end.
bool SpanBothEnds(int renderType, const WallHeights &h1, const WallHeights &h2,
	float *top, float *bottom)
{
	WallPart p1, p2;
	switch (renderType)
	{
	case RENDERWALL_TOP:    ComputeUpperSpan(h1, h2, p1, p2); break;
	case RENDERWALL_BOTTOM: ComputeLowerSpan(h1, h2, p1, p2); break;
	case RENDERWALL_M1S:    p1 = ComputeMiddlePart(h1); p2 = ComputeMiddlePart(h2); break;
	default: return false;   // the two-sided middle is answered inline, against texture Z
	}
	top[0] = p1.top; bottom[0] = p1.bottom;
	top[1] = p2.top; bottom[1] = p2.bottom;
	return true;
}

} // namespace

void GetDeriveStats(int &derived, int &fellBack) { derived = g_derived; fellBack = g_fellBack; }
void GetDeriveFallbacks(int &twoSidedMiddle, int &special, int &noTexture, int &noSpan, int &seam)
{
	twoSidedMiddle = g_fbMiddle; special = g_fbSpecial; noTexture = g_fbNoTexture;
	noSpan = g_fbNoSpan; seam = g_fbSeam;
}
void NoteDeriveSeamFallback() { g_fbSeam++; g_fellBack++; }
void ResetDeriveStats()
{
	g_derived = g_fellBack = 0;
	g_fbMiddle = g_fbSpecial = g_fbNoTexture = g_fbNoSpan = g_fbSeam = 0;
}
void NoteDeriveFallback() { g_fellBack++; }

bool BuildDerivedWallLight(const seg_t *seg, DerivedWallLight &out, const sector_t *&colormapFrom)
{
	if (seg == NULL || seg->sidedef == NULL || seg->frontsector == NULL) return false;
	const sector_t *front = seg->frontsector;
	// [rc4l] A sector with a 3D floor light list does not have ONE light level -- and that is now the
	// CALLER's problem rather than a reason to refuse.
	//
	// SplitWall cuts the wall at each band and gives every fragment that band's light and colormap.
	// This used to decline the sector outright, which was honest and had a cost nobody had measured:
	// the seg was handed back to the capture, and when GL stops walking the level there is no
	// capture, so those walls simply were not there. That is what kept fua_dg_standalone off, and
	// standalone is worth two thirds of the frame on Sunder MAP10.
	//
	// What comes back here is the wall's OWN light -- band zero, which SplitWall also leaves alone.
	// The bands below it are the caller's to apply, from wallbands_compute.

	colormapFrom = front;
	// Transcribed from GLWall::Process, which is the only place this is written down. gl_ClampLight
	// and GetLightLevel are the engine's; the shape of the rellight clamp is not obvious and is not
	// re-derived here on purpose.
	int rel = 0;
	const int orglightlevel = gl_ClampLight(front->lightlevel);
	const bool foggy = (!gl_isBlack(front->ColorMap->Fade) || (level.flags & LEVEL_HASFADETABLE));
	out.lightLevel = gl_ClampLight(seg->sidedef->GetLightLevel(foggy, orglightlevel, false, &rel));
	if (orglightlevel >= 253)                    out.relLight = 0;   // fake contrast is invisible here
	else if (out.lightLevel - rel > 256)         out.relLight = 256 - out.lightLevel + rel;
	else                                         out.relLight = rel;
	out.valid = true;
	return true;
}

bool BuildDerivedWallSpan(const seg_t *seg, int renderType, DerivedWallSpan &out)
{
	if (seg == NULL || seg->sidedef == NULL || seg->linedef == NULL ||
	    seg->frontsector == NULL) return false;

	int texpos = side_t::mid;
	if (!PartOf(renderType, texpos)) { g_fbSpecial++; g_fellBack++; return false; }
	// [rc4l] A two-sided middle is left to the capture on purpose.
	//
	// It does not fill an opening: DoMidTexture hangs it by its own height and clips it, and the
	// ladder scores it lowest of the three. Moving the parts that are understood is what this is for;
	// moving one that is not would put a guess on screen and call it progress.
	const bool twoSidedMid = (renderType == RENDERWALL_M2S || renderType == RENDERWALL_M2SNF);
	if (twoSidedMid && seg->backsector == NULL) { g_fbMiddle++; g_fellBack++; return false; }

	FMaterial *mat = FMaterial::ValidateTexture(seg->sidedef->GetTexture((side_t::ETexpart)texpos),
		false, true);
	// [rc4l] A sloped step with NO texture on the sidedef is drawn with the SECTOR'S FLAT.
	//
	// GLWall::Process falls back to it when both sectors are sloped and neither plane is sky --
	// "render it anyway with the sector's floor texture. With a background sky there are ugly holes
	// otherwise and slopes are simply not precise enough to match in any case." Without this the map
	// does not account for 131 of dbab04's parts, 38 uppers and 93 lowers, all of them on the map
	// with 337 sloped pieces.
	if (mat == NULL && seg->backsector != NULL &&
	    (renderType == RENDERWALL_TOP || renderType == RENDERWALL_BOTTOM))
	{
		const sector_t *fs = seg->frontsector, *bs = seg->backsector;
		const bool ceilingPart = (renderType == RENDERWALL_TOP);
		const secplane_t &fp = ceilingPart ? fs->ceilingplane : fs->floorplane;
		const secplane_t &bp = ceilingPart ? bs->ceilingplane : bs->floorplane;
		const int which = ceilingPart ? sector_t::ceiling : sector_t::floor;
		if ((fp.a | fp.b | bp.a | bp.b) != 0 &&
		    fs->GetTexture(which) != skyflatnum && bs->GetTexture(which) != skyflatnum)
			mat = FMaterial::ValidateTexture(fs->GetTexture(which), false, true);
	}
	if (mat == NULL) { g_fbNoTexture++; g_fellBack++; return false; }

	FTexCoordInfo tci;
	mat->GetTexCoordInfo(&tci, seg->sidedef->GetTextureXScale((side_t::ETexpart)texpos),
		seg->sidedef->GetTextureYScale((side_t::ETexpart)texpos));
	const float th = (float)tci.mRenderHeight;
	if (th == 0.f) { g_fbNoTexture++; g_fellBack++; return false; }

	// [rc4l] The ORIGINAL sectors, which is what GL aligns against.
	//
	// GLWall::Process works from realfront/realback -- &sectors[sectornum] -- and not from the
	// sectors it was handed, because those can be the substituted copies a fake floor or a
	// transfer-heights sector puts in their place. Their PLANES agree; their texture Z does not have
	// to, and texture Z is the whole of alignment.
	const sector_t *rf = &sectors[seg->frontsector->sectornum];
	const sector_t *rb = seg->backsector ? &sectors[seg->backsector->sectornum] : NULL;
	const int lineFlags = seg->linedef->flags;

	bool pegged = false;
	float refCeil = 0.f, refFloor = 0.f, vOffset = 0.f;
	if (renderType == RENDERWALL_TOP)
	{
		if (rb == NULL) { g_fbNoSpan++; g_fellBack++; return false; }
		pegged = (lineFlags & ML_DONTPEGTOP) == 0;
		refCeil = FIXED2FLOAT(rf->GetPlaneTexZ(sector_t::ceiling));
		refFloor = FIXED2FLOAT(rb->GetPlaneTexZ(sector_t::ceiling));
	}
	else if (renderType == RENDERWALL_BOTTOM)
	{
		if (rb == NULL) { g_fbNoSpan++; g_fellBack++; return false; }
		pegged = (lineFlags & ML_DONTPEGBOTTOM) != 0;
		refCeil = FIXED2FLOAT(rb->GetPlaneTexZ(sector_t::floor));
		refFloor = FIXED2FLOAT(rf->GetPlaneTexZ(sector_t::floor));
		// [rc4l] The lower texture's extra term, which no other part has: a pegged lower continues
		// the picture down from the wall above it, so its reference reaches to the front sector's
		// ceiling -- and under sky on both sides, to the back sector's.
		const bool bothSky = rf->GetTexture(sector_t::ceiling) == skyflatnum &&
			rb->GetTexture(sector_t::ceiling) == skyflatnum;
		vOffset = FIXED2FLOAT(rf->GetPlaneTexZ(sector_t::floor) -
			(bothSky ? rb->GetPlaneTexZ(sector_t::ceiling) : rf->GetPlaneTexZ(sector_t::ceiling)));
	}
	else if (twoSidedMid)
	{
		// [rc4l] A two-sided middle is not pegged the way the other parts are: DoMidTexture places it
		// directly, from the higher floor upward when unpegged-bottom and from the lower ceiling
		// downward otherwise, and clips it to the opening. It writes a texturebottom/texturetop pair
		// rather than going through DoTexture at all.
		pegged = false;
		if (lineFlags & ML_DONTPEGBOTTOM)
		{
			const float bottomRef = FIXED2FLOAT(MAX(rf->GetPlaneTexZ(sector_t::floor),
				rb->GetPlaneTexZ(sector_t::floor)));
			refCeil = bottomRef + th;
			refFloor = bottomRef;
		}
		else
		{
			refCeil = FIXED2FLOAT(MIN(rf->GetPlaneTexZ(sector_t::ceiling),
				rb->GetPlaneTexZ(sector_t::ceiling)));
			refFloor = refCeil - th;
		}
	}
	else   // one-sided middle
	{
		pegged = (lineFlags & ML_DONTPEGBOTTOM) != 0;
		refCeil = FIXED2FLOAT(rf->GetPlaneTexZ(sector_t::ceiling));
		refFloor = FIXED2FLOAT(rf->GetPlaneTexZ(sector_t::floor));
	}

	const float rowOfs = FIXED2FLOAT(tci.RowOffset(
		seg->sidedef->GetTextureYOffset((side_t::ETexpart)texpos)));
	const float texTop = ComputeTextureTop(refCeil, refFloor, th, pegged, rowOfs, vOffset);

	// Both ends, so a slope stays a slope. Evaluated at the LINE's vertices rather than the seg's,
	// because that is the span GL draws -- see the fracleft note below.
	const vertex_t *lv1 = seg->linedef->v1, *lv2 = seg->linedef->v2;
	if (seg->sidedef != seg->linedef->sidedef[0]) { lv1 = seg->linedef->v2; lv2 = seg->linedef->v1; }
	const fixed_t px[2] = { lv1->x, lv2->x };
	const fixed_t py[2] = { lv1->y, lv2->y };
	// Both ends' heights first: the upper's and the lower's clamps are decided by the pair, not by
	// each end on its own -- see SpanBothEnds.
	WallHeights hEnd[2];
	HeightsAt(seg, px[0], py[0], hEnd[0]);
	HeightsAt(seg, px[1], py[1], hEnd[1]);
	float spanTop[2] = { 0.f, 0.f }, spanBottom[2] = { 0.f, 0.f };
	if (twoSidedMid)
	{
		// [rc4l] The hanging texture, and then GL's own clip -- which is not the opening.
		//
		// texTop already carries the pegging and the row offset; the texture hangs its own height
		// below it. What survives is decided by ComputeMiddleClip, which asks the four planes the
		// questions DoMidTexture asks rather than assuming the gap between the two nearest ones.
		MidTextureClip c;
		c.texTop = texTop;
		c.texBottom = texTop - th;
		const side_t *sd = seg->sidedef;
		c.hasUpper = NonNullTexture(sd->GetTexture(side_t::top));
		c.hasLower = NonNullTexture(sd->GetTexture(side_t::bottom));
		c.frontCeilingSky = seg->frontsector->GetTexture(sector_t::ceiling) == skyflatnum;
		c.backCeilingSky = seg->backsector->GetTexture(sector_t::ceiling) == skyflatnum;
		c.wrap = (lineFlags & ML_WRAP_MIDTEX) != 0 || (sd->Flags & WALLF_WRAP_MIDTEX) != 0;
		// GL clips to planes unless both sides really are the same sector with nothing forcing it.
		c.clipToPlanes = (rf != rb) || c.wrap || rf->GetHeightSec() != NULL;
		WallPart p0, p1;
		ComputeMiddleClip(hEnd[0], hEnd[1], c, p0, p1);
		spanTop[0] = p0.top; spanBottom[0] = p0.bottom;
		spanTop[1] = p1.top; spanBottom[1] = p1.bottom;
	}
	else if (!SpanBothEnds(renderType, hEnd[0], hEnd[1], spanTop, spanBottom))
		{ g_fbNoSpan++; g_fellBack++; return false; }

	// GL's own test: nothing to draw only when BOTH ends are empty.
	if (!(spanTop[0] > spanBottom[0] || spanTop[1] > spanBottom[1]))
		{ g_fbNoSpan++; g_fellBack++; return false; }

	// [rc4l] And a wall that pinches out is CUT SHORT, not turned into a triangle -- see
	// ComputeWallPinch. The fractions come back with it, and both the ends and the horizontal
	// texture coordinate move to where the wall actually stops.
	WallPinch pinch;
	ComputeWallPinch(spanTop, spanBottom, pinch);
	for (int e = 0; e < 2; e++)
	{
		out.ztop[e] = pinch.ztop[e];
		out.zbottom[e] = pinch.zbottom[e];
		out.vTop[e] = ComputeWallV(pinch.ztop[e], texTop, th);
		out.vBottom[e] = ComputeWallV(pinch.zbottom[e], texTop, th);
	}

	// [rc4l] CheckTexturePosition, which is where a wall's v coordinates actually END UP.
	//
	// GL runs this on every wall DoTexture makes, immediately before putting it in the render list:
	// subtract floor(min(uplft.v, uprgt.v)) from all four, so the quad starts inside its first copy
	// of the texture. On a wall that REPEATS this is invisible -- a whole copy up or down looks the
	// same -- which is why the derivation could go without it and still read 100% on geometry.
	//
	// On a wall that CLAMPS it is the whole picture: outside [0,1] the clamp holds the edge texel and
	// the wall comes out as a smear of one row of pixels. 159 pieces on dbab04 sat a whole texture
	// away from the capture, 75 of them on clamping walls, and that is the half-percent the map-bake
	// A/B would not give up.
	//
	// Not for a two-sided middle: DoMidTexture writes its coordinates directly and never calls this.
	// Not for a canvas texture, which GL also skips.
	if (fua_surface_vshift && !twoSidedMid && !((FMaterial *)mat)->tex->bHasCanvas)
	{
		const float lead = (out.vTop[0] < out.vTop[1]) ? out.vTop[0] : out.vTop[1];
		const float sub = floorf(lead);
		out.vTop[0] -= sub; out.vTop[1] -= sub;
		out.vBottom[0] -= sub; out.vBottom[1] -= sub;
	}
	// [rc4l] And the horizontal, for everything that is not a polyobject.
	//
	// GLWall::Process sets fracleft 0 and fracright 1 for an ordinary wall and takes the linedef's
	// vertices, so the two edges START as the sidedef's x offset and that plus the line's texel
	// length -- and then SetWallCoordinates moves whichever end pinched out, which is what `pinch`
	// carries. A polyobject is drawn per seg with real fractions and is left to the capture.
	if (!(seg->sidedef->Flags & WALLF_POLYOBJ))
	{
		const float ul = tci.FloatToTexU(FIXED2FLOAT(tci.TextureOffset(
			seg->sidedef->GetTextureXOffset((side_t::ETexpart)texpos))));
		const float texLength = tci.FloatToTexU(seg->sidedef->TexelLength);
		out.uLeft = ul + texLength * pinch.fracLeft;
		out.uRight = ul + texLength * pinch.fracRight;
		out.hasU = true;
	}
	// [rc4l] The ends, from the linedef, ordered by side -- moved in to where the wall was cut short,
	// exactly as the u coordinates were. GLWall::Process draws the whole line, so these start as the
	// line's vertices and not the seg's; see the fracleft note above.
	{
		const vertex_t *lv1 = seg->linedef->v1, *lv2 = seg->linedef->v2;
		if (seg->sidedef != seg->linedef->sidedef[0]) { lv1 = seg->linedef->v2; lv2 = seg->linedef->v1; }
		const float ax = FIXED2FLOAT(lv1->x), ay = FIXED2FLOAT(lv1->y);
		const float bx = FIXED2FLOAT(lv2->x), by = FIXED2FLOAT(lv2->y);
		out.x1 = ax + pinch.fracLeft * (bx - ax);
		out.y1 = ay + pinch.fracLeft * (by - ay);
		out.x2 = ax + pinch.fracRight * (bx - ax);
		out.y2 = ay + pinch.fracRight * (by - ay);
	}
	out.material = mat;
	// The BASE texture, so an animated one keeps animating -- see MeshPiece::baseTex. The sector's
	// flat when that is what is being drawn, so the fallback animates too.
	// [rc4l] The BASE texture, and TexMan[] is not it.
	//
	// operator[] applies the animation translation, so it hands back whichever frame is showing right
	// now -- which changes every few tics and makes "the base texture" a moving target. Everything
	// downstream keys a batch on this, so an animated wall then looked like it had moved to a
	// different batch several times a second: 311,437 rebatches on dbab04, each one a full scene
	// rebuild. ByIndex skips the translation, which is what MeshPiece::baseTex says it wants.
	out.baseTex = TexMan.ByIndex(seg->sidedef->GetTexture((side_t::ETexpart)texpos).GetIndex());
	if (out.baseTex == NULL || ((FTexture *)out.baseTex)->Name.Len() == 0)
		out.baseTex = (mat != NULL) ? (const void *)((FMaterial *)mat)->tex : NULL;
	out.valid = true;
	g_derived++;
	return true;
}

}} // namespace zx::surfaces
