// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include <stddef.h>   // offsetof, for the cached-record guard
#include "features/levelmesh/wallcache.h"
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/levelmesh.h"
#include "features/levelmesh/computation/flatmesh_compute.h"

#include "r_defs.h"
#include "r_state.h"
#include "p_3dfloors.h"   // F3DFloor, for the 3D floor wall faces
#include "features/levelmesh/computation/lightband_compute.h"   // [rc4l] BandPlane, the band boundaries in map units
#include "doomdata.h"
#include "r_sky.h"   // skyflatnum, for the sky surfaces
#include "gl/scene/gl_drawinfo.h"
#include "features/hwrender/computation/walllight_compute.h"
#include "gl/data/gl_data.h"
#include "gl/data/gl_vertexbuffer.h"   // FFlatVertex
#include "gl/renderer/gl_renderer.h"   // in_area, area_default
#include "gl/renderer/gl_renderstate.h"
#include "gl/renderer/gl_lightdata.h"  // gl_SetColor / gl_SetFog
#include "c_dispatch.h"
#include "stats.h"   // cycle_t, to time the invalidation loop
#include "c_cvars.h"
#include "c_console.h"

// [rc4l] Declared at global scope: EXTERN_CVAR inside the namespace would name a different symbol
// than the CVAR defined in gl_drawinfo.cpp, and only the linker would notice.
EXTERN_CVAR(Bool, gl_wallmesh)
EXTERN_CVAR(Bool, fua_surface_mapbake_auto)
EXTERN_CVAR(Bool, fua_mesh_sky)
EXTERN_CVAR(Bool, gl_mirrors)
EXTERN_CVAR(Bool, fua_surface_bands)
// [rc4l] Print each banded wall the map bake splits: its span, the first boundary and the band that lights its top.
CVAR(Bool, fua_band_log, false, 0)
// [rc4l] Build wall heights and texture position from the MAP rather than from GLWall.
//
// ON. A wall's vertical span and both of its texture coordinates come from the map now, and the
// frame is pixel-identical to GL's on every map tested -- including Sunder MAP16, where 71,743
// surfaces are derived and six fall back. See features/surfaces/README.md for the table.
//
// What still comes from the capture is which surfaces exist at all, the special kinds (3D floor
// faces, skies, horizons), and the shading -- which is deliberately last, being the part where a
// second implementation drifted before.
CVAR(Bool, fua_surface_derive, true, 0)
// [rc4l] ...and its light, which was the last of a wall's appearance still taken from GLWall.
//
// Separable from the geometry because it is a different question with a different failure mode: a
// wrong span is a hole in the world, a wrong light level is a room that is subtly too bright. On,
// and pixel-identical: 0.0% on Doom 2 MAP01 and dbab04, 0.1% on Sunder MAP16 against a reload noise
// floor of 0.4%.
//
// This is not a second lighting implementation. CaptureShading calls the engine's own gl_SetColor
// and gl_SetFog either way; what changed is that its three inputs are read off the sector and the
// sidedef rather than off a GLWall that GL had to walk the BSP to produce.
CVAR(Bool, fua_surface_derive_light, true, 0)
EXTERN_CVAR(Int, gl_fogmode)
// [rc4l] The animated-texture re-resolve on replay, as a switch, so its cost can be A/B'd from the
// console instead of from two builds. Off renders stale animation frames -- a measurement aid, not
// a setting anyone should turn off.
CVAR(Bool, gl_wallcache_anim, true, 0)

#include "features/surfaces/surfacebuild.h"
#include "features/surfaces/computation/wallbands_compute.h"
#include "features/surfaces/computation/ffblocks_compute.h"
#include "features/horizon/computation/horizon_compute.h"

