// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/levelmesh/wallcache.h"
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/levelmesh.h"
#include "features/levelmesh/computation/flatmesh_compute.h"

#include "r_defs.h"
#include "r_state.h"
#include "doomdata.h"
#include "gl/scene/gl_drawinfo.h"
#include "gl/data/gl_data.h"
#include "gl/data/gl_vertexbuffer.h"   // FFlatVertex
#include "gl/renderer/gl_renderer.h"   // in_area, area_default
#include "gl/renderer/gl_renderstate.h"
#include "gl/renderer/gl_lightdata.h"  // gl_SetColor / gl_SetFog
#include "c_dispatch.h"
#include "c_cvars.h"
#include "c_console.h"

// [rc4l] Declared at global scope: EXTERN_CVAR inside the namespace would name a different symbol
// than the CVAR defined in gl_drawinfo.cpp, and only the linker would notice.
EXTERN_CVAR(Bool, gl_wallmesh)
EXTERN_CVAR(Int, gl_fogmode)
// [rc4l] The animated-texture re-resolve on replay, as a switch, so its cost can be A/B'd from the
// console instead of from two builds. Off renders stale animation frames -- a measurement aid, not
// a setting anyone should turn off.
CVAR(Bool, gl_wallcache_anim, true, 0)

namespace zx { namespace levelmesh {

static TArray<SegCache> g_cache;
// [rc4l] Per-sector fua_dirty as of the last invalidation sweep, and the segs each sector owns.
//
// See InvalidateMovedSectors: a moving sector's geometry has to be dropped from the mesh whether or
// not the BSP happens to walk it, and answering "which segs belong to this sector" by scanning all
// segs every frame would be silly when the mapping never changes.
static TArray<int>          g_sectorDirty;
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
	g_cache.Clear();
	g_uncacheable.Clear();
	if (numsegs <= 0) return;
	g_cache.Resize(numsegs);
	g_uncacheable.Resize(numsegs);
	g_segFate.Clear();
	g_segFate.Resize(numsegs);
	for (int i = 0; i < numsegs; i++) g_segFate[i] = SEG_UNSEEN;
	g_sectorDirty.Clear();
	g_sectorSegs.Clear();
	if (numsectors > 0)
	{
		g_sectorDirty.Resize(numsectors);
		g_sectorSegs.Resize(numsectors);
		for (int i = 0; i < numsectors; i++) { g_sectorDirty[i] = sectors[i].fua_dirty; g_sectorSegs[i].Clear(); }
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
		for (int k = 0; k < kMaxCachedPieces; k++)
		{
			g_cache[i].pieces[k].list = 0;
			g_cache[i].pieces[k].range.offset = 0;
			g_cache[i].pieces[k].range.count = 0;
		}
		g_uncacheable[i] = false;
	}
	g_captureSeg = -1;
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
	if (piece >= g_cache[seg].pieceCount) return NULL;
	const MeshRange &r = g_cache[seg].pieces[piece].range;
	return r.count ? &r : NULL;
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
void BakeSeg(int segIndex)
{
	if (!gl_wallmesh) return;   // [rc4l] the mesh draw path is off; baking would be pure cost
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return;
	SegCache &sc = g_cache[segIndex];

	static FFlatVertex fan[GLWall::MAX_BATCH_FAN_VERTICES];
	static FFlatVertex tris[GLWall::MAX_BATCH_FAN_VERTICES * 3];

	// [rc4l] Collapse pieces this seg no longer produces.
	//
	// The mesh hands a backend fixed vertex ranges, and a backend draws them until told otherwise --
	// there is no "remove" in a bump-allocated arena. So a piece that stops existing has to be
	// squashed to zero area rather than abandoned, or it keeps rendering. Opening a door is exactly
	// this case: its middle texture disappears, and without this the shut door stayed drawn across
	// the doorway in the Vulkan view while GL showed the room behind it.
	for (int i = sc.pieceCount; i < sc.bakedCount && i < kMaxCachedPieces; i++)
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
		CaptureWallShading(sc.walls[i], mp);

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
			case RENDERWALL_TOP:    mp.baseTex = TexMan[sd->GetTexture(side_t::top)]; break;
			case RENDERWALL_BOTTOM: mp.baseTex = TexMan[sd->GetTexture(side_t::bottom)]; break;
			case RENDERWALL_M1S:
			case RENDERWALL_M2S:
			case RENDERWALL_M2SNF:  mp.baseTex = TexMan[sd->GetTexture(side_t::mid)]; break;
			default: break;
			}
		}
		MeshRegisterPiece(mp);
	}
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

