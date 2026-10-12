// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/captureshading.h"   // [rc4l] CaptureShading, the light model for a GL colormap
#include "features/sprites/spritebuild.h"   // [rc4l] NoteGLSprite, for fua_sprite_check
#include "features/hwrender/computation/lightside_compute.h"   // the plane bake, tested off-engine
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/modelmesh.h"
#include "features/levelmesh/fakeplanes.h"   // [rc4l] GL's missing-texture fills, baked as fake planes
#include "features/levelmesh/wallcache.h"
#include "features/levelmesh/computation/wallcache_compute.h"   // [rc4l] ComputeScrollInRange    // [rc4l] RebakeTransferSegs, for a view area change
#include "features/levelmesh/computation/wallbatch_compute.h"
#include "features/levelmesh/computation/flatmesh_compute.h"
#include "features/hwrender/computation/slot_compute.h"
#include "features/levelmesh/computation/packedcolor_compute.h"
#include "features/horizon/horizon.h"
#include "features/horizon/computation/horizon_compute.h"

#include "r_defs.h"
#include "r_state.h"
#include "gl/scene/gl_wall.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/data/gl_data.h"   // getExtraLight
#include "gl/textures/gl_material.h"   // FMaterial::TextureWidth, for the plane UV transform
#include "gl/utility/gl_convert.h"      // ANGLE_TO_FLOAT
#include "tarray.h"
#include "p_3dfloors.h"   // F3DFloor, P_GetPlaneLight
#include "r_sky.h"   // skyflatnum
#include "gl/renderer/gl_lightdata.h"   // gl_ClampLight
#include "gl/renderer/gl_renderer.h"    // [rc4l] gl_FakeFlat and its area_t, for a height transfer's planes
#include "gl/renderer/gl_renderstate.h"   // gl_RenderState.GetDynLight, for the sprite light fold
#include "c_dispatch.h"   // CCMD, for fua_sprite_sweep
#include "c_cvars.h"
#include "r_utility.h"   // R_PointInSubsector, for PieceStackId
#include "d_player.h"    // players, for the viewer a mirror walk draws
#include "doomstat.h"    // consoleplayer
#include "features/hwrender/hud2d.h"   // StandaloneActive


// [rc4l] Print every new flat-mesh key. See RegisterFlatSubsector: a key that misses when it should
// have hit is a surface about to be stored, and drawn, twice.
CVAR(Bool, fua_flat_keylog, false, 0)

// [rc4l] At FILE scope, not inside the namespace: p_mobj.cpp externs it to say whether a missile
// managed to stick its mark, and a namespaced cvar is a different symbol that will not link.
CVAR(Bool, fua_decal_log, false, 0)

// [rc4l] The sky as SURFACES that write depth, rather than a background painted first and drawn
// over. Off restores the old behaviour. See docs/skybleed-scope.md.
// [rc4l] The sky as SURFACES that write depth, taken from GL, rather than a background painted
// first and drawn over. It cuts the sky at the horizon, which is what removes the bright streak
// along the waterline: measured 3.86 against GL 1.21 on that row, where the background blit
// reads 30+. Off restores the old behaviour. See docs/skybleed-scope.md.
CVAR(Bool, fua_mesh_sky, true, CVAR_ARCHIVE)

EXTERN_CVAR(Bool, fua_surface_mapbake_auto)
EXTERN_CVAR(Bool, fua_mesh_rescroll)

namespace zx { namespace levelmesh {

// How often the flat cache answered without rebuilding anything. Printed by fua_wallcache_stats,
// because a cache nobody can see the hit rate of is a cache nobody can tell has stopped working.
static int g_flatHits = 0, g_flatRebuilds = 0, g_flatReshades = 0, g_flatRescrolls = 0;
void GetFlatCacheStats(int &hits, int &rebuilds) { hits = g_flatHits; rebuilds = g_flatRebuilds; }
int GetFlatReshades() { return g_flatReshades; }
int GetFlatRescrolls() { return g_flatRescrolls; }

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
	// [rc4l] Next slot for the same subsector. See g_flatHead.
	int next;
	bool fake;   // [rc4l] another sector's plane GL's missing-texture fill draws here, squashed when the fill moves away

	// [rc4l] Everything the stored geometry and the stored piece were built from.
	//
	// Walls have had a cache since the beginning; flats never got one, so every visible flat was
	// rebuilt from scratch every frame -- its vertices recomputed, its UVs run through a rotation,
	// its triangles restaged and memcpy'd into the mesh. Sunder MAP16 does that 3406 times a frame.
	//
	// A floor changes when it MOVES, when its texture animates, or when the light on it changes.
	// The rest of the time the answer is the one already in the buffer.
	FlatStamp stamp;
	bool stamped;
	bool scrolled;   // [rc4l] the piece carries a scroll from the offsets in stamp, which are the ones its vertices were baked with
};
static TArray<FlatKey> g_flats;

// [rc4l] Which slots belong to which subsector, so finding one is not a walk of all of them.
//
// This lookup was a linear scan over every flat in the level, run once per flat per FRAME. That is
// quadratic in a quantity Sunder MAP10 puts at 3069, and it was costing 4.7 ms a frame -- more than
// the entire stock GL frame it was riding on. Nothing in the picture said so: the time sat inside
// the flat render clock, which reads as "flats are expensive" rather than "our capture is".
//
// A subsector holds a handful of planes -- its own floor and ceiling plus two per 3D floor above it
// -- so a head index per subsector and a chain through the slots turns the walk into a few steps.
static TArray<int> g_flatHead;

// [rc4l] How many registrations are 3D-floor planes rather than a subsector's own floor or ceiling.
// Zero would mean the planes never reach the capture at all, which is a different problem from them
// arriving and overwriting each other -- and the two look identical in a screenshot.
static int g_flat3D = 0, g_flatOwn = 0;
static int g_decalsThisFrame = 0;
int DecalsThisFrame() { return g_decalsThisFrame; }
void ResetDecalCount() { g_decalsThisFrame = 0; }
void GetFlatStats(int &own, int &threeD) { own = g_flatOwn; threeD = g_flat3D; }

void ClearFlats()
{
	g_flat3D = g_flatOwn = 0;
	ClearFakePlanes();

	// [rc4l] Give the ranges back before forgetting the keys that own them.
	//
	// Clearing the key table alone forgets WHERE each flat lives without telling the mesh, so the
	// next registration misses its key, MeshStore bump-allocates a fresh range, and
	// MeshRegisterPiece -- which dedupes by range OFFSET -- appends a second piece instead of
	// replacing the first. The old piece is still in the list and still points at valid geometry, so
	// every re-registered flat ends up drawn twice, coplanar with itself.
	//
	// Two identical copies are invisible: same texture, same light, same depth. They appear the
	// moment the copies are shaded DIFFERENTLY -- which is what a dynamic light does -- and then the
	// pair fights for the depth buffer in bands following the contours of constant depth. On a floor
	// seen from above those contours are horizontal, which is why it was reported as horizontal
	// stripes across a lit floor, and only ever under a light.
	//
	// InvalidateAll calls this and a gl_wallmesh toggle calls InvalidateAll, so the world's flats
	// were duplicated on every toggle -- including every time a test harness set gl_wallmesh 1.
	for (unsigned i = 0; i < g_flats.Size(); i++) MeshRetireRange(g_flats[i].range);
	g_flats.Clear();
	// The chains point into the table that was just emptied.
	for (unsigned i = 0; i < g_flatHead.Size(); i++) g_flatHead[i] = -1;
}

int FlatPieceCount() { return (int)g_flats.Size(); }

// [rc4l] Read the captured flats, so a derivation can be checked against them.
//
// features/surfaces works out where a plane is from the map; this is what the capture recorded for
// the same plane. Comparing the two on real levels is the only honest way to promote one over the
// other, and it is read-only -- the verifier looks, it never bakes.
bool CachedFlat(int index, const subsector_t **sub, bool *ceiling, const sector_t **model,
	int *whichPlane, MeshRange *range)
{
	if ((unsigned)index >= g_flats.Size()) return false;
	const FlatKey &k = g_flats[index];
	if (sub) *sub = k.sub;
	if (ceiling) *ceiling = k.ceiling;
	if (model) *model = k.model;
	if (whichPlane) *whichPlane = k.whichPlane;
	if (range) *range = k.range;
	return true;
}

// [rc4l] What a flat is MADE OF, separated from where those values came from.
//
// The fields are named after GLFlat's so the storing half below reads unchanged, which is the point:
// the map-driven bake fills exactly the same set and the code that turns them into geometry does not
// get written a second time. Everything here except the material and the dynamic light index is map
// data -- a sector's plane, its offsets, its scale, its rotation, its light.
struct FlatSource
{
	GLSectorPlane plane;
	float         dz;
	FMaterial    *gltexture;
	int           lightlevel;
	FColormap     Colormap;
	float         alpha;
	ERenderStyle  renderstyle;
	int           mMeshLightIndex;
	const sector_t *mMeshModel;
	int           mMeshWhichPlane;
	int           mirrorSector = -1;   // [rc4l] see MeshPiece::mirrorSector
	bool          mirrorCeiling = false;
	bool          fake = false;        // [rc4l] see FlatKey::fake
};

static void StoreFlat(const FlatSource &flat, subsector_t *sub, bool ceiling)
{
	if (sub == NULL || sub->numlines < 3) return;
	if (flat.gltexture == NULL) return;
	// [rc4l] The subsector's own plane, by its sector or the render sector the bake draws it with, rather than a 3D floor's.
	const bool own = ComputeIsOwnFlat(flat.mMeshModel != NULL ? flat.mMeshModel->sectornum : -1,
		sub->sector != NULL ? sub->sector->sectornum : -1, sub->render_sector != NULL ? sub->render_sector->sectornum : -1);
	if (own) g_flatOwn++;
	else g_flat3D++;

	// The subsector's own chain, not every flat in the level.
	const int subIndex = (int)(sub - subsectors);
	if ((unsigned)subIndex >= g_flatHead.Size())
	{
		const unsigned was = g_flatHead.Size();
		g_flatHead.Resize((unsigned)numsubsectors > 0 ? (unsigned)numsubsectors : (unsigned)subIndex + 1);
		for (unsigned i = was; i < g_flatHead.Size(); i++) g_flatHead[i] = -1;
	}

	FlatKey *slot = NULL;
	for (int i = g_flatHead[subIndex]; i >= 0; i = g_flats[i].next)
		if (g_flats[i].ceiling == ceiling && g_flats[i].model == flat.mMeshModel &&
			g_flats[i].whichPlane == flat.mMeshWhichPlane)
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
		k.next = g_flatHead[subIndex];
		k.stamped = false;
		k.scrolled = false;
		k.fake = flat.fake;
		const unsigned pushed = g_flats.Push(k);
		g_flatHead[subIndex] = (int)pushed;
		slot = &g_flats[pushed];
		// [rc4l] Say when a NEW flat slot is created, and with what key.
		//
		// A slot that should have been found and was not is a duplicate surface: the same floor stored
		// twice under two keys, drawn twice, fighting for the depth buffer. The key is four fields and
		// the count says nothing about which of them disagreed, so it is printed.
		if ( fua_flat_keylog )
		{
			Printf( "flatkey NEW: sub %d  ceiling %d  model %d  whichPlane %d  (slots %u)\n",
				sub ? (int)(sub - subsectors) : -1, (int)ceiling,
				flat.mMeshModel ? (int)(flat.mMeshModel - sectors) : -1,
				flat.mMeshWhichPlane, g_flats.Size() );
		}
	}


