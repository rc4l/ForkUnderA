#include "gl/system/gl_system.h"
#include <stddef.h>   // offsetof, for the cached-record guard
#include "gl/scene/gl_wallcache.h"
#include "gl/scene/gl_drawinfo.h"
#include "gl/renderer/gl_renderer.h"   // in_area, area_default
#include "gl/textures/gl_material.h"
#include "features/hwrender/computation/walllight_compute.h"   // [rc4l] kNoWallLightIndex

#include "c_cvars.h"
#include "c_dispatch.h"
#include "r_defs.h"
#include "r_sky.h"   // skyflatnum
#include "r_state.h"

// [rc4l] An animated wall texture is re-resolved on replay, or the cache freezes it at its captured frame.
CVAR(Bool, gl_wallcache_anim, true, 0)

int gl_wallcacheMode = 1;

namespace {

struct GLSegCache
{
	GLWall walls[kGLCachedWalls];
	int    lists[kGLCachedWalls];   // [rc4l] the draw list PutWall routed each to, so replay puts it back there
	int    count;
	zx::levelmesh::WallCacheStamp stamp;
	bool   filled;
	GLSegCache() : count(0), filled(false) {}
};

TArray<GLSegCache>     g_cache;
TArray<bool>          g_uncacheable;   // [rc4l] sticky: this seg produced per-frame state, never replay it
TArray<unsigned char> g_segFate;
int  g_captureSeg = -1;
bool g_sawPortal = false;
int  g_hits = 0, g_misses = 0, g_uncacheableHits = 0, g_animRefresh = 0;

//==========================================================================
//
// ForgetFrameState
//
// [rc4l] Clears what describes this frame rather than the wall (dynlightindex, viewdistance, firstwall, numwalls, the skybox/sky/horizon/portal union), since a replayed wall would read data since overwritten.
//
//==========================================================================

void ForgetFrameState(GLWall &w)
{
	w.dynlightindex = zx::hwrender::kNoWallLightIndex;
	w.viewdistance = 0;
	w.firstwall = 0;
	w.numwalls = 0;
	w.skybox = NULL;   // the union: every member of it is a per-frame pointer
}

// [rc4l] A field added to GLWall changes its size or shifts the ones after it, and stops the build until it is classified above; numwalls is pinned because an int added after dynlightindex fell into the padding before the union and moved nothing else.
#if defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__)
static_assert(RENDERWALL_TOP == zx::levelmesh::kRenderWallTop && RENDERWALL_M1S == zx::levelmesh::kRenderWallM1S &&
	RENDERWALL_BOTTOM == zx::levelmesh::kRenderWallBottom && RENDERWALL_MIRRORSURFACE == zx::levelmesh::kRenderWallMirrorSurface,
	"gl_wall.h's RENDERWALL_ order moved: update wallcache_compute.h");
static_assert(sizeof(GLWall) == 912,
	"GLWall changed size. Classify the new field as world state or frame state in ForgetFrameState, clear it there if it is frame state, then update this number.");
static_assert(offsetof(GLWall, seg) == 896, "GLWall fields moved: see ForgetFrameState.");
static_assert(offsetof(GLWall, numwalls) == 184, "GLWall fields moved: see ForgetFrameState.");
static_assert(offsetof(GLWall, dynlightindex) == 176, "GLWall fields moved: see ForgetFrameState.");
#endif

} // namespace

void gl_WallCacheAlloc(int numsegs)
{
	g_cache.Clear();
	g_uncacheable.Clear();
	g_segFate.Clear();
	g_captureSeg = -1;
	g_hits = g_misses = g_uncacheableHits = g_animRefresh = 0;
	if (numsegs <= 0) return;
	g_cache.Resize(numsegs);
	g_uncacheable.Resize(numsegs);
	g_segFate.Resize(numsegs);
	for (int i = 0; i < numsegs; i++)
	{
		// [rc4l] Resize does not construct a recycled slot, so each is cleared here.
		g_cache[i].count = 0;
		g_cache[i].filled = false;
		g_uncacheable[i] = false;
		g_segFate[i] = GLSEG_UNSEEN;
	}
}

void gl_WallCacheFree()
{
	g_cache.Clear();
	g_uncacheable.Clear();
	g_segFate.Clear();
	g_captureSeg = -1;
}

