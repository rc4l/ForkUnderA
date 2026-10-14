// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The level's walls baked from the map, per seg: the ranges each owns, what each took its light from, and the sweep that re-bakes what moved or relit.

#ifndef ZX_WALLCACHE_H
#define ZX_WALLCACHE_H

#include "features/levelmesh/staticmesh.h"
#include "features/surfaces/surfacebuild.h"                 // [rc4l] DerivedWallSpan, DerivedWallLight
#include "features/surfaces/computation/wallbands_compute.h"   // [rc4l] WallBand
#include "features/levelmesh/computation/wallcache_compute.h"   // [rc4l] kPart*
#include "features/levelmesh/computation/flatmesh_compute.h"    // [rc4l] MidTextureBlend
#include "features/materials/materials.h"                       // [rc4l] MaterialRef
#include "tarray.h"

struct seg_t;
struct line_t;
struct sector_t;
struct vertex_t;
struct F3DFloor;
class FTextureID;

class AActor;
class GLWall;

namespace zx { namespace levelmesh {

// [rc4l] How many mesh ranges a seg can own: a wall in a sector with 3D floors is cut into a piece per light band, so three parts by five bands, then the 3D-floor faces.
const int kMaxMapPieces = 24;

// [rc4l] One baked piece of a seg: its geometry in the persistent buffer, count 0 for none.
struct CachedWallPiece
{
	int       list;
	MeshRange range;
};

// [rc4l] Bake one seg from the map with no GLWall, each part and band in a fixed slot so a re-bake reuses the same mesh range.
int BakeSegFromMap(int segIndex);

// How many vertices the mesh holds for one part of one seg (0 upper, 1 lower, 2 middle).
int MapBakePartCount(int segIndex, int part);

// Every wall in the level, from the map, in one pass. Returns the number of parts built.
int BakeLevelFromMap();

// [rc4l] The most light bands a wall part is cut into.
const int kBandsPerPart = 5;

// [rc4l] One light band of a wall part as GLWall::SplitWall cuts it, lit by Put3DWall's rule: its level, the colormap whose light colour it takes (when lightEntry is not -1), and which light list entry that was.
struct PartBand
{
	zx::surfaces::WallBand band;
	int lightLevel;
	FDynamicColormap *lightFrom;
	int lightEntry;
};

// [rc4l] The bands of seg's part with span `d` and own light `dl`, top down; how many, at most kBandsPerPart.
int DerivedPartBands(const seg_t *seg, const zx::surfaces::DerivedWallSpan &d, const zx::surfaces::DerivedWallLight &dl, PartBand *out);

// [rc4l] The seg that bakes sidedef `side`'s walls, or -1.
int SideOwnerSeg(int side);

// [rc4l] Part `part` (kPart*) of seg as the bake derives it, false when the seg has none; `twoSidedMiddle` says the middle is a two-sided line's, which blends.
bool DerivedPart(const seg_t *seg, int part, zx::surfaces::DerivedWallSpan &d, bool &twoSidedMiddle, bool mirrorFace = false);

// [rc4l] One 3D floor face on a seg (DoFFloorBlocks, BuildFFBlock): the opening's ends in side order, the block cut from it, its slab, and the light it takes, the front sector's band its top falls in (lightIndex -1 for the wall's own).
struct FFFace
{
	float x1, y1, x2, y2;
	float top[2], bottom[2];
	F3DFloor *rover;
	int light;
	FDynamicColormap *lightFrom;   // [rc4l] the band's colour, or NULL for the wall's own
	int lightIndex;
};
enum { kMaxFFFaces = 16 };

// [rc4l] A face's texture by BuildFFBlock's rule: the sidedef's upper or lower when the slab asks for one, else the master sidedef's middle; false with no master.
bool FFFaceTexture(const seg_t *seg, const F3DFloor *rover, FTextureID &texid, int &texpos);

// [rc4l] The faces seg's back sector's slabs put on it, top down, at most `max`, `dl` the wall's own light; how many.
int DerivedFFFaces(const seg_t *seg, const zx::surfaces::DerivedWallLight &dl, FFFace *out, int max);

// [rc4l] How a two-sided middle of `line` drawn with `mat` blends, as GL draws it.
MidTextureBlend LineMidBlend(const line_t *line, zx::materials::MaterialRef mat);

// [rc4l] Whether seg's line draws no wall of its own, as a horizon or a line mirror does.
bool LineDrawsNoWall(const seg_t *seg);

// [rc4l] gl_RecalcVertexHeights: the heights of the planes meeting at v, which r_seamless splits wall ends at, worked out again.
void RecalcVertexHeights(vertex_t *v);

// [rc4l] Whether seg's line is a line mirror, which draws its reflection where the wall would be.
bool LineIsMirror(const seg_t *seg);

struct SegCache
{
	CachedWallPiece pieces[kMaxMapPieces];
	int             pieceCount;
	// [rc4l] How many pieces were in the mesh last time this seg baked.
	//
	// A seg does not always produce the same number of pieces: a closed door has a middle texture
	// that an open one does not, so re-baking can leave slots occupied in the mesh that nothing
	// writes to any more. Those slots keep their old vertices and keep drawing -- the closed door
	// stayed painted across the doorway while the GL view showed the room beyond it. Knowing the
	// previous count is what lets BakeSegFromMap collapse the abandoned ones.
	int             bakedCount;