	// [rc4l] Has anything this flat is made of actually changed?
	//
	// Everything below rebuilds the surface from scratch: a vertex per edge, a rotation per vertex,
	// a triangle fan staged and copied into the mesh, then a piece rebuilt and re-registered. A floor
	// changes when it moves, when its texture animates, or when the light on it changes -- and the
	// rest of the time all of that arrives at the bytes already sitting in the buffer.
	FlatStamp now;
	{
		const secplane_t &pl = flat.plane.plane;
		now.pa = pl.a.Raw(); now.pb = pl.b.Raw(); now.pc = pl.c.Raw(); now.pd = pl.d.Raw();
		now.dz = flat.dz;
		now.xoffs = flat.plane.xoffs.Raw(); now.yoffs = flat.plane.yoffs.Raw();
		now.xscale = flat.plane.xscale.Raw(); now.yscale = flat.plane.yscale.Raw();
		now.angle = flat.plane.angle;
		now.material = flat.gltexture;
		now.baseTex = TexMan[flat.plane.texture];
		now.lightLevel = flat.lightlevel;
		now.extraLight = getExtraLight();
		now.lightColor = flat.Colormap.LightColor.d;
		now.fadeColor = flat.Colormap.FadeColor.d;
		now.desaturation = flat.Colormap.desaturation;
		now.blendFactor = flat.Colormap.blendfactor;
		now.alpha = FLOAT2FIXED(flat.alpha);
		now.renderStyle = (int)flat.renderstyle;
	}
	FlatChange change = ComputeFlatChange(slot->stamped && slot->range.count > 0, slot->stamp, now, slot->scrolled);
	// [rc4l] A scroll is where the plane maps the origin under the offsets now, less under the baked ones, and one a float cannot carry goes back into the vertices.
	float scrollU = 0.f, scrollV = 0.f;
	if (change == kFlatRescroll)
	{
		PlaneUVTransform t;
		PlaneUVFor(flat.plane, flat.gltexture, t);
		PlaneUVTransform t0 = t;
		t0.xoffs = FIXED2FLOAT(fixed_t::FromRaw(slot->stamp.xoffs));
		t0.yoffs = FIXED2FLOAT(fixed_t::FromRaw(slot->stamp.yoffs));
		float u1, v1, u0, v0;
		ComputePlaneUV(0.f, 0.f, t, u1, v1);
		ComputePlaneUV(0.f, 0.f, t0, u0, v0);
		scrollU = u1 - u0;
		scrollV = v1 - v0;
		if (!fua_mesh_rescroll || !ComputeScrollInRange(scrollU, scrollV)) { change = kFlatRebuild; scrollU = scrollV = 0.f; }
	}
	if (change == kFlatSame)
	{
		// The piece is still in the mesh's list from the last time it was registered -- pieces are
		// keyed by range offset and replaced rather than appended, so nothing needs saying again.
		g_flatHits++;
		return;
	}
	const bool facesDown = ComputeFlatWindingReversed(ceiling);
	if (change == kFlatReshade) g_flatReshades++;
	else if (change == kFlatRescroll) g_flatRescrolls++;
	else
	{
		g_flatRebuilds++;

		// [rc4l] Cap matches the wall path's staging array; a subsector with more edges than this is
		// vanishingly rare and is simply left to the GL renderer.
		static MeshVertex fan[GLWall::MAX_BATCH_FAN_VERTICES];
		static MeshVertex tris[GLWall::MAX_BATCH_FAN_VERTICES * 3];
		const int n = (int)sub->numlines;
		if (n > GLWall::MAX_BATCH_FAN_VERTICES) return;

		// Mirrors GLFlat::DrawSubsector for the positions, and gl_SetPlaneTextureRotation for the texture
		// coordinates -- which this used to skip entirely, baking the bare (x/64, -y/64) that is only
		// right for an unoffset, unscaled, unrotated 64x64 flat.
		const secplane_t &p = flat.plane.plane;
		const bool sloped = (p.a | p.b) != 0;
		const float zc = sloped ? 0.0f : (FIXED2FLOAT(p.Zat0()) + flat.dz);

		PlaneUVTransform uvt;
		PlaneUVFor(flat.plane, flat.gltexture, uvt);

		for (int k = 0; k < n; k++)
		{
			vertex_t *vt = sub->firstline[k].v1;
			fan[k].x = vt->fx;
			fan[k].y = vt->fy;
			fan[k].z = sloped ? (float)(p.ZatPoint(vt->fx, vt->fy) + flat.dz) : zc;
			ComputePlaneUV(vt->fx, vt->fy, uvt, fan[k].u, fan[k].v);
		}

		const int triVerts = ComputeFanTriangleVertexCount(n);
		if (triVerts <= 0) return;
		// [rc4l] Wind a surface seen from below the other way round, so back-face culling keeps it.
		//
		// A subsector's vertices come in one fixed order, so a floor and a ceiling built from them have
		// the SAME winding while facing opposite directions -- and a single cull mode then deletes one of
		// them. Enabling culling for the world removed every ceiling in the level.
		//
		// The discriminator is `ceiling`, which is GLFlat's record of which SIDE the surface is being
		// viewed from, and NOT the plane's normal. Using the normal looked more principled and was wrong:
		// a 3D floor's walkable top surface is the control sector's CEILING plane, so its normal points
		// down while the surface is seen from above, and the normal rule culled exactly those. See
		// GLFlat::ProcessSector, which sets ceiling=true for the pass that draws 3D floor faces from
		// below and ceiling=false for the pass that draws them from above.
		int w = 0;
		for (int t = 0; t < n - 2; t++)
			for (int c = 0; c < 3; c++)
			{
				const int cc = facesDown ? (2 - c) : c;
				tris[w++] = fan[ComputeFanTriangleVertex(n, t, cc)];
			}

		if (!MeshStore(slot->range, tris, w)) return;
	}

	MeshPiece mp;
	mp.range = slot->range;
	mp.material = flat.gltexture;
	mp.dynLightIndex = flat.mMeshLightIndex;
	// [rc4l] Plane normal, mapped into the mesh's (x, z-up, y) space: a secplane is
	// a*x + b*y + c*z + d = 0 with z up, so (a, b, c) becomes (a, c, b) here.
	//
	// Then turned to face the side the surface is SEEN from, which is not always the way its plane
	// points. A 3D floor's underside is the control sector's floor plane, so its normal points up
	// while the surface is looked at from below -- and the backend's dynamic-light side test then
	// finds every light in the room behind it and lights none of them. Same trap as the winding, one
	// step further on, and quieter: a surface facing the wrong way is simply never lit, which looks
	// exactly like a light that is out of range. Measured on dbab02 as a corridor ceiling that took
	// no plasma light at all while GL lit it.
	{
		const secplane_t &pl = flat.plane.plane;
		// [rc4l] Baked through lightside_compute, where the derivation is unit-tested against
		// gl_flats.cpp's own form of the same test. The two have to agree exactly, including for a
		// light lying ON the surface, and they did not.
		zx::hwrender::SecPlaneF sp;
		sp.a = FIXED2FLOAT(pl.a); sp.b = FIXED2FLOAT(pl.b);
		sp.c = FIXED2FLOAT(pl.c); sp.d = FIXED2FLOAT(pl.d);
		float n[3];
		zx::hwrender::ComputeMeshPlane(sp, ceiling, n, NULL);
		mp.normX = n[0]; mp.normY = n[1]; mp.normZ = n[2];
	}
	mp.lightLevel = flat.lightlevel;
	mp.lightColor = flat.Colormap.LightColor.d;
	mp.fadeColor = flat.Colormap.FadeColor.d;
	// GLFlat::Draw's GLPASS_PLAIN arm, with its `rel = getExtraLight()`.
	CaptureShading(flat.lightlevel, getExtraLight(), flat.Colormap, mp);

