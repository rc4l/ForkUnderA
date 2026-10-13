// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The persistent wall geometry buffer (plan phase P2c) -- the actual level mesh.
//
// Today a visible wall's vertices are written into a streaming buffer every frame. This uploads them
// ONCE into a GPU-resident buffer and gives each wall piece a stable (offset, count) range, so a draw
// references geometry instead of producing it.
//
// That is the step that leads to the renderer swap, and it matters more than the frametime it saves:
//   * a range is what glMultiDrawArrays consumes, and later what an indirect draw command contains;
//   * stable ranges are what a GPU culling pass writes into an index buffer;
//   * the buffer is plain vertex data with no GLWall in it, so a Vulkan/Diligent backend consumes
//     exactly the same thing without the scene layer coming along.
//
// Ranges are allocated by a bump allocator on first capture and reused in place when a seg is
// re-captured at the same size -- which is the normal case, since a wall's split count only changes
// when its geometry topology does. A size change reallocates and abandons the old range; that is
// bounded because sizes are stable, and the allocator refuses to grow past a cap rather than
// repeating the unbounded-growth failure that killed an earlier attempt.

#ifndef ZX_STATICMESH_H
#define ZX_STATICMESH_H

#include <stddef.h>
#include "features/levelmesh/computation/shading_compute.h"

namespace zx { namespace levelmesh {

// [rc4l] A mesh vertex: position as (x, height, y) and texture coordinate, the layout GL's FFlatVertex has.
struct MeshVertex
{
	float x, z, y;
	float u, v;

	void Set(float xx, float zz, float yy, float uu, float vv) { x = xx; z = zz; y = yy; u = uu; v = vv; }
};

// One piece's geometry in the persistent buffer.
struct MeshRange
{
	unsigned int offset;   // first vertex
	unsigned int count;    // triangle-list vertices; 0 means "not baked"
};

// [rc4l] One drawable piece of the level: geometry plus everything a backend needs to shade it.
// This is the whole hand-off to a foreign renderer -- deliberately free of GLWall, draw lists and
// anything else the GL scene layer owns.
struct MeshPiece
{
	MeshRange     range;
	const void   *material;     // FMaterial*, identity only -- the backend resolves it to its own texture
	int           lightLevel;   // 0..255 after fake-contrast
	unsigned int  lightColor;   // FColormap::LightColor, packed RGBA
	unsigned int  fadeColor;    // FColormap::FadeColor, packed RGBA

	// [rc4l] The FINAL shading inputs, captured from FRenderState right after the engine's own
	// gl_SetColor/gl_SetFog ran for this surface. A backend should use these and not re-derive
	// anything from the three fields above: those are the *inputs* to a calculation that also folds
	// in rellight, extralight, desaturation and blendfactor, and a second implementation of it drifts.
	float         colorR, colorG, colorB;   // vColor
	float         softLight;                // uLightLevel: 0..1 in software lighting, else -1
	float         fogDensity;               // uFogDensity, ready for exp2()
	unsigned int  fogColor;                 // uFogColor, packed RGB
	int           fogMode;                  // uFogEnabled: 0 off, +gl_fogmode plain, -n coloured
	int           desaturation;             // 0..255

	// [rc4l] Blending. Walls and flats are always opaque here; sprites are not, and a Doom scene
	// without translucency is missing plasma, fireballs, invisibility and every specter.
	//   0 = opaque / alpha-tested   1 = normal translucent   2 = additive   3 = fuzz (shadow)
	int           blendMode;
	// [rc4l] Palette remap, in FMaterial::CreateTexBuffer's convention.
	//
	// A sprite's colours are frequently not its own: GvH recolours projectiles and class gear this
	// way, and Doom itself recolours the player. GLSprite carries one and this never recorded it, so
	// every sprite drew in its base palette -- a frozen mortar came out in the original fire colours.
	int           translation;
	float         alpha;
	// Centre of the piece, for the back-to-front sort translucency needs. Only meaningful for
	// dynamic pieces; blendMode 0 never gets sorted.
	float         sortX, sortY, sortZ;

	// [rc4l] The sidedef's or sector's BASE texture (FTexture*), not the resolved animation frame.
	// NULL when unknown.
	//
	// `material` is an FMaterial* resolved at bake time, so a baked surface freezes on whichever
	// animation frame happened to be showing -- nukage stops flowing, computer screens stop
	// flickering, and the world looks subtly dead. Animation in ZDoom lives in TexMan's translation
	// table keyed by the BASE id, so the backend re-resolves this per frame via `->id`. Capturing the
	// resolved frame's own id would not work: an animation frame translates to itself.
	//
	// Stored as void* because staticmesh.h is included widely and has no business pulling in the
	// texture headers; the backend casts it back.
	const void   *baseTex;

