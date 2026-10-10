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

struct subsector_t;
struct FPortal;
class GLWall;
class GLFlat;
class GLSprite;
struct FFlatVertex;
struct FColormap;
struct sector_t;
struct GLSectorPlane;
class FMaterial;
namespace zx { namespace levelmesh { struct MeshRange; struct PlaneUVTransform; struct MeshPiece; } }

namespace zx { namespace levelmesh {

// Bake one subsector's plane into the mesh and register it as a piece. Cheap and idempotent: a
// (subsector, plane) already baked at the same geometry is skipped.
void RegisterFlatSubsector(const GLFlat &flat, subsector_t *sub, bool ceiling);

// [rc4l] Whether the walk in progress belongs to the player's view (true) or a camera texture's, as RenderViewpoint was told.
void SetMainView(bool mainView);
bool MainView();

// [rc4l] GL's DrawScene nesting: the player's scene is 1, every portal's runs inside it.
void EnterScene();
void LeaveScene();

// [rc4l] Whether what is being captured right now belongs to the frame (ComputeOwnsFrameCapture): never a mirror, skybox or camera-texture scene.
bool PlayerWalkActive();

// [rc4l] A stacked view the player's walk saw this frame: the eye's move into it in map units, and the plane it opens from.
struct StackViewInfo
{
	float dx, dy;
	bool ceiling;
};

// [rc4l] A wall GL stencils a stacked view through (RENDERWALL_SECTORSTACK, from the stacked plane to the map's limit), taken as geometry into its portal's view; with the cap it is the whole of GL's stencil, so geometry beyond a stacked plane still hides the view as it does in GL.
void RegisterStackWall(const GLWall &wall, const FPortal *portal);
int StackViewCount();
StackViewInfo StackView(int slot);
const FFlatVertex *StackViewVertices(int slot, int &count);

// [rc4l] A subsector GL's walk accepted (DoSubsector) and a sidedef whose seg its clipper let through (AddLine), kept for the stacked view whose walk it is.
void NoteWalkedSubsector(int subsector);
void NoteWalkedSide(int side);
// [rc4l] The id a stacked view keeps or drops a level-mesh piece by: its subsector for a flat, numsubsectors plus its sidedef for a wall, -1 if it cannot be placed.
int PieceStackId(const MeshPiece &p);
// [rc4l] This frame's ComputeMarkStackVisible bits over the subsectors then the sidedefs, or NULL with no stacked view.
const unsigned int *StackVisibility(int &words);

// [rc4l] Marks GL's walk through a line mirror (GLMirrorPortal::DrawContents), so the viewer's sprite it draws reaches the backend's mirror pass.
struct MirrorWalk
{
	MirrorWalk();
	~MirrorWalk();
	bool saved;
};

// [rc4l] Marks GL's walk through a stacked view (GLSectorStackPortal::DrawContents), so its sky quads fill that view's list.
struct StackWalk
{
	explicit StackWalk(const void *portal);
	~StackWalk();
	const void *saved;
};

// [rc4l] A fullbright texture draws at light 255 and white light, as GLFlat::Process and DrawFloodedPlane both decide it.
void ApplyFullbright(const FMaterial *mat, FColormap &cm, int &lightLevel);

// [rc4l] A sector plane's texture mapping, by gl_SetPlaneTextureRotation's inputs: its offsets, scale, angle and the texture's size.
void PlaneUVFor(const GLSectorPlane &plane, FMaterial *mat, PlaneUVTransform &out);

// [rc4l] Every flat in the level, from the map, with no GLFlat involved.
//
// The companion to BakeLevelFromMap for walls, and the other half of what fua_dg_standalone needs:
// GL's walk is what produces GLFlats, so with it cut out the floors and ceilings have to come from
// somewhere else. Returns how many planes it built.
int BakeFlatsFromMap();

// One sector's planes again after it has moved -- see the definition for why the stamp cannot notice.
int BakeFlatsForSector(int sectorIndex);

// [rc4l] How often a visible flat was left alone rather than rebuilt. A cache whose hit rate
// nobody can see is a cache nobody can tell has stopped working.
void GetFlatCacheStats(int &hits, int &rebuilds);

// [rc4l] One captured flat, read-only: which subsector and side it is, which sector and plane it
// was taken from, and where its vertices live. For checking a derivation against the capture.
bool CachedFlat(int index, const subsector_t **sub, bool *ceiling, const sector_t **model,
	int *whichPlane, MeshRange *range);

// Forget every baked flat -- called when the wall cache is invalidated, so the two stay in step.
void ClearFlats();
// How many flat registrations were a subsector own plane vs a 3D floor plane.
void GetFlatStats(int &own, int &threeD);

int FlatPieceCount();

// [rc4l] Sprites. Unlike walls and flats these MOVE, so the range is keyed on the actor and reused
// in place -- registering a fresh range each frame would grow the buffer without bound, which is the
// failure that killed an early version of the wall cache.
// [rc4l] The four corners are passed in, not taken off the GLSprite.
//
// GL rasterises v1..v4 -- the billboard-rotated corners -- while this used to read the un-rotated
// x1/x2/y1/y2/z1/z2 straight off the sprite. They agree only while no rotation is applied, so
// RF_ROLLSPRITE, RF_FORCEXYBILLBOARD and gl_billboard_mode 1 were silently ignored by the mesh.
// Corner order matches GL's strip: (x1,z1,y1), (x2,z1,y2), (x1,z2,y1), (x2,z2,y2).
void RegisterSprite(const GLSprite &spr, const float *c1, const float *c2,
	const float *c3, const float *c4);

// Sprite render styles seen since load, indexed by STYLEOP_*. See fua_spritestyles.
void GetSpriteStyleStats(int *ops16, int &flagsSeen);
void GetSpriteStyleDetail(int *dest16, int *classified4);

// [rc4l] One decal quad, captured where GLWall::DrawDecal emits its four vertices.
//
// Decals live on a sidedef's AttachedDecals list and are re-walked every frame, so they belong in
// the DYNAMIC stream with the sprites rather than the baked mesh: no invalidation to get wrong when
// one spawns, fades or is replaced. `quad` is four FFlatVertex in the fan order GL emits.
void RegisterDecal(const FFlatVertex *quad, const void *material, int translation,
                   bool shadow, bool additive, float alpha,
                   int lightlevel, int rel, const FColormap &colormap,
                   bool redToAlpha, unsigned int alphaColor,
                   float sortX, float sortY, float sortZ, const float *dynLight);

// [rc4l] The same, for a mark whose shape is not a quad: a projected decal, clipped to whatever
// geometry it landed on. `tris` is a triangle LIST, so count is a multiple of three.
void RegisterDecalTriangles(const FFlatVertex *tris, int count, const void *material, int translation,
                            bool shadow, bool additive, float alpha,
                            int lightlevel, int rel, const FColormap &colormap,
                            bool redToAlpha, unsigned int alphaColor,
                            float sortX, float sortY, float sortZ, const float *dynLight);
void ClearSprites();

// [rc4l] What the per-frame actor sweep actually looked at, so "narrow the sweep" can be argued from
// a number rather than from the shape of the loop. Reported by fua_sprite_sweep.
void RecordSpriteSweep(int seen, int noSector, int behind, int offScreen, int processed);
int SpritePieceCount();

// [rc4l] Print this frame's sprite pieces -- z range, texture-coordinate range, light, resolved
// colour, alpha, blend. See fua_sprites.
void DumpSpriteNotes();

// [rc4l] A vertical quad from (x1, y1) to (x2, y2), as six triangle-list vertices in fan order.
void MakeWallQuad(FFlatVertex *out6, float x1, float y1, float x2, float y2,
	const float *zbottom2, const float *ztop2);

}} // namespace zx::levelmesh

#endif // ZX_FLATMESH_H
