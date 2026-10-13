// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/modelmesh.h"
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/captureshading.h"   // [rc4l] CaptureShading, the light model for a GL colormap
#include "features/levelmesh/flatmesh.h"   // [rc4l] DynamicCaptureActive, StackCaptureSlot, MirrorCaptureLine

#include "features/models/modelgeometry.h"
#include "features/rendersettings/rendersettings.h"   // [rc4l] the render settings it reads
#include "features/hwrender/hud2d.h"   // StandaloneActive, BackendPresenting
#include "features/sprites/spritebuild.h"   // [rc4l] NoteGLSprite and NoteDerivedSprite, for fua_sprite_check
#include "tarray.h"
#include "c_dispatch.h"
#include "c_cvars.h"

// [rc4l] Off switch for the whole capture, so a model fault can be told apart from a mesh fault
// without a rebuild -- the same role fua_dg_skyportal plays for the sky.
CVAR(Bool, fua_mesh_models, true, CVAR_ARCHIVE)

namespace zx { namespace levelmesh {

namespace {

// [rc4l] The model being captured: its look, its matrix, its actor, and whether it was derived and only checked.
MeshPiece g_look;
const float *g_matrix = NULL;
const void *g_who = NULL;
ModelCaptureKind g_kind = kModelDraw;

int g_models = 0, g_surfaces = 0, g_triangles = 0, g_skipped = 0;
// [rc4l] Every model GL started to draw, captured or not, and the walk the last one was in, so "not drawn" and "not ours" read apart.
int g_begins = 0;
bool g_lastMainView = false;

// [rc4l] Where the triangles LANDED. A count proves the capture ran and says nothing about whether
// what it produced is in front of the camera -- and from outside, those two failures look the same.
float g_bbMin[3] = { 0, 0, 0 }, g_bbMax[3] = { 0, 0, 0 };
float g_lastSort[3] = { 0, 0, 0 };
// The piece as handed over, so "invisible" can be told from "drawn black" or "drawn at alpha zero".
float g_lastAlpha = 0.f, g_lastRGB[3] = { 0, 0, 0 };
int g_lastBlend = -1, g_lastLight = -1;
const void *g_lastMaterial = NULL;
bool g_haveBB = false;

// Scratch, kept across surfaces: a model surface is a few hundred triangles and this function runs
// per surface per model per frame, so the allocation is worth not repeating.
TArray<MeshVertex> g_tris;

// The model matrix, applied to one vertex. VSMatrix is column-major, as GL wants it, so a column is
// four consecutive floats and the translation lives in the last one.
inline void Transform(const float *m, float x, float y, float z, float *out)
{
	out[0] = m[0] * x + m[4] * y + m[8]  * z + m[12];
	out[1] = m[1] * x + m[5] * y + m[9]  * z + m[13];
	out[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
}

} // namespace

void ModelPieceLook(int lightLevel, int extraLight, const FColormap &cm, bool fullbright, const float dyn[3], unsigned thingColor,
	float alpha, int blendMode, float x, float y, float z, MeshPiece &mp)
{
	mp = MeshPiece();
	mp.range.offset = 0;
	mp.range.count = 0;
	mp.dynLightIndex = -1;
	mp.lightLevel = lightLevel;
	mp.lightColor = cm.LightColor.d;
	mp.fadeColor = cm.FadeColor.d;
	CaptureShading(lightLevel, extraLight, cm, mp);
	// [rc4l] GL's shader multiplies a model by the actor's ThingColor as it does a sprite.
	const PalEntry color(thingColor);
	mp.colorR *= color.r / 255.f;
	mp.colorG *= color.g / 255.f;
	mp.colorB *= color.b / 255.f;
	// [rc4l] The light is the CPU's, exactly as it is for a sprite: gl_SetDynSpriteLight has already
	// run for this actor and left its answer in the render state, having applied gl_light_sprites and
	// DONTLIGHTSELF. A shader loop over the level's lights knows neither.
	if (!fullbright) { mp.dynR = dyn[0]; mp.dynG = dyn[1]; mp.dynB = dyn[2]; }
	mp.cpuLight = true;   // [rc4l] see RegisterSprite
	// [rc4l] No normal, and this one is a real limitation rather than a shortcut for its own sake.
	// FModelVertex::SetNormal is a no-op in this tree -- the loaders read normals and throw them
	// away, because GL's model shader never wanted them -- so there is nothing to capture. Zero
	// means "no side" and the backend skips its dynamic-light facing test, which is the same
	// treatment a billboard gets. A model lit from behind is therefore lit all over.
	mp.normX = mp.normY = mp.normZ = 0.f;
	mp.alpha = alpha;
	mp.blendMode = blendMode;
	// The actor's own position, so the back-to-front sort puts a translucent model where the actor
	// is rather than where its first triangle happens to be.
	mp.sortX = x; mp.sortY = y; mp.sortZ = z;
	mp.quad = false;
	// [rc4l] Solid, which is what separates a model from every other dynamic piece: it is a closed
	// shell, so its near panels must hide its far ones. Left in the sorted depth-write-off pass
	// with the sprites, a car draws its own interior floor and seats through the bodywork.
	mp.solid = true;
}

void ModelCaptureBegin(const MeshPiece &look, const float *modelToWorld, const void *who, ModelCaptureKind kind)
{
	g_look = look;
	g_matrix = modelToWorld;
	g_who = who;
	g_kind = kind;
	g_begins++;
	g_lastMainView = MainView();
	if (ModelCaptureActive()) g_models++;
}

void ModelCaptureEnd()
{
	g_matrix = NULL;
	g_who = NULL;
}

bool ModelCaptureActive()
{
	// [rc4l] NOT gated on the backend presenting, and that is deliberate -- RegisterSprite is not
	// either. BackendPresenting means the Diligent window is covering GL's, which is false in a
	// side-by-side session where BOTH windows are live: gating on it left the Vulkan window with no
	// models in exactly the configuration used to compare the two renderers. The mesh is what a
	// backend draws from, so it is filled whenever there is something to put in it.
	// [rc4l] ...and only from a walk the backend draws again, each kept for its own pass, since wdiexsec's mirror walks once wiped a barrel the player's had already drawn.
	return fua_mesh_models && g_matrix != NULL && DynamicCaptureActive();
}

void ModelStatsReset() { g_models = g_surfaces = g_triangles = g_skipped = g_begins = 0; g_haveBB = false; }
static void ModelBeginsGet(int &begins, bool &mainView) { begins = g_begins; mainView = g_lastMainView; }

void ModelStatsGet(int &models, int &surfaces, int &triangles, int &skipped)
{
	models = g_models; surfaces = g_surfaces; triangles = g_triangles; skipped = g_skipped;
}

void ModelBoundsGet(float *mn, float *mx, float *lastSort)
{
	for (int c = 0; c < 3; c++) { mn[c] = g_bbMin[c]; mx[c] = g_bbMax[c]; lastSort[c] = g_lastSort[c]; }
}

void ModelPieceGet(float &alpha, int &blend, int &light, float *rgb, const void *&material)
{
	alpha = g_lastAlpha; blend = g_lastBlend; light = g_lastLight; material = g_lastMaterial;
	for (int c = 0; c < 3; c++) rgb[c] = g_lastRGB[c];
}

void CaptureModelSurface(const FModelGeometry *geometry, unsigned int base1, unsigned int base2,
	float inter, unsigned int firstIndex, unsigned int indexCount,
	const void *material, int translation)
{
	if (!ModelCaptureActive() || geometry == NULL || material == NULL) return;

	const FModelVertex *verts = geometry->verts.Size() ? &geometry->verts[0] : NULL;
	// [rc4l] No index buffer means the draw was sequential, not that there is nothing to draw. MD2
	// keeps one vertex per corner and calls glDrawArrays; MD3 and voxels are indexed. Both arrive
	// here, so the absence of indices is a numbering scheme rather than a failure.
	const unsigned int *idx = geometry->indexed && geometry->indices.Size() ? &geometry->indices[0] : NULL;
	if (verts == NULL || indexCount < 3)
	{
		g_skipped++;
		return;
	}
	// [rc4l] Bounds checked rather than trusted. These offsets come from a file on disk -- a model
	// whose surface header disagrees with its vertex count would otherwise walk off the array, and
	// the loaders do not validate them.
	if (idx != NULL && firstIndex + indexCount > geometry->indices.Size())
	{
		g_skipped++;
		return;
	}

	const float *m = g_matrix;
	const unsigned int nverts = geometry->verts.Size();
	const bool lerp = (base1 != base2) && (inter > 0.f);

	g_tris.Resize(indexCount);
	for (unsigned int i = 0; i < indexCount; i++)
	{
		const unsigned int vi = idx ? idx[firstIndex + i] : (firstIndex + i);
		if (base1 + vi >= nverts || base2 + vi >= nverts)
		{
			g_skipped++;
			return;
		}
		const FModelVertex &a = verts[base1 + vi];
		float x = a.x, y = a.y, z = a.z;
		if (lerp)
		{
			// [rc4l] The lerp GL's vertex shader does, done here. Frames are keyframes of the same
			// vertex list, so this is the same arithmetic on the same pairs -- what changes is who
			// runs it, and the bill is per visible model rather than per level vertex.
			const FModelVertex &b = verts[base2 + vi];
			x += (b.x - x) * inter;
			y += (b.y - y) * inter;
			z += (b.z - z) * inter;
		}
		float w[3];
		Transform(m, x, y, z, w);
		g_tris[i].Set(w[0], w[1], w[2], a.u, a.v);
		for (int c = 0; c < 3; c++)
		{
			if (!g_haveBB || w[c] < g_bbMin[c]) g_bbMin[c] = w[c];
			if (!g_haveBB || w[c] > g_bbMax[c]) g_bbMax[c] = w[c];
		}
		g_haveBB = true;
	}

	MeshPiece mp = g_look;
	// [rc4l] The FMaterial itself, not its FTexture. MeshPiece::material is an identity the backend
	// resolves to its own texture, and it resolves it AS an FMaterial -- handing it the inner pointer
	// type-checks, then faults inside CreateTexBuffer on the first frame that draws a model.
	mp.material = material;
	mp.mirrorLine = MirrorCaptureLine();
	mp.stackSlot = StackCaptureSlot();
	mp.translation = translation;
	g_lastSort[0] = mp.sortX; g_lastSort[1] = mp.sortY; g_lastSort[2] = mp.sortZ;
	g_lastAlpha = mp.alpha; g_lastBlend = mp.blendMode; g_lastLight = mp.lightLevel;
	g_lastRGB[0] = mp.colorR; g_lastRGB[1] = mp.colorG; g_lastRGB[2] = mp.colorB;
	g_lastMaterial = mp.material;

	// [rc4l] GL's surfaces are only compared: GL draws its frame before the backend's scene, which clears the stream they would go to.
	if (g_kind == kModelFromGL)
	{
		zx::sprites::NoteGLSprite(g_who, false, mp, &g_tris[0], (int)indexCount);
		return;
	}
	if (g_kind == kModelChecked) zx::sprites::NoteDerivedSprite(g_who, mp, &g_tris[0], (int)indexCount);
	DynAppend(&g_tris[0], indexCount, mp);
	g_surfaces++;
	g_triangles += (int)(indexCount / 3);
}

}} // namespace zx::levelmesh

CCMD(fua_models)
{
	int models = 0, surfaces = 0, triangles = 0, skipped = 0;
	zx::levelmesh::ModelStatsGet(models, surfaces, triangles, skipped);
	Printf("models: %d captured last frame, %d surfaces, %d triangles, %d surfaces skipped\n",
		models, surfaces, triangles, skipped);
	{
		float mn[3], mx[3], sort[3];
		zx::levelmesh::ModelBoundsGet(mn, mx, sort);
		Printf("  world bounds x %.0f..%.0f  up %.0f..%.0f  y %.0f..%.0f   last actor at %.0f,%.0f,%.0f\n",
			mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], sort[0], sort[1], sort[2]);
	}
	{
		float alpha = 0.f, rgb[3] = { 0, 0, 0 };
		int blend = -1, light = -1;
		const void *material = NULL;
		zx::levelmesh::ModelPieceGet(alpha, blend, light, rgb, material);
		Printf("  last piece: alpha %.3f  blend %d  light %d  rgb %.2f,%.2f,%.2f  material %s\n",
			alpha, blend, light, rgb[0], rgb[1], rgb[2], material ? "yes" : "NULL");
	}
	{
		int begins = 0;
		bool mainView = false;
		zx::levelmesh::ModelBeginsGet(begins, mainView);
		Printf("  %d model draws started since the reset, the last in %s view\n", begins, mainView ? "the player's" : "a camera's");
	}
	Printf("  capture is %s; the backend %s presenting\n", fua_mesh_models ? "on" : "off",
		zx::hwrender::BackendPresenting() ? "is" : "is not (GL's own window, or side by side)");
}
