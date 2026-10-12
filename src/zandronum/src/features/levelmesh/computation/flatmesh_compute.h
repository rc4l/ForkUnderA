// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The decisions the flat mesh makes about a surface, pulled out where they can be tested.
//
// Every one of these shipped wrong at least once, and each failure was silent and total rather than
// noisy and partial -- which is exactly the shape a unit test catches and a screenshot does not:
//
//   * winding    -- a flat wound the wrong way for the side it is viewed from is DELETED by back-face
//                   culling. Getting this wrong removed every ceiling in the level, and getting it
//                   wrong a second way removed every 3D floor's walkable top surface. Neither
//                   produced an error; both produced a room with a hole in it.
//   * blend      -- a translucent surface classified opaque renders solid. A grate over a lava pit
//                   came out as solid metal.
//   * overlap    -- two coplanar surfaces claiming the same area do not agree on depth to the last
//                   bit, so the rasteriser stipples between them. 1799 such pairs existed before
//                   anyone noticed, because the artefact is only visible at certain angles.
//
// Engine-free and header-pure: flatmesh.cpp and the fua_mesh_verify diagnostic are the glue.

#ifndef ZX_FLATMESH_COMPUTE_H
#define ZX_FLATMESH_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] Must this flat's triangles be wound in reverse?
//
// A subsector's vertices arrive in one fixed order, so a floor and a ceiling built from them have
// the SAME winding while facing opposite directions. Exactly one of the two has to be reversed or a
// single cull mode deletes it.
//
// The argument is which SIDE the surface is viewed from, not which way its plane's normal points.
// Those differ: a 3D floor's walkable top surface is the control sector's ceiling plane, so its
// normal points down while the surface is seen from above. Winding by the normal looked more
// principled and culled exactly those surfaces.
bool ComputeFlatWindingReversed(bool viewedFromBelow);

// [rc4l] Where one face of a 3D floor takes its light, by ProcessSector's four calls to GLFlat::SetFrom3DFloor.
struct FFFaceLight
{
	bool lightBelow;   // [rc4l] read the plane light from under the plane (P_GetPlaneLight's underside)
	bool modelLight;   // [rc4l] FF_FIX: the model sector's own light and colormap instead
};

// [rc4l] A top plane is lit from above it and a bottom plane from below, whichever side it is seen from (a fog face the other way round), and only a bottom seen from above takes FF_FIX's model light.
FFFaceLight ComputeFFFaceLight(bool top, bool viewedFromBelow, bool fog, bool fix);

// [rc4l] Must this flat's NORMAL be flipped to face the side it is seen from?
//
// The same trap as the winding, one step further on, and it fails even more quietly. A secplane's
// normal describes the plane, not the surface: a 3D floor's underside is the control sector's FLOOR
// plane, so its normal points up while the surface is seen from below. Taken as given, the backend's
// dynamic-light side test then finds every light in the room BEHIND that surface and lights none of
// them -- which is indistinguishable, in a screenshot, from a light that is simply out of range.
//
// `planeUp` is the plane normal's vertical component. Zero is a vertical plane, which cannot be a
// flat and is left alone rather than flipped on the sign of a rounding error.
bool ComputeFlatNormalFlipped(bool viewedFromBelow, float planeUp);

// [rc4l] Which blend mode a surface with this render style and alpha needs.
//
//   0 opaque / alpha-tested   1 normal translucent   2 additive
//
// The alpha threshold is one 8-bit step: Doom expresses translucency in 0..255, so anything at or
// above 255/256 is opaque and rounding it any other way makes fully-opaque surfaces take the sorted
// translucent path for nothing.
int ComputeSurfaceBlendMode(bool additive, float alpha);

// [rc4l] Which blend mode a WALL needs.
//
// A wall's own alpha is not the whole story, and using it alone drew a pane of frosted glass as a
// solid white slab. A two-sided middle texture carries its transparency in the TEXTURE's alpha
// channel while the wall's alpha stays 1, and the alpha-tested opaque pass turns every partially
// transparent texel fully opaque. What actually knows is the draw list the engine routed the wall
// into: GLDL_TRANSLUCENT means "this needs blending", whatever the numbers say.
//
// Same lesson as the flat winding: use the decision the engine already made rather than re-deriving
// it from the inputs it made it from.
int ComputeWallBlendMode(bool inTranslucentList, bool additive, float alpha);

