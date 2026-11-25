// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Model geometry resident on the GPU, which the dynamic vertex shader blends and places a model surface from (modelgpu_compute): each model's keyframes and indices sent once, when a surface of it is first drawn, and dropped with the models.

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/diligent/dgshared.h"
#include "features/levelmesh/staticmesh.h"   // [Doruk-Aksoy] DynPieces, MeshPiece
#include "features/levelmesh/modelmesh.h"   // [Doruk-Aksoy] GpuModelStorage
#include "features/levelmesh/computation/modelgpu_compute.h"
#include "features/models/modelgeometry.h"
#include "features/models/models.h"   // [Doruk-Aksoy] ModelsGeneration
#include "features/telemetry/telemetry.h"   // [Doruk-Aksoy] ZX_COUNT

#include <cstddef>   // [Doruk-Aksoy] size_t
#include <unordered_map>
#include <vector>

namespace zx { namespace hwrender {

namespace {

// [Doruk-Aksoy] Every geometry held this generation, in the order its words were placed, so a bigger buffer can be filled again.
std::vector<const FModelGeometry *> g_held;
// [Doruk-Aksoy] Words placed so far, the next geometry's first.
unsigned long long g_fill = 0;
unsigned g_generation = 0;
bool g_haveGeneration = false;

// [Doruk-Aksoy] A keyframe span's box in model space, kept per geometry, keyframe and span since a model shows the same few frames over and over.
struct SpanKey
{
	const FModelGeometry *g;
	unsigned base, firstIndex, count;
	bool operator==(const SpanKey &o) const { return g == o.g && base == o.base && firstIndex == o.firstIndex && count == o.count; }
};
struct SpanKeyHash
{
	size_t operator()(const SpanKey &k) const
	{
		size_t h = std::hash<const void *>()(k.g);
		h = h * 31 + k.base;
		h = h * 31 + k.firstIndex;
		return h * 31 + k.count;
	}
};
struct SpanBox { bool ok; float box[6]; };
std::unordered_map<SpanKey, SpanBox, SpanKeyHash> g_boxes;

// [Doruk-Aksoy] Everything held is dropped when the models are, since their geometry went with them.
void FollowModels()
{
	const unsigned gen = ModelsGeneration();
	if (g_haveGeneration && gen == g_generation) return;
	g_haveGeneration = true;
	g_generation = gen;
	g_held.clear();
	g_fill = 0;
	g_boxes.clear();
}

// [Doruk-Aksoy] One geometry's words, packed where it was placed.
void Pack(const FModelGeometry *g, std::vector<unsigned> &out)
{
	const unsigned nverts = g->verts.Size(), nindices = g->indices.Size();
	out.resize(zx::levelmesh::ComputeModelGeoWords(nverts, nindices));
	if (out.empty()) return;
	zx::levelmesh::ComputeModelGeoPack(nverts ? &g->verts[0].x : NULL, nverts, nindices ? &g->indices[0] : NULL, nindices, g->gpuVertWord, out.data());
}

const SpanBox &BoxOf(const FModelGeometry *g, unsigned base, unsigned firstIndex, unsigned count)
{
	const SpanKey key = { g, base, firstIndex, count };
	auto it = g_boxes.find(key);
	if (it != g_boxes.end()) return it->second;
	SpanBox b;
	const unsigned *idx = g->indexed && g->indices.Size() ? &g->indices[0] : NULL;
	b.ok = zx::levelmesh::ComputeModelSpanBox(g->verts.Size() ? &g->verts[0].x : NULL, g->verts.Size(), idx, g->indices.Size(), base, firstIndex, count, b.box);
	return g_boxes.emplace(key, b).first->second;
}

} // namespace

// [Doruk-Aksoy] What one storage buffer binds on this device, in words.
static unsigned long long MaxWords() { return zx::levelmesh::GpuModelStorage() / 4; }

bool EnsureModelGeometry(Diligent::IDeviceContext *ctx)
{
	FollowModels();
	int count = 0;
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::DynPieces(count);
	const size_t heldBefore = g_held.size();
	for (int i = 0; i < count; i++)
	{
		const FModelGeometry *g = pieces[i].gpuGeometry;
		if (g == NULL || g->gpuState != kModelGpuUnknown) continue;
		const unsigned long long words = zx::levelmesh::ComputeModelGeoWords(g->verts.Size(), g->indices.Size());
		// [Doruk-Aksoy] A geometry past what one buffer binds is refused, and its surfaces draw from the CPU from the next frame.
		if (g_fill + words > MaxWords() || zx::levelmesh::ComputeModelGeoCapacity(g_fill + words, MaxWords()) == 0)
		{
			g->gpuState = kModelGpuRefused;
			ZX_COUNT("gpu/model_geometry_refused", "models", 1);
			continue;
		}
		g->gpuVertWord = (unsigned)g_fill;
		g->gpuIndexWord = (unsigned)(g_fill + (unsigned long long)g->verts.Size() * zx::levelmesh::kModelVertexWords);
		g->gpuState = kModelGpuResident;
		g_held.push_back(g);
		g_fill += words;
	}
	if (g_held.size() == heldBefore) return false;
	// [Doruk-Aksoy] Past the buffer, it is made again at the new size with everything held, by EnsureScenePipeline, and the pipelines binding it with it.
	if (!g_scene.modelGeoBuf || g_fill > g_scene.modelGeoCapacity)
	{
		g_scene.modelGeoBuf.Release();
		g_scene.modelGeoCapacity = 0;
		return true;
	}
	static std::vector<unsigned> words;
	for (size_t k = heldBefore; k < g_held.size(); k++)
	{
		Pack(g_held[k], words);
		if (!words.empty()) UpdateBufferInPages(ctx, g_scene.modelGeoBuf, (unsigned long long)g_held[k]->gpuVertWord * 4, words.size() * sizeof(unsigned), words.data());
	}
	return false;
}

bool MakeModelGeoBuffer(FString &err)
{
	FollowModels();
	// [Doruk-Aksoy] A device that draws no model from it (WebGPU) still binds a token one, since every dynamic pipeline declares it.
	unsigned capacity = zx::levelmesh::ComputeModelGeoCapacity(g_fill, MaxWords());
	if (capacity == 0) capacity = 4;
	if (!CreateStructuredBuffer("fua model geometry", 4, capacity, NULL, g_scene.modelGeoBuf)) { err = "model geometry buffer creation failed"; return false; }
	g_scene.modelGeoCapacity = capacity;
	// [Doruk-Aksoy] Everything held sent again, a geometry at a time, rather than through one buffer the size of all of it.
	Diligent::IDeviceContext *ctx = GetContext();
	std::vector<unsigned> words;
	for (const FModelGeometry *g : g_held)
	{
		Pack(g, words);
		if (ctx != NULL && !words.empty()) UpdateBufferInPages(ctx, g_scene.modelGeoBuf, (unsigned long long)g->gpuVertWord * 4, words.size() * sizeof(unsigned), words.data());
	}
	return true;
}

bool ModelSurfaceWorldBox(const zx::levelmesh::MeshPiece &p, const float *matrix, float out[6])
{
	const FModelGeometry *g = p.gpuGeometry;
	if (g == NULL || g->gpuState != kModelGpuResident || matrix == NULL) return false;
	const SpanBox &a = BoxOf(g, p.gpuBase1, p.gpuFirstIndex, p.range.count);
	const SpanBox &b = BoxOf(g, p.gpuBase2, p.gpuFirstIndex, p.range.count);
	if (!a.ok || !b.ok) return false;
	zx::levelmesh::ComputeModelBoxUnder(a.box, b.box, matrix, out);
	return true;
}

}} // namespace zx::hwrender
