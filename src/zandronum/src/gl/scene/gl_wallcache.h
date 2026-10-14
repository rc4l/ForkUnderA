#ifndef __GL_WALLCACHE_H
#define __GL_WALLCACHE_H

// [rc4l] GL's per-seg wall cache: the GLWalls a seg produced, replayed into the draw lists while their stamp (wallcache_compute.h) holds, which took Sunder MAP10 from 4.60 ms to 4.00 ms.

#include "features/levelmesh/computation/wallcache_compute.h"
#include "gl/scene/gl_wall.h"

struct seg_t;
struct sector_t;

// [rc4l] A seg needing more walls than this is not cached; upper, middle and lower plus a spare.
const int kGLCachedWalls = 4;

// [rc4l] Sized per level, cleared when the level goes.
void gl_WallCacheAlloc(int numsegs);
void gl_WallCacheFree();

// [rc4l] The stamp and eligibility of a seg from its current sectors.
void gl_WallCacheBuildStamp(const seg_t *seg, const sector_t *frontsector, const sector_t *backsector, zx::levelmesh::WallCacheStamp &out);
void gl_WallCacheBuildEligibility(const seg_t *seg, const sector_t *frontsector, const sector_t *backsector, zx::levelmesh::WallCacheEligibility &out);

// [rc4l] Push a seg's cached walls back into the draw lists; false when there is nothing usable, and the caller runs Process.
bool gl_WallCacheTryReplay(int segIndex, const zx::levelmesh::WallCacheEligibility &e, const zx::levelmesh::WallCacheStamp &current);

// [rc4l] While a capture is open, GLWall::PutWall records what it routed; a seg that turns out to need per-frame state is never replayed.
void gl_WallCacheBeginCapture(int segIndex);
void gl_WallCacheRecord(const GLWall &wall, int list);
void gl_WallCacheNoteUnreplayable();
void gl_WallCacheEndCapture(const zx::levelmesh::WallCacheStamp &stamp, const zx::levelmesh::WallCacheEligibility &e);

// [rc4l] The wall a draw item's packed (seg, wall) reference names.
inline int gl_WallCachePack(int segIndex, int wall) { return segIndex * kGLCachedWalls + wall; }
GLWall *gl_WallCacheStaticWall(int packedIndex);

// [rc4l] What was captured, read-only, so a derivation can be checked against GL's walls.
int gl_WallCacheSegCount();
int gl_WallCacheWallCount(int segIndex);
const GLWall *gl_WallCacheWall(int segIndex, int wall);

// [rc4l] Why each seg ended as it did: never captured, cached, or refused for a portal or fill, a polyobject, a 3D floor, a height-transfer area or another reason.
enum { GLSEG_UNSEEN = 0, GLSEG_OK, GLSEG_PORTAL, GLSEG_POLY, GLSEG_FFLOOR, GLSEG_AREA, GLSEG_OTHER };
int gl_WallCacheSegFate(int segIndex);
const char *gl_WallCacheFateName(int fate);

// [rc4l] gl_wallcache: 0 off, 1 on, 2 a diagnostic that skips validation and renders stale walls.
extern int gl_wallcacheMode;

#endif