	// [rc4l] Flats are NOT all opaque, and 3D floors are where that shows.
	//
	// CaptureShading fills in alpha 1 and blend mode 0 because a sector's own floor always is one.
	// A 3D floor is frequently not: dbab01 hangs a translucent metal grate over a lava pit, and
	// baking it opaque drew the grate as solid lava-lit metal or let the lava beneath win outright.
	// The same classification the sprite path uses, for the same reason.
	mp.alpha = flat.alpha;
	// Which side this surface is viewed from, kept so fua_mesh_verify can check the winding above
	// rather than take it on trust. See MeshPiece::facesDown.
	mp.facesDown = facesDown;
	// renderstyle here is an ERenderStyle enum, not an FRenderStyle, so it is compared not inspected.
	mp.blendMode = ComputeSurfaceBlendMode(flat.renderstyle == STYLE_Add, flat.alpha);

	// [rc4l] Base plane texture, so animated flats (nukage, lava, blood) keep flowing.
	//
	// Straight off the plane the engine resolved, NOT re-derived from a sector and a plane index.
	// Deriving it took two wrong answers in a row: first the containing sector's flat, so every 3D
	// floor plane recorded the texture of the floor it hangs over and got repainted with it; then the
	// model sector's, which is wrong for a 3D floor because F3DFloor::top and ::bottom are planerefs
	// that carry their OWN texture and can name a different sector than the rover's model. On dbab02
	// that resolved to the null texture, whose id translates to itself, so the lava under the 3D
	// floor rendered correctly and then never animated again -- while the strip of the same floor
	// outside the 3D floor's footprint animated fine, because that piece took the simple path.
	//
	// plane.texture is what GLFlat::Process itself fed to ValidateTexture, so by construction it is
	// the base id of the texture actually being drawn, in every case, with no cases to enumerate.
	mp.baseTex = TexMan[flat.plane.texture];
	mp.mirrorSector = flat.mirrorSector;
	mp.mirrorCeiling = flat.mirrorCeiling;
	// [rc4l] GLFlat::PutFlat puts a sector's own plane in the solid pass and masks a 3D floor's.
	mp.plain = own;
	mp.scrollU = scrollU;
	mp.scrollV = scrollV;
	MeshRegisterPiece(mp);

	// Only now: a stamp recorded before the store would claim a surface that never made it in.
	// [rc4l] Its offsets stay the ones baked into the vertices unless they were baked again just now.
	const long long bakedX = slot->stamp.xoffs, bakedY = slot->stamp.yoffs;
	slot->stamp = now;
	if (change != kFlatRebuild) { slot->stamp.xoffs = bakedX; slot->stamp.yoffs = bakedY; }
	slot->scrolled = scrollU != 0.f || scrollV != 0.f;
	slot->stamped = true;
}

// [rc4l] Flats built from the MAP, with no GLFlat involved.
//
// GLFlat::ProcessSector decides what to draw from where the VIEWER is: a floor only when the plane
// is below the eye, a ceiling only when it is above, a 3D floor's face only from the side it is
// being looked at. That is right for a renderer walking the BSP once a frame and impossible for a
// bake, which has no viewer.
//
// So this emits BOTH sides of everything and lets back-face culling drop the half that is turned
// away -- which is what fua_dg_cull is for, and why a flat's winding is chosen by which side it is
// seen from rather than by its plane's normal. The result is the same set of surfaces GL would have
// produced across a walk that visited the sector from every side.
// [rc4l] planeFrom, when given, is where the plane itself is read (a height transfer's gl_FakeFlat copy), model staying the flat's key.
static void BakeOnePlane(subsector_t *sub, sector_t *model, int whichPlane, bool ceiling,
	int lightlevel, FColormap cm, float alpha, ERenderStyle style, bool reflects = false, bool fake = false, sector_t *planeFrom = NULL)
{
	if (model == NULL || sub == NULL) return;
	sector_t *from = planeFrom != NULL ? planeFrom : model;
	FlatSource f;
	f.plane.GetFromSector(from, whichPlane);
	// The sky is not a surface: GLFlat::Process returns before making one, and the sky pass draws it.
	if (f.plane.texture == skyflatnum) return;
	f.gltexture = FMaterial::ValidateTexture(f.plane.texture, false, true);
	if (f.gltexture == NULL) return;
	ApplyFullbright(f.gltexture, cm, lightlevel);
	// GLFlat::Process's own rule, and the reason a transfer-heights door's floor does not z-fight
	// with the one it sits on.
	f.dz = (whichPlane == sector_t::floor && from->transdoor) ? -1.f : 0.f;
	f.lightlevel = lightlevel;
	f.Colormap = cm;
	f.alpha = alpha;
	f.renderstyle = style;
	f.mMeshLightIndex = -1;
	f.mirrorSector = reflects ? (int)(model - sectors) : -1;
	f.mirrorCeiling = (whichPlane == sector_t::ceiling);
	f.mMeshModel = model;
	f.mMeshWhichPlane = whichPlane;
	f.fake = fake;
	StoreFlat(f, sub, ceiling);
}

void ApplyFullbright(const FMaterial *mat, FColormap &cm, int &lightLevel)
{
	if (mat == NULL || !mat->tex->isFullbright()) return;
	cm.LightColor.r = cm.LightColor.g = cm.LightColor.b = 0xff;
	lightLevel = 255;
}

void PlaneUVFor(const GLSectorPlane &plane, FMaterial *mat, PlaneUVTransform &out)
{
	out = ComputeIdentityPlaneUV();
	out.xoffs = FIXED2FLOAT(plane.xoffs);
	out.yoffs = FIXED2FLOAT(plane.yoffs);
	out.xscale = FIXED2FLOAT(plane.xscale);
	out.yscale = FIXED2FLOAT(plane.yscale);
	out.angleDegrees = ANGLE_TO_FLOAT(plane.angle);
	out.texWidth = (float)mat->TextureWidth();
	out.texHeight = (float)mat->TextureHeight();
	out.hasCanvas = mat->tex->bHasCanvas;
}

// One face of a 3D floor, by GLFlat::SetFrom3DFloor's rule with the viewer test taken out.
static void Bake3DFace(subsector_t *sub, sector_t *real, F3DFloor *rover, bool top, bool ceiling)
{
	F3DFloor::planeref &pr = top ? rover->top : rover->bottom;
	if (pr.copied) return;
	// [rc4l] Lit by which plane it is, not by which way it faces: the water's underside reading the light under it came out the script's blue.
	const FFFaceLight fl = ComputeFFFaceLight(top, ceiling, (rover->flags & FF_FOG) != 0, (rover->flags & FF_FIX) != 0);
	lightlist_t *light = P_GetPlaneLight(real, pr.plane, fl.lightBelow);
	int lightlevel = *light->p_lightlevel;

	FColormap cm;
	cm = real->ColorMap;
	if (rover->flags & FF_FOG) cm.LightColor = (light->extra_colormap)->Fade;
	else cm.CopyLightColor(light->extra_colormap);
	// ProcessSector overrides the fade with the sector's own for every 3D floor face it puts.
	cm.FadeColor = real->ColorMap->Fade;

	float alpha = rover->alpha / 255.0f;
	ERenderStyle style = (rover->flags & FF_ADDITIVETRANS) ? STYLE_Add : STYLE_Translucent;

	// FF_FIX takes its light from the model sector outright, which is the one case where the plane
	// light list is not the answer.
	if (fl.modelLight)
	{
		lightlevel = gl_ClampLight(rover->model->lightlevel);
		cm = rover->GetColormap();
	}

	BakeOnePlane(sub, pr.model, pr.isceiling, ceiling, lightlevel, cm, alpha, style);
}

// [rc4l] The view area the height transfers' planes were last baked for, a kViewArea, which is GL's area_t.
static int g_transferArea = kViewAreaNormal;
static_assert(area_normal == kViewAreaNormal && area_below == kViewAreaBelow && area_above == kViewAreaAbove, "kViewArea spells GL's area_t");