namespace zx { namespace levelmesh {

static TArray<SegCache> g_cache;
// [rc4l] Per-sector fua_dirty as of the last invalidation sweep, and the segs each sector owns.
//
// See InvalidateMovedSectors: a moving sector's geometry has to be dropped from the mesh whether or
// not the BSP happens to walk it, and answering "which segs belong to this sector" by scanning all
// segs every frame would be silly when the mapping never changes.
static TArray<int>          g_sectorDirty;
// [rc4l] Per-sidedef fua_dirty as of the last sweep, so a switch's new texture reaches the map bake.
static TArray<int>          g_sideDirty;
// [rc4l] Per-sector fua_placement as of the last sweep, so a scrolled floor reaches the map bake.
static TArray<int>          g_sectorPlacement;
static TArray<TArray<int> > g_sectorSegs;
static TArray<bool>     g_uncacheable;   // sticky: this seg produced a portal, never cache it
static int              g_captureSeg = -1;
static bool             g_sawPortal = false;
static int              g_hits = 0, g_misses = 0, g_uncacheableHits = 0;
// [rc4l] How many replayed walls came back with a DIFFERENT material than the one they were
// captured with -- which is the count of animated wall textures the cache would otherwise have
// frozen. Zero means the re-resolve below is dead weight; nonzero is the bug, measured.
static int              g_animRefresh = 0;
// [rc4l] Why each seg ended up as it did, ONE entry per seg rather than one per capture attempt.
//
// The reject counters below count attempts, and a refused seg re-captures every frame, so they say
// 541713 3D-floor rejects on a map with 1317 segs touching a 3D floor. That number cannot answer
// "how much of the level is missing and because of what", which is the only question that matters
// when a grate is absent from the Vulkan view. This can.
enum { SEG_UNSEEN = 0, SEG_OK, SEG_PORTAL, SEG_POLY, SEG_FFLOOR, SEG_AREA, SEG_OTHER };
static TArray<unsigned char> g_segFate;
// [rc4l] Why captures fail, split by cause. See EndCapture.
static int g_rejPortal = 0, g_rejPoly = 0, g_rejFFloor = 0, g_rejArea = 0, g_rejOther = 0,
           g_captureOk = 0;
int                     g_wallcacheMode = 1;
// [rc4l] Bumped once per level load. A backend that wants to set itself up per level needs to know a
// level happened, and "numsegs changed" is not that -- reloading the same map gives the same count.
static int              g_levelGeneration = 0;

int LevelGeneration() { return g_levelGeneration; }

void AllocForLevel(int numsegs)
{
	g_levelGeneration++;
	// [rc4l] The flats and sprites belong to the level too, and used to outlive it.
	//
	// AllocForLevel resized the WALL cache and left the flat table alone, so after a map change
	// every flat key still named a subsector index from the previous level and still owned a mesh
	// range in an arena that MeshInitForLevel was about to wipe. Anything that read a cached flat
	// afterwards -- the standalone Vulkan frame, or fua_surface_verify -- was reading the old
	// level through pointers the new one had already reused, which took the process down with
	// nothing in the log.
	//
	// Cleared HERE and not in MeshInitForLevel because ClearFlats gives its ranges back to the
	// mesh, and this runs while that mesh is still the one that handed them out.
	// The per-sidedef bake owners belong to the level that is going away.
	extern void ClearSideOwners();
	ClearSideOwners();
	zx::levelmesh::ClearFlats();
	zx::levelmesh::ClearSprites();
	g_cache.Clear();
	g_uncacheable.Clear();
	if (numsegs <= 0) return;
	g_cache.Resize(numsegs);
	g_uncacheable.Resize(numsegs);
	g_segFate.Clear();
	g_segFate.Resize(numsegs);
	for (int i = 0; i < numsegs; i++) g_segFate[i] = SEG_UNSEEN;
	g_sectorDirty.Clear();
	g_sectorPlacement.Clear();
	g_sectorSegs.Clear();
	g_sideDirty.Clear();
	if (numsides > 0)
	{
		g_sideDirty.Resize(numsides);
		for (int i = 0; i < numsides; i++) g_sideDirty[i] = sides[i].fua_dirty;
	}
	if (numsectors > 0)
	{
		g_sectorDirty.Resize(numsectors);
		g_sectorPlacement.Resize(numsectors);
		g_sectorSegs.Resize(numsectors);
		for (int i = 0; i < numsectors; i++)
		{
			g_sectorDirty[i] = sectors[i].fua_dirty;
			g_sectorPlacement[i] = sectors[i].fua_placement;
			g_sectorSegs[i].Clear();
		}
		for (int i = 0; i < numsegs; i++)
		{
			if (segs[i].frontsector != NULL)
				g_sectorSegs[int(segs[i].frontsector - sectors)].Push(i);
			if (segs[i].backsector != NULL && segs[i].backsector != segs[i].frontsector)
				g_sectorSegs[int(segs[i].backsector - sectors)].Push(i);
		}
	}

	for (int i = 0; i < numsegs; i++)
	{
		// Belt and braces with SegCache's constructor: Resize is not guaranteed to construct a
		// recycled slot, and a stale range here corrupts the new level's mesh rather than merely
		// wasting a slot.
		g_cache[i].filled = false;
		g_cache[i].pieceCount = 0;
		g_cache[i].bakedCount = 0;
		g_cache[i].mapBaked = false;
		for (int k = 0; k < kMaxMapPieces; k++)
		{
			g_cache[i].pieces[k].list = 0;
			g_cache[i].pieces[k].range.offset = 0;
			g_cache[i].pieces[k].range.count = 0;
		}
		g_uncacheable[i] = false;
	}
	g_captureSeg = -1;
	ResetCaptureVRangeStats();
	ResetStats();
}

void FreeLevel()
{
	g_cache.Clear();
	g_uncacheable.Clear();
	g_captureSeg = -1;
}

const MeshRange *StaticWallRange(int packedIndex)
{
	if (packedIndex < 0) return NULL;
	const int seg = packedIndex / kMaxCachedPieces;
	const int piece = packedIndex % kMaxCachedPieces;
	if ((unsigned)seg >= g_cache.Size()) return NULL;
	const SegCache &sc = g_cache[seg];
	// [rc4l] NULL draws the wall from its own captured vertices, which is what GL must show once the map bake's ranges sit in these slots.
	if (!ComputeReplaysFromMesh(sc.mapBaked, piece, sc.pieceCount, sc.pieces[piece].range.count)) return NULL;
	return &sc.pieces[piece].range;
}

// [rc4l] A wall piece into the mesh, filed under the sidedef it was baked from, since working it out again from where the piece lies picks a neighbour on a narrow wall (WDI14's painting wall).
static void RegisterSegPiece(MeshPiece &mp, const seg_t *seg)
{
	if (seg != NULL && seg->sidedef != NULL) mp.linkId = numsubsectors + int(seg->sidedef - sides);
	MeshRegisterPiece(mp);
}

// [rc4l] Mirrors GLWall::SetupBatchState's first two lines -- deliberately, because those two lines
// ARE the definition of how a wall is lit. The shared CaptureShading does the work; this just supplies
// the wall's own rellight and its RENDERWALL_M2SNF exception (that type renders with fog forced off).
static void CaptureWallShading(const GLWall &wall, MeshPiece &mp)
{
	CaptureShading(wall.lightlevel, wall.rellight + getExtraLight(), wall.Colormap, mp,
		wall.type == RENDERWALL_M2SNF);
}

// [rc4l] Turn a captured seg's walls into persistent geometry. The fan the streaming path would have
// emitted is expanded to an independent triangle list, because a baked range is drawn with
// glMultiDrawArrays and a GL_TRIANGLE_FAN restarts at its own first vertex.
// [rc4l] Which of the two bakes owns this seg -- and it is a per-SEG question, not a per-level one.
//
// The map bake needs a light level for the wall, and there is one sector kind it cannot give one
// for: a sector with a 3D floor light list has no single light level, because SplitWall cuts the
// wall at every band and gives each fragment that band's own light and colormap. Deriving one number
// for the whole wall would be wrong in exactly the rooms people build 3D floors for, so
// BuildDerivedWallLight declines.
//
// What that must NOT mean is "there is no wall". It meant exactly that for a while: BakeSegFromMap
// squashed all three parts of every seg in such a sector, and on dbab01 an entire brick wall went
// missing and the lava room behind it came through -- 10.5% of the frame, and the ladder read 100%
// because the ladder never asks about light. So the seg stays with the capture, which knows how to
// take GL's split walls with their per-band shading, and the two paths divide the level cleanly
// instead of one path leaving holes in it.
// [rc4l] Which of the two bakes owns this seg -- and it is a per-SEG question, not a per-level one.
//
// The map bake needs a light level for the wall, and there is one sector kind it cannot give one
// for: a sector with a 3D floor light list has no single light level, because SplitWall cuts the
// wall at every band and gives each fragment that band's own light and colormap. Deriving one number
// for the whole wall would be wrong in exactly the rooms people build 3D floors for, so
// BuildDerivedWallLight declines.
//
// What that must NOT mean is "there is no wall". It meant exactly that for a while: BakeSegFromMap
// squashed all three parts of every seg in such a sector, and on dbab01 an entire brick wall went
// missing and the lava room behind it came through -- 10.5% of the frame, and the ladder read 100%
// because the ladder never asks about light. So the seg stays with the capture, which knows how to
// take GL's split walls with their per-band shading, and the two paths divide the level cleanly
// instead of one path leaving holes in it.
// [rc4l] One quad per SIDEDEF, not one per seg.
//
// The derivation draws the WHOLE LINEDEF -- fracleft 0, fracright 1, the line's own vertices -- which
// is what GLWall::Process does. Doing that once per seg means a linedef the BSP split into four segs
// contributes four identical coplanar quads, and the map bake walks every seg in the level, so it
// makes all four where the capture only ever made the ones GL happened to walk.
//
// They are duplicates in the exact sense: same corners, same material, same shading. Nothing on
// screen needs more than the first, and three of them are depth-buffer noise and wasted vertices.
static TArray<int> g_sideOwner;      // sidedef index -> the one seg that bakes it

static void BuildSideOwners()
{
	g_sideOwner.Clear();
	if (sides == NULL || numsides <= 0 || segs == NULL) return;
	g_sideOwner.Reserve(numsides);
	for (int i = 0; i < numsides; i++) g_sideOwner[i] = -1;
	for (int i = 0; i < numsegs; i++)
	{
		if (segs[i].sidedef == NULL) continue;
		const int sd = (int)(segs[i].sidedef - sides);
		if (sd < 0 || sd >= numsides) continue;
		if (g_sideOwner[sd] < 0) g_sideOwner[sd] = i;
	}
}

void ClearSideOwners() { g_sideOwner.Clear(); }

static bool SegOwnsItsSide(int segIndex)
{
	if (segs == NULL || segIndex < 0 || segIndex >= numsegs) return false;
	const side_t *sd = segs[segIndex].sidedef;
	if (sd == NULL) return false;
	// Rebuilt when the table does not match the level's sidedef count, which is what a level change
	// looks like from here. (An earlier version stamped this with level.totaltime -- which changes
	// every tic, so it rebuilt the whole table on every seg of every bake.)
	if (g_sideOwner.Size() != (unsigned)numsides) BuildSideOwners();
	const int idx = (int)(sd - sides);
	if (idx < 0 || (unsigned)idx >= g_sideOwner.Size()) return false;
	return g_sideOwner[idx] == segIndex;
}

static bool MapBakeOwnsSeg(int segIndex)
{
	if (!fua_surface_mapbake_auto) return false;
	if (segs == NULL || segIndex < 0 || segIndex >= numsegs) return false;
	zx::surfaces::DerivedWallLight dl;
	const sector_t *cmFrom = NULL;
	return zx::surfaces::BuildDerivedWallLight(&segs[segIndex], dl, cmFrom) && cmFrom != NULL;
}

void BakeSeg(int segIndex)
{
	if (!gl_wallmesh) return;   // [rc4l] the mesh draw path is off; baking would be pure cost
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return;
	// [rc4l] When the map OWNS this seg, the capture stands down for it.
	//
	// Both baking the same seg is not twice the work, it is a fight: the map bake assigns slots by
	// PART and the capture assigns them in the order pieces happen to arrive, so each overwrites the
	// other's slots with a different material and a different range, every frame, and every one of
	// those counts as a rebatch. Measured before this line existed: 11,820 scene rebuilds on Doom 2
	// MAP03 in under a minute.
	//
	// Per SEG and not per level, because the map bake cannot do every seg -- see MapBakeOwnsSeg.
	if (MapBakeOwnsSeg(segIndex)) return;
	SegCache &sc = g_cache[segIndex];
	sc.mapBaked = false;

	static FFlatVertex fan[GLWall::MAX_BATCH_FAN_VERTICES];
	static FFlatVertex tris[GLWall::MAX_BATCH_FAN_VERTICES * 3];

	// [rc4l] Collapse pieces this seg no longer produces.
	//
	// The mesh hands a backend fixed vertex ranges, and a backend draws them until told otherwise --
	// there is no "remove" in a bump-allocated arena. So a piece that stops existing has to be
	// squashed to zero area rather than abandoned, or it keeps rendering. Opening a door is exactly
	// this case: its middle texture disappears, and without this the shut door stayed drawn across
	// the doorway in the Vulkan view while GL showed the room behind it.
	for (int i = sc.pieceCount; i < sc.bakedCount && i < kMaxMapPieces; i++)
	{
		MeshRange &r = sc.pieces[i].range;
		if (r.count == 0) continue;
		const unsigned int n = r.count;
		if (n > (unsigned)(GLWall::MAX_BATCH_FAN_VERTICES * 3)) { r.count = 0; continue; }
		// All vertices at one point: the triangles have no area and rasterise to nothing.
		memset(tris, 0, n * sizeof(FFlatVertex));
		MeshStore(r, tris, (int)n);
	}
	sc.bakedCount = sc.pieceCount;

	for (int i = 0; i < sc.pieceCount; i++)
	{
		const int fanCount = sc.walls[i].BuildFanVertices(fan, GLWall::MAX_BATCH_FAN_VERTICES);
		// [rc4l] The first thing in this renderer that DERIVES a surface instead of transcribing one.
		//
		// features/surfaces has been able to work out a sidedef's vertical span and texture position
		// from the map for a while, and until now nothing read the answer -- three ladders scored it and
		// the renderer went on using GL's. This puts the derived numbers in the mesh, for the two things
		// the ladders actually measure and nothing else: the heights, and where the picture sits on
		// them. The horizontal coordinate is derived too, by GLWall::Process's own rule: the whole
		// linedef for an ordinary wall and the seg's share of it for a polyobject.
		//
		// Only the plain four-corner case: gl_seamless adds vertices along the edges to hide cracks,
		// and their heights are interpolated rather than derived. Those fall back and are counted, so
		// "how much of the world is derived" is a number rather than an impression.
		if (fua_surface_derive && fanCount != 4) zx::surfaces::NoteDeriveSeamFallback();
		if (fua_surface_derive && fanCount == 4)
		{
			zx::surfaces::DerivedWallSpan d;
			if (zx::surfaces::BuildDerivedWallSpan(sc.walls[i].seg, sc.walls[i].type, d))
			{
				// The fan is bottom-left, top-left, top-right, bottom-right -- see BuildFanVertices.
				fan[0].z = d.zbottom[0]; fan[0].v = d.vBottom[0];
				fan[1].z = d.ztop[0];    fan[1].v = d.vTop[0];
				fan[2].z = d.ztop[1];    fan[2].v = d.vTop[1];
				fan[3].z = d.zbottom[1]; fan[3].v = d.vBottom[1];
				fan[0].u = fan[1].u = d.uLeft;
				fan[2].u = fan[3].u = d.uRight;
			}
		}
		const int triVerts = ComputeFanTriangleVertexCount(fanCount);
		// [rc4l] Give the range back before forgetting it, or the piece registered against it is
		// orphaned: it stays in the piece list drawing the geometry that was there, and the next
		// successful store allocates a FRESH range -- so the surface ends up in the list twice.
		//
		// Zeroing the count also disables the retire inside MeshStore, which only fires when the count
		// is non-zero. Two ways of saying "this is empty now" that do not agree is what left 13% of
		// live pieces duplicating another piece's geometry.
		if (triVerts <= 0) { MeshRetireRange(sc.pieces[i].range); sc.pieces[i].range.count = 0; continue; }

		int w = 0;
		for (int t = 0; t < fanCount - 2; t++)
			for (int c = 0; c < 3; c++)
				tris[w++] = fan[ComputeFanTriangleVertex(fanCount, t, c)];

		// MeshStore has already retired the old range if it reallocated; if it FAILED there is nothing
		// stored, and the same reasoning as above applies to whatever was there before.
		if (!MeshStore(sc.pieces[i].range, tris, w))
		{
			MeshRetireRange(sc.pieces[i].range);
			sc.pieces[i].range.count = 0;
			continue;
		}

		// [rc4l] Register the shading state alongside the geometry, so a backend can group and draw
		// without ever seeing a GLWall.
		MeshPiece mp;
		mp.range = sc.pieces[i].range;
		mp.material = sc.walls[i].gltexture;
		mp.lightLevel = sc.walls[i].lightlevel;
		mp.lightColor = sc.walls[i].Colormap.LightColor.d;
		mp.fadeColor = sc.walls[i].Colormap.FadeColor.d;
		// [rc4l] The light this wall is shaded with, derived rather than taken from GLWall.
		//
		// Not a second lighting implementation -- CaptureShading calls the engine's own gl_SetColor
		// and gl_SetFog either way, which is what has kept the two renderers agreeing. What changes
		// is where its three INPUTS come from: the sector's light, the sidedef's fake contrast, and
		// the colormap are map data, and taking them from the map is the last thing standing between
		// a wall's appearance and needing GL to have walked the BSP first.
		//
		// A sector with a 3D floor light list has no single light level -- SplitWall cuts the wall
		// into bands, each with its own -- so those keep the capture and are counted.
		bool litFromMap = false;
		if (fua_surface_derive_light)
		{
			zx::surfaces::DerivedWallLight dl;
			const sector_t *cmFrom = NULL;
			if (zx::surfaces::BuildDerivedWallLight(sc.walls[i].seg, dl, cmFrom) && cmFrom != NULL)
			{
				// sector_t::ColorMap is an FDynamicColormap*, and GLWall::Process copies it into an
				// FColormap by assignment -- the same conversion, spelled the same way.
				FColormap cm;
				cm = cmFrom->ColorMap;
				zx::levelmesh::CaptureShading(dl.lightLevel, dl.relLight + getExtraLight(), cm, mp,
					sc.walls[i].type == RENDERWALL_M2SNF);
				mp.lightLevel = dl.lightLevel;
				mp.lightColor = cm.LightColor.d;
				mp.fadeColor = cm.FadeColor.d;
				litFromMap = true;
			}
		}
		if (!litFromMap) CaptureWallShading(sc.walls[i], mp);

		// [rc4l] AFTER CaptureShading, which resets alpha to 1 and blendMode to 0 on the way past.
		//
		// Setting them before it is silently undone, and the surface renders opaque with no sign
		// anything was captured -- which is precisely how a pane of glass survived a fix that was
		// otherwise correct. The flat path already had this ordering; the wall path did not, and the
		// two were written a day apart.
		//
		// A wall is not always opaque: two-sided middle textures carry the LINEDEF's alpha, and the
		// sides of a translucent 3D floor carry the rover's. The draw list the engine chose is what
		// settles it -- a frosted-glass texture keeps alpha 1 and hides its transparency in the
		// texture's own alpha channel.
		mp.alpha = sc.walls[i].alpha;
		{
			const int list = sc.pieces[i].list;
			const bool trans = (list == GLDL_TRANSLUCENT || list == GLDL_TRANSLUCENTBORDER);
			mp.blendMode = ComputeWallBlendMode(trans, sc.walls[i].RenderStyle == STYLE_Add,
			                                    sc.walls[i].alpha);
		}
		mp.dynLightIndex = sc.walls[i].dynlightindex;

		// [rc4l] Wall normal: perpendicular to the seg's horizontal direction, in mesh space
		// (x, z-up, y). Which of the two perpendiculars is the front follows the same winding the
		// geometry uses, so the front face and its normal agree.
		{
			const float dx = sc.walls[i].glseg.x2 - sc.walls[i].glseg.x1;
			const float dy = sc.walls[i].glseg.y2 - sc.walls[i].glseg.y1;
			const float len = sqrtf(dx*dx + dy*dy);
			if (len > 0.0001f)
			{
				mp.normX = dy / len;
				mp.normY = 0.f;
				mp.normZ = -dx / len;
			}
		}

		// [rc4l] The sidedef part this wall came from, so animated textures keep animating. See
		// MeshPiece::texId. Only the three ordinary parts map cleanly; 3D-floor and special walls
		// are left unresolved and simply do not animate.
		mp.baseTex = NULL;
		if (sc.walls[i].seg != NULL && sc.walls[i].seg->sidedef != NULL)
		{
			const side_t *sd = sc.walls[i].seg->sidedef;
			switch (sc.walls[i].type)
			{
			// [rc4l] ByIndex, not operator[]: the latter applies the animation translation and hands
			// back whichever frame is showing, which makes the BASE texture a moving target. See
			// the same note in features/surfaces/surfacebuild.cpp.
			case RENDERWALL_TOP:    mp.baseTex = TexMan.ByIndex(sd->GetTexture(side_t::top).GetIndex()); break;
			case RENDERWALL_BOTTOM: mp.baseTex = TexMan.ByIndex(sd->GetTexture(side_t::bottom).GetIndex()); break;
			case RENDERWALL_M1S:
			case RENDERWALL_M2S:
			case RENDERWALL_M2SNF:  mp.baseTex = TexMan.ByIndex(sd->GetTexture(side_t::mid).GetIndex()); break;
			default: break;
			}
		}
		// [rc4l] ...and anything the switch does not name still needs A base texture.
		//
		// Leaving it null was described above as "simply does not animate", and it is worse than
		// that: the piece is registered with no identity for the animation pass to re-resolve from,
		// and fua_mesh_verify counts it as a fault -- 435 pieces on dbab02 in the GL-driven mesh
		// alone. A 3D floor block or a special wall still knows what it is drawing; the GLWall's own
		// material is that answer, and it is what the piece is already being drawn with.
		if (mp.baseTex == NULL && sc.walls[i].gltexture != NULL)
			mp.baseTex = sc.walls[i].gltexture->tex;
		mp.plain = ComputeWallPlainPass(sc.walls[i].type);
		RegisterSegPiece(mp, sc.walls[i].seg);
	}
}

// [rc4l] Bake a seg from the MAP, with no GLWall involved at all.
//
// This is the last dependency in the phase: everything about a wall is derived now except WHICH
// walls there are, and that still comes from GL walking the BSP and telling us what it drew. A bake
// driven by the map answers it from the sidedef, which is where Doom keeps it.
//
// fua_surface_mapcover measured whether that is possible before this was written: on dbab04 the map
// accounts for 1332 of the 1336 parts GL draws, and on Sunder MAP16 for 59,477 of 59,483 across
// 52,052 segs. The handful left over are why the capture is still there.
//
// Slots are assigned by PART -- upper, lower, middle -- rather than in the order pieces happen to
// arrive, which the capture path cannot do and which makes a re-bake reuse the same mesh range every
// time.
// [rc4l] Slots per part, so a part+band always lands in the SAME mesh range across re-bakes.
//
// Assigning slots in the order pieces happen to come out would move a part's range whenever the band
// count changed -- a lift crossing a 3D floor does that several times a second -- and a range that
// moves is a rebatch, which is a full scene rebuild. Fixed arithmetic keeps the property the
// per-part assignment had before bands existed.
static const int kBandsPerPart = 5;

// [rc4l] Where the 3D floor faces live, above the parts. A sidedef with more slabs than this keeps
// as many as fit, which is a missing face on one wall rather than a wrong one everywhere.
static const int kFFSlot0 = 3 * kBandsPerPart;
static const int kMaxFFBlocks = kMaxMapPieces - kFFSlot0;

// The rovers that produced each block, so the emitter can read their flags back.
static F3DFloor *g_ffRoverOf[16];

// [rc4l] An engine plane in map units for the pure band helpers; reading one through FLOAT2FIXED's ints once took the double ZatPoint and lost a factor of 65536.
static zx::levelmesh::BandPlane ToBandPlane(const secplane_t &p)
{
	return zx::levelmesh::ComputeBandPlaneFromFixed((double)p.a, (double)p.b, (double)p.ic, (double)p.d);
}

// [rc4l] Where each light band ends at a wall's two ends: band i is bounded below by light i+1.
static void BandBottoms(const TArray<lightlist_t> &lights, int nLights, float x1, float y1, float x2, float y2,
	float out[][2])
{
	for (int i = 0; i + 1 < nLights; i++)
		zx::levelmesh::ComputeBandBottomAtEnds(ToBandPlane(lights[i + 1].plane), x1, y1, x2, y2, out[i]);
}

// A 3D floor plane evaluated at the wall's two ends, the way GLWall::GetPlanePos does it.
static void PlaneAt(const F3DFloor::planeref &pr, float x0, float y0, float x1, float y1, float *out)
{
	const secplane_t *p = pr.plane;
	if (p->a | p->b)
	{
		out[0] = zx::levelmesh::ComputeBandPlaneZ(ToBandPlane(*p), x0, y0);
		out[1] = zx::levelmesh::ComputeBandPlaneZ(ToBandPlane(*p), x1, y1);
	}
	else if (pr.isceiling == sector_t::ceiling) out[0] = out[1] = FIXED2FLOAT(p->d);
	else                                        out[0] = out[1] = FIXED2FLOAT(-p->d);
}

// [rc4l] A slab as the block walk sees it along this span: where it sits at both ends, which way it draws sides, and its clip kind.
static void ToFFRover(F3DFloor *rover, float x0, float y0, float x1, float y1, zx::surfaces::FFRover &r)
{
	PlaneAt(rover->top, x0, y0, x1, y1, r.top);
	PlaneAt(rover->bottom, x0, y0, x1, y1, r.bottom);
	r.renderSides = (rover->flags & FF_RENDERSIDES) != 0;
	r.invertSides = (rover->flags & FF_INVERTSIDES) != 0;
	r.kind = rover->flags & (FF_SWIMMABLE | FF_TRANSLUCENT);
}


// Why a reshade handed the seg back to the full bake, so the fallback rate is explainable.
static int g_reshadeFF = 0, g_reshadeNoLight = 0, g_reshadeNotOwner = 0, g_reshadeNoPiece = 0;

// [rc4l] Re-light a seg WITHOUT re-deriving it. Returns pieces repainted, or -1 if it declined.
//
// The case this exists for is a sector whose light changed and whose planes did not -- three
// quarters of all invalidations on a live level, because Doom's light effects run every tic and
// sector_t bumps fua_dirty for a flicker exactly as it does for a lift. BakeSegFromMap answered
// those by working the wall out from the map again: spans, UVs, band splits, materials, six
// vertices per band, all to arrive at the same geometry it already had and one different light.
//
// If the planes did not move, the band structure cannot have changed -- band boundaries are 3D-floor
// plane heights. So the only thing to redo is each band's light and the CaptureShading that follows
// it, and the rest can be read back off the piece the last bake registered.
//
// It declines rather than guesses whenever it is not certain the geometry is untouched: a seg it
// never baked, one carrying 3D-floor pieces (whose light comes from an F3DFloor, not the band list),
// or one whose stored piece has gone. The caller falls back to the full bake, which is always right.
static int ReshadeSegFromMap(int segIndex)
{
	if (!gl_wallmesh) return -1;
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return -1;
	if (segs == NULL || segIndex >= numsegs) return -1;
	const seg_t *seg = &segs[segIndex];
	if (seg->sidedef == NULL || seg->linedef == NULL || seg->frontsector == NULL) return -1;


	SegCache &sc = g_cache[segIndex];
	if (sc.hasOwnLitPiece) { g_reshadeFF++; return -1; }   // [rc4l] its light is not in bandLight

	zx::surfaces::DerivedWallLight dl;
	const sector_t *cmFrom = NULL;
	if (!zx::surfaces::BuildDerivedWallLight(seg, dl, cmFrom) || cmFrom == NULL) { g_reshadeNoLight++; return -1; }
	if (!SegOwnsItsSide(segIndex)) { g_reshadeNotOwner++; return -1; }

	const sector_t *front = seg->frontsector;
	const TArray<lightlist_t> *lights =
		(front != NULL && front->e != NULL) ? &front->e->XFloor.lightlist : NULL;
	int nLights = (fua_surface_bands && lights != NULL) ? (int)lights->Size() : 0;
	if (nLights > kBandsPerPart) nLights = kBandsPerPart;

	const int midType = (seg->backsector != NULL) ? RENDERWALL_M2S : RENDERWALL_M1S;
	const int kParts[3] = { RENDERWALL_TOP, RENDERWALL_BOTTOM, midType };

	int done = 0;
	for (int slot = 0; slot < kMaxMapPieces; slot++)
	{
		const MeshRange &range = sc.pieces[slot].range;
		if (range.count == 0) continue;
		const zx::levelmesh::MeshPiece *had = zx::levelmesh::MeshFindPiece(range.offset);
		if (had == NULL || had->range.count == 0) { g_reshadeNoPiece++; return -1; }

		// The band's light, by the same rule the bake uses -- see the band loop.
		int lightLevel = dl.lightLevel;
		FColormap cm;
		cm = cmFrom->ColorMap;
		const int li = sc.bandLight[slot];
		if (li >= 0 && li < nLights)
		{
			const lightlist_t &ll = (*lights)[li];
			if (ll.p_lightlevel != &seg->sidedef->sector->lightlevel)
				lightLevel = gl_ClampLight(*ll.p_lightlevel);
			cm.CopyLightColor(ll.extra_colormap);
		}

		// Everything that is not light comes back off the registered piece, which is the point.
		zx::levelmesh::MeshPiece mp = *had;
		const void *savedBase = mp.baseTex;
		const float nx = mp.normX, ny = mp.normY, nz = mp.normZ;
		// A translucent 3D-floor slab carries its own alpha and blend mode, set after the bake's
		// CaptureShading. Re-running it would hand them back their opaque defaults and the slab would
		// turn solid, so they are preserved here exactly as the base texture and normal are.
		const float savedAlpha = mp.alpha;
		const int   savedBlend = mp.blendMode;
		zx::levelmesh::CaptureShading(lightLevel, dl.relLight + getExtraLight(), cm, mp,
			slot < kFFSlot0 && kParts[slot / kBandsPerPart] == RENDERWALL_M2SNF);
		// AFTER CaptureShading, which clears baseTex and resets alpha and blend mode on the way past.
		mp.baseTex    = savedBase;
		mp.alpha      = savedAlpha;
		mp.blendMode  = savedBlend;
		mp.lightLevel = lightLevel;
		mp.lightColor = cm.LightColor.d;
		mp.fadeColor  = cm.FadeColor.d;
		mp.normX = nx; mp.normY = ny; mp.normZ = nz;
		RegisterSegPiece(mp, seg);
		done++;
	}
	return done;
}

// [rc4l] One flooded gap: the wall quad, textured per fragment from the back plane and lit by DrawFloodedPlane's rule (that sector's plane light and colormap, extra light only).
static bool BakeFloodPiece(MeshRange &range, const zx::surfaces::DerivedWallSpan &d)
{
	static FFlatVertex tris[6];
	FFlatVertex fan[4];
	fan[0].Set(d.x1, d.zbottom[0], d.y1, d.uLeft,  d.vBottom[0]);
	fan[1].Set(d.x1, d.ztop[0],    d.y1, d.uLeft,  d.vTop[0]);
	fan[2].Set(d.x2, d.ztop[1],    d.y2, d.uRight, d.vTop[1]);
	fan[3].Set(d.x2, d.zbottom[1], d.y2, d.uRight, d.vBottom[1]);
	int w = 0;
	for (int t = 0; t < 2; t++)
		for (int c = 0; c < 3; c++)
			tris[w++] = fan[ComputeFanTriangleVertex(4, t, c)];
	if (!MeshStore(range, tris, w)) { MeshSquash(range); return false; }

	const sector_t *sec = d.floodSector;
	const FMaterial *mat = (const FMaterial *)d.material;
	FColormap cm;
	cm = sec->ColorMap;
	int lightLevel = d.floodCeiling ? sec->GetCeilingLight() : sec->GetFloorLight();
	if (lightLevel < 0) lightLevel = -lightLevel;   // [rc4l] DrawFloodedPlane takes abs() of it
	ApplyFullbright(mat, cm, lightLevel);
	MeshPiece mp;
	mp.range = range;
	mp.material = d.material;
	zx::levelmesh::CaptureShading(lightLevel, getExtraLight(), cm, mp, false);
	// [rc4l] After CaptureShading, which resets what it does not decide.
	mp.baseTex = d.baseTex;
	mp.lightLevel = lightLevel;
	mp.lightColor = cm.LightColor.d;
	mp.fadeColor = cm.FadeColor.d;
	mp.flood = true;
	mp.floodZ = d.floodZ;
	for (int i = 0; i < 6; i++) mp.floodUV[i] = d.floodUV[i];
	MeshRegisterPiece(mp);
	return true;
}

int BakeSegFromMap(int segIndex)
{
	// [rc4l] DECLINED (-1) and "took it, made nothing" (0) are different answers.
	//
	// InvalidateMovedSectors needs to tell them apart: a seg this bake declined still belongs to
	// GL's capture and has to be squashed so the walk re-fills it, while a seg the bake took has
	// already had every slot it did not fill squashed by the loops below. Returning 0 for both meant
	// the second kind was squashed a second time -- zeroing ranges that had just been written and
	// marking them dirty again, every frame, which is the cost this whole change exists to remove.
	if (!gl_wallmesh) return -1;
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return -1;
	if (segs == NULL || segIndex >= numsegs) return -1;
	const seg_t *seg = &segs[segIndex];
	if (seg->sidedef == NULL || seg->linedef == NULL || seg->frontsector == NULL) return -1;

	SegCache &sc = g_cache[segIndex];
	static FFlatVertex tris[6];

	const int midType = (seg->backsector != NULL) ? RENDERWALL_M2S : RENDERWALL_M1S;
	const int kParts[3] = { RENDERWALL_TOP, RENDERWALL_BOTTOM, midType };

	zx::surfaces::DerivedWallLight dl;
	const sector_t *cmFrom = NULL;
	// Not ours: leave every range exactly as it is and let the capture keep this seg. Squashing here
	// is what put a hole in dbab01 -- see MapBakeOwnsSeg.
	if (!zx::surfaces::BuildDerivedWallLight(seg, dl, cmFrom) || cmFrom == NULL) return -1;

	// One quad per sidedef -- see SegOwnsItsSide. Every other seg on it would build the same four
	// corners again, in the same plane, and the two would stipple against each other.
	if (!SegOwnsItsSide(segIndex)) return -1;
	sc.mapBaked = true;

	// [rc4l] A horizon line or a mirror is taken and left empty, since GL draws no wall on either -- see features/horizon and RenderMirrors.
	if (zx::horizon::ComputeIsHorizonLine(seg->linedef->special) ||
	    ComputeIsMirrorWall(seg->linedef->special, seg->backsector == NULL, gl_mirrors))
	{
		for (int k = 0; k < kMaxMapPieces; k++) MeshSquash(sc.pieces[k].range);
		return 0;
	}

	// [rc4l] The light bands the sector's 3D floors cast on every wall in it.
	//
	// A sector with 3D floors has no single light level: each floor contributes a band, and a wall
	// crossing one is drawn as two pieces with two lights. Computed per part because the span being
	// cut is the part's -- see wallbands_compute for the rule, which is GLWall::SplitWall's.
	sc.hasOwnLitPiece = false;   // [rc4l] set below; a fog boundary or a flood is a piece a reshade cannot redo
	const sector_t *front = seg->frontsector;
	const TArray<lightlist_t> *lights =
		(front != NULL && front->e != NULL) ? &front->e->XFloor.lightlist : NULL;
	int nLights = (fua_surface_bands && lights != NULL) ? (int)lights->Size() : 0;
	if (nLights > kBandsPerPart) nLights = kBandsPerPart;

	int made = 0;
	for (int part = 0; part < 3; part++)
	{
		const int slot0 = part * kBandsPerPart;

		zx::surfaces::DerivedWallSpan d;
		if (!zx::surfaces::BuildDerivedWallSpan(seg, kParts[part], d))
		{
			// [rc4l] Nothing here now -- SQUASH it, do not give the range back.
			//
			// A part that comes and goes is the normal case: a door's upper exists when it is shut
			// and not when it is open, several times a second. Retiring the range means the next
			// re-bake allocates a fresh one at a different offset, which is a rebatch, which is a
			// full scene rebuild -- 4869 of them on dbab04 before this line said squash. Squashing
			// keeps the range so the geometry can come back into it, which is exactly the reasoning
			// InvalidateMovedSectors gives for doing the same thing.
			for (int k = 0; k < kBandsPerPart; k++) MeshSquash(sc.pieces[slot0 + k].range);
			continue;
		}

		// [rc4l] A flooded gap is one piece lit like the plane it shows, as DrawFloodedPlane lights it, so it takes no bands.
		if (d.flood)
		{
			if (BakeFloodPiece(sc.pieces[slot0].range, d)) { sc.hasOwnLitPiece = true; made++; }
			for (int k = 1; k < kBandsPerPart; k++) MeshSquash(sc.pieces[slot0 + k].range);
			continue;
		}

		float bandBottom[kBandsPerPart][2];
		if (nLights > 1) BandBottoms(*lights, nLights, d.x1, d.y1, d.x2, d.y2, bandBottom);

		zx::surfaces::WallBand bands[kBandsPerPart];
		const int n = zx::surfaces::ComputeWallBands(d.ztop, d.zbottom, bandBottom, nLights,
			bands, kBandsPerPart);
		if (fua_band_log && nLights > 1)
		{
			Printf("bands seg %d part %d: wall %.1f..%.1f, %d lights, first boundary %.1f, %d bands, first lit by %d\n",
				segIndex, part, d.ztop[0], d.zbottom[0], nLights, bandBottom[0][0], n, n > 0 ? bands[0].lightIndex : -1);
		}
		if (n <= 0)
		{
			for (int k = 0; k < kBandsPerPart; k++) MeshSquash(sc.pieces[slot0 + k].range);
			continue;
		}

		const float spanTop0 = d.ztop[0], spanBottom0 = d.zbottom[0];
		const float spanTop1 = d.ztop[1], spanBottom1 = d.zbottom[1];
		const float h0 = spanTop0 - spanBottom0, h1 = spanTop1 - spanBottom1;
		for (int bi = 0; bi < n; bi++)
		{
			const zx::surfaces::WallBand &band = bands[bi];
			MeshRange &range = sc.pieces[slot0 + bi].range;

			// v follows z linearly across the whole wall, so a band takes its own v off the parent's
			// pair rather than recomputing it from the texture -- the same interpolation SplitWall
			// does when it cuts a copy of itself.
			const float vt0 = (h0 != 0.f) ? d.vTop[0] + (spanTop0 - band.ztop[0])    * (d.vBottom[0] - d.vTop[0]) / h0 : d.vTop[0];
			const float vb0 = (h0 != 0.f) ? d.vTop[0] + (spanTop0 - band.zbottom[0]) * (d.vBottom[0] - d.vTop[0]) / h0 : d.vBottom[0];
			const float vt1 = (h1 != 0.f) ? d.vTop[1] + (spanTop1 - band.ztop[1])    * (d.vBottom[1] - d.vTop[1]) / h1 : d.vTop[1];
			const float vb1 = (h1 != 0.f) ? d.vTop[1] + (spanTop1 - band.zbottom[1]) * (d.vBottom[1] - d.vTop[1]) / h1 : d.vBottom[1];

			FFlatVertex fan[4];
			fan[0].Set(d.x1, band.zbottom[0], d.y1, d.uLeft,  vb0);
			fan[1].Set(d.x1, band.ztop[0],    d.y1, d.uLeft,  vt0);
			fan[2].Set(d.x2, band.ztop[1],    d.y2, d.uRight, vt1);
			fan[3].Set(d.x2, band.zbottom[1], d.y2, d.uRight, vb1);
			int w = 0;
			for (int t = 0; t < 2; t++)
				for (int c = 0; c < 3; c++)
					tris[w++] = fan[ComputeFanTriangleVertex(4, t, c)];

			if (!MeshStore(range, tris, w)) { MeshSquash(range); continue; }

			MeshPiece mp;
			mp.range = range;
			mp.material = d.material;

			// [rc4l] The band's light, by GLWall::Put3DWall's rule.
			//
			// Only the light LEVEL is conditional -- a band whose level points at the seg's own
			// sector is a light-transfer effect and must not override it -- while the colormap is
			// copied whenever the piece went through Put3DWall at all. The uppermost piece does not:
			// SplitWall puts that one with PutWall, which alters neither.
			int lightLevel = dl.lightLevel;
			FColormap cm;
			cm = cmFrom->ColorMap;
			// Remembered so ReshadeSegFromMap can redo exactly this without re-deriving the wall.
			sc.bandLight[slot0 + bi] =
				(!band.ownLight && band.lightIndex < nLights) ? (signed char)band.lightIndex : -1;
			if (!band.ownLight && band.lightIndex < nLights)
			{
				const lightlist_t &ll = (*lights)[band.lightIndex];
				if (ll.p_lightlevel != &seg->sidedef->sector->lightlevel)
					lightLevel = gl_ClampLight(*ll.p_lightlevel);
				cm.CopyLightColor(ll.extra_colormap);
			}

			zx::levelmesh::CaptureShading(lightLevel, dl.relLight + getExtraLight(), cm, mp,
				kParts[part] == RENDERWALL_M2SNF);
			// [rc4l] AFTER CaptureShading, which ends by clearing baseTex.
			//
			// It sets the fields a surface has no opinion about -- blend mode, alpha, sort position,
			// base texture -- back to their defaults, on the assumption the caller fills in whatever
			// it actually knows afterwards. The capture path does exactly that and has always been
			// fine. This one set it BEFORE and had it wiped every time: 1800 of dbab02's map-baked
			// walls carried no base texture at all, which is the identity the animation pass
			// re-resolves a material from, so a map-driven nukage wall could never have flowed.
			mp.baseTex = d.baseTex;
			mp.lightLevel = lightLevel;
			mp.lightColor = cm.LightColor.d;
			mp.fadeColor = cm.FadeColor.d;
			// [rc4l] A two-sided middle blends as GL draws it: the linedef's alpha and style, or the texture's own see-through texels.
			if (kParts[part] == RENDERWALL_M2S || kParts[part] == RENDERWALL_M2SNF)
			{
				const line_t *ln = seg->linedef;
				const float lineAlpha = FIXED2FLOAT(ln->Alpha);
				const bool additive = lineAlpha != 0.f && (ln->flags & ML_ADDTRANS) != 0;
				const FMaterial *fm = (const FMaterial *)d.material;
				const bool trans = ComputeMidTextureTranslucent(lineAlpha, additive, fm != NULL && fm->GetTransparent());
				mp.alpha = (lineAlpha != 0.f) ? lineAlpha : 1.f;
				mp.blendMode = ComputeWallBlendMode(trans, additive, mp.alpha);
			}
			// [rc4l] The wall's normal: perpendicular to its direction, in mesh space (x, z-up, y), on
			// the side the winding faces -- the same rule the capture path uses, so front faces agree.
			{
				const float dx = d.x2 - d.x1, dy = d.y2 - d.y1;
				const float len = sqrtf(dx*dx + dy*dy);
				if (len > 0.0001f) { mp.normX = dy / len; mp.normY = 0.f; mp.normZ = -dx / len; }
			}
			mp.plain = ComputeWallPlainPass(kParts[part]);
			RegisterSegPiece(mp, seg);
			made++;
		}
		// Bands this part no longer has: hold the range, do not give it back.
		for (int k = n; k < kBandsPerPart; k++) MeshSquash(sc.pieces[slot0 + k].range);
	}
	// [rc4l] ...and the wall faces the sector's 3D floors put on this sidedef.
	//
	// A 3D floor is a slab hanging in the sector on the OTHER side of this line, and each one cuts a
	// block out of the wall behind it. GL builds these during its walk (DoFFloorBlocks), so when the
	// walk stops they vanish -- on dbab02 that was every red rock panel and the whole of that map's
	// 4.0% against the GL-driven picture.
	//
	// The slabs come from the BACK sector, because a slab is drawn on the wall you see it against.
	// The walk itself -- clip each to what the last one left, stop at the wall's bottom -- is in
	// ffblocks_compute, where it is tested against the cases GL's loop encodes.
	if (seg->backsector != NULL && seg->backsector->e != NULL)
	{
		const TArray<F3DFloor *> &ff = seg->backsector->e->XFloor.ffloors;
		// [rc4l] The span to cut is the OPENING, not the middle texture's.
		//
		// Asking BuildDerivedWallSpan for the middle looked tidy and gated the whole thing on the
		// sidedef having a midtexture -- which most two-sided lines with a 3D floor behind them do
		// not, so 32 sectors' worth of slabs produced three faces. DoFFloorBlocks takes the gap
		// between the planes instead: the lower of the two ceilings and the higher of the two floors,
		// each decided at BOTH ends together.
		struct { float x1, y1, x2, y2; float ztop[2]; float zbottom[2]; } mid;
		bool haveSpan = false;
		if (ff.Size() != 0 && seg->frontsector != NULL)
		{
			const vertex_t *lv1 = seg->linedef->v1, *lv2 = seg->linedef->v2;
			if (seg->sidedef != seg->linedef->sidedef[0]) { lv1 = seg->linedef->v2; lv2 = seg->linedef->v1; }
			mid.x1 = FIXED2FLOAT(lv1->x); mid.y1 = FIXED2FLOAT(lv1->y);
			mid.x2 = FIXED2FLOAT(lv2->x); mid.y2 = FIXED2FLOAT(lv2->y);
			const sector_t *fs = seg->frontsector, *bs = seg->backsector;
			float fch[2], bch[2], ffh[2], bfh[2];
			fch[0] = FIXED2FLOAT(fs->ceilingplane.ZatPoint(lv1)); fch[1] = FIXED2FLOAT(fs->ceilingplane.ZatPoint(lv2));
			bch[0] = FIXED2FLOAT(bs->ceilingplane.ZatPoint(lv1)); bch[1] = FIXED2FLOAT(bs->ceilingplane.ZatPoint(lv2));
			ffh[0] = FIXED2FLOAT(fs->floorplane.ZatPoint(lv1));   ffh[1] = FIXED2FLOAT(fs->floorplane.ZatPoint(lv2));
			bfh[0] = FIXED2FLOAT(bs->floorplane.ZatPoint(lv1));   bfh[1] = FIXED2FLOAT(bs->floorplane.ZatPoint(lv2));
			if (fch[0] < bch[0] && fch[1] < bch[1]) { mid.ztop[0] = fch[0]; mid.ztop[1] = fch[1]; }
			else                                    { mid.ztop[0] = bch[0]; mid.ztop[1] = bch[1]; }
			if (ffh[0] > bfh[0] && ffh[1] > bfh[1]) { mid.zbottom[0] = ffh[0]; mid.zbottom[1] = ffh[1]; }
			else                                    { mid.zbottom[0] = bfh[0]; mid.zbottom[1] = bfh[1]; }
			haveSpan = true;
		}
		if (haveSpan)
		{
			// [rc4l] Where the front sector's light bands end along THIS span.
			//
			// BuildFFBlock finishes with SplitWall, so a 3D floor's face is cut by the front
			// sector's light list exactly as an ordinary wall part is. Skipping that is what turned
			// dbab04's blue light strip grey: the geometry and the texture were already right and
			// only the colormap was the sector's instead of the band's.
			float ffBandBottom[kBandsPerPart][2];
			if (nLights > 1) BandBottoms(*lights, nLights, mid.x1, mid.y1, mid.x2, mid.y2, ffBandBottom);

			zx::surfaces::FFRover rovers[kMaxFFBlocks];
			int nr = 0;
			for (unsigned k = 0; k < ff.Size() && nr < kMaxFFBlocks; k++)
			{
				F3DFloor *rover = ff[k];
				if (rover == NULL || !(rover->flags & FF_EXISTS)) continue;
				ToFFRover(rover, mid.x1, mid.y1, mid.x2, mid.y2, rovers[nr]);
				// [rc4l] A fog slab DOES draw -- it just draws no texture.
				//
				// This used to skip them, reasoning that an untextured face is nothing for the mesh
				// to hold. It is not nothing: BuildFFBlock turns a fog slab into a flat translucent
				// panel of the light's fade colour, laid over the wall behind it, and the wall shows
				// through tinted. On dbab04 that is a blue water band, and skipping it left the band
				// grey with its geometry and its texture both already correct -- a colour-only fault,
				// which is the kind that survives every check that compares shapes.
				//
				// The capture path already registers such a piece with a NULL material, so the mesh
				// and the backend both handle one; there was nothing to add but the decision.
				g_ffRoverOf[nr] = rover;
				nr++;
			}

			// [rc4l] This side's own slabs, which take away the water a neighbour's water would wall off, as GL's ClipFFloors does.
			zx::surfaces::FFRover nearRovers[kMaxFFBlocks];
			int nn = 0;
			const sector_t *fs = seg->frontsector;
			for (unsigned k = 0; fs->e != NULL && k < fs->e->XFloor.ffloors.Size() && nn < kMaxFFBlocks; k++)
			{
				F3DFloor *rover = fs->e->XFloor.ffloors[k];
				if (rover != NULL && (rover->flags & FF_EXISTS))
					ToFFRover(rover, mid.x1, mid.y1, mid.x2, mid.y2, nearRovers[nn++]);
			}

			zx::surfaces::FFBlock blocks[kMaxFFBlocks];
			const int nb = zx::surfaces::ComputeFFBlocks(mid.ztop, mid.zbottom, rovers, nr,
				blocks, kMaxFFBlocks, nearRovers, nn);

			for (int bi = 0; bi < nb; bi++)
			{
				const zx::surfaces::FFBlock &blk = blocks[bi];
				F3DFloor *rover = g_ffRoverOf[blk.rover];
				MeshRange &range = sc.pieces[kFFSlot0 + bi].range;

				// [rc4l] Which texture, by BuildFFBlock's rule: the sidedef's upper or lower when the
				// slab asks for one, and otherwise the MASTER sidedef's middle -- the line that
				// defines the 3D floor, not the one being drawn on.
				const bool isFog = (rover->flags & FF_FOG) != 0;
				if (isFog)
				{
					// The fog plane defines the light, not the front sector -- GL's own comment.
					lightlist_t *fl = (rover->target != NULL && rover->top.plane != NULL) ?
						P_GetPlaneLight(rover->target, rover->top.plane, true) : NULL;
					if (fl == NULL || fl->extra_colormap == NULL) { MeshSquash(range); continue; }

					FFlatVertex ffan[4];
					ffan[0].Set(mid.x1, blk.bottom[0], mid.y1, 0.f, 0.f);
					ffan[1].Set(mid.x1, blk.top[0],    mid.y1, 0.f, 0.f);
					ffan[2].Set(mid.x2, blk.top[1],    mid.y2, 0.f, 0.f);
					ffan[3].Set(mid.x2, blk.bottom[1], mid.y2, 0.f, 0.f);
					int fgw = 0;
					for (int t = 0; t < 2; t++)
						for (int cc = 0; cc < 3; cc++)
							tris[fgw++] = ffan[ComputeFanTriangleVertex(4, t, cc)];
					if (!MeshStore(range, tris, fgw)) { MeshSquash(range); continue; }

					MeshPiece fmp;
					fmp.range = range;
					fmp.material = NULL;   // no texture: the panel is its colour and nothing else
					FColormap fcm;
					fcm.Clear();
					fcm.LightColor = fl->extra_colormap->Fade;
					zx::levelmesh::CaptureShading(gl_ClampLight(*fl->p_lightlevel), 0, fcm, fmp, false);
					fmp.baseTex = NULL;
					fmp.lightLevel = gl_ClampLight(*fl->p_lightlevel);
					fmp.lightColor = fcm.LightColor.d;
					fmp.fadeColor = fcm.FadeColor.d;
					fmp.alpha = rover->alpha / 255.0f;
					fmp.blendMode = ComputeSurfaceBlendMode((rover->flags & FF_ADDITIVETRANS) != 0,
						fmp.alpha);
					{
						const float dx = mid.x2 - mid.x1, dy = mid.y2 - mid.y1;
						const float len = sqrtf(dx*dx + dy*dy);
						if (len > 0.0001f) { fmp.normX = dy / len; fmp.normY = 0.f; fmp.normZ = -dx / len; }
					}
					RegisterSegPiece(fmp, seg);
					// A fog boundary takes its light straight off the F3DFloor rather than from the
					// band list, so it is the one piece bandLight cannot describe -- see Reshade.
					sc.hasOwnLitPiece = true;
					made++;
					continue;
				}

				side_t *mastersd = (rover->master != NULL) ? rover->master->sidedef[0] : NULL;
				FTextureID texid;
				int texpos;
				if (rover->flags & FF_UPPERTEXTURE)      { texid = seg->sidedef->GetTexture(side_t::top);    texpos = side_t::top; }
				else if (rover->flags & FF_LOWERTEXTURE) { texid = seg->sidedef->GetTexture(side_t::bottom); texpos = side_t::bottom; }
				else if (mastersd != NULL)               { texid = mastersd->GetTexture(side_t::mid);        texpos = side_t::mid; }
				else continue;

				FMaterial *fmat = FMaterial::ValidateTexture(texid, false, true);
				if (fmat == NULL) { MeshSquash(range); continue; }

				FTexCoordInfo tci;
				const side_t *scalesd = (rover->flags & (FF_UPPERTEXTURE | FF_LOWERTEXTURE)) ?
					seg->sidedef : mastersd;
				fmat->GetTexCoordInfo(&tci, scalesd->GetTextureXScale((side_t::ETexpart)texpos),
					scalesd->GetTextureYScale((side_t::ETexpart)texpos));
				if (tci.mRenderHeight == 0) { MeshSquash(range); continue; }

				// u: the master's x offset plus this sidedef's, over the line's texel length -- the
				// same pair BuildFFBlock adds together.
				const float toU = (rover->flags & (FF_UPPERTEXTURE | FF_LOWERTEXTURE)) ? 0.f :
					FIXED2FLOAT(tci.TextureOffset(mastersd->GetTextureXOffset(side_t::mid)));
				const float ul = tci.FloatToTexU(toU +
					FIXED2FLOAT(tci.TextureOffset(seg->sidedef->GetTextureXOffset(side_t::mid))));
				const float uR = ul + tci.FloatToTexU(seg->sidedef->TexelLength);

				// v: measured DOWN from the slab's own texture height, which is the plane's, not the
				// wall's -- a 3D floor's texture hangs off the slab and not off the sector.
				float toV = (rover->flags & (FF_UPPERTEXTURE | FF_LOWERTEXTURE)) ? 0.f :
					FIXED2FLOAT(tci.RowOffset(mastersd->GetTextureYOffset(side_t::mid)));
				toV += FIXED2FLOAT(tci.RowOffset(seg->sidedef->GetTextureYOffset(side_t::mid)));
				const float texh = FIXED2FLOAT(*rover->top.texheight);
				const float vt0 = tci.FloatToTexV(toV + texh - blk.top[0]);
				const float vt1 = tci.FloatToTexV(toV + texh - blk.top[1]);
				const float vb0 = tci.FloatToTexV(toV + texh - blk.bottom[0]);
				const float vb1 = tci.FloatToTexV(toV + texh - blk.bottom[1]);

				FFlatVertex fan[4];
				fan[0].Set(mid.x1, blk.bottom[0], mid.y1, ul, vb0);
				fan[1].Set(mid.x1, blk.top[0],    mid.y1, ul, vt0);
				fan[2].Set(mid.x2, blk.top[1],    mid.y2, uR, vt1);
				fan[3].Set(mid.x2, blk.bottom[1], mid.y2, uR, vb1);
				int fw = 0;
				for (int t = 0; t < 2; t++)
					for (int cc = 0; cc < 3; cc++)
						tris[fw++] = fan[ComputeFanTriangleVertex(4, t, cc)];

				if (!MeshStore(range, tris, fw)) { MeshSquash(range); continue; }

				MeshPiece mp;
				mp.range = range;
				mp.material = fmat;
				// A slab's own planes are usually the light boundaries, so the block lands inside
				// one band -- and where it straddles, its first piece takes the band SplitWall
				// would have given it.
				int ffLight = dl.lightLevel;
				FColormap cm;
				cm = cmFrom->ColorMap;
				{
					zx::surfaces::WallBand bb[kBandsPerPart];
					const int nbb = zx::surfaces::ComputeWallBands(blk.top, blk.bottom,
						ffBandBottom, nLights, bb, kBandsPerPart);
					sc.bandLight[kFFSlot0 + bi] =
						(nbb > 0 && !bb[0].ownLight && bb[0].lightIndex < nLights)
							? (signed char)bb[0].lightIndex : -1;
					if (nbb > 0 && !bb[0].ownLight && bb[0].lightIndex < nLights)
					{
						const lightlist_t &ll = (*lights)[bb[0].lightIndex];
						if (ll.p_lightlevel != &seg->sidedef->sector->lightlevel)
							ffLight = gl_ClampLight(*ll.p_lightlevel);
						cm.CopyLightColor(ll.extra_colormap);
					}
				}
				zx::levelmesh::CaptureShading(ffLight, dl.relLight + getExtraLight(), cm, mp, false);
				mp.baseTex = TexMan.ByIndex(texid.GetIndex());
				mp.lightLevel = ffLight;
				mp.lightColor = cm.LightColor.d;
				mp.fadeColor = cm.FadeColor.d;
				// A translucent or additive slab is not opaque, the way a 3D floor's own plane is not.
				if (rover->flags & (FF_TRANSLUCENT | FF_ADDITIVETRANS))
				{
					mp.alpha = rover->alpha / 255.0f;
					mp.blendMode = ComputeSurfaceBlendMode((rover->flags & FF_ADDITIVETRANS) != 0,
						mp.alpha);
				}
				{
					const float dx = mid.x2 - mid.x1, dy = mid.y2 - mid.y1;
					const float len = sqrtf(dx*dx + dy*dy);
					if (len > 0.0001f) { mp.normX = dy / len; mp.normY = 0.f; mp.normZ = -dx / len; }
				}
				RegisterSegPiece(mp, seg);
				made++;
			}
			for (int k = nb; k < kMaxFFBlocks; k++) MeshSquash(sc.pieces[kFFSlot0 + k].range);
		}
		else
		{
			for (int k = 0; k < kMaxFFBlocks; k++) MeshSquash(sc.pieces[kFFSlot0 + k].range);
		}
	}
	else
	{
		for (int k = 0; k < kMaxFFBlocks; k++) MeshSquash(sc.pieces[kFFSlot0 + k].range);
	}

	sc.bakedCount = kMaxMapPieces;
	return made;
}

// [rc4l] Every wall in the level, from the map, in one pass.
//
// The GL-driven bake arrives a subsector at a time as the player walks and as ArmFullBake pushes the
// traversal along. This does not need a traversal at all: a seg either has parts or it does not, and
// the sidedef says which.
//
// Returns how many parts it built, so the caller can say whether it is worth having.
int BakeLevelFromMap()
{
	if (segs == NULL || numsegs <= 0) return 0;
	int made = 0;
	for (int i = 0; i < numsegs; i++) { const int m = BakeSegFromMap(i); if (m > 0) made += m; }
	return made;
}

// [rc4l] How many vertices the mesh actually holds for one part of one seg.
//
// Every other question about the map bake has been asked of the derivation, which can answer "yes I
// would build that" for a part the bake then does not build -- because the bake also needs a light,
// and needs MeshStore to accept it. This asks the MESH, which is the only thing the frame is drawn
// from. Part is 0 upper, 1 lower, 2 middle, matching BakeSegFromMap's slots.
int MapBakePartCount(int segIndex, int part)
{
	if ((unsigned)segIndex >= g_cache.Size()) return -1;
	if (part < 0 || part >= 3) return -1;
	// [rc4l] A part is spread across its BANDS, so the answer is their sum and not one slot.
	//
	// Reading slot `part` alone was right until a wall could be cut into bands and the lower moved
	// from slot 1 to slot 5. The probe then reported 718 parts missing from a mesh that had all of
	// them, which is a diagnostic lying in the direction that costs the most time.
	int total = 0;
	for (int k = 0; k < kBandsPerPart; k++)
		total += (int)g_cache[segIndex].pieces[part * kBandsPerPart + k].range.count;
	return total;
}

int CachedSegCount() { return (int)g_cache.Size(); }

int CachedPieceCount(int segIndex)
{
	if ((unsigned)segIndex >= g_cache.Size()) return 0;
	return g_cache[segIndex].filled ? g_cache[segIndex].pieceCount : 0;
}

const GLWall *CachedPiece(int segIndex, int piece)
{
	if ((unsigned)segIndex >= g_cache.Size()) return NULL;
	if (piece < 0 || piece >= g_cache[segIndex].pieceCount) return NULL;
	return &g_cache[segIndex].walls[piece];
}

GLWall *StaticWall(int packedIndex)
{
	if (packedIndex < 0) return NULL;
	const int seg = packedIndex / kMaxCachedPieces;
	const int piece = packedIndex % kMaxCachedPieces;
	if ((unsigned)seg >= g_cache.Size()) return NULL;
	if (piece >= g_cache[seg].pieceCount) return NULL;
	return &g_cache[seg].walls[piece];
}

void InvalidateAll()
{
	for (unsigned i = 0; i < g_cache.Size(); i++) g_cache[i].filled = false;
	// [rc4l] Flats share the same mesh, so they invalidate together or a backend sees a half-stale world.
	zx::levelmesh::ClearFlats();
	zx::levelmesh::ClearSprites();
}

bool IsCapturing()
{
	return g_captureSeg >= 0;
}

void BeginCapture(int segIndex)
{
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) { g_captureSeg = -1; return; }
	g_captureSeg = segIndex;
	g_sawPortal = false;
	g_cache[segIndex].pieceCount = 0;
	g_cache[segIndex].filled = false;
}

//==========================================================================
//
// ForgetFrameState
//
// [rc4l] The fields of a wall that describe THIS FRAME rather than the wall.
//
// A cached wall is stored once and replayed for the life of the level, which is only sound for
// values that describe the world. Everything Doom's renderer computes per frame -- indices into
// per-frame arrays, distances from wherever the camera stood, pointers into per-frame pools --
// means something different by the next frame, and a replayed wall carrying one is reading data
// that has since been overwritten by something unrelated.
//
// Not hypothetical: dynlightindex pointed into the light buffer, kept pointing there after the
// light died, and a dead plasma bolt lit a wall for as long as nobody fired again. It cost a day,
// because the light really was gone -- every instrument that starts from the light said so -- and
// the wall was the last place anyone looked.
//
// So the classification is written down here, and the assert makes adding a field to GLWall a build
// error until someone has decided which kind it is.
//
// WORLD, and therefore cached: glseg, vertexes, ztop/zbottom, the four texcoords, alpha, gltexture
// (re-resolved on replay, so animation keeps running), Colormap, RenderStyle, lightlevel, type,
// flags, rellight, the glow colours, topflat/bottomflat, topplane/bottomplane, zceil/zfloor, seg,
// sub. None of these can change without the seg being re-captured or its stamp going stale.
//
// FRAME, and therefore cleared:
//
//   dynlightindex          an offset into a buffer that is rewritten as lights come and go
//   viewdistance           measured from wherever the camera was standing at capture
//   firstwall, numwalls    indices into the per-frame walls[] array the sorter splits into
//   the union              skybox/sky/horizon/portal/planemirror, all from per-frame pools
//
// The portal members cannot reach a replay anyway, since NotePortal marks the seg uncacheable for
// the level -- but a dangling pointer into a freed pool is not worth leaving in a structure on the
// grounds that nothing currently reads it.
//
// [rc4l] Considered and rejected: a FrameScoped<T> that resets on copy. The sorter SPLITS walls by
// copying them (SortWallIntoWall, SortWallIntoPlane) and those copies must keep their lights --
// they are the same wall in the same frame. A copy cannot tell "crossing into the cache" from
// "splitting in place", so the cache boundary is where the rule belongs, not the type.
//
//==========================================================================

static void ForgetFrameState(GLWall &w)
{
	w.dynlightindex = zx::hwrender::kNoWallLightIndex;
	w.viewdistance = 0;
	w.firstwall = 0;
	w.numwalls = 0;
	w.skybox = NULL;   // the union: every member of it is a per-frame pointer
}

// [rc4l] What this guard does, and -- measured -- what it does not.
//
// A field added to GLWall changes its size or shifts the fields after it, and either way this stops
// the build until someone has classified it above. That is the mechanism: the fault this file
// exists to prevent was never a hard question, it was a question nobody was asked.
//
// It is not airtight, and the gap was measured rather than guessed. Adding `int f;` directly after
// dynlightindex first compiled CLEAN: the int landed in the padding before the 8-aligned union, so
// the size stayed 912 and seg did not move. A small field dropped into an interior hole is
// invisible to a check written only in terms of size.
//
// So the holes get named too. With numwalls pinned -- it sits immediately before that padding --
// the same insertion now fails the build, verified by making it. Other holes may exist; the
// honest statement is "everything that changes the shape, plus the hole we found", and the
// structural answer is the cache holding a record built for it rather than a whole GLWall, which
// is where Phase 2b goes (docs/renderer-modernization-PLAN.md).
#if defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__)
static_assert(RENDERWALL_TOP == kRenderWallTop && RENDERWALL_M1S == kRenderWallM1S && RENDERWALL_BOTTOM == kRenderWallBottom &&
	RENDERWALL_MIRRORSURFACE == kRenderWallMirrorSurface, "gl_wall.h's RENDERWALL_ order moved: update wallcache_compute.h");
static_assert(sizeof(GLWall) == 912,
	"GLWall changed size. Classify the new field as world state or frame state in ForgetFrameState "
	"above, clear it there if it is frame state, then update this number.");
// The tail, so an insertion that keeps the size the same still has to move something.
static_assert(offsetof(GLWall, seg) == 896, "GLWall fields moved: see ForgetFrameState above.");
// numwalls sits immediately before the union's alignment padding, which is the hole an added int
// disappeared into when this was tested. Nailing it down closes that particular slip.
static_assert(offsetof(GLWall, numwalls) == 184, "GLWall fields moved: see ForgetFrameState above.");
static_assert(offsetof(GLWall, dynlightindex) == 176, "GLWall fields moved: see ForgetFrameState above.");
#endif

// [rc4l] Whether the wall arrives here already inside the first copy of its texture.
//
// CheckTexturePosition is supposed to guarantee that for everything DoTexture makes, and the
// derivation in features/surfaces is scored against it. Reading the cache afterwards said 129 walls
// on one map were outside it, which is either a producer that skips the step or something
// rewriting v after capture -- and those two have completely different fixes. This counts it at
// the moment of capture, which is the only place the two can be told apart.
static int g_recTopInRange[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int g_recTopOutOfRange[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int g_recTopOutOfRangeSplit = 0;

int CaptureVOutOfRangeSplits() { return g_recTopOutOfRangeSplit; }

// Cleared per level. Cumulative counts across a session say more about which segs were re-processed
// every frame than about the level, and a diagnostic that answers a different question than the one
// asked is worse than none.
void ResetCaptureVRangeStats()
{
	for (int i = 0; i < 8; i++) { g_recTopInRange[i] = 0; g_recTopOutOfRange[i] = 0; }
	g_recTopOutOfRangeSplit = 0;
}

void GetCaptureVRangeStats(int type, int &inRange, int &outOfRange)
{
	if (type < 0 || type >= 8) { inRange = outOfRange = 0; return; }
	inRange = g_recTopInRange[type];
	outOfRange = g_recTopOutOfRange[type];
}

void RecordPiece(const GLWall &wall, int list)
{
	if (g_captureSeg < 0) return;
	if (wall.gltexture != NULL)
	{
		const float topV = (wall.uplft.v < wall.uprgt.v) ? wall.uplft.v : wall.uprgt.v;
		int *bucket = (topV >= 0.f && topV < 1.f) ? g_recTopInRange : g_recTopOutOfRange;
		if (wall.type >= 0 && wall.type < 8) bucket[wall.type]++;
		// SplitWall marks the fragments it makes, and a fragment's v is interpolated from a parent
		// that CheckTexturePosition already normalised -- so a fragment can start deep inside the
		// texture while its parent did not. If the out-of-range walls are the fragments, the
		// derivation is not missing a rule, it is being compared against a piece of a wall.
		if (!(topV >= 0.f && topV < 1.f) && (wall.flags & (GLWall::GLWF_NOSPLITUPPER | GLWall::GLWF_NOSPLITLOWER)))
			g_recTopOutOfRangeSplit++;
	}
	// [rc4l] A translucent wall may be BAKED but must never be REPLAYED.
	//
	// GLDrawList's BSP sorter indexes the per-frame walls[] array directly in a dozen places
	// (SortWallIntoPlane, SortWallIntoWall, DoDrawSorted...) and would read garbage for a cache-owned
	// item, so replaying one into a GL draw list is not safe. This used to `return` before storing
	// it, which kept GL correct and also meant the geometry never reached the mesh at all -- so the
	// Vulkan backend had no translucent walls anywhere, in any map. The give-away was sitting in its
	// own upload report for weeks: "51 material batches (0 translucent)" on a map full of them.
	//
	// Marking the seg uncacheable is what keeps GL safe: TryReplay refuses it forever, so GL keeps
	// processing the seg itself every frame, exactly as before. EndCapture still bakes the seg once,
	// and now the translucent piece is in it.
	if (list == GLDL_TRANSLUCENT || list == GLDL_TRANSLUCENTBORDER)
		g_sawPortal = true;   // reuses the sticky uncacheable path -- but keep the piece
	// [rc4l] Written into the seg's own fixed slots -- re-capture overwrites, never appends, so a
	// flickering-light sector cannot strand geometry. Overflowing the slots gives up on the seg.
	SegCache &sc = g_cache[g_captureSeg];
	if (sc.pieceCount >= kMaxCachedPieces)
	{
		g_sawPortal = true;   // reuses the sticky give-up path
		return;
	}
	sc.walls[sc.pieceCount] = wall;
	// [rc4l] Crossing into the cache: the frame it was captured in does not come with it.
	ForgetFrameState( sc.walls[sc.pieceCount] );
	// [rc4l] The one field of a captured wall that must not be kept: which lights it had.
	//
	// Everything else here describes the wall, and a wall does not change. A light index describes
	// one FRAME -- it points into a buffer that is rewritten as lights come and go -- so a replay
	// carrying it lights the wall from whatever happens to sit at that offset later. See
	// walllight_compute.h; the draw paths clear it again, and it should never have been stored.
	sc.walls[sc.pieceCount].dynlightindex = zx::hwrender::kNoWallLightIndex;
	sc.pieces[sc.pieceCount].list = list;
	sc.pieceCount++;
}

void NotePortal()
{
	// [rc4l] Portal walls are handed to the portal manager rather than a draw list, and the manager
	// keeps per-frame state, so a seg that produces one can never be replayed. Sticky for the level.
	if (g_captureSeg < 0) return;
	g_sawPortal = true;
}

void EndCapture(const WallCacheStamp &stamp, const WallCacheEligibility &e)
{
	if (g_captureSeg < 0) return;
	const int idx = g_captureSeg;
	g_captureSeg = -1;

	bool refuse = false;
	if (g_sawPortal)
	{
		g_rejPortal++;
		if ((unsigned)idx < g_segFate.Size()) g_segFate[idx] = SEG_PORTAL;
		refuse = true;
	}
	else if (!ComputeIsCacheable(e))
	{
		// [rc4l] Which rule fired, not just that one did -- the bake reaches only a fraction of the
		// level and "it is the eligibility check" is not actionable without knowing which clause.
		if (e.isPolyobject)  { g_rejPoly++;   if ((unsigned)idx < g_segFate.Size()) g_segFate[idx] = SEG_POLY; }
		else if (e.hasFFloors) { g_rejFFloor++; if ((unsigned)idx < g_segFate.Size()) g_segFate[idx] = SEG_FFLOOR; }
		else if (e.inArea)     { g_rejArea++;   if ((unsigned)idx < g_segFate.Size()) g_segFate[idx] = SEG_AREA; }
		else                   { g_rejOther++;  if ((unsigned)idx < g_segFate.Size()) g_segFate[idx] = SEG_OTHER; }
		refuse = true;
	}

	if (refuse)
	{
		// [rc4l] Refused for REPLAY is not the same as having nothing worth baking.
		//
		// A seg is refused when any of its pieces is a portal, is translucent, or overflows the slots
		// -- and the whole seg was then thrown away. But those pieces are never recorded in the first
		// place (portals go to the portal manager, translucents bail out of RecordPiece), so what is
		// sitting in sc.pieces is exactly the seg's SOLID geometry, which is static and perfectly
		// drawable. Discarding it cost most of the level: on Sunder MAP10, 289142 captures were
		// refused against 38683 kept, because in a huge open map most walls sit under sky, and sky is
		// a portal.
		//
		// Baked once only, on the false->true edge -- these segs re-capture every frame, since
		// TryReplay always refuses them. The exception is a full-level bake pass, which bakes them
		// regardless: by the time a bake is asked for, everything the player already walked past is
		// long since marked uncacheable, so the edge would never fire again and the bake would add
		// almost nothing. ClaimSideForBake keeps that to one visit per sidedef.
		if (!g_uncacheable[idx] || IsBakePassActive()) BakeSeg(idx);
		g_uncacheable[idx] = true;
		g_cache[idx].pieceCount = 0;
		g_cache[idx].filled = false;
		return;
	}

	g_captureOk++;
	if ((unsigned)idx < g_segFate.Size()) g_segFate[idx] = SEG_OK;
	g_cache[idx].stamp = stamp;
	g_cache[idx].filled = true;
	BakeSeg(idx);
}

bool TryReplay(int segIndex, const WallCacheEligibility &e, const WallCacheStamp &current)
{
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return false;
	if (g_uncacheable[segIndex]) { g_uncacheableHits++; return false; }

	// [rc4l] A seg that borders the SKY is always processed, never replayed.
	//
	// GL builds its sky quads inside GLWall::Process, and a replayed seg never runs it -- so the
	// sky surfaces the backend needs simply never existed for any cached seg, which is why
	// capturing them caught nothing at all the first time. Refusing the cache for these segs costs
	// the derivation on the few that border sky (four linedefs were in view on the vantage this
	// was found from) and buys the one thing the map cannot provide: GL's own visibility.
	if (fua_mesh_sky && segIndex < numsegs)
	{
		const seg_t *sg = &segs[segIndex];
		// [rc4l] BOTH sides. SkyTop and SkyBottom look at the front sector AND the back one -- the sky
		// boundary of a two-sided line belongs to whichever side carries the sky flat, and a seg is
		// walked from either side depending on which subsector the traversal reaches first. Checking
		// only the front left every seg whose sky is on the far side replayed from cache, so its quad
		// was never built and the sky covered part of the view: at 1280x720 that read as the skyline
		// sitting inside a hard-edged rectangle with the map dark around it.
		const sector_t *fsec = sg->frontsector;
		const sector_t *bsec = sg->backsector;
		const bool frontSky = fsec != NULL && (fsec->GetTexture(sector_t::ceiling) == skyflatnum ||
		                                       fsec->GetTexture(sector_t::floor) == skyflatnum);
		const bool backSky = bsec != NULL && (bsec->GetTexture(sector_t::ceiling) == skyflatnum ||
		                                      bsec->GetTexture(sector_t::floor) == skyflatnum);
		if (frontSky || backSky) return false;
	}

	// [rc4l] DIAGNOSTIC ONLY (gl_wallcache 2): skip validation to isolate how much of the cache's
	// cost is the stamp compare versus the replay itself. Renders stale geometry -- never ship on.
	extern int g_wallcacheMode;
	SegCache &sc = g_cache[segIndex];
	if (g_wallcacheMode == 2 ? !sc.filled : !ComputeCanReuse(e, sc.filled, sc.stamp, current))
	{
		g_misses++;
		return false;
	}

	for (int i = 0; i < sc.pieceCount; i++)
	{
		// [rc4l] Re-resolve the texture before replaying, because the cache freezes it otherwise.
		//
		// A replayed GLWall carries the FMaterial it was captured with, and the stamp is built from
		// geometry and lighting generation counters -- an ANIMDEFS frame change moves none of them.
		// So a cached wall kept drawing frame 1 forever: LAVFALL1..4 on dbab02 was a still image in
		// GL while the Vulkan backend, which re-resolves every batch from its base texture each
		// frame, animated it correctly. The cache was making the GL renderer WRONG, not just fast.
		//
		// Only the three ordinary sidedef parts map cleanly, the same three BakeSeg resolves; 3D
		// floor and special walls are left alone and keep whatever they were captured with.
		GLWall &w = sc.walls[i];
		// [rc4l] And crossing back OUT: the frame that last drew this wall wrote its own state
		// into it, and this is a different frame. The draw paths clear it too, but the invariant
		// belongs at the boundary -- a wall leaving the cache carries no frame with it.
		ForgetFrameState(w);
		if (gl_wallcache_anim && w.gltexture != NULL && w.seg != NULL && w.seg->sidedef != NULL)
		{
			const side_t *sd = w.seg->sidedef;
			int part = -1;
			switch (w.type)
			{
			case RENDERWALL_TOP:    part = side_t::top; break;
			case RENDERWALL_BOTTOM: part = side_t::bottom; break;
			case RENDERWALL_M1S:
			case RENDERWALL_M2S:
			case RENDERWALL_M2SNF:  part = side_t::mid; break;
			default: break;
			}
			if (part >= 0)
			{
				// The same call gl_walls.cpp makes: translate == true is what follows the animation.
				FMaterial *now = FMaterial::ValidateTexture(sd->GetTexture(part), false, true);
				if (now != NULL && now != w.gltexture) { w.gltexture = now; g_animRefresh++; }
			}
		}
		// [rc4l] An 8-byte reference, not a struct copy -- this is the change attempt 1 was missing.
		gl_drawinfo->drawlists[sc.pieces[i].list].AddStaticWall(PackWallRef(segIndex, i));
	}
	g_hits++;
	return true;
}

void BuildStamp(const seg_t *seg, const sector_t *frontsector, const sector_t *backsector,
                WallCacheStamp &out)
{
	out.frontDirty = frontsector->fua_dirty;
	out.backDirty  = backsector ? backsector->fua_dirty : 0;
	out.sideDirty  = seg->sidedef ? seg->sidedef->fua_dirty : 0;
}

void BuildEligibility(const seg_t *seg, const sector_t *frontsector, const sector_t *backsector,
                      WallCacheEligibility &out)
{
	out.isPolyobject = (seg->sidedef != NULL) && !!(seg->sidedef->Flags & WALLF_POLYOBJ);
	out.hasHeightsec = (frontsector->heightsec != NULL) ||
	                   (backsector != NULL && backsector->heightsec != NULL);
	out.hasFFloors   = (frontsector->e != NULL && frontsector->e->XFloor.ffloors.Size() > 0) ||
	                   (backsector != NULL && backsector->e != NULL && backsector->e->XFloor.ffloors.Size() > 0);
	out.producesPortal = false;   // discovered during capture, kept sticky in g_uncacheable
	// [rc4l] area_default means gl_CheckViewArea resolves the area from the viewpoint as the BSP
	// walk runs, so the same seg can generate different geometry from different positions.
	// [rc4l] gl_CheckViewArea only resolves the area when a BACK sector has a heightsec, so an
	// undetermined area is harmless without one. Testing in_area alone disqualified nearly every
	// seg on nearly every map, which is why the cache never got a fair measurement.
	out.inArea = (in_area == area_default) && out.hasHeightsec;
}

void GetStats(int &hits, int &misses, int &uncacheable)
{
	hits = g_hits;
	misses = g_misses;
	uncacheable = g_uncacheableHits;
}

int GetAnimRefreshes() { return g_animRefresh; }

// [rc4l] For fua_wallcache_census: what became of this seg, and does it have geometry a backend
// could actually draw. bakedCount is the honest test -- a seg can be "cached" and still have baked
// nothing if every piece it produced was refused by MeshStore.
int SegFate(int segIndex)
{
	if (segIndex < 0 || (unsigned)segIndex >= g_segFate.Size()) return 0;
	return g_segFate[segIndex];
}

const char *SegFateName(int fate)
{
	static const char *kName[] = { "never captured", "cached", "portal", "polyobject", "3D floor", "heightsec area", "other" };
	return (fate >= SEG_UNSEEN && fate <= SEG_OTHER) ? kName[fate] : "?";
}

bool SegHasBakedGeometry(int segIndex)
{
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return false;
	const SegCache &sc = g_cache[segIndex];
	for (int i = 0; i < sc.bakedCount && i < kMaxMapPieces; i++)
		if (sc.pieces[i].range.count > 0) return true;
	return false;
}

void ResetStats()
{
	g_hits = g_misses = g_uncacheableHits = 0;
	g_animRefresh = 0;
}

void GetRejects(int &portal, int &poly, int &ffloor, int &area, int &other, int &ok)
{
	portal = g_rejPortal; poly = g_rejPoly; ffloor = g_rejFFloor;
	area = g_rejArea; other = g_rejOther; ok = g_captureOk;
}

void GetCoverage(CoverageStats &out)
{
	out.segs = (int)g_cache.Size();
	out.baked = out.uncacheable = out.pieces = 0;
	// [rc4l] Minisegs -- BSP-generated segs with no sidedef -- draw nothing at all, so counting them
	// against coverage understates it badly. Sunder MAP10 has 54845 segs but only 26691 sides, and
	// without this split "28% of segs baked" reads as a bug rather than as arithmetic.
	out.drawableSegs = 0;
	for (int i = 0; i < numsegs; i++)
		if (segs[i].sidedef != NULL) out.drawableSegs++;
	for (unsigned i = 0; i < g_cache.Size(); i++)
	{
		if (i < g_uncacheable.Size() && g_uncacheable[i]) out.uncacheable++;
		int p = 0;
		for (int k = 0; k < g_cache[i].pieceCount; k++)
			if (g_cache[i].pieces[k].range.count > 0) p++;
		if (p > 0) out.baked++;
		out.pieces += p;
	}
}

// [rc4l] Drop baked geometry for sectors that moved, whether or not anything can see them.
//
// This is the bug the door hunt ended at, and it is a property of the design rather than a slip. The
// mesh draws the WHOLE level; the wall cache only re-bakes segs the BSP walks. Those two are fine
// while nothing moves. Open a door and they part company: the near face is on screen, so it re-bakes
// every frame and tracks the door correctly -- while the FAR face, eight units behind it, is never
// walked, so it keeps the full-height quad it was baked with while the door was shut. GL never showed
// it because GL only draws what it walked. Vulkan drew it, and it stood in the open doorway looking
// exactly like a door that had failed to open.
//
// Squash rather than re-bake: re-baking here would mean running GLWall::Process outside the BSP walk,
// with no subsector, no area resolution and no draw lists to route into. A seg nobody has re-baked
// since its sector moved is simply not valid to draw, so it renders as nothing until the BSP reaches
// it -- which, if it is ever actually visible, is the same frame.
//
// Called before the BSP walk so segs that ARE visible re-bake immediately afterwards and never blink.
// [rc4l] How many sectors moved on the last frame this ran.
//
// Standalone rendering skips the BSP walk, and the walk is what re-bakes a seg whose sector moved.
// So a frame where something moved is a frame that still needs GL, and this is how that is known
// without a second traversal of the level to find out.
static int g_movedThisFrame = 0;
int SectorsMovedLastFrame() { return g_movedThisFrame; }

// [rc4l] Why a sector was dirty, not just that it was.
//
// InvalidateMovedSectors treats every dirty sector as one that MOVED and answers with a full
// geometry re-derive. But sector_t bumps fua_dirty for light changes too, and Doom's light effects
// -- flicker, strobe, glow -- run every tic. So a room with a flickering lamp can be re-deriving its
// walls from the map several times a second to express a light level. Counted here so the guess
// stops being a guess.
int g_dirtyLightOnly = 0, g_dirtyGeometry = 0;
// [rc4l] One timer for the WHOLE loop, not one per piece.
//
// An earlier profile put a timer around each piece and spent twelve thousand clock reads a frame
// measuring a frame it had made slower. The question here is only "how much of the frame is this
// loop", so one read either side answers it and costs nothing.
static double g_invalMs = 0.0; static int g_invalFrames = 0; static int g_segsBaked = 0;
static int g_segsReshaded = 0;
// The last frame's own figures, not the running totals -- so a frame-time breakdown can ask what
// THIS frame spent rather than what the level has spent since it loaded.
static double g_invalMsLast = 0.0; static int g_sectorsLast = 0;   // segs that took the light-only path instead of a full re-derive
static TArray<SectorLook> g_lastLook;
static int g_dirtyNoChange = 0;   // dirty, but nothing that shows on a wall actually differed

double InvalidationMsLastFrame() { return g_invalMsLast; }
int    SectorsDirtyLastFrame()   { return g_sectorsLast; }

void SectorDirtyBreakdown(int &lightOnly, int &geometry)
{
	lightOnly = g_dirtyLightOnly;
	geometry  = g_dirtyGeometry;
}

// [rc4l] Re-run the whole-map wall bake now, to tell a bake that was wrong from one that went stale.
CCMD( fua_mapbake_now )
{
	Printf( "%d wall parts rebuilt from the map\n", zx::levelmesh::BakeLevelFromMap( ) );
}

CCMD( fua_bake_dirty )
{
	Printf( "sector invalidations since load: %d light-only, %d moved a plane, %d re-baked in the last frame\n",
		g_dirtyLightOnly, g_dirtyGeometry, g_movedThisFrame );
	Printf( "  %d were dirty with nothing visibly changed\n", g_dirtyNoChange );
	Printf( "  reshade declined: %d 3D-floor, %d no light, %d not the side owner, %d piece missing\n",
		g_reshadeFF, g_reshadeNoLight, g_reshadeNotOwner, g_reshadeNoPiece );
	{
		unsigned int used = 0, parked = 0; int reuses = 0, reallocs = 0;
		zx::levelmesh::MeshArenaStats( used, parked, reuses, reallocs );
		Printf( "  arena: %u verts held, %u parked on the free list, %d reused, %d size changes\n",
			used, parked, reuses, reallocs );
		unsigned int pieces = 0, indexed = 0;
		zx::levelmesh::MeshPieceListStats( pieces, indexed );
		Printf( "  pieces: %u records, %u offsets indexed\n", pieces, indexed );
	}
	if ( g_invalFrames > 0 )
		Printf( "  invalidation loop: %.3f ms/frame over %d frames, %.1f baked + %.1f reshaded per frame\n",
			g_invalMs / g_invalFrames, g_invalFrames,
			(double)g_segsBaked / g_invalFrames, (double)g_segsReshaded / g_invalFrames );
	if ( argv.argc() > 1 )
	{
		g_dirtyLightOnly = 0; g_dirtyGeometry = 0; g_dirtyNoChange = 0;
		g_reshadeFF = 0; g_reshadeNoLight = 0; g_reshadeNotOwner = 0; g_reshadeNoPiece = 0;
		g_invalMs = 0.0; g_invalFrames = 0; g_segsBaked = 0; g_segsReshaded = 0;
		Printf( "  (counters reset)\n" );
	}
}

// [rc4l] Re-bake one seg whose sector or sidedef changed: re-light it when only the light moved, re-derive it otherwise, and hand it back to GL's walk if the map bake declines it.
static void RebakeChangedSeg(int idx, bool geometryChanged)
{
	if ((unsigned)idx >= g_cache.Size()) return;
	SegCache &sc = g_cache[idx];
	sc.filled = false;   // [rc4l] force a re-capture when the BSP next reaches it
	// [rc4l] Re-arm the once-per-seg bake, or a seg GL refused once (every 3D floor, everything under sky) is squashed and never baked again, as dbab02's lava grate was.
	g_uncacheable[idx] = false;
	// [rc4l] Bake first and squash only what the bake declined, since squashing zeroes the range and defeats MeshStore's compare-before-upload (3.7x slower on wdiex02's flickering lights).
	int made = geometryChanged ? -2 : ReshadeSegFromMap(idx);
	if (made == -1 || made == -2)
	{
		made = BakeSegFromMap(idx);
		g_segsBaked++;
	}
	else g_segsReshaded++;
	if (made < 0)   // [rc4l] declined: this seg is GL's, hand it back
	{
		// [rc4l] Squash, not release, so the re-capture writes back over a range that stays allocated instead of the arena growing every frame the sector moves.
		for (int i = 0; i < kMaxMapPieces; i++)
			MeshSquash(sc.pieces[i].range);
	}
}

// [rc4l] A sector, then every sector it is a 3D floor in (XFloor.attached), whose planes, sides and light bands are drawn from it.
template <typename Fn>
static void ForSectorAndTargets(int s, Fn fn)
{
	fn(s);
	if (sectors[s].e == NULL) return;
	const TArray<sector_t *> &targets = sectors[s].e->XFloor.attached;
	for (unsigned k = 0; k < targets.Size(); k++) fn(int(targets[k] - sectors));
}

static SectorLook LookOf(const sector_t &s)
{
	SectorLook l;
	l.light = s.lightlevel;
	l.floorD = s.floorplane.d.Raw();
	l.ceilD = s.ceilingplane.d.Raw();
	l.floorPic = s.GetTexture(sector_t::floor).GetIndex();
	l.ceilPic = s.GetTexture(sector_t::ceiling).GetIndex();
	l.colormap = s.ColorMap;
	return l;
}

void InvalidateMovedSectors()
{
	g_movedThisFrame = 0;
	cycle_t invalClock; invalClock.Reset(); invalClock.Clock();
	g_invalFrames++;
	if ((int)g_lastLook.Size() != numsectors)
	{
		g_lastLook.Resize((unsigned)numsectors);
		for (int i = 0; i < numsectors; i++) g_lastLook[i] = LookOf(sectors[i]);
	}
	if (!gl_wallmesh || g_sectorDirty.Size() == 0) return;

	for (int s = 0; s < numsectors; s++)
	{
		if (!ComputeTakeChange(g_sectorDirty[s], sectors[s].fua_dirty)) continue;
		g_movedThisFrame++;
		// [rc4l] fua_dirty says only that a setter ran (SetLightLevel, ChangeLightLevel, SetTexture, SetPlaneTexZ, ChangePlaneTexZ, SetColor, SetFade), and a strobe re-asserts its held level every tic, so a sector whose look did not change is skipped.
		const SectorLook now = LookOf(sectors[s]);
		const int change = ComputeSectorRebake(g_lastLook[s], now);
		g_lastLook[s] = now;
		if (change == kSectorSame) { g_dirtyNoChange++; continue; }
		if (change == kSectorMoved) g_dirtyGeometry++; else g_dirtyLightOnly++;
		// A texture change alters UVs, so only a pure relight may take the reshade path.
		const bool sectorMoved = change >= kSectorRetextured;

		// [rc4l] A 3D floor's control sector moves or relights the floors, sides and light bands of the sectors it sits in, so those go again with it.
		ForSectorAndTargets(s, [sectorMoved](int t) {
			const TArray<int> &segList = g_sectorSegs[t];
			// [rc4l] Planes that held still cannot have moved the walls, so those segs only re-light (ReshadeSegFromMap) and anything else takes the full bake.
			for (unsigned k = 0; k < segList.Size(); k++) RebakeChangedSeg(segList[k], sectorMoved);
			// [rc4l] ...and the sector's own planes, which have no walk to notice them either.
			if (fua_surface_mapbake_auto) zx::levelmesh::BakeFlatsForSector(t);
		});
	}
	// [rc4l] ...and every sidedef a setter touched: a switch's texture, a scroller's offset, which no sector change reports.
	for (int i = 0; i < numsides && (unsigned)i < g_sideDirty.Size(); i++)
	{
		if (!ComputeTakeChange(g_sideDirty[i], sides[i].fua_dirty)) continue;
		for (int k = 0; k < sides[i].numsegs; k++) RebakeChangedSeg(int(sides[i].segs[k] - segs), true);
	}
	// [rc4l] ...and every sector whose floor or ceiling texture a scroller, its interpolation or a script placed, re-baking its flats and those of any sector it is a 3D floor in, since no wall is drawn from it.
	for (int s = 0; fua_surface_mapbake_auto && s < numsectors && (unsigned)s < g_sectorPlacement.Size(); s++)
	{
		if (ComputeTakeChange(g_sectorPlacement[s], sectors[s].fua_placement))
			ForSectorAndTargets(s, [](int t) { zx::levelmesh::BakeFlatsForSector(t); });
	}
	invalClock.Unclock();
	g_invalMs += invalClock.TimeMS();
	g_invalMsLast = invalClock.TimeMS();
	g_sectorsLast = g_movedThisFrame;
}

// [rc4l] Read-only views of one seg's cache state, for fua_line_mesh. The cache array is file-static
// on purpose; exposing two getters beats exporting the storage.
void GetSegMeshInfo(int segIndex, int &pieces, int &baked)
{
	pieces = baked = 0;
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return;
	pieces = g_cache[segIndex].pieceCount;
	baked  = g_cache[segIndex].bakedCount;
}

// [rc4l] The light-list entry a baked slot took its light from, or -1 for the wall's own light.
int GetSegBandLight(int segIndex, int piece)
{
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size() || piece < 0 || piece >= kMaxMapPieces) return -1;
	return g_cache[segIndex].bandLight[piece];
}

void GetSegPieceRange(int segIndex, int piece, unsigned int &offset, unsigned int &count)
{
	offset = count = 0;
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return;
	if (piece < 0 || piece >= kMaxMapPieces) return;
	offset = g_cache[segIndex].pieces[piece].range.offset;
	count  = g_cache[segIndex].pieces[piece].range.count;
}

}} // namespace zx::levelmesh

