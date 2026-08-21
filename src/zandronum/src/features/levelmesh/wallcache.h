// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Per-seg wall geometry cache (plan phase P2b).
//
// GLWall::Process regenerates every visible wall every frame -- 8251 of them on Sunder MAP10, for a
// level whose geometry did not move. This keeps the GLWalls a seg produced, together with a stamp of
// the inputs they were generated from, and replays them straight into the draw lists when nothing
// those inputs cover has changed.
//
// The validity rule itself is pure and lives in computation/wallcache_compute.h; this file is the
// storage plus the two hooks (capture during Process, replay instead of it).

#ifndef ZX_WALLCACHE_H
#define ZX_WALLCACHE_H

#include "features/levelmesh/computation/wallcache_compute.h"
#include "features/levelmesh/staticmesh.h"
#include "gl/scene/gl_wall.h"
#include "tarray.h"

struct seg_t;
struct sector_t;

namespace zx { namespace levelmesh {

// [rc4l] Storage is FIXED per seg, not a growing level-wide array.
//
// The growing version leaked: any re-capture -- and a sector with a flickering light re-captures
// every tic, because its light level is part of the stamp -- stranded the seg's previous walls
// forever. That reached 47 GB and killed the process. With a fixed slot per seg, re-capture
// overwrites in place and leaking is structurally impossible.
//
// A seg needing more pieces than this is simply not cached. Four covers upper/mid/lower plus one
// spare; 3D-floor segs, which are the ones that need more, are already ineligible.
const int kMaxCachedPieces = 4;

// One GLWall as PutWall routed it, so replay can put it back in the same list without re-deciding.
struct CachedWallPiece
{
	int       list;
	MeshRange range;   // [rc4l] baked geometry in the persistent buffer; count 0 = stream instead
};

// Resolve a draw item's packed (seg, piece) reference to the GLWall it draws.
GLWall *StaticWall(int packedIndex);

// The baked geometry range for the same reference, or NULL if that piece is not baked.
const MeshRange *StaticWallRange(int packedIndex);

// [rc4l] Walk what was captured, so a derivation can be checked against it.
//
// features/surfaces is working out surfaces from the map instead of transcribing what GL made of
// it, and the only honest way to promote that over the capture is to check it against the capture
// on real maps, piece by piece. Read-only: the verifier looks, it does not bake.
int CachedSegCount();
int CachedPieceCount(int segIndex);
const GLWall *CachedPiece(int segIndex, int piece);

// Bake a captured seg's pieces into the persistent buffer. Called after a successful capture.
void BakeSeg(int segIndex);

// [rc4l] Bake one seg from the MAP, with no GLWall involved -- see the definition for what it costs
// and what it cannot do yet.
int BakeSegFromMap(int segIndex);

// How many vertices the mesh holds for one part of one seg (0 upper, 1 lower, 2 middle).
int MapBakePartCount(int segIndex, int part);

// Every wall in the level, from the map, in one pass. Returns the number of parts built.
int BakeLevelFromMap();

// Pack/unpack the reference a draw item carries.
inline int PackWallRef(int segIndex, int piece) { return segIndex * kMaxCachedPieces + piece; }

struct SegCache
{
	GLWall          walls[kMaxCachedPieces];
	CachedWallPiece pieces[kMaxCachedPieces];
	int             pieceCount;
	// [rc4l] How many pieces were in the mesh last time this seg baked.
	//
	// A seg does not always produce the same number of pieces: a closed door has a middle texture
	// that an open one does not, so re-baking can leave slots occupied in the mesh that nothing
	// writes to any more. Those slots keep their old vertices and keep drawing -- the closed door
	// stayed painted across the doorway while the GL view showed the room beyond it. Knowing the
	// previous count is what lets BakeSeg collapse the abandoned ones.
	int             bakedCount;
	WallCacheStamp  stamp;
	bool            filled;