// [rc4l] A sector's own floor or ceiling as ProcessSector lights it, laid over sub: the sector's own subsector, or with fake another one GL's missing-texture fill covers with it.
static int BakeSectorPlane(subsector_t *sub, sector_t *real, bool ceiling, bool fake)
{
	const int which = ceiling ? sector_t::ceiling : sector_t::floor;
	// [rc4l] A height transfer draws its control sector's planes, and under or over them that sector's textures, light and colour.
	sector_t transfer;
	sector_t *src = fake ? real : TransferSector(real, &transfer, false);
	int ll = gl_ClampLight(ceiling ? src->GetCeilingLight() : src->GetFloorLight());
	FColormap cm;
	cm = src->ColorMap;
	const float alpha = ComputePlaneAlpha(src->portals[which] != NULL, FIXED2FLOAT(src->GetAlpha(which)), src->GetReflect(which));
	// [rc4l] GL reflects a plane with no portal stack on it, and a fully reflective one still needs the surface the reflection is painted on.
	const bool reflects = src->GetReflect(which) > 0 && src->portals[which] == NULL;
	if (real->e != NULL && real->e->XFloor.ffloors.Size() != 0)
	{
		lightlist_t *light = P_GetPlaneLight(real, ceiling ? &real->ceilingplane : &real->floorplane, ceiling);
		if (ComputePlaneTakesBandLight(ceiling, (real->GetFlags(which) & PLANEF_ABSLIGHTING) != 0, light->fromsector,
				light->p_lightlevel == &real->lightlevel))
			ll = *light->p_lightlevel;
		cm.CopyLightColor(light->extra_colormap);
	}
	if (alpha == 0.0f && !reflects) return 0;
	BakeOnePlane(sub, real, which, ceiling, ll, cm, alpha, STYLE_Translucent, reflects, fake, src);
	return 1;
}

sector_t *TransferSector(sector_t *s, sector_t *scratch, bool back)
{
	return (s != NULL && ViewDependent(s)) ? gl_FakeFlat(s, scratch, (area_t)g_transferArea, back) : s;
}

void RefreshTransferArea()
{
	if (sectors == NULL || numsectors <= 0 || subsectors == NULL) return;
	// [rc4l] The render sector, as SetViewArea takes it.
	sector_t *vs = R_PointInSubsector(viewx, viewy)->render_sector;
	sector_t *hs = vs != NULL ? vs->heightsec : NULL;
	const bool has = hs != NULL && !(hs->MoreFlags & SECF_IGNOREHEIGHTSEC);
	const int area = ComputeViewArea(has, FIXED2FLOAT(viewz), has ? FIXED2FLOAT(hs->floorplane.ZatPoint(viewx, viewy)) : 0.f,
		has ? FIXED2FLOAT(hs->ceilingplane.ZatPoint(viewx, viewy)) : 0.f, has && (hs->MoreFlags & SECF_FAKEFLOORONLY) != 0);
	if (area == g_transferArea) return;
	g_transferArea = area;
	for (int i = 0; i < numsectors; i++)
		if (ViewDependent(&sectors[i])) BakeFlatsForSector(i);
	RebakeTransferSegs();
}

static int BakeFlatsForSubsector(subsector_t *sub)
{
	int made = 0;
	{
		if (sub == NULL || sub->sector == NULL || sub->numlines < 3) return 0;
		// [rc4l] The render sector's own record, as DoSubsector and ProcessSector take it: the nodebuilder's sector is wrong inside a self-referencing trick, and a fake floor's stand-in lasts one frame.
		sector_t *real = &sectors[ComputeFlatSector(sub->render_sector != NULL ? sub->render_sector->sectornum : -1,
			sub->sector->sectornum)];
		if (real->e == NULL) return 0;
		extsector_t::xfloor &x = real->e->XFloor;

		// [rc4l] The sector's own floor and ceiling.
		made += BakeSectorPlane(sub, real, false, false);
		made += BakeSectorPlane(sub, real, true, false);

		// [rc4l] ...and another sector's plane wherever GL's missing-texture fill draws one over this subsector.
		int fakes = 0;
		const FakePlane *fp = FakePlanesFor(int(sub - subsectors), fakes);
		for (int i = 0; i < fakes; i++) made += BakeSectorPlane(sub, &sectors[fp[i].model], fp[i].upper, true);

		// ---- every face of every 3D floor in it ----
		//
		// The four cases are ProcessSector's, which runs two loops -- one for the faces seen from
		// below and one for those seen from above -- and each loop has an ordinary arm and an
		// inverted one. Its height bookkeeping (lastceilingheight, lastfloorheight) is a
		// view-dependent overlap trim and has no meaning without a viewer, so it is not here.
		for (unsigned k = 0; k < x.ffloors.Size(); k++)
		{
			F3DFloor *rover = x.ffloors[k];
			if ((rover->flags & (FF_EXISTS | FF_RENDERPLANES | FF_THISINSIDE))
				!= (FF_EXISTS | FF_RENDERPLANES)) continue;
			// A fog face draws no texture at all -- GLFlat::Process nulls the material for it -- so
			// there is nothing for the mesh to hold.
			if (rover->flags & FF_FOG) continue;

			const bool inverted = (rover->flags & (FF_INVERTPLANES | FF_BOTHPLANES)) != 0;
			if (!(rover->flags & FF_INVERTPLANES))
			{
				Bake3DFace(sub, real, rover, true,  false);  // [rc4l] walkable top, seen from above
				Bake3DFace(sub, real, rover, false, true);   // [rc4l] underside, seen from below
				made += 2;
			}
			if (inverted)
			{
				Bake3DFace(sub, real, rover, true,  true);   // [rc4l] the top, seen from below
				Bake3DFace(sub, real, rover, false, false);  // [rc4l] the bottom, seen from above
				made += 2;
			}
		}
	}
	return made;
}

// [rc4l] Collapse a subsector's fake planes, so a fill that has moved away stops drawing; the ones still there are baked again over them.
static void SquashFakeFlats(int sub)
{
	if (sub < 0 || (unsigned)sub >= g_flatHead.Size()) return;
	for (int i = g_flatHead[sub]; i >= 0; i = g_flats[i].next)
		if (g_flats[i].fake && g_flats[i].range.count > 0) { MeshSquash(g_flats[i].range); g_flats[i].stamped = false; }
}

int BakeFlatsFromMap()
{
	if (subsectors == NULL || numsubsectors <= 0 || sectors == NULL) return 0;
	TArray<int> changed;
	RecomputeAllFakePlanes(changed);
	for (unsigned i = 0; i < changed.Size(); i++) SquashFakeFlats(changed[i]);
	int made = 0;
	for (int i = 0; i < numsubsectors; i++) made += BakeFlatsForSubsector(&subsectors[i]);
	return made;
}

// [rc4l] The fills' changed subsectors drop their old fake planes and bake again.
static void RebakeChangedFills(const TArray<int> &changed)
{
	for (unsigned i = 0; i < changed.Size(); i++)
	{
		SquashFakeFlats(changed[i]);
		BakeFlatsForSubsector(&subsectors[changed[i]]);
	}
}

void RefreshFakePlanes(const TArray<int> &movedSectors, const TArray<int> &changedSides)
{
	TArray<int> changed;
	RecomputeFakePlanesFor(changed, movedSectors, changedSides);
	RebakeChangedFills(changed);
}

void RefreshAllFakePlanes()
{
	TArray<int> changed;
	RecomputeAllFakePlanes(changed);
	RebakeChangedFills(changed);
}

void RebakeFakesModelledOn(int sector)
{
	int count = 0;
	const int *subs = FakeSubsModelledOn(sector, count);
	for (int i = 0; i < count; i++) BakeFlatsForSubsector(&subsectors[subs[i]]);
}

// [rc4l] One sector's planes again, because it moved.
//
// The flat cache notices a plane has changed by stamping what it was made of and comparing -- which
// works because RegisterFlatSubsector is called every frame by GL's walk. With the walk gone nothing
// asks, and a lift's floor keeps the height it was baked at while the player rides it. Walls already
// had this: InvalidateMovedSectors re-bakes their segs from the map for exactly the same reason.
int BakeFlatsForSector(int sectorIndex)
{
	if (sectors == NULL || sectorIndex < 0 || sectorIndex >= numsectors) return 0;
	sector_t &sec = sectors[sectorIndex];
	int made = 0;
	for (int i = 0; i < sec.subsectorcount; i++) made += BakeFlatsForSubsector(sec.subsectors[i]);
	return made;
}

// [rc4l] The capture's way in: a GLFlat, unpacked into the same fields the map bake fills.
void RegisterFlatSubsector(const GLFlat &flat, subsector_t *sub, bool ceiling)
{
	// When the map is driving, the capture stands down for flats exactly as it does for walls --
	// both writing the same slots is a fight over them, not twice the coverage.
	if (fua_surface_mapbake_auto) return;
	FlatSource f;
	f.plane           = flat.plane;
	f.dz              = flat.dz;
	f.gltexture       = flat.gltexture;
	f.lightlevel      = flat.lightlevel;
	f.Colormap        = flat.Colormap;
	f.alpha           = flat.alpha;
	f.renderstyle     = flat.renderstyle;
	f.mMeshLightIndex = flat.mMeshLightIndex;
	f.mMeshModel      = flat.mMeshModel;
	f.mMeshWhichPlane = flat.mMeshWhichPlane;
	StoreFlat(f, sub, ceiling);
}

// ---------------------------------------------------------------------------
// Sprites
// ---------------------------------------------------------------------------