// [rc4l] Which blend mode a render STYLE needs -- sprites, decals, anything with an FRenderStyle.
//
// The full style matrix resolves to GL blend enums, which would then have to be mapped back into
// another API's. These four cases are what Doom content actually uses: opaque/masked, normal
// translucency, additive (plasma, fireballs, explosions, scorch decals) and the fuzz shadow.
// Anything exotic lands on normal translucency -- wrong, but visible rather than invisible.
int ComputeStyleBlendMode(bool shadow, bool additive, float alpha);

// [rc4l] Twice the signed area of a triangle projected onto the horizontal plane.
//
// Positive and negative correspond to the two winding directions; which one is "front" is a
// rasteriser convention and deliberately NOT decided here. What matters, and what the verifier
// checks, is that surfaces viewed from the same side all agree.
//
// Returns 0 for a degenerate triangle -- squashed pieces are legitimate (a retired range is zeroed
// in place) and must not be read as a winding error.
float ComputeTriangleWindingZ(float ax, float ay, float bx, float by, float cx, float cy);

// An axis-aligned bound, in map units. Degenerate on the axis a flat surface is flat in.
struct MeshBox
{
	float x0, x1, y0, y1, z0, z1;
};

// [rc4l] Do these two surfaces lie in the same plane AND claim overlapping area in it?
//
// Both conditions are needed. Two surfaces that merely touch along an edge are ordinary level
// geometry -- every floor in a level shares edges with its neighbours -- so the overlap has to be
// wide in at least two axes before it counts. Without that, the predicate flags most of the map and
// reports nothing.
bool ComputeCoplanarOverlap(const MeshBox &a, const MeshBox &b, float eps);

// [rc4l] Is the winding convention applied consistently across the level?
//
// Given, for surfaces viewed from above and from below, how many wind each way: the convention holds
// when each group is internally unanimous AND the two groups are opposite. Anything else means
// back-face culling keeps some surfaces and drops others that should be equally visible.
//
// A group with no members at all is vacuously fine -- a level with no ceilings is not a bug.
bool ComputeWindingConsistent(int fromAbovePositive, int fromAboveNegative,
                              int fromBelowPositive, int fromBelowNegative);

// [rc4l] Everything gl_SetPlaneTextureRotation applies to a flat's texture coordinates.
//
// The mesh baked UVs as (x/64, -y/64) and stopped there, which is only correct for an unoffset,
// unscaled, unrotated 64x64 flat. GL builds a texture matrix from all five of these, so scrolling
// floors did not scroll -- and, less visibly and far more widely, every flat whose texture is not
// 64x64 tiled at the wrong rate, because the 64/size term compensating for that lives in the same
// matrix. Nobody reported the second one; it reads as "the texture looks slightly off".
struct PlaneUVTransform
{
	float xoffs, yoffs;        // map units; what a scroller animates
	float xscale, yscale;      // 1 = unscaled
	float angleDegrees;        // the plane's own angle, NOT pre-negated
	float texWidth, texHeight; // in texels; the 64x64 assumption this replaces
	bool  hasCanvas;           // a camera texture is stored upside down, so its V scale is negated
};

// A transform that changes nothing, for a plain 64x64 flat.
PlaneUVTransform ComputeIdentityPlaneUV();

// [rc4l] Map a world position to texture coordinates for a flat.
//
// Mirrors gl_SetPlaneTextureRotation's matrix exactly, including its order. That matrix is built by
// post-multiplication, so the transforms apply to the coordinate in the REVERSE of the order they
// are written: rotate, then the 64/size correction, then the offset, then the plane's own scale.
// Applying them in the written order looks equally reasonable and puts scrolled or rotated flats in
// the wrong place.
void ComputePlaneUV(float px, float py, const PlaneUVTransform &t, float &u, float &v);

// [rc4l] The same mapping as six numbers (u = ux*x + uy*y + u0, v likewise), for a plane sampled per fragment rather than per vertex.
struct PlaneUVAffine
{
	float ux, uy, u0;
	float vx, vy, v0;
};
PlaneUVAffine ComputePlaneUVAffine(const PlaneUVTransform &t);

// [rc4l] The most stacked-sector views (GL's GLSectorStackPortal) a frame renders; a further portal in sight is left undrawn.
enum { kMaxStackViews = 2 };

// [rc4l] The walk-visibility slots each id has: one per stacked view, the player's own walk (what GL drew behind a sky hole), then GL's walk from the skybox viewpoint.
enum { kMainWalkSlot = kMaxStackViews, kSkyboxWalkSlot = kMaxStackViews + 1, kWalkSlots = kMaxStackViews + 2 };

// [rc4l] The sky-surface lists a GL scene walk can fill: the player's view, a camera texture's, one per stacked view, or none.
enum { kSkyCaptureNone = -1, kSkyCaptureMain = 0, kSkyCaptureCamera = 1, kSkyCaptureStack = 2,
       kSkyCaptureLists = kSkyCaptureStack + kMaxStackViews };