	// [rc4l] pieces[] must be cleared here, not left to whatever was in the memory.
	//
	// MeshRange is a POD and this constructor used to skip the array entirely, which is invisible on
	// the first level and wrong on every one after it: TArray reuses its heap block, so a new level's
	// segs came up holding the PREVIOUS level's vertex ranges. BakeSeg then saw a nonzero count, took
	// MeshStore's reuse-in-place path, and wrote the new wall over whatever legitimately occupied that
	// offset in the new arena -- while never allocating space of its own. The map came up with walls
	// missing and geometry smeared across the ones that survived, and only after a map CHANGE:
	// launching straight into the same map was always fine, because the block started zeroed.
	SegCache() : pieceCount(0), bakedCount(0), filled(false)
	{
		for (int i = 0; i < kMaxCachedPieces; i++)
		{
			pieces[i].list = 0;
			pieces[i].range.offset = 0;
			pieces[i].range.count = 0;
		}
	}
};

// Allocated per level, cleared when the level changes.
void AllocForLevel(int numsegs);

// Bumped once per level load, so a backend can notice it must set itself up again.
int LevelGeneration();
void FreeLevel();
void InvalidateAll();

// Capture: while a capture is open, GLWall::PutWall records what it routed instead of only pushing.
// Opening a capture on a seg that turns out to produce a portal marks it uncacheable for the level.
void BeginCapture(int segIndex);
void EndCapture(const WallCacheStamp &stamp, const WallCacheEligibility &e);
bool IsCapturing();
void RecordPiece(const GLWall &wall, int list);
void NotePortal();

// Replay: push the cached pieces for this seg back into the draw lists. Returns false if there is
// nothing usable, in which case the caller must run Process as normal.
bool TryReplay(int segIndex, const WallCacheEligibility &e, const WallCacheStamp &current);

// Build the stamp and eligibility for a seg from its current sectors.
void BuildStamp(const seg_t *seg, const sector_t *frontsector, const sector_t *backsector,
                WallCacheStamp &out);
void BuildEligibility(const seg_t *seg, const sector_t *frontsector, const sector_t *backsector,
                      WallCacheEligibility &out);

// Stats for fua_levelmesh_stats / tuning.
void GetStats(int &hits, int &misses, int &uncacheable);

// [rc4l] Replayed walls whose material changed under them -- animated textures the cache froze.
int GetAnimRefreshes();

// [rc4l] Per-seg census: what became of this seg (see the SEG_* enum) and whether it baked anything.
int  SegFate(int segIndex);
bool SegHasBakedGeometry(int segIndex);
void ResetStats();

// [rc4l] How many captured walls of a RENDERWALL_ type arrived with their top v inside the first
// copy of the texture, counted at capture rather than read back from the cache -- see RecordPiece.
void GetCaptureVRangeStats(int type, int &inRange, int &outOfRange);

// How many of those out-of-range walls were fragments SplitWall made.
int CaptureVOutOfRangeSplits();

void ResetCaptureVRangeStats();

// [rc4l] How much of the level the wall mesh actually holds, as opposed to how much it could.
//
// A full-level bake grew the mesh from 8408 to 21886 pieces and the obvious question -- is that all
// of it? -- had no answer, so the next step was going to be guesswork about which culling check was
// still in the way. Segs that never bake are the real ceiling on a draw-the-whole-level design, and
// they need to be countable, not inferred from a triangle total.
struct CoverageStats
{
	int segs;            // segs in the level
	int drawableSegs;    // segs with a sidedef (minisegs draw nothing)
	int baked;           // segs with at least one baked piece
	int uncacheable;     // segs permanently excluded (portals and friends)
	int pieces;          // baked wall pieces
};
void GetCoverage(CoverageStats &out);

// Drop baked geometry for any sector that moved since the last call. Runs before the BSP walk.
void InvalidateMovedSectors();

// [rc4l] Sectors that moved the last time InvalidateMovedSectors ran. A standalone frame skips the
// BSP walk, and the walk is what re-bakes a moved seg -- so a frame with movement in it is a frame
// that has to go back through GL.
int SectorsMovedLastFrame();

// One seg's cache state, for the fua_line_mesh dump.
void GetSegMeshInfo(int segIndex, int &pieces, int &baked);
void GetSegPieceRange(int segIndex, int piece, unsigned int &offset, unsigned int &count);

// Why captures were refused, split by cause, plus how many succeeded.
void GetRejects(int &portal, int &poly, int &ffloor, int &area, int &other, int &ok);

}} // namespace zx::levelmesh

#endif // ZX_WALLCACHE_H