// [rc4l] Sprites go into the DYNAMIC stream, rebuilt every frame -- see staticmesh.h. They are
// billboards built for one viewpoint, so they are not level geometry and must never be baked.
static int g_spritesThisFrame = 0;
static int g_skyQuadsCaptured = 0;
static int g_skyWalks[kSkyCaptureLists];   // [rc4l] Walks per list, so a second walk per frame shows up as a count.
static AActor *g_skyViewpoint = NULL;
// [rc4l] The player's view was walked outside GL this frame (features/visibility), and its slot holds what that walk reached.
static bool g_mainVisibility = false;
// [rc4l] GL's own walk filled the player's slot since the visibility walk last ran, so all of it must be cleared.
static bool g_mainSlotNeedsClear = true;
// [rc4l] One list per view the backend draws, indexed by the kSkyCapture* lists, and the one the walk in progress fills.
static TArray<MeshVertex> g_skyVerts[kSkyCaptureLists];
static int g_skyList = kSkyCaptureNone;
// [rc4l] The player's walk's sky holes this frame, kSkyHoleFloats each, for the scene shader.
static TArray<float> g_skyHoles;

// [rc4l] What each sprite piece was registered WITH, for one frame, so a seam can be read off
// numbers instead of guessed at from a screenshot.
//
// A sprite is not always one quad. GLSprite::SplitSprite cuts it wherever a 3D floor's light band
// starts, gives each piece that band's light and colormap, and draws them as separate sprites. When
// two pieces of one explosion come out different in the backend and identical in GL, the question is
// which of those inputs diverged -- and every candidate (light, colour, alpha, blend, the texture
// coordinates of the cut) is a number that is already in hand right here.
struct SpriteNote
{
	float z1, z2, vt, vb;
	int   light;
	unsigned int lightColor, fadeColor;
	float alpha, r, g, b;
	int   blend;
	const void *material;
};
static const int kMaxSpriteNotes = 64;
static SpriteNote g_spriteNotes[kMaxSpriteNotes];
static int g_spriteNoteCount = 0;
// [rc4l] What render styles sprites actually arrive with, counted rather than assumed.
//
// Plasma impacts came out with black holes where their bright cores should be, which is what an
// ADDITIVE or SUBTRACTIVE sprite looks like when it is drawn with ordinary alpha blending: its dark
// texels paint dark instead of adding nothing. Guessing which styles a mod uses is how the last
// four of these went; this counts them.
static int g_styleOps[16];
static int g_styleFlagsSeen = 0;
static int g_styleDest[16];
static int g_classified[4];
void GetSpriteStyleStats(int *ops, int &flags)
{
	for (int i = 0; i < 16; i++) ops[i] = g_styleOps[i];
	flags = g_styleFlagsSeen;
}

void GetSpriteStyleDetail(int *dest16, int *classified4)
{
	for (int i = 0; i < 16; i++) dest16[i] = g_styleDest[i];
	for (int i = 0; i < 4; i++) classified4[i] = g_classified[i];
}

int SkyQuadsCaptured() { return g_skyQuadsCaptured; }

// [rc4l] A walk starting: an outer one empties the list it fills, and a portal re-entry (mirror, skybox) fills none.
namespace { bool s_mainView = true; int s_sceneNesting = 0; }
void SetMainView(bool mainView) { s_mainView = mainView; }
bool MainView() { return s_mainView; }
void EnterScene() { s_sceneNesting++; }
void LeaveScene() { if (s_sceneNesting > 0) s_sceneNesting--; }

// [rc4l] The stacked views the player's walk saw this frame, by slot, and the portal whose view GL is walking now.
static const void *g_stackPortals[kMaxStackViews];
static StackViewInfo g_stackInfo[kMaxStackViews];
static TArray<MeshVertex> g_stackVerts[kMaxStackViews];
static int g_stackUsed = 0;
static const void *s_stackWalk = NULL;
static TArray<unsigned int> g_stackVis;

StackWalk::StackWalk(const void *portal) : saved(s_stackWalk) { s_stackWalk = portal; }

static bool s_skyboxWalk = false;
static bool g_skyboxWalked = false;
SkyboxWalk::SkyboxWalk() : saved(s_skyboxWalk) { s_skyboxWalk = true; }
SkyboxWalk::~SkyboxWalk() { s_skyboxWalk = saved; }
bool SkyboxWalked() { return g_skyboxWalked; }

static int s_mirrorLine = -1;
// [rc4l] The line mirrors GL's walk went through since the player's walk began this frame.
static TArray<int> g_walkedMirrors;
MirrorWalk::MirrorWalk(int line) : saved(s_mirrorLine)
{
	s_mirrorLine = line;
	if (!MirrorWalkedThisFrame(line)) g_walkedMirrors.Push(line);
}
bool MirrorWalkedThisFrame(int line)
{
	for (unsigned i = 0; i < g_walkedMirrors.Size(); i++) if (g_walkedMirrors[i] == line) return true;
	return false;
}
MirrorWalk::~MirrorWalk() { s_mirrorLine = saved; }

// [rc4l] The light GLWall::RenderMirrorSurface draws each line mirror's sheen in, from the player's walk this frame.
struct MirrorSheenShade { int line; MeshPiece shade; };
static TArray<MirrorSheenShade> g_mirrorSheens;
const MeshPiece *MirrorSheen(int line)
{
	for (unsigned i = 0; i < g_mirrorSheens.Size(); i++) if (g_mirrorSheens[i].line == line) return &g_mirrorSheens[i].shade;
	return NULL;
}
void RegisterMirrorSheen(const GLWall &wall)
{
	if (!PlayerWalkActive() || wall.seg == NULL || wall.seg->linedef == NULL) return;
	const int line = int(wall.seg->linedef - lines);
	if (MirrorSheen(line) != NULL) return;
	MirrorSheenShade s;
	s.line = line;
	// [rc4l] gl_SetColor and gl_SetFog as RenderMirrorSurface calls them: no relative light, and fogged to black as an added surface.
	CaptureShading(wall.lightlevel, 0, wall.Colormap, s.shade, false, true);
	g_mirrorSheens.Push(s);
}
StackWalk::~StackWalk() { s_stackWalk = saved; }

static int StackWalkSlot()
{
	for (int i = 0; i < g_stackUsed; i++)
		if (g_stackPortals[i] == s_stackWalk) return i;
	return -1;
}

int StackCaptureSlot() { return ComputeStackCaptureSlot(s_mainView, s_sceneNesting, StackWalkSlot()); }

// [rc4l] Marks an id for the walk in progress: a skybox's, a stacked view's, or the player's own, which a sky hole lets through what it drew.
static void NoteWalked(int id)
{
	if (g_stackVis.Size() == 0) return;
	const int slot = ComputeWalkSlot(s_skyboxWalk, s_stackWalk != NULL, StackCaptureSlot(), PlayerWalkActive());
	if (slot < 0 || id < 0 || id >= numsubsectors + numsides) return;
	ComputeMarkStackVisible(&g_stackVis[0], id, slot);
	if (slot == kSkyboxWalkSlot) g_skyboxWalked = true;
}
void NoteWalkedSubsector(int subsector) { if (subsector < numsubsectors) NoteWalked(subsector); }
void NoteWalkedSide(int side) { if (side >= 0 && side < numsides) NoteWalked(numsubsectors + side); }

const unsigned int *StackVisibility(int &words)
{
	words = ComputeWalkBitsNeeded(g_stackUsed, (int)g_skyHoles.Size(), g_skyboxWalked, g_mainVisibility) ? (int)g_stackVis.Size() : 0;
	return words ? &g_stackVis[0] : NULL;
}

const float *SkyHoles(int &count)
{
	count = (int)g_skyHoles.Size() / kSkyHoleFloats;
	return count ? &g_skyHoles[0] : NULL;
}

int PieceStackId(const MeshPiece &p)
{
	if (p.linkId >= 0) return p.linkId;
	int count = 0;
	const MeshVertex *v = MeshVertexData(count);
	float cx, cy, cz;
	if (numsubsectors <= 0 || !ComputeCentroid(v, (unsigned)count, p.range.offset, p.range.count, cx, cy, cz)) return -1;
	float px, py;
	const bool wall = ComputePieceProbe(cx, cy, p.normX, p.normY, p.normZ, px, py);
	const subsector_t *ss = R_PointInSubsector(FLOAT2FIXED(px), FLOAT2FIXED(py));
	if (ss == NULL) return -1;
	if (!wall) return int(ss - subsectors);
	// [rc4l] A wall belongs to the side of the seg it lies on, the one of its subsector's segs nearest its centre.
	int best = -1;
	float bestD = 1e30f;
	for (DWORD i = 0; i < ss->numlines; i++)
	{
		const seg_t &sg = ss->firstline[i];
		if (sg.sidedef == NULL) continue;
		const float d = ComputeSegmentDistance2(cx, cy, FIXED2FLOAT(sg.v1->x), FIXED2FLOAT(sg.v1->y),
			FIXED2FLOAT(sg.v2->x), FIXED2FLOAT(sg.v2->y));
		if (d < bestD) { bestD = d; best = int(sg.sidedef - sides); }
	}
	return (best >= 0) ? numsubsectors + best : int(ss - subsectors);
}