	// [rc4l] Index into the dynamic light buffer for this surface, or -1 for none. The buffer is a
	// index into the light list the backend collects each frame. Muzzle
	// flashes, plasma, rocket trails and every lamp in a mod come through here; without it the world
	// is lit only by its sector light and looks flat and static.
	int           dynLightIndex;

	// [rc4l] Surface normal, in the mesh's (x, z-up, y) space.
	//
	// The engine only ever applies a dynamic light to surfaces on the lit SIDE of it -- gl_GetLight
	// does a secplane PointOnSide test per surface, and the light lists are per subsector. Testing
	// every light against every fragment without that check lights the backs of walls and the room
	// next door, which showed up as a scene far more saturated than GL's: a torch read
	// (44.4, 13.7, 2.5) against GL's (39.8, 32.1, 22.1).
	float         normX, normY, normZ;

	// [rc4l] Light this piece was given ON THE CPU, added after the lighting equation.
	//
	// A sprite's dynamic light is worked out per actor by gl_SetDynSpriteLight, not by the shader's
	// loop, and where it is added matters as much as what it is. GL adds it AFTER the distance
	// equation has scaled the sector light; folding it into the vertex colour instead puts it before,
	// so the equation attenuates it and a lit item in a dark room comes out dimmer than GL's. It is
	// therefore carried separately rather than mixed into colorR/G/B.
	float         dynR, dynG, dynB;
	// [rc4l] Lit by the CPU's light for the whole actor (dynR..dynB) rather than the shader's per-fragment loop, as GL lights sprites and models.
	bool          cpuLight;
	int           mirrorLine;   // [rc4l] the line mirror whose walk alone drew it (ComputeMirrorCaptureLine), or -1 for the frame's
	int           stackSlot;    // [rc4l] the stacked view whose walk alone drew it (ComputeStackCaptureSlot), or -1 for the frame's
	int           linkId;       // [rc4l] numsubsectors plus the sidedef a wall was baked from, the id GL's light links name it by, or -1 to place it by geometry (PieceStackId)
	bool          plain;        // [rc4l] GL draws it in its solid pass, so its see-through texels are drawn rather than discarded

	// [rc4l] This surface is seen from BELOW: a ceiling, or the underside of a 3D floor.
	//
	// Recorded so the winding can be verified rather than trusted. Back-face culling deletes any flat
	// whose triangles wind the wrong way for the side it is viewed from, and that failure is silent
	// and total -- every ceiling in the level, or every 3D floor top. fua_mesh_verify checks that all
	// pieces with the same value here wind the same way, which catches it without a camera.
	bool          facesDown;

	// [rc4l] Nudge this piece towards the camera in the depth test.
	//
	// A decal is a quad glued flat against the wall it marks, so it is exactly coplanar with it and
	// z-fights without help. GL wraps its decal pass in glPolygonOffset(-1, -128) for this; the
	// equivalent here is a pipeline with a depth bias, and the flag is what routes the piece to it.
	bool          depthBias;

	// [rc4l] This surface's texture is an ALPHA MASK, not a colour image.
	//
	// A shaded decal -- a scorch mark, most bullet impacts -- stores its shape in the red channel and
	// takes its colour from the decal's own AlphaColor. GL routes these through TM_REDTOALPHA with an
	// object colour. Sampled as an ordinary texture instead, the red channel reads as bright colour
	// and a black scorch mark renders as a white blob.
	bool          redToAlpha;
	// [rc4l] A stencil: GL's TM_MASK, the texture only its shape and the colour all the actor's fill, which the shading already carries.
	bool          stencil;
	// [rc4l] A sprite GL alpha-tests at gl_mask_sprite_threshold, which drops its soft edge texels rather than blending them.
	bool          alphaTest;
	bool          inverse;      // [rc4l] GL's TM_INVERSE, the texel's colour inverted, as night vision shows monsters and items
	bool          colorBlend;   // [rc4l] gl_usecolorblending's additive sprite, blended by its own colour (ComputeColorBlending)
	bool          fogLayer;     // [rc4l] a 3D fog floor's untextured panel, which GL leaves out under a fixed colormap
	int           fuzz;         // [rc4l] the fuzz shader it draws with (fuzz_compute's kFuzz*), 0 for none
	// [rc4l] A wall's glow from its sector's GLDEFS glowing ceiling and floor (GLWall::CheckGlowing): each colour with its height in w (0 for none), and the planes the glow is measured from as a, b, 1/c, d.
	float         glowTop[4], glowBottom[4], glowTopPlane[4], glowBottomPlane[4];
	bool          hud;          // [rc4l] a HUD model's surface, drawn over the world as GL draws one after clearing the depth buffer