void RecordPiece(const GLWall &wall, int list)
{
	if (g_captureSeg < 0) return;
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

bool SegHasBakedGeometry(int segIndex)
{
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return false;
	const SegCache &sc = g_cache[segIndex];
	for (int i = 0; i < sc.bakedCount && i < kMaxCachedPieces; i++)
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
void InvalidateMovedSectors()
{
	if (!gl_wallmesh || g_sectorDirty.Size() == 0) return;

	for (int s = 0; s < numsectors; s++)
	{
		if (sectors[s].fua_dirty == g_sectorDirty[s]) continue;
		g_sectorDirty[s] = sectors[s].fua_dirty;

		const TArray<int> &segList = g_sectorSegs[s];
		for (unsigned k = 0; k < segList.Size(); k++)
		{
			const int idx = segList[k];
			if ((unsigned)idx >= g_cache.Size()) continue;
			SegCache &sc = g_cache[idx];
			sc.filled = false;   // force a re-capture when the BSP next reaches it
			// [rc4l] Re-arm the once-per-seg bake, or squashing this geometry destroys it forever.
			//
			// EndCapture bakes a refused seg only on the false->true edge of g_uncacheable, because a
			// refused seg re-captures every single frame and baking it every frame would burn the
			// arena. But g_uncacheable is sticky and nothing ever cleared it, so a seg that had
			// already been refused once -- which is every 3D floor and everything under sky -- got
			// its ranges squashed here and then never re-baked. On dbab02 a switch raises a platform,
			// and the 16-unit-thick grate over the lava pit next to it vanished from the Vulkan view
			// permanently while GL kept drawing it. Clearing the flag makes the next capture bake
			// exactly once more, which is the same budget the first bake had.
			g_uncacheable[idx] = false;
			// Squash, not release: the range stays allocated so the re-bake writes back over it.
			// Freeing here re-allocates every frame the sector moves and the arena runs away.
			for (int i = 0; i < kMaxCachedPieces; i++)
				MeshSquash(sc.pieces[i].range);
		}
	}
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

void GetSegPieceRange(int segIndex, int piece, unsigned int &offset, unsigned int &count)
{
	offset = count = 0;
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return;
	if (piece < 0 || piece >= kMaxCachedPieces) return;
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
	static const char *kName[7] = { "never captured", "cached", "portal", "polyobject",
	                                "3D floor", "heightsec area", "other" };
	Printf( "wallcache census over %d drawable segs:\n", drawable );
	for ( int i = 0; i < 7; i++ )
		if ( fate[i] > 0 )
			Printf( "  %-15s %5d segs (%4.1f%%), %5d with baked geometry (%4.1f%% of them)\n",
					kName[i], fate[i], 100.0 * fate[i] / MAX( 1, drawable ),
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
		for ( int k = 0; k < pieces; k++ )
		{
			unsigned off = 0, cnt = 0;
			zx::levelmesh::GetSegPieceRange( i, k, off, cnt );
			if ( cnt == 0 ) { Printf( "   piece %d: empty\n", k ); continue; }
			float zlo = 1e9f, zhi = -1e9f;
			for ( unsigned v = 0; v < cnt && off + v < (unsigned)nv; v++ )
			{
				const float z = verts[off + v].z;
				if ( z < zlo ) zlo = z;
				if ( z > zhi ) zhi = z;
			}
			Printf( "   piece %d: range %u+%u, z %.1f..%.1f\n", k, off, cnt, zlo, zhi );
		}
	}
	if ( found == 0 ) Printf( "line %d has no segs\n", want );
}
