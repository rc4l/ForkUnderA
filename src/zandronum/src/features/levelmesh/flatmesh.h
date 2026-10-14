// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Flats (floors and ceilings) into the level mesh, so a backend gets the whole world rather
// than just its walls.
//
// Walls came through the wall cache because they are *derived* every frame and worth caching. Flats
// are different: FFlatVertexBuffer already keeps them in a static buffer with stable per-sector
// ranges, so nothing here needs to cache generation. What is needed is the same hand-off walls get --
// triangle-list geometry plus the material and light to shade it -- keyed so a subsector's floor and
// ceiling are each registered exactly once.
//
// The uv convention (`fx/64`, `-fy/64`) and the plane evaluation mirror GLFlat::DrawSubsector
// exactly; if that function changes, this must follow or flats will drift from the GL render.

#ifndef ZX_FLATMESH_H
#define ZX_FLATMESH_H

#include "tarray.h"   // [rc4l] the moved sectors and changed sides RefreshFakePlanes takes
#include "features/levelmesh/staticmesh.h"                   // [rc4l] MeshVertex, which PortalViewState holds
#include "features/levelmesh/computation/flatmesh_compute.h" // [rc4l] the walk slots and sky lists
#include "features/materials/materials.h"                    // [rc4l] MaterialRef

struct subsector_t;
struct FPortal;
struct FColormap;
struct sector_t;
struct GLSectorPlane;
class FMaterial;
namespace zx { namespace levelmesh { struct MeshRange; struct PlaneUVTransform; struct MeshPiece; struct MeshVertex; } }