	// [rc4l] This piece is a QUAD: four corners, not six vertices.
	//
	// A sprite is a billboard, and the mesh stores triangle lists, so it was written out as two
	// triangles with two of the four corners duplicated -- thirty floats where twenty would do, and
	// then copied again on the backend's side into its own vertex format. Upstream stores four and
	// draws a triangle strip. Storing four and expanding once, where the backend is already writing
	// its own vertices anyway, costs nothing and removes a third of the traffic.
	//
	// Corner order is GL's strip order: (x1,z1,y1), (x2,z1,y2), (x1,z2,y1), (x2,z2,y2).
	bool          quad;
	// [rc4l] A closed 3D shell (a model) rather than a flat billboard, so it must occlude itself:
	// the backend draws these with the world, depth-writing, instead of in the sorted blend pass.
	bool          solid;

	// [rc4l] A flooded gap: drawn with the plane at floodZ seen through it, u = floodUV[0..2] . (x, y, 1) and v likewise from [3..5].
	bool          flood;
	float         floodZ;
	float         floodUV[6];
	// [rc4l] A reflective floor or ceiling (Sector_SetPlaneReflection): its sector, -1 for none, and which plane.
	int           mirrorSector;
	bool          mirrorCeiling;
	// [rc4l] How far a scroller has moved the texture since the vertices were baked, added to every u and v by the shader.
	float         scrollU, scrollV;