int StackViewCount() { return g_stackUsed; }
StackViewInfo StackView(int slot) { return g_stackInfo[slot]; }
const MeshVertex *StackViewVertices(int slot, int &count)
{
	count = (int)g_stackVerts[slot].Size();
	return count ? &g_stackVerts[slot][0] : NULL;
}

// [rc4l] The slot of a portal's stacked view, made on its first surface of the frame, or -1 once every slot is taken.
static int StackSlotFor(const FPortal *portal)
{
	const int before = g_stackUsed;
	const int slot = zx::hwrender::ComputeSlotFor<const void *>(g_stackPortals, g_stackUsed, kMaxStackViews, portal);
	if (slot >= 0 && slot == before)
	{
		StackViewInfo &v = g_stackInfo[slot];
		v.dx = FIXED2FLOAT(portal->xDisplacement);
		v.dy = FIXED2FLOAT(portal->yDisplacement);
		v.ceiling = portal->plane == sector_t::ceiling;
	}
	return slot;
}

void RegisterStackWall(const GLWall &wall, const FPortal *portal)
{
	if (portal == NULL || !PlayerWalkActive()) return;
	const int slot = StackSlotFor(portal);
	if (slot < 0) return;
	MeshVertex q[6];
	MakeWallQuad(q, wall.glseg.x1, wall.glseg.y1, wall.glseg.x2, wall.glseg.y2, wall.zbottom, wall.ztop);
	for (int k = 0; k < 6; k++) g_stackVerts[slot].Push(q[k]);
}

// [rc4l] The last walks begun, newest last: which view, how deep, and what the list they emptied held.
struct SkyWalkNote { bool mainView; int nesting; int list; int heldBefore; };
static SkyWalkNote g_walkNotes[8];
static int g_walkNoteCount = 0;

// [rc4l] What the last frame's walks left: the skybox they named, the sky holes, the stacked views and the mirrors they reached.
static void ForgetFrameWalks()
{
	g_mainVisibility = false;
	g_mainSlotNeedsClear = true;
	g_skyViewpoint = NULL;
	g_skyHoles.Clear();
	g_skyboxWalked = false;
	g_stackUsed = 0;
	g_walkedMirrors.Clear();
	g_mirrorSheens.Clear();
	for (int i = 0; i < kMaxStackViews; i++) { g_stackVerts[i].Clear(); g_skyVerts[kSkyCaptureStack + i].Clear(); }
	g_stackVis.Resize((unsigned)ComputeStackVisWords(numsubsectors + numsides));
	if (g_stackVis.Size() > 0) memset(&g_stackVis[0], 0, g_stackVis.Size() * sizeof(unsigned int));
}

int BeginSkyWalk()
{
	g_skyList = ComputeSkyCaptureList(s_mainView, s_sceneNesting, StackWalkSlot());
	const SkyWalkNote note = { s_mainView, s_sceneNesting, g_skyList,
		g_skyList == kSkyCaptureNone ? -1 : (int)g_skyVerts[g_skyList].Size() / 6 };
	g_walkNotes[g_walkNoteCount++ % 8] = note;
	if (g_skyList == kSkyCaptureNone) return g_skyList;
	g_skyVerts[g_skyList].Clear();
	// [rc4l] The player's walk starts the frame, so last frame's stacked views and their skies go with it.
	if (g_skyList == kSkyCaptureMain) ForgetFrameWalks();
	g_skyWalks[g_skyList]++;
	return g_skyList;
}

// [rc4l] The ids the visibility walk marked last frame, so the next one clears just those; GL's walk filling the slot instead asks for the whole slot.
static TArray<int> g_mainMarked;

void BeginMainVisibility()
{
	// [rc4l] Only the player's slot: what GL's walks captured before standalone (sheens, skyboxes, stacked views) is all standalone has of them.
	const int ids = numsubsectors + numsides;
	const unsigned words = (unsigned)ComputeStackVisWords(ids);
	if (g_stackVis.Size() != words)
	{
		g_stackVis.Resize(words);
		if (words > 0) memset(&g_stackVis[0], 0, words * sizeof(unsigned int));
		g_mainSlotNeedsClear = false;
		g_mainMarked.Clear();
	}
	if (g_mainSlotNeedsClear)
	{
		for (int id = 0; id < ids; id++) ComputeClearStackVisible(&g_stackVis[0], id, kMainWalkSlot);
		g_mainSlotNeedsClear = false;
	}
	else
		for (unsigned i = 0; i < g_mainMarked.Size(); i++) ComputeClearStackVisible(&g_stackVis[0], g_mainMarked[i], kMainWalkSlot);
	g_mainMarked.Clear();
	g_mainVisibility = true;
}

void NoteMainVisible(int id)
{
	if (g_stackVis.Size() == 0 || id < 0 || id >= numsubsectors + numsides) return;
	ComputeMarkStackVisible(&g_stackVis[0], id, kMainWalkSlot);
	g_mainMarked.Push(id);
}

bool MainVisibility() { return g_mainVisibility; }
void CancelMainVisibility() { g_mainVisibility = false; }

const unsigned int *WalkBits(int &words)
{
	words = (int)g_stackVis.Size();
	return words ? &g_stackVis[0] : NULL;
}

void ForgetWalks()
{
	for (int i = 0; i < kSkyCaptureLists; i++) g_skyVerts[i].Clear();
	ForgetFrameWalks();
}
int SkyWalks(int list) { return g_skyWalks[list]; }
void PrintSkyWalkNotes()
{
	for (int i = g_walkNoteCount > 8 ? g_walkNoteCount - 8 : 0; i < g_walkNoteCount; i++)
	{
		const SkyWalkNote &n = g_walkNotes[i % 8];
		Printf("  walk %d: %s view, nesting %d, list %d, emptied %d quads\n", i, n.mainView ? "main" : "camera", n.nesting, n.list, n.heldBefore);
	}
}

// The viewpoint the visible sky surfaces name; NULL until a walk has produced one.
AActor *SkyQuadViewpoint() { return g_skyViewpoint; }

const MeshVertex *SkyQuadVertices(int &count, int list)
{
	count = (int)g_skyVerts[list].Size();
	return count ? &g_skyVerts[list][0] : NULL;
}

// [rc4l] Models are cleared with sprites because they arrive with them, and a stale count reads as a model still there.
void ClearSprites() { g_spritesThisFrame = 0; g_spriteNoteCount = 0; ModelStatsReset(); }

// [rc4l] The sweep's own arithmetic, kept from the last frame that ran one.
static int g_swSeen = 0, g_swNoSector = 0, g_swBehind = 0, g_swOffScreen = 0, g_swProcessed = 0;
void RecordSpriteSweep(int seen, int noSector, int behind, int offScreen, int processed)
{
	g_swSeen = seen; g_swNoSector = noSector; g_swBehind = behind;
	g_swOffScreen = offScreen; g_swProcessed = processed;
}

CCMD( fua_sprite_sweep )
{
	if (g_swSeen == 0) { Printf( "fua_sprite_sweep: no sweep has run -- needs fua_dg_standalone.\n" ); return; }
	const int culled = g_swNoSector + g_swBehind + g_swOffScreen;
	Printf( "fua_sprite_sweep: %d actors iterated, %d culled (%d no sector, %d behind, %d off screen),\n",
		g_swSeen, culled, g_swNoSector, g_swBehind, g_swOffScreen );
	Printf( "  %d handed to GLSprite::Process, of which %d registered a sprite\n",
		g_swProcessed, g_spritesThisFrame );
}
int SpritePieceCount() { return g_spritesThisFrame; }

// [rc4l] One quad of the wall between heights zb and zt at its two ends into the sky list GL's walk is filling, a list of its own because GL's skybox re-entry clears the dynamic stream with DynClear.
static void PushSkyQuad(const GLWall &wall, const float *zb, const float *zt)
{
	if (zt[0] <= zb[0] && zt[1] <= zb[1]) return;
	MeshVertex q[6];
	MakeWallQuad(q, wall.glseg.x1, wall.glseg.y1, wall.glseg.x2, wall.glseg.y2, zb, zt);
	for (int k = 0; k < 6; k++) g_skyVerts[g_skyList].Push(q[k]);
	g_skyQuadsCaptured++;
}

// [rc4l] A sky quad, as GEOMETRY, taken from GL as it emits it.
//
// GL paints its sky through a stencil built from these, so anything further away than one is
// behind the sky and the depth buffer says so. This backend painted the sky full-screen before
// the world instead, which is the same picture only while nothing is drawn BEYOND a sky surface
// -- and the map-derived bake, having no visibility test, draws walls GL culls.
//
// Taken from GL rather than derived, and that is the point: GL emits these only for segs its BSP
// walk reaches, so the set carries GL VISIBILITY with it. Deriving them from the map was tried
// three ways and every one over-emitted -- on the reported vantage GL produces sky quads for FOUR
// linedefs where the map has thousands of sky segs. See docs/skybleed-scope.md.
void RegisterSkyQuad(const GLWall &wall, AActor *viewpoint)
{
	if (!fua_mesh_sky || g_skyList == kSkyCaptureNone) return;
	// [rc4l] Passed in, never read off wall.skybox: that shares a union with wall.sky, so on a plain sky it is a GLSkyInfo.
	if (viewpoint != NULL && ComputeTakesSkyViewpoint(g_skyList, g_skyViewpoint != NULL)) g_skyViewpoint = viewpoint;

	// [rc4l] A horizon line's sky belongs to features/horizon, which builds it for every eye height.
	if (zx::horizon::HorizonsActive() && wall.seg != NULL && wall.seg->linedef != NULL &&
	    zx::horizon::ComputeIsHorizonLine(wall.seg->linedef->special)) return;

	PushSkyQuad(wall, wall.zbottom, wall.ztop);
}