namespace zx { namespace levelmesh {


// [rc4l] Whether the walk in progress belongs to the player's view (true) or a camera texture's, as RenderViewpoint was told.
void SetMainView(bool mainView);
bool MainView();

// [rc4l] GL's DrawScene nesting: the player's scene is 1, every portal's runs inside it.
void EnterScene();
void LeaveScene();

// [rc4l] Whether what is being captured right now belongs to the frame (ComputeOwnsFrameCapture): never a mirror, skybox or camera-texture scene.
bool PlayerWalkActive();
// [rc4l] The stacked view whose walk GL is in now (ComputeStackCaptureSlot), or -1.
int StackCaptureSlot();
// [rc4l] The mirror view whose walk is in now (ComputeMirrorViewId: a first reflection's line, a deeper one's id past the lines), or -1.
int MirrorCaptureLine();
// [rc4l] The id of the mirror view down the path of `depth` lines from the player outward, -2 when no walk made it this frame.
int MirrorViewOf(const int *path, int depth);
// [rc4l] This frame's steps between mirror views (ComputeMirrorSteps), for a traced ray to follow.
void MirrorSteps(std::vector<int> &out);
// [rc4l] Whether a sprite, model or decal GL draws now is captured: the player's walk's for the frame, a stacked view's or a line mirror's walk's for that pass alone.
bool DynamicCaptureActive();
// [rc4l] A particle handed to the backend this frame, for fua_particles.
void CountParticle();

// [rc4l] A stacked view the player's walk saw this frame: the eye's move into it in map units, and the plane it opens from.
struct StackViewInfo
{
	float dx, dy;
	bool ceiling;
};

// [rc4l] The same wall from a visibility walk (features/visibility); returns the portal's slot, or -1 once every slot is taken.
int AddStackWall(const FPortal *portal, float x1, float y1, float x2, float y2, const float *zb, const float *zt);
// [rc4l] Start the frame's stacked views, their sky surfaces, the stacked views' and skyboxes' walk bits and the line mirrors walked afresh, for walks outside GL.
void BeginPortalViews();
// [rc4l] Whether those came from walks outside GL since GL last walked.
bool PortalViewsDerived();
// [rc4l] A line mirror's sheen light, kept with its linedef.
struct MirrorSheenShade { int line; MeshPiece shade; };
// [rc4l] All of that, exchanged with `other`, so a derivation can be compared against what GL's walks captured.
struct PortalViewState
{
	PortalViewState();
	int stackUsed;
	TArray<const void *> portals;
	TArray<StackViewInfo> info;
	TArray<TArray<MeshVertex> > walls, sky;
	TArray<unsigned int> bits;
	TArray<TArray<int> > marked;
	TArray<bool> needsClear;
	bool skyboxWalked;
	TArray<int> mirrors;
	TArray<MirrorSheenShade> sheens;
};
void SwapPortalViews(PortalViewState &other);
// [rc4l] At level load: how many stacked views the level can show at once, one per sector-stack portal it has, which sizes the walk bits, the sky lists and the views' slots.
void SetStackViewCapacity(int views);
int StackViewCapacity();
// [rc4l] The walk slots each id has this level (ComputeWalkSlots), the stride the walk bits are read by.
int WalkSlots();
int StackViewCount();
StackViewInfo StackView(int slot);
const MeshVertex *StackViewVertices(int slot, int &count);

// [rc4l] A subsector GL's walk accepted (DoSubsector) and a sidedef whose seg its clipper let through (AddLine), kept for the stacked view or the player's walk whose it is.
void NoteWalkedSubsector(int subsector);
void NoteWalkedSide(int side);
// [rc4l] The id a stacked view keeps or drops a level-mesh piece by, and GL's light links name it by: the sidedef it was baked from (MeshPiece::linkId), else its subsector for a flat or numsubsectors plus the nearest sidedef for a wall, -1 if it cannot be placed.
int PieceStackId(const MeshPiece &p);
// [rc4l] This frame's ComputeMarkStackVisible bits over the subsectors then the sidedefs, or NULL when no pass reads them (ComputeWalkBitsNeeded).
const unsigned int *StackVisibility(int &words);
// [rc4l] The player's walk's sky holes this frame (ComputeSkyHoleRecord, count of them), or NULL with none.
const float *SkyHoles(int &count);

// [rc4l] Whether GL's walk this frame went through the line mirror on linedef line, which is what says it can be seen.
bool MirrorWalkedThisFrame(int line);
// [rc4l] A line mirror a visibility walk saw this frame, as MirrorWalk takes GL's.
void AddWalkedMirror(int line);

// [rc4l] The same from a visibility walk, from the wall's light and colormap as GL's first piece of it holds them.
void AddMirrorSheen(int line, int lightlevel, const FColormap &cm);
// [rc4l] That light for linedef line this frame, or NULL when GL drew no sheen on it.
const MeshPiece *MirrorSheen(int line);

// [rc4l] Marks GL's walk through a line mirror (GLMirrorPortal::DrawContents) by its linedef, so the sprites, models and decals it draws reach that mirror's pass in the backend.
struct MirrorWalk
{
	explicit MirrorWalk(int line);
	~MirrorWalk();
	int saved, savedView;
};

// [rc4l] A scene entered for as long as it is in scope, as EnterScene and LeaveScene pair.
struct SceneScope
{
	SceneScope() { EnterScene(); }
	~SceneScope() { LeaveScene(); }
};

// [rc4l] Marks GL's walk from a skybox viewpoint (GLSkyboxPortal::DrawContents), so what it reaches is the sky portal pass's to draw.
struct SkyboxWalk
{
	SkyboxWalk();
	~SkyboxWalk();
	bool saved;
};
// [rc4l] Whether a skybox walk marked anything since the player's walk began this frame.
bool SkyboxWalked();

// [rc4l] Marks GL's walk through a stacked view (GLSectorStackPortal::DrawContents), so its sky quads fill that view's list.
struct StackWalk
{
	explicit StackWalk(const void *portal);
	~StackWalk();
	const void *saved;
};

// [rc4l] A fullbright texture draws at light 255 and white light, as GLFlat::Process and DrawFloodedPlane both decide it.
void ApplyFullbright(zx::materials::MaterialRef mat, FColormap &cm, int &lightLevel);

// [rc4l] A sector plane's texture mapping, by gl_SetPlaneTextureRotation's inputs: its offsets, scale, angle and the texture's size.
void PlaneUVFor(const GLSectorPlane &plane, zx::materials::MaterialRef mat, PlaneUVTransform &out);

// [rc4l] s as GL draws it in the view area last baked for (gl_FakeFlat into scratch), or s itself without a height transfer; back as GL asks for a seg's far sector.
sector_t *TransferSector(sector_t *s, sector_t *scratch, bool back);

// [rc4l] Re-bakes every height transfer's planes when the view has crossed into another area (ComputeViewArea), as an eye dipping under a lake's surface does; call once a frame with the view set.
void RefreshTransferArea();

// [rc4l] Every flat in the level, from the map, with no GLFlat involved.
//
// The companion to BakeLevelFromMap for walls, and the other half of what fua_dg_standalone needs:
// GL's walk is what produces GLFlats, so with it cut out the floors and ceilings have to come from
// somewhere else. Returns how many planes it built.
int BakeFlatsFromMap();

// One sector's planes again after it has moved -- see the definition for why the stamp cannot notice.
int BakeFlatsForSector(int sectorIndex);

// [rc4l] GL's missing-texture fills again after sectors moved or sides changed texture (features/levelmesh/fakeplanes.h): each subsector whose fake planes changed drops the old ones and bakes the new.
void RefreshFakePlanes(const TArray<int> &movedSectors, const TArray<int> &changedSides);
// [rc4l] The same with every candidate read again.
void RefreshAllFakePlanes();
// [rc4l] The fake planes modelled on sector baked again, since they take its light, colour and texture.
void RebakeFakesModelledOn(int sector);

// [rc4l] How often a visible flat was left alone rather than rebuilt. A cache whose hit rate
// nobody can see is a cache nobody can tell has stopped working.
void GetFlatCacheStats(int &hits, int &rebuilds);
// [rc4l] Flats whose shade changed and were re-registered without rebuilding their vertices.
int GetFlatReshades();
// [rc4l] Flats whose offsets scrolled and were shifted in the piece without rebuilding their vertices.
int GetFlatRescrolls();

// [rc4l] One captured flat, read-only: which subsector and side it is, which sector and plane it
// was taken from, and where its vertices live. For checking a derivation against the capture.
bool CachedFlat(int index, const subsector_t **sub, bool *ceiling, const sector_t **model,
	int *whichPlane, MeshRange *range);

// Forget every baked flat -- called when the wall cache is invalidated, so the two stay in step.
void ClearFlats();
// How many flat registrations were a subsector own plane vs a 3D floor plane.
void GetFlatStats(int &own, int &threeD);

int FlatPieceCount();

// [rc4l] One decal quad, four corners in the fan order GLWall::DrawDecal emits and drawn with `blendMode` (MeshPiece::blendMode), into the dynamic stream with the sprites: no invalidation to get wrong when one spawns, fades or is replaced.
void RegisterDecal(const MeshVertex *quad, const void *material, int translation,
                   int blendMode, float alpha,
                   int lightlevel, int rel, const FColormap &colormap,
                   bool redToAlpha, unsigned int alphaColor,
                   float sortX, float sortY, float sortZ, const float *dynLight, int onMirror = -1);

void ClearSprites();

// [rc4l] What the per-frame actor sweep actually looked at, so "narrow the sweep" can be argued from
// a number rather than from the shape of the loop. Reported by fua_sprite_sweep.
void RecordSpriteSweep(int seen, int noSector, int behind, int offScreen, int processed);
int SpritePieceCount();

// [rc4l] A vertical quad from (x1, y1) to (x2, y2), as six triangle-list vertices in fan order.
void MakeWallQuad(MeshVertex *out6, float x1, float y1, float x2, float y2,
	const float *zbottom2, const float *ztop2);

}} // namespace zx::levelmesh

#endif // ZX_FLATMESH_H