//==========================================================================
//
// [rc4l] Is the wall cache actually being exercised? A hit rate near zero means any timing A/B was
// measuring nothing at all, which is exactly the trap the first two attempts fell into.
//
//==========================================================================

//==========================================================================
//
// fua_wallcache_census
//
// [rc4l] One line per OUTCOME, counting segs -- not capture attempts.
//
// A grate went missing from the Vulkan view and the available numbers were 541713 3D-floor rejects
// against 1317 segs that touch a 3D floor, because a refused seg re-captures every frame. Attempt
// counts cannot say how much of the level is absent or why. This walks every seg once and reports
// what became of it, and how many actually have baked geometry, which is the number that decides
// whether the backend can draw it.
//
//==========================================================================

CCMD( fua_wallcache_census )
{
	int fate[7] = { 0, 0, 0, 0, 0, 0, 0 };
	int bakedBy[7] = { 0, 0, 0, 0, 0, 0, 0 };
	int drawable = 0, ffloorSegs = 0, ffloorBaked = 0;
	for ( int i = 0; i < numsegs; i++ )
	{
		if ( segs[i].sidedef == NULL || segs[i].linedef == NULL ) continue;   // miniseg
		drawable++;
		const int f = zx::levelmesh::SegFate( i );
		const bool baked = zx::levelmesh::SegHasBakedGeometry( i );
		fate[f]++;
		if ( baked ) bakedBy[f]++;
		const sector_t *fs = segs[i].frontsector, *bs = segs[i].backsector;
		const bool touches3d = ( fs != NULL && fs->e != NULL && fs->e->XFloor.ffloors.Size() > 0 ) ||
		                       ( bs != NULL && bs->e != NULL && bs->e->XFloor.ffloors.Size() > 0 );
		if ( touches3d ) { ffloorSegs++; if ( baked ) ffloorBaked++; }
	}
	Printf( "wallcache census over %d drawable segs:\n", drawable );
	for ( int i = 0; i < 7; i++ )
		if ( fate[i] > 0 )
			Printf( "  %-15s %5d segs (%4.1f%%), %5d with baked geometry (%4.1f%% of them)\n",
					zx::levelmesh::SegFateName( i ), fate[i], 100.0 * fate[i] / MAX( 1, drawable ),
					bakedBy[i], 100.0 * bakedBy[i] / MAX( 1, fate[i] ) );
	Printf( "  segs touching a 3D floor: %d, of which %d have baked geometry (%.1f%%)\n",
			ffloorSegs, ffloorBaked, 100.0 * ffloorBaked / MAX( 1, ffloorSegs ) );
}