	// [rc4l] Zero-initialise. Every site fills the fields it cares about and leaves the rest, so a
	// field added later is garbage at the sites that predate it -- which is how SegCache::pieces
	// silently inherited the previous level's vertex ranges. A translation of garbage would remap
	// every wall in the level to a random palette.
	MeshPiece()
	{
		range.offset = range.count = 0;
		material = NULL; baseTex = NULL;
		lightLevel = 0; lightColor = 0; fadeColor = 0;
		colorR = colorG = colorB = 1.f;
		softLight = -1.f; fogDensity = 0.f; fogColor = 0; fogMode = 0; desaturation = 0;
		blendMode = 0; translation = 0; alpha = 1.f;
		sortX = sortY = sortZ = 0.f;
		dynLightIndex = -1;
		normX = normY = normZ = 0.f;
		dynR = dynG = dynB = 0.f;
		quad = false; solid = false;
		facesDown = false; depthBias = false; redToAlpha = false; stencil = false; alphaTest = false; inverse = false; colorBlend = false; fogLayer = false; fuzz = 0; hud = false;
		for (int i = 0; i < 4; i++) glowTop[i] = glowBottom[i] = glowTopPlane[i] = glowBottomPlane[i] = 0.f;
		flood = false; floodZ = 0.f;
		mirrorSector = -1; mirrorCeiling = false;
		scrollU = scrollV = 0.f;
		cpuLight = false;
		mirrorLine = -1;
		stackSlot = -1;
		linkId = -1;
		plain = false;
		for (int i = 0; i < 6; i++) floodUV[i] = 0.f;
	}
};

// [rc4l] Shade a piece by the light model (ComputeShading), the one place that decides how a surface is lit, and reset what shading does not decide.
void ShadePiece(const ShadingInput &in, const ShadingEnv &env, MeshPiece &out);

void MeshInitForLevel();
void MeshFreeLevel();

// Reserve (or reuse) a range and copy `count` triangle-list vertices into it. Returns false if the
// buffer is full or the input is unusable, in which case the caller keeps streaming that wall.
// Make a range draw nothing without freeing it, so a re-bake can write straight back over it.
void MeshSquash(const MeshRange &range);

bool MeshStore(MeshRange &range, const MeshVertex *verts, int count);

// [rc4l] The span the backend must upload again after a door, lift or crusher rewrote a seg's vertices, in mesh-vertex units and only when the bytes actually changed.
void MeshTakeDirty(unsigned int &lo, unsigned int &hi);

// [rc4l] The dirty spans, itemised. One lo..hi covers everything BETWEEN two changes, which on a
// large level is the whole world; these are the ranges that actually moved. NULL means the list
// overflowed and the caller should fall back to the span.
const MeshRange *MeshTakeDirtyRanges(int &count);

// [rc4l] Changes when the piece LAYOUT changes -- a piece appearing, moving, resizing, or changing
// the base texture or blend mode that decides its batch. Not shading, and not an animated material
// swap. A backend keeping its own copy of the geometry compares this instead of hashing every
// piece every frame to find out that nothing happened.
unsigned int MeshLayoutGeneration();

// Which of the three reasons has been bumping it: a piece added, a piece resized, a piece changing
// the texture or blend mode that decides its batch.
void MeshLayoutReasons(int &added, int &resized, int &rebatched);

// The piece owning a range, for patching a dirty span with the shading that goes with it.
const MeshPiece *MeshPieceByOffset(unsigned int offset);
void MeshClearDirtyRanges();

// Stats for fua_levelmesh_stats.
void MeshGetStats(int &bakedPieces, int &vertices, int &bytes, int &reallocs);

// [rc4l] The CPU mirror of the baked geometry, for a backend that wants to own its own upload.
// This is the seam the renderer swap consumes: plain interleaved position+uv vertices, no GLWall,
// no draw list, nothing GL-specific -- exactly what a Vulkan/Diligent backend needs.
const MeshVertex *MeshVertexData(int &count);

// [rc4l] Every baked piece, in bake order. A backend groups these by material itself.
// [rc4l] Give a range back: its vertices are blanked and the piece registered against it is dropped.
// A table of ranges that is cleared without retiring them leaves those pieces in the list forever.
void MeshRetireRange(const MeshRange &range);

void MeshRegisterPiece(const MeshPiece &piece);

// [rc4l] The two questions a backend has to ask about surfaces it has already uploaded, answered by
// one comparison rather than by four channels that each knew about part of a surface. See
// features/surfaces/computation/surfacechange_compute.h.
//
// A REBATCH means a surface no longer belongs where it is -- a different material, pass, palette or
// range -- and moving one between batches is not something a patch can express, so it is a rebuild.
// A REPAINT means the surface is in the right place with the wrong shading baked into it, which
// since the shading moved into a per-piece record is a sixty-four byte upload.
//
// Both revisions are cumulative for the life of the level: a backend stores the value it last built
// at and compares, so it cannot miss one by not looking on the right frame.
unsigned int MeshRebatchRevision();
unsigned int MeshRepaintRevision();
void MeshTakeRepaints(const unsigned int *&list, int &count);
void MeshClearRepaints();
const MeshPiece *MeshPieces(int &count);

// The registered piece at a vertex offset, or NULL. Lets a caller re-shade a surface without
// re-deriving it -- see the definition.
const MeshPiece *MeshFindPiece(unsigned int offset);

// Arena occupancy: vertices handed out, vertices parked on the free list, reuses and size-change
// reallocations since the level loaded. `parked` growing without bound is the leak this watches for.
void MeshArenaStats(unsigned int &used, unsigned int &parked, int &reuses, int &reallocs);
void MeshPieceListStats(unsigned int &pieces, unsigned int &indexed);
void MeshClearPieces();

// ---------------------------------------------------------------------------
// Dynamic stream -- geometry that is rebuilt every frame
// ---------------------------------------------------------------------------
//
// [rc4l] Sprites belong here, not in the static mesh. A sprite quad is built facing ONE viewpoint,
// so baked into a static buffer it stops facing the player the moment they turn, and a full-level
// bake leaves every actor on the map drawn at once from wherever each was first seen. That is
// exactly what happened, and it survived review for a while because it was only ever checked against
// a screenshot taken from the camera the bake ran from.
//
// So: cleared at the top of every frame, appended to as the engine generates each sprite, consumed
// by the backend at the end of the frame. No keying, no reuse, no invalidation -- the whole thing is
// thrown away and rebuilt, which is the correct lifetime for view-dependent geometry.

// Drop last frame's dynamic geometry. Called once per frame before anything generates any.
void DynClear();

// Append one piece's triangle-list vertices. `proto` carries the material and shading; its range is
// filled in by this call.
void DynAppend(const MeshVertex *verts, int count, const MeshPiece &proto);

// This frame's dynamic vertices and pieces.
const MeshVertex *DynVertices(int &count);
const MeshPiece   *DynPieces(int &count);

// [rc4l] Bumped by every DynClear, so a backend can tell "same data as last time" from "new frame".
//
// A backend that re-uploads on every draw call rather than every frame will exhaust Vulkan's dynamic
// heap: the benchmark issues 300 draws between frame boundaries, and the heap is only recycled when a
// frame completes. 300 sprite uploads at ~420 KB each is 126 MB against an 8 MB heap.
unsigned int DynGeneration();

}} // namespace zx::levelmesh

#endif // ZX_STATICMESH_H