	// [rc4l] What each baked slot took its LIGHT from, so a light change need not re-derive the wall.
	//
	// Three quarters of the sectors invalidated on a live level have not moved at all -- sector_t
	// bumps fua_dirty for ChangeLightLevel exactly as it does for ChangePlaneTexZ, and Doom's light
	// effects run every tic. Re-deriving a wall's spans, UVs and bands to express a flicker is the
	// bulk of the once-per-tic frame spike that reads as choppy movement.
	//
	// Geometry cannot have moved if the planes did not, so the band STRUCTURE is unchanged and only
	// each band's light value differs. This records which light list entry a slot used (-1 = the
	// seg's own light), which is the one input a reshade cannot re-derive for free.
	signed char     bandLight[kMaxMapPieces];
	// 3D-floor slots take their light from an F3DFloor rather than the band list; a seg carrying any
	// falls back to a full bake rather than growing a second rule here.
	bool            hasOwnLitPiece;   // [rc4l] a fog boundary or a flooded gap, whose light is not in bandLight
	bool            mapBaked;         // [rc4l] pieces[] holds the map bake's slots by part, not the captured walls in order

	// [rc4l] pieces[] must be cleared here, not left to whatever was in the memory.
	//
	// MeshRange is a POD and this constructor used to skip the array entirely, which is invisible on
	// the first level and wrong on every one after it: TArray reuses its heap block, so a new level's
	// segs came up holding the PREVIOUS level's vertex ranges. BakeSegFromMap then saw a nonzero count, took
	// MeshStore's reuse-in-place path, and wrote the new wall over whatever legitimately occupied that
	// offset in the new arena -- while never allocating space of its own. The map came up with walls
	// missing and geometry smeared across the ones that survived, and only after a map CHANGE:
	// launching straight into the same map was always fine, because the block started zeroed.
	SegCache() : pieceCount(0), bakedCount(0), hasOwnLitPiece(false), mapBaked(false)
	{
		for (int i = 0; i < kMaxMapPieces; i++)
		{
			pieces[i].list = 0;
			pieces[i].range.offset = 0;
			pieces[i].range.count = 0;
			bandLight[i] = -1;
		}
	}
};

// Allocated per level, cleared when the level changes.
void AllocForLevel(int numsegs);

// Bumped once per level load, so a backend can notice it must set itself up again.
int LevelGeneration();
void FreeLevel();
void InvalidateAll();

// [rc4l] The kSkyCapture* list the GL walk in progress fills, which BeginSkyWalk picked.
int WalkSkyList();
int SkyQuadsCaptured();   // [rc4l] Since the level loaded, for fua_sky_quads.
AActor *SkyQuadViewpoint();
// [rc4l] A GL walk starting at this portal depth, for the main view or a camera texture; returns the kSkyCapture* list it fills.
int BeginSkyWalk();
// [rc4l] A view's sky: start kSkyCapture* list `list` afresh (the player's with its skybox and holes), then add each sky wall, with the skybox it names (NULL for a plain sky), and each sky hole, an untextured one-sided wall's span its full-screen sky shows through.
void BeginViewSky(int list);
// [rc4l] skyKey is the sector's sky as ComputeSkyQuadKey numbers it, 0 for the level's.
void AddViewSkyQuad(int list, float x1, float y1, float x2, float y2, const float *zb, const float *zt, AActor *viewpoint, int lineSpecial, int skyKey);
void AddViewSkyHole(int list, float x1, float y1, float x2, float y2, const float *zfloor, const float *zceil);
// [rc4l] The player's sky surfaces, holes and skybox, exchanged with `other`, so a second derivation can be compared against the first.
struct MainSkyState { TArray<MeshVertex> quads; TArray<float> holes; AActor *viewpoint = nullptr; };
void SwapMainSky(MainSkyState &other);
// [rc4l] Drop everything GL's walks captured, at level unload, since a backend frame before the next level's first walk would otherwise draw the old level's sky from its freed SkyViewpoint.
void ForgetWalks();
// [rc4l] A walk outside GL (features/visibility) into walk slot `slot` (kMainWalkSlot, a stacked view's, kSkyboxWalkSlot): start the slot afresh, then mark each subsector or sidedef id it reaches.
void BeginSlotVisibility(int slot);
void NoteSlotVisible(int slot, int id);
// [rc4l] NoteSlotVisible split for a walk run on several lanes: MarkSlotBits sets the bits of ids one lane owns (ranges aligned to whole words of bits, so no two lanes share one), and NoteSlotMarked then keeps them for the slot's next start, on one thread.
void MarkSlotBits(int slot, const int *ids, int count);
void NoteSlotMarked(int slot, const int *ids, int count);
// [rc4l] Whether the player's pass culls to its slot this frame: set by the player's visibility walk, cleared when GL walks.
void SetMainVisibility(bool on);
bool MainVisibility();
// [rc4l] The frame's walk bits whatever filled them, for comparing one walk against another.
int SkyWalks(int list);
// [rc4l] One view's sky quads: kSkyCaptureMain for the player, kSkyCaptureCamera for a camera texture.
const MeshVertex *SkyQuadVertices(int &count, int list);


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
	int pieces;          // baked wall pieces
};
void GetCoverage(CoverageStats &out);

// [rc4l] What this frame's walks reached, for re-baking only what can be seen: the sectors whose change re-bakes something reached, and the sidedefs reached, each once.
struct WalkedThisFrame
{
	const int *sectors;
	int sectorCount;
	const int *sides;
	int sideCount;
};

// [rc4l] Drops baked geometry for any sector that moved since the last call, before the BSP walk, and with `walked` leaves a sector or side nothing reached dirty until a walk reaches it.
void InvalidateMovedSectors(const WalkedThisFrame *walked = NULL);

// [rc4l] Re-derives every seg on a height transfer's sector, whose heights and light follow the view area (RefreshTransferArea).
void RebakeTransferSegs();

// One seg's cache state, for the fua_line_mesh dump.
void GetSegMeshInfo(int segIndex, int &pieces, int &baked);
void GetSegPieceRange(int segIndex, int piece, unsigned int &offset, unsigned int &count);
int GetSegBandLight(int segIndex, int piece);


// [rc4l] Why sectors were invalidated: only their light changed, or their planes actually moved.
//
// InvalidateMovedSectors answers every dirty sector with a full geometry re-derive, but sector_t
// bumps fua_dirty for light changes too and Doom's light effects run every tic. These two counters
// say which is really happening before anything is built on the assumption.
void SectorDirtyBreakdown(int &lightOnly, int &geometry);

// What the LAST frame spent invalidating, and how many sectors it answered. For attributing a
// frame-time spike to a cause rather than guessing at one.
double InvalidationMsLastFrame();
int    SectorsDirtyLastFrame();

}} // namespace zx::levelmesh

#endif // ZX_WALLCACHE_H