CCMD( fua_wallcache_stats )
{
	int hits = 0, misses = 0, uncacheable = 0;
	zx::levelmesh::GetStats( hits, misses, uncacheable );
	const int total = hits + misses + uncacheable;
	Printf( "wallcache since last reset: %d hits, %d misses, %d uncacheable (%d total)\n",
			hits, misses, uncacheable, total );
	if ( total > 0 )
		Printf( "  hit rate %.1f%%, uncacheable %.1f%%\n",
				100.0 * hits / total, 100.0 * uncacheable / total );
	Printf( "  animated texture refreshes on replay: %d\n", zx::levelmesh::GetAnimRefreshes( ) );
	zx::levelmesh::ResetStats( );

	// [rc4l] And the flats, which had no cache at all until they were measured.
	//
	// Every visible flat was rebuilt from scratch every frame -- vertices, UVs, triangles, a memcpy
	// into the mesh and a piece re-registered -- 3406 times a frame on Sunder MAP16. Reported beside
	// the wall numbers because the two answer the same question and only one of them was being asked.
	int fh = 0, fr = 0;
	zx::levelmesh::GetFlatCacheStats( fh, fr );
	if ( fh + fr > 0 )
		Printf( "  flats: %d left alone, %d rebuilt (%.1f%% hit)\n", fh, fr,
			100.0 * (double)fh / (double)( fh + fr ) );
}

