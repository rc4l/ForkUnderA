// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/levelmesh/walldecals.h"
#include "features/levelmesh/computation/walldecal_compute.h"
#include "features/levelmesh/computation/flatmesh_compute.h"   // [rc4l] ComputeMidTextureTranslucent
#include "features/levelmesh/flatmesh.h"                       // [rc4l] RegisterDecal
#include "features/levelmesh/wallcache.h"                      // [rc4l] the bake's parts and bands
#include "features/materials/materials.h"
#include "features/sprites/spritebuild.h"                      // [rc4l] PointDynamicLight
#include "features/hwrender/spriteview.h"                      // [rc4l] DynamicLightCount

#include "a_sharedglobal.h"   // [rc4l] DBaseDecal
#include "actor.h"            // [rc4l] RF_*
#include "doomdata.h"         // [rc4l] ML_*
#include "r_defs.h"
#include "r_state.h"
#include "r_data/renderstyle.h"
#include "textures/textures.h"
#include "c_cvars.h"
#include "features/rendersettings/rendersettings.h"   // [rc4l] the render settings it reads

EXTERN_CVAR(Bool, gl_lights)
EXTERN_CVAR(Bool, gl_light_sprites)

namespace zx { namespace levelmesh {

namespace {

// [rc4l] The planes' texture heights a decal on seg's part hangs from, the front plane a 3D floor's model sector for a mid-clipped one, as DrawDecal picks them.
DecalHeightIn HeightIn(const DBaseDecal *decal, const seg_t *seg, int part)
{
	const sector_t *front = (decal->RenderFlags & RF_CLIPMASK) == RF_CLIPMID ? decal->Sector : seg->frontsector;
	const sector_t *back = seg->backsector;
	DecalHeightIn in;
	in.rel = int((decal->RenderFlags & RF_RELMASK) >> 8);
	in.part = part == kPartUpper ? kDecalPartUpper : part == kPartLower ? kDecalPartLower : kDecalPartMiddle;
	in.unpegTop = (seg->linedef->flags & ML_DONTPEGTOP) != 0;
	in.unpegBottom = (seg->linedef->flags & ML_DONTPEGBOTTOM) != 0;
	in.z = FIXED2DBL(decal->Z);
	in.frontCeiling = FIXED2DBL(front->GetPlaneTexZ(sector_t::ceiling));
	in.frontFloor = FIXED2DBL(front->GetPlaneTexZ(sector_t::floor));
	in.backCeiling = back != NULL ? FIXED2DBL(back->GetPlaneTexZ(sector_t::ceiling)) : 0.0;
	in.backFloor = back != NULL ? FIXED2DBL(back->GetPlaneTexZ(sector_t::floor)) : 0.0;
	return in;
}

// [rc4l] One decal on one band of one part, registered as DrawDecal would draw it there.
void BuildDecal(DBaseDecal *decal, side_t *side, const seg_t *seg, int part, const zx::surfaces::DerivedWallSpan &d,
	const zx::surfaces::DerivedWallLight &dl, const FColormap &partColormap, const PartBand &band)
{
	if (decal->RenderFlags & RF_INVISIBLE) return;
	FTexture *texture = TexMan[decal->PicNum];
	if (texture == NULL) return;
	const zx::materials::MaterialRef mat = zx::materials::MaterialForTexture(texture, true);
	zx::materials::MaterialInfo info;
	if (!zx::materials::DescribeMaterial(mat, info)) return;

	double zpos;
	if (!ComputeDecalHeight(HeightIn(decal, seg, part), zpos)) return;
	const float scaleX = FIXED2FLOAT(decal->ScaleX), scaleY = FIXED2FLOAT(decal->ScaleY);
	DecalPicture pic;
	pic.width = info.renderWidth * scaleX;
	pic.height = info.renderHeight * scaleY;
	pic.leftOffset = info.leftOffset * scaleX;
	pic.topOffset = info.topOffset * scaleY;
	pic.scaleX = scaleX;
	pic.materialWidth = (float)info.materialWidth;
	pic.leftDistance = float(decal->LeftDistance);
	pic.zpos = zpos;
	pic.flipx = (decal->RenderFlags & RF_XFLIP) != 0;
	pic.flipy = (decal->RenderFlags & RF_YFLIP) != 0;
	DecalWall wall;
	wall.x1 = d.x1; wall.y1 = d.y1; wall.x2 = d.x2; wall.y2 = d.y2;
	wall.fracLeft = d.fracLeft;
	wall.fracRight = d.fracRight;
	wall.texelLength = (float)side->TexelLength;
	for (int e = 0; e < 2; e++) { wall.ztop[e] = band.band.ztop[e]; wall.zbottom[e] = band.band.zbottom[e]; }
	DecalCorner dv[4];
	double top;
	if (!ComputeWallDecalQuad(wall, pic, dv, top)) return;

	const bool fullbright = (decal->RenderFlags & RF_FULLBRIGHT) != 0;
	const int light = fullbright ? 255 : band.lightLevel;
	const int rel = fullbright ? 0 : dl.relLight + getExtraLight();
	FColormap cm = partColormap;
	if (band.lightEntry >= 0) cm.CopyLightColor(band.lightFrom);
	if (gl_fixedcolormap) cm.Clear();
	if (glset.nocoloredspritelighting) cm.Decolorize();

	float dyn[3];
	const bool lit = gl_lights && zx::hwrender::SpriteView().DynamicLightCount() && !gl_fixedcolormap && gl_light_sprites;
	if (lit)
	{
		fixed_t x, y;
		decal->GetXY(side, x, y);
		zx::sprites::PointDynamicLight(x, y, FLOAT2FIXED(top), seg->Subsector, dyn);
	}
	MeshVertex quad[4];
	for (int i = 0; i < 4; i++) quad[i].Set(dv[i].x, dv[i].z, dv[i].y, dv[i].u, dv[i].v);
	const FRenderStyle &style = decal->RenderStyle;
	const bool redAlpha = (style.Flags & STYLEF_RedIsAlpha) != 0;
	RegisterDecal(quad, mat, decal->Translation, style.BlendOp == STYLEOP_Shadow,
		style.BlendOp == STYLEOP_Add && style.DestAlpha == STYLEALPHA_One, FIXED2FLOAT(decal->Alpha),
		light, rel, cm, redAlpha, redAlpha ? (unsigned int)decal->AlphaColor : 0xffffffu,
		(quad[0].x + quad[3].x) * 0.5f, (quad[0].z + quad[3].z) * 0.5f, (quad[0].y + quad[3].y) * 0.5f, lit ? dyn : NULL);
}

} // namespace

void BuildWallDecals(const int *sideList, int count)
{
	for (int i = 0; i < count; i++)
	{
		side_t *side = &sides[sideList[i]];
		if (side->AttachedDecals == NULL) continue;
		const int segIndex = SideOwnerSeg(sideList[i]);
		if (segIndex < 0) continue;
		const seg_t *seg = &segs[segIndex];
		if (seg->linedef == NULL || seg->frontsector == NULL || LineDrawsNoWall(seg)) continue;
		// [rc4l] Lit as the bake lit the wall, which bakes none it cannot light.
		zx::surfaces::DerivedWallLight dl;
		FDynamicColormap *cmFrom = NULL;
		if (!zx::surfaces::BuildDerivedWallLight(seg, dl, cmFrom) || cmFrom == NULL) continue;
		FColormap partColormap;
		partColormap = cmFrom;
		for (int part = 0; part < kWallParts; part++)
		{
			zx::surfaces::DerivedWallSpan d;
			bool twoSidedMiddle;
			if (!DerivedPart(seg, part, d, twoSidedMiddle) || d.flood) continue;
			if (twoSidedMiddle)
			{
				const line_t *ln = seg->linedef;
				const float lineAlpha = FIXED2FLOAT(ln->Alpha);
				const bool additive = lineAlpha != 0.f && (ln->flags & ML_ADDTRANS) != 0;
				zx::materials::MaterialInfo info;
				const bool masked = zx::materials::DescribeMaterial(d.material, info) && info.masked;
				if (!ComputeDecalsOnPart(true, ComputeMidTextureTranslucent(lineAlpha, additive, zx::materials::MaterialTransparent(d.material)), masked))
					continue;
			}
			PartBand bands[kBandsPerPart];
			const int n = DerivedPartBands(seg, d, dl, bands);
			// [rc4l] Each band is a wall piece of its own in GL, which draws every decal of the side on it in turn.
			for (int b = 0; b < n; b++)
				for (DBaseDecal *decal = side->AttachedDecals; decal != NULL; decal = decal->WallNext)
					BuildDecal(decal, side, seg, part, d, dl, partColormap, bands[b]);
		}
	}
}

}} // namespace zx::levelmesh