void RegisterSkyHole(const GLWall &wall)
{
	if (!fua_mesh_sky || g_skyList == kSkyCaptureNone) return;
	// [rc4l] The player's own go to the scene shader, which drops only what GL's walk did not draw behind them; another view's, or one past the shader's room, still paints sky over everything behind it.
	if (ComputeSkyHoleToShader(g_skyList, (int)g_skyHoles.Size() / kSkyHoleFloats))
	{
		float rec[kSkyHoleFloats];
		ComputeSkyHoleRecord(wall.glseg.x1, wall.glseg.y1, wall.glseg.x2, wall.glseg.y2, wall.zfloor, wall.zceil, rec);
		for (int i = 0; i < kSkyHoleFloats; i++) g_skyHoles.Push(rec[i]);
		return;
	}
	PushSkyQuad(wall, wall.zfloor, wall.zceil);
}

void MakeWallQuad(MeshVertex *out6, float x1, float y1, float x2, float y2,
	const float *zbottom2, const float *ztop2)
{
	MeshVertex fan[4];
	fan[0].Set(x1, zbottom2[0], y1, 0.f, 0.f);
	fan[1].Set(x1, ztop2[0],    y1, 0.f, 0.f);
	fan[2].Set(x2, ztop2[1],    y2, 0.f, 0.f);
	fan[3].Set(x2, zbottom2[1], y2, 0.f, 0.f);
	int w = 0;
	for (int t = 0; t < 2; t++)
		for (int c = 0; c < 3; c++)
			out6[w++] = fan[ComputeFanTriangleVertex(4, t, c)];
}

bool PlayerWalkActive()
{
	return ComputeOwnsFrameCapture(s_mainView, s_sceneNesting);
}

int MirrorCaptureLine() { return ComputeMirrorCaptureLine(s_mirrorLine, s_mainView, s_sceneNesting); }

bool DynamicCaptureActive() { return PlayerWalkActive() || StackCaptureSlot() >= 0 || MirrorCaptureLine() >= 0; }

void RegisterSprite(const GLSprite &spr, const float *c1, const float *c2,
	const float *c3, const float *c4)
{
	// [rc4l] Each walk the backend draws again keeps its own sprites, for its pass alone: the player's for the frame, a stacked view's or a line mirror's (the viewer and whatever stands behind the player) for that one.
	if (spr.gltexture == NULL || !DynamicCaptureActive()) return;

	// [rc4l] GLSprite emits a 4-vertex TRIANGLE STRIP; the mesh is triangle lists, so expand to
	// (0,1,2) and (2,1,3) -- which preserves the strip's winding rather than flipping the second
	// triangle.
	MeshVertex q[4];
	q[0].Set(c1[0], c1[1], c1[2], spr.ul, spr.vt);
	q[1].Set(c2[0], c2[1], c2[2], spr.ur, spr.vt);
	q[2].Set(c3[0], c3[1], c3[2], spr.ul, spr.vb);
	q[3].Set(c4[0], c4[1], c4[2], spr.ur, spr.vb);

	MeshPiece mp;
	mp.range.offset = 0;
	mp.range.count = 0;
	mp.material = spr.gltexture;
	mp.mirrorLine = MirrorCaptureLine();
	mp.stackSlot = StackCaptureSlot();
	// [rc4l] GLSprite has no light index. Its dynamic light is computed per actor on the CPU and
	// folded into the vertex colour below -- see the fold after CaptureShading, which is what makes
	// that true. It was asserted here before it was done, and sprites went dark when the shader
	// stopped adding lights for them on the strength of this comment.
	mp.dynLightIndex = -1;
	mp.lightLevel = spr.lightlevel;
	mp.lightColor = spr.Colormap.LightColor.d;
	mp.fadeColor = spr.Colormap.FadeColor.d;
	// GLSprite::Draw's own `rel`: fullbright sprites take no extra light.
	// [rc4l] Not memoised, and that was measured rather than assumed -- see docs/sprites-scope.md.
	// A cache keyed on the six inputs hits 98% of 3053 sprites on Sunder MAP16 and saves nothing:
	// what it skips is two calls and eight getters, and writing the eight answers costs the same.
	CaptureShading(spr.lightlevel, spr.fullbright ? 0 : getExtraLight(), spr.Colormap, mp);

	// [rc4l] The dynamic light GL worked out for this actor, folded in here.
	//
	// gl_SetDynSpriteLight runs just before this and leaves its answer in the render state, where
	// main.fp picks it up as a uniform and ADDS it after the vertex colour. CaptureShading reads
	// only gl_SetColor, so the captured colour is the sector light and nothing else -- a sprite
	// standing in a green pool came out grey while GL drew it green.
	//
	// Taking it from the render state rather than walking the light list again is the whole point:
	// that function has already applied gl_light_sprites, and DONTLIGHTSELF, which is why an item is
	// not lit by the glow it carries itself. A shader loop over every light in the level knows none
	// of that, and lit each armor bonus with its own light until it was stopped.
	if (!spr.fullbright)
	{
		float dyn[3] = { 0.f, 0.f, 0.f };
		gl_RenderState.GetDynLight(dyn[0], dyn[1], dyn[2]);
		// Carried, not folded: it has to survive the lighting equation the shader applies to vColor,
		// which is where GL adds it too. Mixed into the colour it would be attenuated by distance and
		// by how dark the sector is, and a bonus in a dark room came out a quarter dimmer than GL's.
		mp.dynR = dyn[0]; mp.dynG = dyn[1]; mp.dynB = dyn[2];
	}
	// [rc4l] A sprite takes this light and no other, fullbright ones none at all: GL never runs the per-light loop on one.
	mp.cpuLight = true;

	// [rc4l] Classify the render style into the handful of blends a backend actually needs.
	//
	// gl_GetRenderStyle resolves the full matrix into GL blend enums, which would then have to be
	// mapped back into Diligent's. These four cases cover what Doom content uses: opaque/masked,
	// normal translucency, additive (plasma, fireballs, explosions) and the fuzz shadow. Anything
	// exotic falls into normal translucency, which is wrong but visible rather than invisible.
	{
		const int op = (int)spr.RenderStyle.BlendOp;
		if (op >= 0 && op < 16) g_styleOps[op]++;
		const int da = (int)spr.RenderStyle.DestAlpha;
		if (da >= 0 && da < 16) g_styleDest[da]++;
		g_styleFlagsSeen |= (int)spr.RenderStyle.Flags;
	}
	// [rc4l] A billboard has NO side, so it gets no normal.
	//
	// CaptureShading leaves a default of straight up, which is right for a flat and meaningless for a
	// sprite: a quad that turns to face the camera has no fixed facing for a light to be in front of
	// or behind. The backend's dynamic-light side test took the default at its word and lit only the
	// fragments below the light, cutting the rest off along a dead straight horizontal line at the
	// light's own height -- a hard seam across the middle of every rocket explosion, which carries a
	// large light at its centre. Zero means "no side", and the test is skipped.
	mp.normX = mp.normY = mp.normZ = 0.f;
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

	if (mp.blendMode >= 0 && mp.blendMode < 4) g_classified[mp.blendMode]++;
	// Sprite centre, for the back-to-front sort.
	mp.sortX = spr.x; mp.sortY = spr.y; mp.sortZ = spr.z;
	// [rc4l] Four corners, not six vertices -- the backend expands them when it writes its own.
	mp.quad = true;

	zx::sprites::NoteGLSprite(spr.actor, mp, q);
	DynAppend(q, 4, mp);
	g_spritesThisFrame++;

	if (g_spriteNoteCount < kMaxSpriteNotes)
	{
		SpriteNote &n = g_spriteNotes[g_spriteNoteCount++];
		n.z1 = spr.z1; n.z2 = spr.z2; n.vt = spr.vt; n.vb = spr.vb;
		n.light = spr.lightlevel;
		n.lightColor = spr.Colormap.LightColor.d;
		n.fadeColor = spr.Colormap.FadeColor.d;
		n.alpha = mp.alpha;
		n.r = mp.colorR; n.g = mp.colorG; n.b = mp.colorB;
		n.blend = mp.blendMode;
		n.material = spr.gltexture;
	}
}

// [rc4l] fua_sprites: this frame's sprite pieces, in the order they were registered.
//
// Pause on the frame in question first -- the list is rebuilt every frame, and an explosion lasts
// about a quarter of a second.
void DumpSpriteNotes()
{
	Printf("fua_sprites: %d piece(s) this frame (%d recorded)\n",
		g_spritesThisFrame, g_spriteNoteCount);
	for (int i = 0; i < g_spriteNoteCount; i++)
	{
		const SpriteNote &n = g_spriteNotes[i];
		Printf("  %2d  z %8.2f..%8.2f  v %.4f..%.4f  light %3d  lit %.3f,%.3f,%.3f\n"
		       "      alpha %.3f  blend %d  lightcol %06x  fade %06x  mat %p\n",
			i, n.z1, n.z2, n.vt, n.vb, n.light, n.r, n.g, n.b,
			n.alpha, n.blend, n.lightColor & 0xffffff, n.fadeColor & 0xffffff, n.material);
	}
}

