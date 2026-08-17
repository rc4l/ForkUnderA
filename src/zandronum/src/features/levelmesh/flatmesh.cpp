// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/computation/wallbatch_compute.h"

#include "r_defs.h"
#include "r_state.h"
#include "gl/scene/gl_wall.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/data/gl_data.h"   // getExtraLight
#include "tarray.h"

namespace zx { namespace levelmesh {

// [rc4l] Keyed on (subsector, plane) so a subsector's floor and ceiling are each baked once. The
// range is kept so a re-bake at the same size overwrites in place, exactly like the wall path --
// which is what stops a moving sector from growing the buffer forever.
struct FlatKey
{
	const subsector_t *sub;
	bool ceiling;
	// [rc4l] The plane's own sector, which for a 3D floor is its MODEL sector.
	//
	// Keying on (subsector, ceiling) alone gives one slot per subsector per side, and a sector with
	// 3D floors draws several planes through the same subsector -- its own floor plus a top and a
	// bottom for every 3D floor above it. They all landed in that one slot and overwrote each other,
	// so only whichever drew last survived. dbab01 has 138 such sectors and 276 3D-floor planes.
	//
	// The model sector is the right discriminator rather than the plane's height: a 3D floor used as
	// a lift changes height every tic, and keying on that would allocate a fresh range per frame and
	// run the arena away.
	const sector_t *model;
	// Which plane OF that model sector: a 3D floor's top and bottom share the model, so the model
	// alone still collides and the surface flickers between the two textures.
	int whichPlane;
	MeshRange range;
};
static TArray<FlatKey> g_flats;

// [rc4l] How many registrations are 3D-floor planes rather than a subsector's own floor or ceiling.
// Zero would mean the planes never reach the capture at all, which is a different problem from them
// arriving and overwriting each other -- and the two look identical in a screenshot.
static int g_flat3D = 0, g_flatOwn = 0;
void GetFlatStats(int &own, int &threeD) { own = g_flatOwn; threeD = g_flat3D; }

void ClearFlats()
{
	g_flat3D = g_flatOwn = 0;
	g_flats.Clear();
}

int FlatPieceCount() { return (int)g_flats.Size(); }

void RegisterFlatSubsector(const GLFlat &flat, subsector_t *sub, bool ceiling)
{
	if (sub == NULL || sub->numlines < 3) return;
	if (flat.gltexture == NULL) return;
	if (sub->sector != NULL && flat.mMeshModel != NULL && flat.mMeshModel != sub->sector) g_flat3D++;
	else g_flatOwn++;

	// [rc4l] Cap matches the wall path's staging array; a subsector with more edges than this is
	// vanishingly rare and is simply left to the GL renderer.
	static FFlatVertex fan[GLWall::MAX_BATCH_FAN_VERTICES];
	static FFlatVertex tris[GLWall::MAX_BATCH_FAN_VERTICES * 3];
	const int n = (int)sub->numlines;
	if (n > GLWall::MAX_BATCH_FAN_VERTICES) return;

	// Mirrors GLFlat::DrawSubsector, including the sloped/flat split and the /64 uv convention.
	const secplane_t &p = flat.plane.plane;
	const bool sloped = (p.a | p.b) != 0;
	const float zc = sloped ? 0.0f : (FIXED2FLOAT(p.Zat0()) + flat.dz);
	for (int k = 0; k < n; k++)
	{
		vertex_t *vt = sub->firstline[k].v1;
		fan[k].x = vt->fx;
		fan[k].y = vt->fy;
		fan[k].z = sloped ? (float)(p.ZatPoint(vt->fx, vt->fy) + flat.dz) : zc;
		fan[k].u = vt->fx / 64.f;
		fan[k].v = -vt->fy / 64.f;
	}

	const int triVerts = ComputeFanTriangleVertexCount(n);
	if (triVerts <= 0) return;
	int w = 0;
	for (int t = 0; t < n - 2; t++)
		for (int c = 0; c < 3; c++)
			tris[w++] = fan[ComputeFanTriangleVertex(n, t, c)];

	FlatKey *slot = NULL;
	for (unsigned i = 0; i < g_flats.Size(); i++)
		if (g_flats[i].sub == sub && g_flats[i].ceiling == ceiling &&
			g_flats[i].model == flat.mMeshModel && g_flats[i].whichPlane == flat.mMeshWhichPlane)
		{ slot = &g_flats[i]; break; }
	if (slot == NULL)
	{
		FlatKey k;
		k.sub = sub;
		k.ceiling = ceiling;
		k.model = flat.mMeshModel;
		k.whichPlane = flat.mMeshWhichPlane;
		k.range.offset = 0;
		k.range.count = 0;
		slot = &g_flats[g_flats.Push(k)];
	}

	if (!MeshStore(slot->range, tris, w)) return;

	MeshPiece mp;
	mp.range = slot->range;
	mp.material = flat.gltexture;
	mp.dynLightIndex = flat.mMeshLightIndex;
	// [rc4l] Plane normal, mapped into the mesh's (x, z-up, y) space: a secplane is
	// a*x + b*y + c*z + d = 0 with z up, so (a, b, c) becomes (a, c, b) here. A floor gives
	// (0, +1, 0) and a ceiling (0, -1, 0), which is exactly the side test the lights need.
	{
		const secplane_t &pl = flat.plane.plane;
		const float nx = FIXED2FLOAT(pl.a), ny = FIXED2FLOAT(pl.b), nz = FIXED2FLOAT(pl.c);
		const float len = sqrtf(nx*nx + ny*ny + nz*nz);
		if (len > 0.0001f) { mp.normX = nx / len; mp.normY = nz / len; mp.normZ = ny / len; }
	}
	mp.lightLevel = flat.lightlevel;
	mp.lightColor = flat.Colormap.LightColor.d;
	mp.fadeColor = flat.Colormap.FadeColor.d;
	// GLFlat::Draw's GLPASS_PLAIN arm, with its `rel = getExtraLight()`.
	CaptureShading(flat.lightlevel, getExtraLight(), flat.Colormap, mp);
	// [rc4l] Base plane texture, so animated flats (nukage, lava, blood) keep flowing. The render
	// sector is the one whose textures are actually drawn -- `sub->sector` can be faked away by
	// deep-water and heightsec tricks.
	{
		const sector_t *ts = sub->render_sector ? sub->render_sector : sub->sector;
		if (ts != NULL)
			mp.baseTex = TexMan[ts->GetTexture(ceiling ? sector_t::ceiling : sector_t::floor)];
	}
	MeshRegisterPiece(mp);
}

// ---------------------------------------------------------------------------
// Sprites
// ---------------------------------------------------------------------------

// [rc4l] Sprites go into the DYNAMIC stream, rebuilt every frame -- see staticmesh.h. They are
// billboards built for one viewpoint, so they are not level geometry and must never be baked.
static int g_spritesThisFrame = 0;

void ClearSprites() { g_spritesThisFrame = 0; }
int SpritePieceCount() { return g_spritesThisFrame; }

void RegisterSprite(const GLSprite &spr)
{
	if (spr.gltexture == NULL) return;

	// [rc4l] GLSprite emits a 4-vertex TRIANGLE STRIP; the mesh is triangle lists, so expand to
	// (0,1,2) and (2,1,3) -- which preserves the strip's winding rather than flipping the second
	// triangle.
	FFlatVertex q[4];
	q[0].Set(spr.x1, spr.z1, spr.y1, spr.ul, spr.vt);
	q[1].Set(spr.x2, spr.z1, spr.y2, spr.ur, spr.vt);
	q[2].Set(spr.x1, spr.z2, spr.y1, spr.ul, spr.vb);
	q[3].Set(spr.x2, spr.z2, spr.y2, spr.ur, spr.vb);

	FFlatVertex tris[6];
	tris[0] = q[0]; tris[1] = q[1]; tris[2] = q[2];
	tris[3] = q[2]; tris[4] = q[1]; tris[5] = q[3];

	MeshPiece mp;
	mp.range.offset = 0;
	mp.range.count = 0;
	mp.material = spr.gltexture;
	// [rc4l] GLSprite has no light index -- sprite lighting is folded into its vertex colour by
	// gl_SetDynSpriteLight before the draw, so it is already in colorR/G/B.
	mp.dynLightIndex = -1;
	mp.lightLevel = spr.lightlevel;
	mp.lightColor = spr.Colormap.LightColor.d;
	mp.fadeColor = spr.Colormap.FadeColor.d;
	// GLSprite::Draw's own `rel`: fullbright sprites take no extra light.
	CaptureShading(spr.lightlevel, spr.fullbright ? 0 : getExtraLight(), spr.Colormap, mp);

	// [rc4l] Classify the render style into the handful of blends a backend actually needs.
	//
	// gl_GetRenderStyle resolves the full matrix into GL blend enums, which would then have to be
	// mapped back into Diligent's. These four cases cover what Doom content uses: opaque/masked,
	// normal translucency, additive (plasma, fireballs, explosions) and the fuzz shadow. Anything
	// exotic falls into normal translucency, which is wrong but visible rather than invisible.
	mp.translation = spr.translation;
	mp.alpha = spr.trans;
	if (spr.RenderStyle.BlendOp == STYLEOP_Shadow)
		mp.blendMode = 3;
	else if (spr.RenderStyle.BlendOp == STYLEOP_Add && spr.RenderStyle.DestAlpha == STYLEALPHA_One)
		mp.blendMode = 2;
	else if (spr.trans < 1.f - 1.f/256.f || spr.RenderStyle.BlendOp != STYLEOP_Add)
		mp.blendMode = (spr.trans < 1.f - 1.f/256.f) ? 1 : 0;
	else
		mp.blendMode = 0;

	// Sprite centre, for the back-to-front sort.
	mp.sortX = spr.x; mp.sortY = spr.y; mp.sortZ = spr.z;

	DynAppend(tris, 6, mp);
	g_spritesThisFrame++;
}

}} // namespace zx::levelmesh
