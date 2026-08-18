// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/computation/wallbatch_compute.h"
#include "features/levelmesh/computation/flatmesh_compute.h"

#include "r_defs.h"
#include "r_state.h"
#include "gl/scene/gl_wall.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/data/gl_data.h"   // getExtraLight
#include "gl/textures/gl_material.h"   // FMaterial::TextureWidth, for the plane UV transform
#include "gl/utility/gl_convert.h"      // ANGLE_TO_FLOAT
#include "tarray.h"

// [rc4l] Print every new flat-mesh key. See RegisterFlatSubsector: a key that misses when it should
// have hit is a surface about to be stored, and drawn, twice.
CVAR(Bool, fua_flat_keylog, false, 0)

namespace zx { namespace levelmesh {

// [rc4l] Keyed on (subsector, plane) so a subsector's floor and ceiling are each baked once. The
// range is kept so a re-bake at the same size overwrites in place, exactly like the wall path --
// which is what stops a moving sector from growing the buffer forever.
struct FlatKey
{
	const subsector_t *sub;
	bool ceiling;
	// [rc4l] The plane's own sector, which for a 3D floor is its MODEL sector.
	//
	// Keying on (subsector, ceiling) alone gives one slot per subsector per side, and a sector with
	// 3D floors draws several planes through the same subsector -- its own floor plus a top and a
	// bottom for every 3D floor above it. They all landed in that one slot and overwrote each other,
	// so only whichever drew last survived. dbab01 has 138 such sectors and 276 3D-floor planes.
	//
	// The model sector is the right discriminator rather than the plane's height: a 3D floor used as
	// a lift changes height every tic, and keying on that would allocate a fresh range per frame and
	// run the arena away.
	const sector_t *model;
	// Which plane OF that model sector: a 3D floor's top and bottom share the model, so the model
	// alone still collides and the surface flickers between the two textures.
	int whichPlane;
	MeshRange range;
};
static TArray<FlatKey> g_flats;

// [rc4l] How many registrations are 3D-floor planes rather than a subsector's own floor or ceiling.
// Zero would mean the planes never reach the capture at all, which is a different problem from them
// arriving and overwriting each other -- and the two look identical in a screenshot.
static int g_flat3D = 0, g_flatOwn = 0;
static int g_decalsThisFrame = 0;
int DecalsThisFrame() { return g_decalsThisFrame; }
void ResetDecalCount() { g_decalsThisFrame = 0; }
void GetFlatStats(int &own, int &threeD) { own = g_flatOwn; threeD = g_flat3D; }

void ClearFlats()
{
	g_flat3D = g_flatOwn = 0;

	// [rc4l] Give the ranges back before forgetting the keys that own them.
	//
	// Clearing the key table alone forgets WHERE each flat lives without telling the mesh, so the
	// next registration misses its key, MeshStore bump-allocates a fresh range, and
	// MeshRegisterPiece -- which dedupes by range OFFSET -- appends a second piece instead of
	// replacing the first. The old piece is still in the list and still points at valid geometry, so
	// every re-registered flat ends up drawn twice, coplanar with itself.
	//
	// Two identical copies are invisible: same texture, same light, same depth. They appear the
	// moment the copies are shaded DIFFERENTLY -- which is what a dynamic light does -- and then the
	// pair fights for the depth buffer in bands following the contours of constant depth. On a floor
	// seen from above those contours are horizontal, which is why it was reported as horizontal
	// stripes across a lit floor, and only ever under a light.
	//
	// InvalidateAll calls this and a gl_wallmesh toggle calls InvalidateAll, so the world's flats
	// were duplicated on every toggle -- including every time a test harness set gl_wallmesh 1.
	for (unsigned i = 0; i < g_flats.Size(); i++) MeshRetireRange(g_flats[i].range);
	g_flats.Clear();
}

int FlatPieceCount() { return (int)g_flats.Size(); }

void RegisterFlatSubsector(const GLFlat &flat, subsector_t *sub, bool ceiling)
{
	if (sub == NULL || sub->numlines < 3) return;
	if (flat.gltexture == NULL) return;
	if (sub->sector != NULL && flat.mMeshModel != NULL && flat.mMeshModel != sub->sector) g_flat3D++;
	else g_flatOwn++;

	// [rc4l] Cap matches the wall path's staging array; a subsector with more edges than this is
	// vanishingly rare and is simply left to the GL renderer.
	static FFlatVertex fan[GLWall::MAX_BATCH_FAN_VERTICES];
	static FFlatVertex tris[GLWall::MAX_BATCH_FAN_VERTICES * 3];
	const int n = (int)sub->numlines;
	if (n > GLWall::MAX_BATCH_FAN_VERTICES) return;

	// Mirrors GLFlat::DrawSubsector for the positions, and gl_SetPlaneTextureRotation for the texture
	// coordinates -- which this used to skip entirely, baking the bare (x/64, -y/64) that is only
	// right for an unoffset, unscaled, unrotated 64x64 flat.
	const secplane_t &p = flat.plane.plane;
	const bool sloped = (p.a | p.b) != 0;
	const float zc = sloped ? 0.0f : (FIXED2FLOAT(p.Zat0()) + flat.dz);

	PlaneUVTransform uvt = ComputeIdentityPlaneUV();
	uvt.xoffs = FIXED2FLOAT(flat.plane.xoffs);
	uvt.yoffs = FIXED2FLOAT(flat.plane.yoffs);
	uvt.xscale = FIXED2FLOAT(flat.plane.xscale);
	uvt.yscale = FIXED2FLOAT(flat.plane.yscale);
	uvt.angleDegrees = ANGLE_TO_FLOAT(flat.plane.angle);
	uvt.texWidth = (float)flat.gltexture->TextureWidth();
	uvt.texHeight = (float)flat.gltexture->TextureHeight();
	uvt.hasCanvas = flat.gltexture->tex->bHasCanvas;

	for (int k = 0; k < n; k++)
	{
		vertex_t *vt = sub->firstline[k].v1;
		fan[k].x = vt->fx;
		fan[k].y = vt->fy;
		fan[k].z = sloped ? (float)(p.ZatPoint(vt->fx, vt->fy) + flat.dz) : zc;
		ComputePlaneUV(vt->fx, vt->fy, uvt, fan[k].u, fan[k].v);
	}

	const int triVerts = ComputeFanTriangleVertexCount(n);
	if (triVerts <= 0) return;
	// [rc4l] Wind a surface seen from below the other way round, so back-face culling keeps it.
	//
	// A subsector's vertices come in one fixed order, so a floor and a ceiling built from them have
	// the SAME winding while facing opposite directions -- and a single cull mode then deletes one of
	// them. Enabling culling for the world removed every ceiling in the level.
	//
	// The discriminator is `ceiling`, which is GLFlat's record of which SIDE the surface is being
	// viewed from, and NOT the plane's normal. Using the normal looked more principled and was wrong:
	// a 3D floor's walkable top surface is the control sector's CEILING plane, so its normal points
	// down while the surface is seen from above, and the normal rule culled exactly those. See
	// GLFlat::ProcessSector, which sets ceiling=true for the pass that draws 3D floor faces from
	// below and ceiling=false for the pass that draws them from above.
	const bool facesDown = ComputeFlatWindingReversed(ceiling);
	int w = 0;
	for (int t = 0; t < n - 2; t++)
		for (int c = 0; c < 3; c++)
		{
			const int cc = facesDown ? (2 - c) : c;
			tris[w++] = fan[ComputeFanTriangleVertex(n, t, cc)];
		}

	FlatKey *slot = NULL;
	for (unsigned i = 0; i < g_flats.Size(); i++)
		if (g_flats[i].sub == sub && g_flats[i].ceiling == ceiling &&
			g_flats[i].model == flat.mMeshModel && g_flats[i].whichPlane == flat.mMeshWhichPlane)
		{ slot = &g_flats[i]; break; }
	if (slot == NULL)
	{
		FlatKey k;
		k.sub = sub;
		k.ceiling = ceiling;
		k.model = flat.mMeshModel;
		k.whichPlane = flat.mMeshWhichPlane;
		k.range.offset = 0;
		k.range.count = 0;
		slot = &g_flats[g_flats.Push(k)];
		// [rc4l] Say when a NEW flat slot is created, and with what key.
		//
		// A slot that should have been found and was not is a duplicate surface: the same floor stored
		// twice under two keys, drawn twice, fighting for the depth buffer. The key is four fields and
		// the count says nothing about which of them disagreed, so it is printed.
		if ( fua_flat_keylog )
		{
			Printf( "flatkey NEW: sub %d  ceiling %d  model %d  whichPlane %d  (slots %u)\n",
				sub ? (int)(sub - subsectors) : -1, (int)ceiling,
				flat.mMeshModel ? (int)(flat.mMeshModel - sectors) : -1,
				flat.mMeshWhichPlane, g_flats.Size() );
		}
	}

	if (!MeshStore(slot->range, tris, w)) return;

	MeshPiece mp;
	mp.range = slot->range;
	mp.material = flat.gltexture;
	mp.dynLightIndex = flat.mMeshLightIndex;
	// [rc4l] Plane normal, mapped into the mesh's (x, z-up, y) space: a secplane is
	// a*x + b*y + c*z + d = 0 with z up, so (a, b, c) becomes (a, c, b) here.
	//
	// Then turned to face the side the surface is SEEN from, which is not always the way its plane
	// points. A 3D floor's underside is the control sector's floor plane, so its normal points up
	// while the surface is looked at from below -- and the backend's dynamic-light side test then
	// finds every light in the room behind it and lights none of them. Same trap as the winding, one
	// step further on, and quieter: a surface facing the wrong way is simply never lit, which looks
	// exactly like a light that is out of range. Measured on dbab02 as a corridor ceiling that took
	// no plasma light at all while GL lit it.
	{
		const secplane_t &pl = flat.plane.plane;
		const float nx = FIXED2FLOAT(pl.a), ny = FIXED2FLOAT(pl.b), nz = FIXED2FLOAT(pl.c);
		const float len = sqrtf(nx*nx + ny*ny + nz*nz);
		if (len > 0.0001f)
		{
			const float sign = ComputeFlatNormalFlipped(ceiling, nz / len) ? -1.f : 1.f;
			mp.normX = sign * nx / len;
			mp.normY = sign * nz / len;
			mp.normZ = sign * ny / len;
		}
	}
	mp.lightLevel = flat.lightlevel;
	mp.lightColor = flat.Colormap.LightColor.d;
	mp.fadeColor = flat.Colormap.FadeColor.d;
	// GLFlat::Draw's GLPASS_PLAIN arm, with its `rel = getExtraLight()`.
	CaptureShading(flat.lightlevel, getExtraLight(), flat.Colormap, mp);

	// [rc4l] Flats are NOT all opaque, and 3D floors are where that shows.
	//
	// CaptureShading fills in alpha 1 and blend mode 0 because a sector's own floor always is one.
	// A 3D floor is frequently not: dbab01 hangs a translucent metal grate over a lava pit, and
	// baking it opaque drew the grate as solid lava-lit metal or let the lava beneath win outright.
	// The same classification the sprite path uses, for the same reason.
	mp.alpha = flat.alpha;
	// Which side this surface is viewed from, kept so fua_mesh_verify can check the winding above
	// rather than take it on trust. See MeshPiece::facesDown.
	mp.facesDown = facesDown;
	// renderstyle here is an ERenderStyle enum, not an FRenderStyle, so it is compared not inspected.
	mp.blendMode = ComputeSurfaceBlendMode(flat.renderstyle == STYLE_Add, flat.alpha);

	// [rc4l] Base plane texture, so animated flats (nukage, lava, blood) keep flowing.
	//
	// Straight off the plane the engine resolved, NOT re-derived from a sector and a plane index.
	// Deriving it took two wrong answers in a row: first the containing sector's flat, so every 3D
	// floor plane recorded the texture of the floor it hangs over and got repainted with it; then the
	// model sector's, which is wrong for a 3D floor because F3DFloor::top and ::bottom are planerefs
	// that carry their OWN texture and can name a different sector than the rover's model. On dbab02
	// that resolved to the null texture, whose id translates to itself, so the lava under the 3D
	// floor rendered correctly and then never animated again -- while the strip of the same floor
	// outside the 3D floor's footprint animated fine, because that piece took the simple path.
	//
	// plane.texture is what GLFlat::Process itself fed to ValidateTexture, so by construction it is
	// the base id of the texture actually being drawn, in every case, with no cases to enumerate.
	mp.baseTex = TexMan[flat.plane.texture];
	MeshRegisterPiece(mp);
}

// ---------------------------------------------------------------------------
// Sprites
// ---------------------------------------------------------------------------

// [rc4l] Sprites go into the DYNAMIC stream, rebuilt every frame -- see staticmesh.h. They are
// billboards built for one viewpoint, so they are not level geometry and must never be baked.
static int g_spritesThisFrame = 0;

// [rc4l] What each sprite piece was registered WITH, for one frame, so a seam can be read off
// numbers instead of guessed at from a screenshot.
//
// A sprite is not always one quad. GLSprite::SplitSprite cuts it wherever a 3D floor's light band
// starts, gives each piece that band's light and colormap, and draws them as separate sprites. When
// two pieces of one explosion come out different in the backend and identical in GL, the question is
// which of those inputs diverged -- and every candidate (light, colour, alpha, blend, the texture
// coordinates of the cut) is a number that is already in hand right here.
struct SpriteNote
{
	float z1, z2, vt, vb;
	int   light;
	unsigned int lightColor, fadeColor;
	float alpha, r, g, b;
	int   blend;
	const void *material;
};
static const int kMaxSpriteNotes = 64;
static SpriteNote g_spriteNotes[kMaxSpriteNotes];
static int g_spriteNoteCount = 0;
// [rc4l] What render styles sprites actually arrive with, counted rather than assumed.
//
// Plasma impacts came out with black holes where their bright cores should be, which is what an
// ADDITIVE or SUBTRACTIVE sprite looks like when it is drawn with ordinary alpha blending: its dark
// texels paint dark instead of adding nothing. Guessing which styles a mod uses is how the last
// four of these went; this counts them.
static int g_styleOps[16];
static int g_styleFlagsSeen = 0;
static int g_styleDest[16];
static int g_classified[4];
void GetSpriteStyleStats(int *ops, int &flags)
{
	for (int i = 0; i < 16; i++) ops[i] = g_styleOps[i];
	flags = g_styleFlagsSeen;
}

void GetSpriteStyleDetail(int *dest16, int *classified4)
{
	for (int i = 0; i < 16; i++) dest16[i] = g_styleDest[i];
	for (int i = 0; i < 4; i++) classified4[i] = g_classified[i];
}

void ClearSprites() { g_spritesThisFrame = 0; g_spriteNoteCount = 0; }
int SpritePieceCount() { return g_spritesThisFrame; }

void RegisterSprite(const GLSprite &spr)
{
	if (spr.gltexture == NULL) return;

	// [rc4l] GLSprite emits a 4-vertex TRIANGLE STRIP; the mesh is triangle lists, so expand to
	// (0,1,2) and (2,1,3) -- which preserves the strip's winding rather than flipping the second
	// triangle.
	FFlatVertex q[4];
	q[0].Set(spr.x1, spr.z1, spr.y1, spr.ul, spr.vt);
	q[1].Set(spr.x2, spr.z1, spr.y2, spr.ur, spr.vt);
	q[2].Set(spr.x1, spr.z2, spr.y1, spr.ul, spr.vb);
	q[3].Set(spr.x2, spr.z2, spr.y2, spr.ur, spr.vb);

	FFlatVertex tris[6];
	tris[0] = q[0]; tris[1] = q[1]; tris[2] = q[2];
	tris[3] = q[2]; tris[4] = q[1]; tris[5] = q[3];

	MeshPiece mp;
	mp.range.offset = 0;
	mp.range.count = 0;
	mp.material = spr.gltexture;
	// [rc4l] GLSprite has no light index -- sprite lighting is folded into its vertex colour by
	// gl_SetDynSpriteLight before the draw, so it is already in colorR/G/B.
	mp.dynLightIndex = -1;
	mp.lightLevel = spr.lightlevel;
	mp.lightColor = spr.Colormap.LightColor.d;
	mp.fadeColor = spr.Colormap.FadeColor.d;
	// GLSprite::Draw's own `rel`: fullbright sprites take no extra light.
	CaptureShading(spr.lightlevel, spr.fullbright ? 0 : getExtraLight(), spr.Colormap, mp);

	// [rc4l] Classify the render style into the handful of blends a backend actually needs.
	//
	// gl_GetRenderStyle resolves the full matrix into GL blend enums, which would then have to be
	// mapped back into Diligent's. These four cases cover what Doom content uses: opaque/masked,
	// normal translucency, additive (plasma, fireballs, explosions) and the fuzz shadow. Anything
	// exotic falls into normal translucency, which is wrong but visible rather than invisible.
	{
		const int op = (int)spr.RenderStyle.BlendOp;
		if (op >= 0 && op < 16) g_styleOps[op]++;
		const int da = (int)spr.RenderStyle.DestAlpha;
		if (da >= 0 && da < 16) g_styleDest[da]++;
		g_styleFlagsSeen |= (int)spr.RenderStyle.Flags;
	}
	// [rc4l] A billboard has NO side, so it gets no normal.
	//
	// CaptureShading leaves a default of straight up, which is right for a flat and meaningless for a
	// sprite: a quad that turns to face the camera has no fixed facing for a light to be in front of
	// or behind. The backend's dynamic-light side test took the default at its word and lit only the
	// fragments below the light, cutting the rest off along a dead straight horizontal line at the
	// light's own height -- a hard seam across the middle of every rocket explosion, which carries a
	// large light at its centre. Zero means "no side", and the test is skipped.
	mp.normX = mp.normY = mp.normZ = 0.f;
	mp.translation = spr.translation;
	mp.alpha = spr.trans;
	if (spr.RenderStyle.BlendOp == STYLEOP_Shadow)
		mp.blendMode = 3;
	else if (spr.RenderStyle.BlendOp == STYLEOP_Add && spr.RenderStyle.DestAlpha == STYLEALPHA_One)
		mp.blendMode = 2;
	else if (spr.trans < 1.f - 1.f/256.f || spr.RenderStyle.BlendOp != STYLEOP_Add)
		mp.blendMode = (spr.trans < 1.f - 1.f/256.f) ? 1 : 0;
	else
		mp.blendMode = 0;

	if (mp.blendMode >= 0 && mp.blendMode < 4) g_classified[mp.blendMode]++;
	// Sprite centre, for the back-to-front sort.
	mp.sortX = spr.x; mp.sortY = spr.y; mp.sortZ = spr.z;

	DynAppend(tris, 6, mp);
	g_spritesThisFrame++;

	if (g_spriteNoteCount < kMaxSpriteNotes)
	{
		SpriteNote &n = g_spriteNotes[g_spriteNoteCount++];
		n.z1 = spr.z1; n.z2 = spr.z2; n.vt = spr.vt; n.vb = spr.vb;
		n.light = spr.lightlevel;
		n.lightColor = spr.Colormap.LightColor.d;
		n.fadeColor = spr.Colormap.FadeColor.d;
		n.alpha = mp.alpha;
		n.r = mp.colorR; n.g = mp.colorG; n.b = mp.colorB;
		n.blend = mp.blendMode;
		n.material = spr.gltexture;
	}
}

// [rc4l] fua_sprites: this frame's sprite pieces, in the order they were registered.
//
// Pause on the frame in question first -- the list is rebuilt every frame, and an explosion lasts
// about a quarter of a second.
void DumpSpriteNotes()
{
	Printf("fua_sprites: %d piece(s) this frame (%d recorded)\n",
		g_spritesThisFrame, g_spriteNoteCount);
	for (int i = 0; i < g_spriteNoteCount; i++)
	{
		const SpriteNote &n = g_spriteNotes[i];
		Printf("  %2d  z %8.2f..%8.2f  v %.4f..%.4f  light %3d  lit %.3f,%.3f,%.3f\n"
		       "      alpha %.3f  blend %d  lightcol %06x  fade %06x  mat %p\n",
			i, n.z1, n.z2, n.vt, n.vb, n.light, n.r, n.g, n.b,
			n.alpha, n.blend, n.lightColor & 0xffffff, n.fadeColor & 0xffffff, n.material);
	}
}

void RegisterDecal(const FFlatVertex *quad, const void *material, int translation,
                   bool shadow, bool additive, float alpha,
                   int lightlevel, int rel, const FColormap &colormap,
                   bool redToAlpha, unsigned int alphaColor,
                   float sortX, float sortY, float sortZ, const float *dynLight)
{
	if (quad == NULL) return;

	// GL draws the decal as a 4-vertex TRIANGLE FAN, so the triangles are (0,1,2) and (0,2,3) --
	// not the strip order the sprite path uses, which would fold the quad in half.
	FFlatVertex tris[6];
	tris[0] = quad[0]; tris[1] = quad[1]; tris[2] = quad[2];
	tris[3] = quad[0]; tris[4] = quad[2]; tris[5] = quad[3];

	RegisterDecalTriangles(tris, 6, material, translation, shadow, additive, alpha,
		lightlevel, rel, colormap, redToAlpha, alphaColor, sortX, sortY, sortZ, dynLight);
}

// [rc4l] The same piece, for a mark that is not a quad.
//
// A projected decal is whatever geometry fell inside its box, clipped -- three vertices or thirty,
// spread over a wall and the floor under it. Everything below the vertex list is identical to the
// glued quad's, and it stayed identical by being one function rather than two that drift.
void RegisterDecalTriangles(const FFlatVertex *tris, int count, const void *material, int translation,
                            bool shadow, bool additive, float alpha,
                            int lightlevel, int rel, const FColormap &colormap,
                            bool redToAlpha, unsigned int alphaColor,
                            float sortX, float sortY, float sortZ, const float *dynLight)
{
	if (tris == NULL || count < 3 || material == NULL) return;

	MeshPiece mp;
	mp.range.offset = 0;
	mp.range.count = 0;
	mp.material = material;
	// [rc4l] A decal takes its dynamic light from the CPU, and its NORMAL is what says so.
	//
	// gl_SetDynSpriteLight computes the light for this decal and RegisterDecal folds it into the
	// vertex colour below, exactly as for a sprite. The shader must therefore not add the lights a
	// second time, and what tells it not to is a zero normal -- MeshPiece's default, deliberately
	// left alone here rather than filled in from the wall the decal is stuck to.
	//
	// That is an easy thing to undo by accident, so it is written down: giving a decal a real normal
	// would look like an improvement and would silently restore the double count. Measured on an
	// unshaded decal (PlasmaScorch, the only kind that can show it) under a blue torch, the blue a
	// dynamic light added was GL +12.4 against the backend's +19.7 -- 59% too much -- and +14.3
	// once the lights were left to the CPU.
	mp.dynLightIndex = -1;
	mp.lightLevel = lightlevel;
	mp.lightColor = colormap.LightColor.d;
	mp.fadeColor = colormap.FadeColor.d;
	CaptureShading(lightlevel, rel, colormap, mp);

	mp.translation = translation;
	mp.alpha = alpha;
	mp.blendMode = ComputeStyleBlendMode(shadow, additive, alpha);
	// [rc4l] Coplanar with the wall it is glued to, so it needs the depth bias GL gets from
	// glPolygonOffset. See MeshPiece::depthBias.
	mp.depthBias = true;
	// [rc4l] A shaded decal's texture is an alpha mask and its colour is its own AlphaColor, so the
	// colour is folded into the vertex light here and the shader shades a white texel. GL does the
	// same thing with SetObjectColor plus TM_REDTOALPHA.
	// [rc4l] The dynamic light the SHADER would have added, folded in BEFORE the decal's own colour.
	//
	// gl_SetDynSpriteLight hands a decal's dynamic light to GL as a uniform, so a capture that takes
	// the vertex colour alone arrives unlit -- the same hole the HUD weapon had, where a muzzle flash
	// lit the room and left the weapon dark.
	//
	// Before, not after, because that is where GL puts it. main.fp computes
	// (texel * uObjectColor) * (vertexLight + dynLight): the decal's own colour multiplies the whole
	// lit result, dynamic light included. Adding it afterwards instead makes the decal colour scale
	// only the sector light, and a SCORCH MARK is the case that exposes the difference -- its colour
	// is black, so the multiply gives zero and the added light is then the entire fragment. Every
	// burn mark in the room lit up in the colour of whatever light reached it. Multiplied instead,
	// black stays black however bright the room gets, which is what GL draws.
	if (dynLight != NULL)
	{
		mp.colorR = (mp.colorR + dynLight[0] > 1.f) ? 1.f : mp.colorR + dynLight[0];
		mp.colorG = (mp.colorG + dynLight[1] > 1.f) ? 1.f : mp.colorG + dynLight[1];
		mp.colorB = (mp.colorB + dynLight[2] > 1.f) ? 1.f : mp.colorB + dynLight[2];
	}

	mp.redToAlpha = redToAlpha;
	if (redToAlpha)
	{
		mp.colorR *= ((alphaColor >> 16) & 0xff) / 255.f;
		mp.colorG *= ((alphaColor >> 8) & 0xff) / 255.f;
		mp.colorB *= (alphaColor & 0xff) / 255.f;
		// [rc4l] An alpha-mask decal ALWAYS blends: its coverage lives in the texture's red channel,
		// so there is no such thing as an opaque one. Classified by alpha alone an unfaded decal
		// comes out as blend mode 0, takes the opaque pipeline, and is sampled as a colour image --
		// which paints a bright blob where GL paints a dark burn. Only the faded ones looked right,
		// so it read as "decals glow" rather than "the wrong shader".
		if (mp.blendMode == 0) mp.blendMode = 1;
	}
	// [rc4l] An ADDITIVE decal is not fogged, because GL does not fog one either.
	//
	// GLWall::DrawDecal forces the fog colour to black for the duration of an additive draw and puts
	// it back afterwards. Additive blending only ever brightens, so fogging one toward the sector's
	// fade colour tints light that should simply get weaker with distance: in a fogged room a BFG's
	// glow came out carrying the fog's colour in Vulkan and clean in GL.
	if (additive) mp.fadeColor = 0;

	mp.sortX = sortX; mp.sortY = sortY; mp.sortZ = sortZ;

	DynAppend(tris, count, mp);
	g_decalsThisFrame++;
}

}} // namespace zx::levelmesh