void RegisterDecal(const MeshVertex *quad, const void *material, int translation,
                   bool shadow, bool additive, float alpha,
                   int lightlevel, int rel, const FColormap &colormap,
                   bool redToAlpha, unsigned int alphaColor,
                   float sortX, float sortY, float sortZ, const float *dynLight)
{
	if (quad == NULL) return;

	// GL draws the decal as a 4-vertex TRIANGLE FAN, so the triangles are (0,1,2) and (0,2,3) --
	// not the strip order the sprite path uses, which would fold the quad in half.
	MeshVertex tris[6];
	tris[0] = quad[0]; tris[1] = quad[1]; tris[2] = quad[2];
	tris[3] = quad[0]; tris[4] = quad[2]; tris[5] = quad[3];

	RegisterDecalTriangles(tris, 6, material, translation, shadow, additive, alpha,
		lightlevel, rel, colormap, redToAlpha, alphaColor, sortX, sortY, sortZ, dynLight);
}

// [rc4l] The same piece, for a mark that is not a quad.
//
// A projected decal is whatever geometry fell inside its box, clipped -- three vertices or thirty,
// spread over a wall and the floor under it. Everything below the vertex list is identical to the
// glued quad's, and it stayed identical by being one function rather than two that drift.
// [rc4l] Say what each decal was captured AS, in the order it was captured.
//
// Two decals of a `lowerdecal` pair land at the same point and have to composite in the order GL
// drew them. When they come out wrong there is nothing on screen to say which of the two it is,
// what blend each took, or whether they even reached the backend -- so this prints it.

void RegisterDecalTriangles(const MeshVertex *tris, int count, const void *material, int translation,
                            bool shadow, bool additive, float alpha,
                            int lightlevel, int rel, const FColormap &colormap,
                            bool redToAlpha, unsigned int alphaColor,
                            float sortX, float sortY, float sortZ, const float *dynLight)
{
	if (!DynamicCaptureActive()) return;   // [rc4l] see RegisterSprite
	if (tris == NULL || count < 3 || material == NULL) return;

	MeshPiece mp;
	mp.range.offset = 0;
	mp.range.count = 0;
	mp.material = material;
	mp.mirrorLine = MirrorCaptureLine();
	mp.stackSlot = StackCaptureSlot();
	// [rc4l] A decal takes its dynamic light from the CPU, and its NORMAL is what says so.
	//
	// gl_SetDynSpriteLight computes the light for this decal and RegisterDecal folds it into the
	// vertex colour below, exactly as for a sprite. The shader must therefore not add the lights a
	// second time, and what tells it not to is a zero normal -- MeshPiece's default, deliberately
	// left alone here rather than filled in from the wall the decal is stuck to.
	//
	// That is an easy thing to undo by accident, so it is written down: giving a decal a real normal
	// would look like an improvement and would silently restore the double count. Measured on an
	// unshaded decal (PlasmaScorch, the only kind that can show it) under a blue torch, the blue a
	// dynamic light added was GL +12.4 against the backend's +19.7 -- 59% too much -- and +14.3
	// once the lights were left to the CPU.
	mp.dynLightIndex = -1;
	mp.lightLevel = lightlevel;
	mp.lightColor = colormap.LightColor.d;
	mp.fadeColor = colormap.FadeColor.d;
	CaptureShading(lightlevel, rel, colormap, mp);

	mp.translation = translation;
	mp.alpha = alpha;
	mp.blendMode = ComputeStyleBlendMode(shadow, additive, alpha);
	// [rc4l] Coplanar with the wall it is glued to, so it needs the depth bias GL gets from
	// glPolygonOffset. See MeshPiece::depthBias.
	mp.depthBias = true;
	// [rc4l] A shaded decal's texture is an alpha mask and its colour is its own AlphaColor, so the
	// colour is folded into the vertex light here and the shader shades a white texel. GL does the
	// same thing with SetObjectColor plus TM_REDTOALPHA.
	// [rc4l] The dynamic light the SHADER would have added, folded in BEFORE the decal's own colour.
	//
	// gl_SetDynSpriteLight hands a decal's dynamic light to GL as a uniform, so a capture that takes
	// the vertex colour alone arrives unlit -- the same hole the HUD weapon had, where a muzzle flash
	// lit the room and left the weapon dark.
	//
	// Before, not after, because that is where GL puts it. main.fp computes
	// (texel * uObjectColor) * (vertexLight + dynLight): the decal's own colour multiplies the whole
	// lit result, dynamic light included. Adding it afterwards instead makes the decal colour scale
	// only the sector light, and a SCORCH MARK is the case that exposes the difference -- its colour
	// is black, so the multiply gives zero and the added light is then the entire fragment. Every
	// burn mark in the room lit up in the colour of whatever light reached it. Multiplied instead,
	// black stays black however bright the room gets, which is what GL draws.
	if (dynLight != NULL)
	{
		mp.colorR = (mp.colorR + dynLight[0] > 1.f) ? 1.f : mp.colorR + dynLight[0];
		mp.colorG = (mp.colorG + dynLight[1] > 1.f) ? 1.f : mp.colorG + dynLight[1];
		mp.colorB = (mp.colorB + dynLight[2] > 1.f) ? 1.f : mp.colorB + dynLight[2];
	}

	mp.redToAlpha = redToAlpha;
	if (redToAlpha)
	{
		float r, g, b;
		ComputeUnpackRGB(alphaColor, r, g, b);
		mp.colorR *= r; mp.colorG *= g; mp.colorB *= b;
		// [rc4l] An alpha-mask decal ALWAYS blends: its coverage lives in the texture's red channel,
		// so there is no such thing as an opaque one. Classified by alpha alone an unfaded decal
		// comes out as blend mode 0, takes the opaque pipeline, and is sampled as a colour image --
		// which paints a bright blob where GL paints a dark burn. Only the faded ones looked right,
		// so it read as "decals glow" rather than "the wrong shader".
		if (mp.blendMode == 0) mp.blendMode = 1;
	}
	// [rc4l] An ADDITIVE decal is not fogged, because GL does not fog one either.
	//
	// GLWall::DrawDecal forces the fog colour to black for the duration of an additive draw and puts
	// it back afterwards. Additive blending only ever brightens, so fogging one toward the sector's
	// fade colour tints light that should simply get weaker with distance: in a fogged room a BFG's
	// glow came out carrying the fog's colour in Vulkan and clean in GL.
	if (additive) mp.fadeColor = 0;

	mp.sortX = sortX; mp.sortY = sortY; mp.sortZ = sortZ;

	if (fua_decal_log)
		Printf("decal: %s  blend %d  redToAlpha %d  light %d  rgb %.2f,%.2f,%.2f  alpha %.2f  "
			   "sort %.0f,%.0f,%.0f\n",
			material ? "tex" : "none", mp.blendMode, mp.redToAlpha ? 1 : 0, mp.lightLevel,
			mp.colorR, mp.colorG, mp.colorB, mp.alpha, mp.sortX, mp.sortY, mp.sortZ);

	DynAppend(tris, count, mp);
	g_decalsThisFrame++;
}

}} // namespace zx::levelmesh

// [rc4l] What each sky list holds now, and the walks that filled them since the last ask: two main walks a frame is the fault that hid wdiexsec's sky.
CCMD(fua_sky_quads)
{
	static int lastMain = 0, lastCamera = 0;
	int heldMain = 0, heldCamera = 0;
	zx::levelmesh::SkyQuadVertices(heldMain, zx::levelmesh::kSkyCaptureMain);
	zx::levelmesh::SkyQuadVertices(heldCamera, zx::levelmesh::kSkyCaptureCamera);
	const int walksMain = zx::levelmesh::SkyWalks(zx::levelmesh::kSkyCaptureMain);
	const int walksCamera = zx::levelmesh::SkyWalks(zx::levelmesh::kSkyCaptureCamera);
	Printf("sky quads captured since load: %d (fua_mesh_sky is %s)\n", zx::levelmesh::SkyQuadsCaptured(),
		fua_mesh_sky ? "on" : "off");
	Printf("  player's view holds %d after %d walks; camera textures hold %d after %d walks\n",
		heldMain / 6, walksMain - lastMain, heldCamera / 6, walksCamera - lastCamera);
	for (int i = 0; i < zx::levelmesh::StackViewCount(); i++)
	{
		int heldStack = 0, wallVerts = 0;
		zx::levelmesh::SkyQuadVertices(heldStack, zx::levelmesh::kSkyCaptureStack + i);
		zx::levelmesh::StackViewVertices(i, wallVerts);
		const zx::levelmesh::StackViewInfo v = zx::levelmesh::StackView(i);
		Printf("  stacked view %d: %s, moved %.0f,%.0f, %d stack walls, sky holds %d\n", i,
			v.ceiling ? "ceiling" : "floor", v.dx, v.dy, wallVerts / 6, heldStack / 6);
	}
	lastMain = walksMain;
	lastCamera = walksCamera;
	zx::levelmesh::PrintSkyWalkNotes();
}