//==========================================================================
//
// fua_line_mesh <linedef>
//
// [rc4l] What the MESH holds for one linedef, as opposed to what the screen shows.
//
// A door that stayed shut in the Vulkan view while GL showed it open produced three plausible
// theories -- stale piece map, uncollapsed slot, cache hit that should have missed -- and screenshots
// could not tell them apart, because every one of them looks like "the old door". The heights of the
// baked vertices settle it in one line: if the mesh still spans the closed door, the bake never ran;
// if it does not, the bake ran and the backend is drawing something else.
//
//==========================================================================

CCMD( fua_line_mesh )
{
	if ( lines == NULL || argv.argc( ) < 2 )
	{
		Printf( "usage: fua_line_mesh <linedef index>\n" );
		return;
	}
	const int want = atoi( argv[1] );
	if ( want < 0 || want >= numlines ) { Printf( "no such line\n" ); return; }

	int nv = 0;
	const FFlatVertex *verts = zx::levelmesh::MeshVertexData( nv );
	int found = 0;
	for ( int i = 0; i < numsegs; i++ )
	{
		if ( segs[i].linedef == NULL || int( segs[i].linedef - lines ) != want ) continue;
		found++;
		int pieces = 0, baked = 0;
		zx::levelmesh::GetSegMeshInfo( i, pieces, baked );
		Printf( "seg %d: %d pieces (%d baked last time)\n", i, pieces, baked );
		// [rc4l] A map-baked seg keeps its geometry in the baked slots, not the captured pieces, so list whichever it has.
		const int listed = ( pieces > baked ) ? pieces : baked;
		for ( int k = 0; k < listed; k++ )
		{
			unsigned off = 0, cnt = 0;
			zx::levelmesh::GetSegPieceRange( i, k, off, cnt );
			if ( cnt == 0 ) continue;
			float zlo = 1e9f, zhi = -1e9f;
			for ( unsigned v = 0; v < cnt && off + v < (unsigned)nv; v++ )
			{
				const float z = verts[off + v].z;
				if ( z < zlo ) zlo = z;
				if ( z > zhi ) zhi = z;
			}
			Printf( "   piece %d: range %u+%u, z %.1f..%.1f, band light %d\n", k, off, cnt, zlo, zhi,
				zx::levelmesh::GetSegBandLight( i, k ) );
		}
	}
	if ( found == 0 ) Printf( "line %d has no segs\n", want );
}