// [rc4l] Which list a walk fills: an outer walk fills the main or camera one, and the player's walk through a stacked view it saw (stackSlot, else -1) fills that view's.
int ComputeSkyCaptureList(bool mainView, int sceneNesting, int stackSlot);

// [rc4l] The stacked view a capture belongs to: stackSlot while the player's walk is inside that view, by ComputeSkyCaptureList's rule, else -1.
int ComputeStackCaptureSlot(bool mainView, int sceneNesting, int stackSlot);

// [rc4l] The list a backend pass draws its sky from: a camera texture's, a stacked view's (stackSlot, else -1), or the player's.
int ComputeSkyDrawList(bool cameraPass, int stackSlot);

// [rc4l] Whether a sky quad's skybox becomes the frame's: the player's own walk always, a stacked view's only while the player's named none.
bool ComputeTakesSkyViewpoint(int list, bool haveOne);

// [rc4l] The words of a frame's bit set holding, for every id (the subsectors, then the sidedefs), one bit per walk slot: whether that walk drew it.
int ComputeStackVisWords(int ids);

// [rc4l] Clears what ComputeMarkStackVisible set, for one walk's slot alone.
void ComputeClearStackVisible(unsigned int *bits, int id, int slot);

// [rc4l] Marks an id drawn by a walk, at bit id * kWalkSlots + slot, which is where the scene shader looks.
void ComputeMarkStackVisible(unsigned int *bits, int id, int slot);

// [rc4l] Whether a walk drew an id, read back as the scene shader reads it; an id outside the words is not drawn.
bool ComputeStackVisible(const unsigned int *bits, int words, int id, int slot);

// [rc4l] The slot a walk's drawing is marked in: a skybox's kSkyboxWalkSlot wherever it was entered from, a stacked view's own (stackSlot, -1 for one no slot took), the player's walk kMainWalkSlot, any other none (-1).
int ComputeWalkSlot(bool skyboxWalk, bool stackWalk, int stackSlot, bool playerWalk);

// [rc4l] The slot a backend pass draws only what that walk drew of: a stacked view's own, the sky portal's the skybox walk when GL made one this frame, else none (-1), and none at all without the frame's bits.
int ComputePassWalkSlot(bool bitsReady, int stackPassSlot, bool skyPortalPass, bool skyboxWalked, bool mainCull = false);

// [rc4l] Whether a frame's walk bits go to the backend: some pass reads them, a stacked view, a sky hole or the sky portal.
bool ComputeWalkBitsNeeded(int stackViews, int skyHoles, bool skyboxWalked, bool mainWalked = false);

// [rc4l] GL's view area (FGLRenderer::SetViewArea), which every height transfer's planes follow: below at or under the view sector's fake floor, above over its fake ceiling unless it transfers the floor alone, else normal, which is also the area with no transfer to read.
enum { kViewAreaNormal = 0, kViewAreaBelow = 1, kViewAreaAbove = 2 };
int ComputeViewArea(bool hasTransfer, float viewz, float fakeFloor, float fakeCeiling, bool floorOnly);

// [rc4l] One sky hole as the scene shader reads it: the wall's ends (x1, y1, x2, y2), then floor and ceiling at each end (zb1, zt1, zb2, zt2), in map units; at most kMaxSkyHoles a frame.
enum { kSkyHoleFloats = 8, kMaxSkyHoles = 256 };
void ComputeSkyHoleRecord(float x1, float y1, float x2, float y2, const float *zfloor, const float *zceil, float *out);

// [rc4l] Whether a sky hole the walk filling list met goes to the scene shader, holding held already: only the player's walk's, while there is room, and any other is painted as a sky quad.
bool ComputeSkyHoleToShader(int list, int held);

// [rc4l] Whether the eye's line to p crosses a hole's span before reaching p, both in mesh space (map x, height, map y) as the scene shader holds them, which then drops p unless GL's walk drew its piece.
bool ComputeBehindSkyHole(const float *eye, const float *p, const float *hole);

// [rc4l] One corner (0..5, two triangles) of a stacked view's cap, the plane at the map's height limit GLPortal::DrawPortalStencil closes a stack's stencil with: overhead for a ceiling stack, underfoot for a floor one.
void ComputeStackCapCorner(bool ceiling, int corner, float &x, float &y, float &z);

// [rc4l] Whether GL draws a two-sided middle in its translucent pass (GLWall::DoMidTexture, PutWall): a linedef alpha below opaque or additive, or a texture with see-through texels; an alpha of 0 skips the linedef's part.
bool ComputeMidTextureTranslucent(float lineAlpha, bool additive, bool textureTransparent);

