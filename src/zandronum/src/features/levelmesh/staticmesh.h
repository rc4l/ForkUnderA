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

struct FFlatVertex;
// [rc4l] Global scope, NOT inside zx::levelmesh -- a forward declaration in the namespace would
// declare zx::levelmesh::FColormap, a different type from the engine's, and the mismatch only shows
// up as a baffling overload-resolution error at the call site.
struct FColormap;

namespace zx { namespace levelmesh {

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

	// [rc4l] This piece is one FACE of a 3D floor, so only the side its normal points at may be drawn.
	//
	// A 3D floor is two planes, a top and a bottom, and the engine processes only whichever faces the
	// viewer. The mesh is a cache and ends up holding both, permanently -- and a decorative grate or
	// bridge is often only a few units thick, or exactly zero, so the pair sit on top of each other
	// and z-fight. The flicker as the view moves is the depth test changing its mind, and GL never
	// shows it because GL never draws both. Only 3D floor planes are marked: an ordinary sector floor
	// has no opposite face to fight with.
	bool          planeFacing;

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
		planeFacing = false;
	}
};

// [rc4l] Run the engine's own gl_SetColor/gl_SetFog for a surface and record what they produced into
// `out`. Walls and flats share it so there is exactly one place that decides how a baked surface is
// lit. `noFog` is the RENDERWALL_M2SNF case, which the wall path renders with fog forced off.
void CaptureShading(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out,
                    bool noFog = false);

void MeshInitForLevel();
void MeshFreeLevel();

// Reserve (or reuse) a range and copy `count` triangle-list vertices into it. Returns false if the
// buffer is full or the input is unusable, in which case the caller keeps streaming that wall.
// Make a range draw nothing without freeing it, so a re-bake can write straight back over it.
void MeshSquash(const MeshRange &range);

bool MeshStore(MeshRange &range, const FFlatVertex *verts, int count);

// Bind the mesh's vertex array for drawing. Returns false if there is nothing baked yet.
bool MeshBind();

// Draw one baked range, and the batched form over many ranges.
void MeshDraw(const MeshRange &range);
void MeshDrawMulti(const MeshRange *ranges, int n);

// ---------------------------------------------------------------------------
// Indexed drawing (plan phase P2d) -- the state-grouped path.
//
// glMultiDrawArrays over N scattered ranges lost to streaming's single contiguous glDrawArrays
// (P2c). An index buffer fixes that without moving any vertices: per frame, the visible pieces of
// one draw-state run have their vertex indices appended contiguously, and the whole run draws with
// ONE glDrawElements. Writing an index costs 4 bytes where streaming a vertex costs 20, so the
// per-frame traffic drops 5x while the draw count stays at one per state run.
//
// This is also the shape the swap needs: a (firstIndex, count) pair over a shared buffer IS a
// DrawElementsIndirectCommand, so a culling pass can later fill these in on the GPU untouched.
// ---------------------------------------------------------------------------

// Start a frame's index accumulation.
void MeshIndexBegin();

// Append one baked range's indices to the current run. Returns false if the buffer is full.
bool MeshIndexAppend(const MeshRange &range);

// Close the current run, returning its (firstIndex, count); count 0 if nothing was appended.
void MeshIndexEndRun(unsigned int &firstIndex, unsigned int &count);

// Upload the frame's indices, once, before any indexed draw.
void MeshIndexFlush();

// Draw one closed run.
void MeshDrawIndexed(unsigned int firstIndex, unsigned int count);

// Push any pending CPU-side writes to the GPU. Called once per frame before drawing.
void MeshFlush();

// [rc4l] A dirty range for a SECOND consumer, independent of MeshFlush's GL one.
//
// The level mesh is not as static as its name suggests: a door, lift or crusher moves a sector plane,
// the wall cache's stamp invalidates, and BakeSeg rewrites that seg's vertices in place. The GL path
// picks that up through MeshFlush; a foreign backend needs its own cursor, because MeshFlush clears
// the range as it consumes it.
//
// Reported in mesh-vertex units, and only when the bytes ACTUALLY changed -- uncacheable segs
// re-capture every frame and rewrite identical vertices, so a naive "was written" flag would report
// the whole level dirty on every frame forever.
void MeshTakeDirty(unsigned int &lo, unsigned int &hi);

// Stats for fua_levelmesh_stats.
void MeshGetStats(int &bakedPieces, int &vertices, int &bytes, int &reallocs);

// [rc4l] The CPU mirror of the baked geometry, for a backend that wants to own its own upload.
// This is the seam the renderer swap consumes: plain interleaved position+uv vertices, no GLWall,
// no draw list, nothing GL-specific -- exactly what a Vulkan/Diligent backend needs.
const FFlatVertex *MeshVertexData(int &count);

// [rc4l] Every baked piece, in bake order. A backend groups these by material itself.
void MeshRegisterPiece(const MeshPiece &piece);
const MeshPiece *MeshPieces(int &count);
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
void DynAppend(const FFlatVertex *verts, int count, MeshPiece proto);

// This frame's dynamic vertices and pieces.
const FFlatVertex *DynVertices(int &count);
const MeshPiece   *DynPieces(int &count);

// [rc4l] Bumped by every DynClear, so a backend can tell "same data as last time" from "new frame".
//
// A backend that re-uploads on every draw call rather than every frame will exhaust Vulkan's dynamic
// heap: the benchmark issues 300 draws between frame boundaries, and the heap is only recycled when a
// frame completes. 300 sprite uploads at ~420 KB each is 126 MB against an 8 MB heap.
unsigned int DynGeneration();

}} // namespace zx::levelmesh

#endif // ZX_STATICMESH_H