void gl_WallCacheBuildStamp(const seg_t *seg, const sector_t *frontsector, const sector_t *backsector, zx::levelmesh::WallCacheStamp &out)
{
	out.frontDirty = frontsector->fua_dirty;
	out.backDirty  = backsector ? backsector->fua_dirty : 0;
	out.sideDirty  = seg->sidedef ? seg->sidedef->fua_dirty : 0;
}

void gl_WallCacheBuildEligibility(const seg_t *seg, const sector_t *frontsector, const sector_t *backsector, zx::levelmesh::WallCacheEligibility &out)
{
	out.isPolyobject = (seg->sidedef != NULL) && !!(seg->sidedef->Flags & WALLF_POLYOBJ);
	out.hasHeightsec = (frontsector->heightsec != NULL) || (backsector != NULL && backsector->heightsec != NULL);
	out.hasFFloors   = (frontsector->e != NULL && frontsector->e->XFloor.ffloors.Size() > 0) ||
	                   (backsector != NULL && backsector->e != NULL && backsector->e->XFloor.ffloors.Size() > 0);
	out.producesPortal = false;   // discovered during capture, kept sticky in g_uncacheable
	// [rc4l] gl_CheckViewArea only resolves the area when a back sector has a heightsec, so an undetermined area is harmless without one.
	out.inArea = (in_area == area_default) && out.hasHeightsec;
}

void gl_WallCacheBeginCapture(int segIndex)
{
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) { g_captureSeg = -1; return; }
	g_captureSeg = segIndex;
	g_sawPortal = false;
	g_cache[segIndex].count = 0;
	g_cache[segIndex].filled = false;
}

void gl_WallCacheRecord(const GLWall &wall, int list)
{
	if (g_captureSeg < 0) return;
	// [rc4l] A translucent wall is never replayed: the BSP sorter indexes the per-frame walls[] array and would read garbage for a cache-owned item.
	if (list == GLDL_TRANSLUCENT || list == GLDL_TRANSLUCENTBORDER) g_sawPortal = true;
	GLSegCache &sc = g_cache[g_captureSeg];
	// [rc4l] Fixed slots per seg, overwritten on re-capture, so a flickering sector cannot strand walls; overflowing them gives up on the seg.
	if (sc.count >= kGLCachedWalls)
	{
		g_sawPortal = true;
		return;
	}
	sc.walls[sc.count] = wall;
	ForgetFrameState(sc.walls[sc.count]);
	sc.lists[sc.count] = list;
	sc.count++;
}

void gl_WallCacheNoteUnreplayable()
{
	// [rc4l] A portal wall, a missing-texture fill or a bare one-sided wall is per-frame state a replay never rebuilds, so the seg is processed every frame, sticky for the level.
	if (g_captureSeg >= 0) g_sawPortal = true;
}

void gl_WallCacheEndCapture(const zx::levelmesh::WallCacheStamp &stamp, const zx::levelmesh::WallCacheEligibility &e)
{
	if (g_captureSeg < 0) return;
	const int idx = g_captureSeg;
	g_captureSeg = -1;
	int fate = GLSEG_OK;
	if (g_sawPortal) fate = GLSEG_PORTAL;
	else if (!zx::levelmesh::ComputeIsCacheable(e))
		fate = e.isPolyobject ? GLSEG_POLY : e.hasFFloors ? GLSEG_FFLOOR : e.inArea ? GLSEG_AREA : GLSEG_OTHER;
	g_segFate[idx] = (unsigned char)fate;
	if (fate != GLSEG_OK)
	{
		g_uncacheable[idx] = true;
		g_cache[idx].count = 0;
		g_cache[idx].filled = false;
		return;
	}
	g_cache[idx].stamp = stamp;
	g_cache[idx].filled = true;
}