// [rc4l] How opaque GLFlat::ProcessSector draws a sector plane: a stacked one by its own alpha (0 for a stack seen straight through, which is not drawn), any other by what its reflection leaves.
float ComputePlaneAlpha(bool stacked, float stackAlpha, float reflect);

// [rc4l] Whether ProcessSector lights a sector's own floor or ceiling with the 3D floor band over it rather than the plane's own light: never its own band, and an absolute plane light gives way only on a floor, to a band no sector casts.
bool ComputePlaneTakesBandLight(bool ceiling, bool absolute, bool bandFromSector, bool ownBand);

// [rc4l] The sector a subsector's flats are drawn with: its render sector (-1 for none yet), which GL takes from the first boundary seg so a self-referencing trick draws its surroundings, else the nodebuilder's.
int ComputeFlatSector(int renderSector, int sector);

// [rc4l] Whether a flat built from model (-1 for none) is its subsector's own plane rather than a 3D floor's: its sector or the render sector the bake draws it with.
bool ComputeIsOwnFlat(int model, int sector, int renderSector);

// [rc4l] The squared distance from a point to a segment, for finding the seg a wall piece lies on.
float ComputeSegmentDistance2(float px, float py, float x1, float y1, float x2, float y2);

// [rc4l] A point inside the subsector a piece lies in: its centre, stepped a unit off a wall along its normal (mesh space, y up) so it lands on the wall's own side; true for a wall, which in Doom is always upright.
bool ComputePieceProbe(float centreX, float centreY, float normX, float normUp, float normY, float &probeX, float &probeY);

// [rc4l] The mean position of vertices [offset, offset + count) of a mesh holding total, for placing or sorting a piece; false, writing nothing, when that run is empty or overruns.
template <typename V>
bool ComputeCentroid(const V *verts, unsigned total, unsigned offset, unsigned count, float &cx, float &cy, float &cz)
{
	if (verts == nullptr || count == 0 || offset > total || count > total - offset) return false;
	cx = cy = cz = 0.f;
	for (unsigned i = 0; i < count; i++) { cx += verts[offset + i].x; cy += verts[offset + i].y; cz += verts[offset + i].z; }
	cx /= (float)count; cy /= (float)count; cz /= (float)count;
	return true;
}

// [rc4l] The line mirror a sprite, model or decal GL draws now is kept for: mirrorLine while GL walks that mirror from the player's view (nesting 2), else -1.
int ComputeMirrorCaptureLine(int mirrorLine, bool mainView, int sceneNesting);

// [rc4l] Whether a pass draws a sprite, model or decal: only the pass whose walk took it, a line mirror's or a stacked view's (each -1 for the player's view), as GL draws each from its own walk.
bool ComputeDrawsDynamicPiece(int pieceMirrorLine, int pieceStackSlot, int drawingMirrorLine, int drawingStackSlot);

// [rc4l] Whether the backend renders a line mirror's reflection this frame: when GL's walk went through it, or in standalone, where nothing walks, when its quad is on screen.
bool ComputeRendersMirror(bool walkedByGL, bool standalone, bool onScreen);

// [rc4l] Whether a sprite, model or decal being captured belongs to the frame: the player's view from the outermost DrawScene (nesting 1, or 0 outside any), since GL raises the portal depth for the player's own scene once its walk is done.
bool ComputeOwnsFrameCapture(bool mainView, int sceneNesting);

// [rc4l] What a stored flat was built from, compared field by field: its plane, placement and texture shape its vertices, the rest only shades the piece, and dynLightIndex stays out because nothing reads it and it changes most frames.
struct FlatStamp
{
	long long pa, pb, pc, pd;
	float dz;
	long long xoffs, yoffs, xscale, yscale;
	unsigned int angle;
	const void *material;
	const void *baseTex;
	int lightLevel, extraLight;
	unsigned int lightColor, fadeColor;
	int desaturation, blendFactor;
	long long alpha;
	int renderStyle;
};

// [rc4l] What a stored flat needs.
enum FlatChange { kFlatSame, kFlatReshade, kFlatRescroll, kFlatRebuild };

// [rc4l] Nothing, a new shade, a scroll from the offsets it was baked with (also to undo one), or its vertices built again; one never stored is built, since AOW03's lights and scrollers rebuilt every floor they touched.
FlatChange ComputeFlatChange(bool stored, const FlatStamp &baked, const FlatStamp &now, bool scrolled);

}} // namespace zx::levelmesh

#endif // ZX_FLATMESH_COMPUTE_H