bool gl_WallCacheTryReplay(int segIndex, const zx::levelmesh::WallCacheEligibility &e, const zx::levelmesh::WallCacheStamp &current)
{
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return false;
	if (g_uncacheable[segIndex]) { g_uncacheableHits++; return false; }
	// [rc4l] A seg bordering the sky on either side is always processed, since its sky quad is built inside Process.
	if (segIndex < numsegs)
	{
		const seg_t *sg = &segs[segIndex];
		const sector_t *fsec = sg->frontsector, *bsec = sg->backsector;
		const bool frontSky = fsec != NULL && (fsec->GetTexture(sector_t::ceiling) == skyflatnum || fsec->GetTexture(sector_t::floor) == skyflatnum);
		const bool backSky = bsec != NULL && (bsec->GetTexture(sector_t::ceiling) == skyflatnum || bsec->GetTexture(sector_t::floor) == skyflatnum);
		if (frontSky || backSky) return false;
	}
	GLSegCache &sc = g_cache[segIndex];
	if (gl_wallcacheMode == 2 ? !sc.filled : !zx::levelmesh::ComputeCanReuse(e, sc.filled, sc.stamp, current))
	{
		g_misses++;
		return false;
	}
	for (int i = 0; i < sc.count; i++)
	{
		GLWall &w = sc.walls[i];
		ForgetFrameState(w);
		// [rc4l] Re-resolve the texture, since an ANIMDEFS frame change moves none of the stamp's counters and a replayed wall would keep its first frame.
		if (gl_wallcache_anim && w.gltexture != NULL && w.seg != NULL && w.seg->sidedef != NULL)
		{
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
				FMaterial *now = FMaterial::ValidateTexture(w.seg->sidedef->GetTexture(part), false, true);
				if (now != NULL && now != w.gltexture) { w.gltexture = now; g_animRefresh++; }
			}
		}
		gl_drawinfo->drawlists[sc.lists[i]].AddStaticWall(gl_WallCachePack(segIndex, i));
	}
	g_hits++;
	return true;
}

GLWall *gl_WallCacheStaticWall(int packedIndex)
{
	if (packedIndex < 0) return NULL;
	const int seg = packedIndex / kGLCachedWalls, wall = packedIndex % kGLCachedWalls;
	if ((unsigned)seg >= g_cache.Size() || wall >= g_cache[seg].count) return NULL;
	return &g_cache[seg].walls[wall];
}

int gl_WallCacheSegCount() { return (int)g_cache.Size(); }

int gl_WallCacheWallCount(int segIndex)
{
	if ((unsigned)segIndex >= g_cache.Size()) return 0;
	return g_cache[segIndex].filled ? g_cache[segIndex].count : 0;
}

const GLWall *gl_WallCacheWall(int segIndex, int wall)
{
	if ((unsigned)segIndex >= g_cache.Size() || wall < 0 || wall >= g_cache[segIndex].count) return NULL;
	return &g_cache[segIndex].walls[wall];
}

int gl_WallCacheSegFate(int segIndex)
{
	if (segIndex < 0 || (unsigned)segIndex >= g_segFate.Size()) return GLSEG_UNSEEN;
	return g_segFate[segIndex];
}

const char *gl_WallCacheFateName(int fate)
{
	static const char *kName[] = { "never captured", "cached", "portal or fill", "polyobject", "3D floor", "heightsec area", "other" };
	return (fate >= GLSEG_UNSEEN && fate <= GLSEG_OTHER) ? kName[fate] : "?";
}

CCMD( fua_gl_wallcache_stats )
{
	const int total = g_hits + g_misses + g_uncacheableHits;
	Printf( "wallcache since last reset: %d hits, %d misses, %d uncacheable (%d total)\n", g_hits, g_misses, g_uncacheableHits, total );
	if ( total > 0 )
		Printf( "  hit rate %.1f%%, uncacheable %.1f%%\n", 100.0 * g_hits / total, 100.0 * g_uncacheableHits / total );
	Printf( "  animated texture refreshes on replay: %d\n", g_animRefresh );
	g_hits = g_misses = g_uncacheableHits = g_animRefresh = 0;
}

CCMD( fua_gl_wallcache_census )
{
	int fate[GLSEG_OTHER + 1] = { 0 };
	int drawable = 0;
	for ( int i = 0; i < numsegs; i++ )
	{
		if ( segs[i].sidedef == NULL || segs[i].linedef == NULL ) continue;   // [rc4l] a miniseg
		drawable++;
		fate[gl_WallCacheSegFate( i )]++;
	}
	Printf( "wallcache census over %d drawable segs:\n", drawable );
	for ( int i = 0; i <= GLSEG_OTHER; i++ )
		if ( fate[i] > 0 ) Printf( "  %-15s %5d segs (%4.1f%%)\n", gl_WallCacheFateName( i ), fate[i], 100.0 * fate[i] / MAX( 1, drawable ) );
}
