// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The per-frame stream: sprites, models and decals, the solid ones drawn after the world and the translucent ones sorted into one pass with the world's.

#include "features/hwrender/diligent/dgscene_internal.h"
#include "r_utility.h"   // [Doruk-Aksoy] r_ActorFrac
#include "features/sprites/spritebuild.h"   // [Doruk-Aksoy] LastTicSplit
#include "features/gamecopy/actors.h"   // [Doruk-Aksoy] FillCount, for fua_dg_ticstats
#include "features/dev-mode/devmode.h"   // [Doruk-Aksoy] fua_dg_ticstats
#include "features/viewsetup/spriteview.h"   // [Doruk-Aksoy] the view tic sprites turn to
#include "features/hwrender/computation/pieceorder_compute.h"   // [rc4l] ComputePieceOrderKey
#include "features/hwrender/computation/pipelineswitch_compute.h"   // [Doruk-Aksoy] fua_dg_blendorder's switch tally
#include "features/hwrender/computation/spriteatlas_compute.h"   // [rc4l] ComputeAtlasUV, ComputeAtlasPieceEligible
#include "features/hwrender/computation/dynlayout_compute.h"   // [rc4l] ComputeDynLayout
#include "features/levelmesh/computation/modelgpu_compute.h"   // [Doruk-Aksoy] ComputeModelRecord, ComputeModelRef
#include "features/models/modelgeometry.h"   // [Doruk-Aksoy] FModelGeometry, where a GPU surface's geometry is held
#include "features/hwrender/computation/flatplane_compute.h"   // [Doruk-Aksoy] flat model surfaces ordered as planes
#include "features/hwshaders/computation/hwshader_compute.h"   // [Doruk-Aksoy] ComputeUserShaderOf
#include "features/jobs/jobs.h"                                 // [rc4l] the pool the records are written on
#include "features/telemetry/telemetry.h"
#include <string.h>   // [rc4l] memcpy
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

// [rc4l] Dump the ORDER the translucent pass actually draws in, for one frame.
//
// Everything about a decal landing on the wrong side of a sprite is decided by one comparator, and
// nothing about the finished picture says what that comparator concluded -- "the scorch is over the
// glow" and "the glow is under the scorch" are the same screenshot. Two attempts at this were made
// by reading pictures and both were wrong. The list itself is one Printf away.
static bool g_dumpBlendOrder = false;
// [rc4l] With a height given, the dump lists every draw whose span holds it instead of the nearest 40.
static bool g_dumpBlendAtZ = false;
static float g_dumpBlendZ = 0.f;
DEV_MODE_CCMD( fua_dg_blendorder )
{
	g_dumpBlendOrder = true;
	g_dumpBlendAtZ = argv.argc( ) > 1;
	g_dumpBlendZ = g_dumpBlendAtZ ? (float)atof( argv[1] ) : 0.f;
	Printf( "fua_dg_blendorder: dumping the next translucent pass\n" );
}

EXTERN_CVAR(Bool, fua_dg_rtmirrors)
EXTERN_CVAR(Bool, fua_dg_premulblend)
EXTERN_CVAR(Bool, fua_dg_rtsprites)
// [rc4l] The translucent pass skips what the pass's walk did not reach; 0 sorts every translucent surface, to measure and compare against.
CVAR(Bool, fua_dg_blendwalk, true, CVAR_FUA_DEV_MODE)
// [Doruk-Aksoy] Picks which actors fua_dg_gpuview draws, 1 those the tic stream may carry and 2 every other piece, the same in either path.
CVAR(Int, fua_dg_gpuview, 0, CVAR_FUA_DEV_MODE)

// [Doruk-Aksoy] One of the frame's flat planes: its height, footprint, and the view whose walk captured it.
struct FlatPlaneInfo { float h; zx::hwrender::FlatFootprint area; int mirrorView, stackSlot; };

// [Doruk-Aksoy] The frame's flat planes, and a flag for each piece that is one; only FUAMODEL's FlatSurface models are looked at, so a mod naming none pays a flag test a piece.
static void FindFlatPlanes(const zx::levelmesh::MeshVertex *src, const zx::levelmesh::MeshPiece *pieces, int npieces,
	std::vector<FlatPlaneInfo> &planes, std::vector<char> &isPlane)
{
	using namespace zx::hwrender;
	planes.clear();
	isPlane.assign(npieces, 0);
	for (int i = 0; i < npieces; i++)
	{
		const zx::levelmesh::MeshPiece &p = pieces[i];
		if (!p.flatSurface || !p.solid || p.blendMode == 0 || p.hud || p.range.count < 3) continue;
		const zx::levelmesh::MeshVertex *v = &src[p.range.offset];
		FlatPlaneInfo pl;
		float lo = v[0].z, hi = v[0].z;
		pl.area = { v[0].x, v[0].x, v[0].y, v[0].y };
		unsigned k = 1;
		for (; k < p.range.count; k++)
		{
			lo = std::min(lo, v[k].z); hi = std::max(hi, v[k].z);
			if (hi - lo > kFlatSliver) break;
			pl.area.minX = std::min(pl.area.minX, v[k].x); pl.area.maxX = std::max(pl.area.maxX, v[k].x);
			pl.area.minY = std::min(pl.area.minY, v[k].y); pl.area.maxY = std::max(pl.area.maxY, v[k].y);
		}
		if (k < p.range.count || !ComputeIsFlatPlane(p.flatSurface, p.solid, p.blendMode, p.hud, lo, hi)) continue;
		pl.h = (lo + hi) * 0.5f; pl.mirrorView = p.mirrorView; pl.stackSlot = p.stackSlot;
		planes.push_back(pl);
		isPlane[i] = 1;
	}
	ZX_COUNT("scene/flat_planes", "planes", planes.size());
}

// [Doruk-Aksoy] The frame's pieces with every sprite quad that crosses a flat plane over its footprint cut at the plane's height, copied into verts and out only when one is; false when none is.
static bool SplitAtFlatPlanes(const zx::levelmesh::MeshVertex *src, int nverts, const zx::levelmesh::MeshPiece *pieces, int npieces,
	const std::vector<FlatPlaneInfo> &planes, std::vector<zx::levelmesh::MeshVertex> &verts, std::vector<zx::levelmesh::MeshPiece> &out)
{
	using namespace zx::hwrender;
	if (planes.empty()) return false;
	static std::vector<FlatFootprint> areas;
	areas.resize(planes.size());
	for (size_t i = 0; i < planes.size(); i++) areas[i] = planes[i].area;
	static FlatGrid grid;
	grid.Build(areas.data(), (int)areas.size(), 128.f, [](int) { return true; });
	// [Doruk-Aksoy] The planes' heights in order, so a sprite no plane height falls well inside skips the grid.
	static std::vector<float> sortedHeights;
	sortedHeights.resize(planes.size());
	for (size_t i = 0; i < planes.size(); i++) sortedHeights[i] = planes[i].h;
	std::sort(sortedHeights.begin(), sortedHeights.end());

	// [Doruk-Aksoy] A sprite crossing several planes is cut at each, up to this many pieces.
	const size_t kMaxPieces = 8;
	static std::vector<std::array<FlatSplitVertex, 4>> quads;
	static std::vector<float> heights;
	bool copied = false;
	int splits = 0;
	for (int i = 0; i < npieces; i++)
	{
		const zx::levelmesh::MeshPiece &p = pieces[i];
		if (!p.quad || p.range.count != 4 || p.solid || p.hud || p.depthBias) continue;
		std::array<FlatSplitVertex, 4> q;
		float lo = 1e30f, hi = -1e30f;
		FlatFootprint area = { 1e30f, -1e30f, 1e30f, -1e30f };
		for (int c = 0; c < 4; c++)
		{
			const zx::levelmesh::MeshVertex &mv = src[p.range.offset + c];
			q[c] = { mv.x, mv.y, mv.z, mv.u, mv.v };
			lo = std::min(lo, mv.z); hi = std::max(hi, mv.z);
			area.minX = std::min(area.minX, mv.x); area.maxX = std::max(area.maxX, mv.x);
			area.minY = std::min(area.minY, mv.y); area.maxY = std::max(area.maxY, mv.y);
		}
		if (!ComputeAnyBetween(sortedHeights, lo + kFlatSliver, hi - kFlatSliver)) continue;
		heights.clear();
		grid.Query(area, [&](int k) {
			const FlatPlaneInfo &pl = planes[k];
			if (pl.mirrorView == p.mirrorView && pl.stackSlot == p.stackSlot && ComputeStraddlesHeight(lo, hi, pl.h)) heights.push_back(pl.h);
		});
		if (heights.empty()) continue;
		quads.clear();
		quads.push_back(q);
		for (float h : heights)
			for (size_t k = 0, n = quads.size(); k < n && quads.size() < kMaxPieces; k++)
			{
				std::array<FlatSplitVertex, 4> a, b;
				if (!ComputeSplitQuadAtHeight(quads[k].data(), h, a.data(), b.data())) continue;
				quads[k] = a;
				quads.push_back(b);
			}
		if (quads.size() == 1) continue;
		if (!copied)
		{
			verts.assign(src, src + nverts);
			out.assign(pieces, pieces + npieces);
			copied = true;
		}
		// [Doruk-Aksoy] The first piece keeps the sprite's place and vertices; the rest are appended as copies of it.
		for (size_t k = 0; k < quads.size(); k++)
		{
			unsigned offset = p.range.offset;
			if (k > 0)
			{
				offset = (unsigned)verts.size();
				verts.resize(verts.size() + 4);
				out.push_back(p);
				out.back().range.offset = offset;
			}
			for (int c = 0; c < 4; c++)
			{
				const FlatSplitVertex &sv = quads[k][c];
				verts[offset + c].Set(sv.x, sv.h, sv.y, sv.u, sv.v);
			}
		}
		splits++;
	}
	ZX_COUNT("scene/flat_splits", "sprites", splits);
	return copied;
}

namespace zx { namespace hwrender {

// [rc4l] Dynamic pieces a job writes at once.
static const int kDynChunk = 64;

// [rc4l] The per-frame sprite geometry: uploaded fresh every frame, never cached.
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_dynVB;
static unsigned int g_dynVBCapacity = 0;

// [rc4l] The SORTED ORDER, as an index buffer, so back-to-front costs one draw instead of hundreds.
//
// The translucent pass sorts by distance and cannot batch by material, so it used to issue a draw
// per surface: 1380 sprites in an arena on Sunder MAP10 came to 931 submissions even after adjacent
// ones were collapsed, because the sort scatters neighbours. At roughly two microseconds of driver
// time each that is most of the frame's CPU cost, on a map where a ninth of the pixels only buys a
// third of the time.
//
// Writing the sorted order into indices instead moves the ordering out of the draw list and into
// the buffer, where it costs nothing: every surface that wants the same pipeline becomes one
// DrawIndexed over a contiguous span of indices, and the order within that span is exactly the order
// the sort produced. Nothing is reordered and nothing is merged that was not adjacent in the sorted
// list -- only the submissions disappear.
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_blendIB;
static unsigned int g_blendIBCapacity = 0;
static TArray<Diligent::Uint32> g_blendIndices;

static bool EnsureBlendIndexBuffer(Diligent::IRenderDevice *dev, unsigned int needed)
{
	if (dev == NULL || needed == 0) return false;
	// Grown with slack, because a frame that needs one more index than the last must not reallocate.
	Diligent::BufferDesc kind;
	kind.Name = "fua blend order";
	kind.BindFlags = Diligent::BIND_INDEX_BUFFER;
	kind.Usage = Diligent::USAGE_DEFAULT;
	return EnsureGrowingBuffer(g_blendIB, g_blendIBCapacity, needed, sizeof(Diligent::Uint32), 4096, kind);
}

// [rc4l] Draw this frame's dynamic geometry -- sprites.
//
// Rebuilt and re-uploaded every frame on purpose: billboards are view-dependent, so there is nothing
// to cache. The cost is bounded by what is on screen (a few hundred quads), and at ~5.6M triangles
// per GPU millisecond the upload dominates the draw by a wide margin.
//
// Drawn AFTER the world with the alpha-tested pipeline and normal depth, so sprites occlude and are
// occluded correctly. Only the opaque sprites are drawn here: the translucent ones are handed to the
// world's translucent pass so the two sort against each other -- an item lying under a glass 3D floor
// has to be composited before the floor, and it cannot be if sprites are a separate pass afterwards.

// [rc4l] The quads a traced mirror's rays can meet, each sprite a mirror's walk took from its first three corners, and the steps between those walks' views.
static void GatherTracedSprites(const SceneVertex *vb, unsigned count)
{
	TArray<zx::hwrender::TracedSprite> &out = g_scene.tracedSprites;
	if (!fua_dg_rtmirrors || !fua_dg_rtsprites) return;
	for (unsigned i = 0; i < g_scene.dynRuns.Size(); i++)
	{
		const DynRun &r = g_scene.dynRuns[i];
		if (r.general < 0 || !zx::hwrender::ComputeTracesRun(r.count, r.solid, r.depthBias, r.hud, r.stackSlot, r.onMirror, r.mirrorView, r.blend) || (unsigned)r.general + 2 >= count) continue;
		const SceneVertex &a = vb[r.general], &b = vb[r.general + 1], &c = vb[r.general + 2];
		const float tl[5] = { a.x, a.y, a.z, a.u, a.v }, tr[5] = { b.x, b.y, b.z, b.u, b.v }, bl[5] = { c.x, c.y, c.z, c.u, c.v };
		zx::hwrender::TracedSprite s;
		if (zx::hwrender::ComputeTracedSprite(tl, tr, bl, (unsigned)(a.pieceIndex + 0.5f), r.mirrorView, s)) out.Push(s);
	}
	zx::levelmesh::MirrorSteps(g_scene.tracedSteps);
}

// [rc4l] The dynamic words (general vertices, packed sprites, model matrices) and the vertex refs into them.
// [Doruk-Aksoy] The tic stream's packed sprites first, kept between frames, then the frame's.
static TArray<unsigned> g_dynWords, g_dynRefs;

// [Doruk-Aksoy] The tic stream's region at the front of the words, refs and runs, made again only when the tic's stream, its slots or its buffers change.
struct TicRegion
{
	unsigned int gen = 0, table = 0;
	bool premul = false, built = false;
	unsigned int words = 0, refs = 0;   // [Doruk-Aksoy] its packed sprites' words and refs
	TArray<DynRun> runs;                // [Doruk-Aksoy] one a packed sprite, ticPiece naming its piece
	std::vector<char> unpacked;         // [Doruk-Aksoy] a tic piece no packed sprite could carry, drawn each frame from a moved copy
	std::vector<PieceSlotSet> slots;    // [Doruk-Aksoy] the slots each piece was packed with, which each reservation must still give it
	std::vector<std::array<zx::hwrender::SpriteCorner, 4>> corners;   // [Doruk-Aksoy] each run's stored corners (scene x, height, map y)
	std::vector<zx::hwrender::SpriteMotion> motion;                     // [Doruk-Aksoy] ...and its motion, for its bounds each frame
	std::vector<std::array<float, 3>> sort;                             // [Doruk-Aksoy] ...and its sort centre at the tic's end
	const void *wordsBuf = NULL, *refsBuf = NULL;   // [Doruk-Aksoy] the buffers it went to, so a new buffer takes it again
};
static TicRegion g_tic;
// [Doruk-Aksoy] Tic stream runs a camera texture's pass drew since fua_dg_ticstats last printed, which must stay 0.
static unsigned g_cameraTicDraws = 0;

// [Doruk-Aksoy] The view the scene shader turns tic sprites to, as dgviews hands it the same frame.
struct TicView { float frac, perpX, perpY, pitch; };
static TicView TicViewNow()
{
	float dirX = 0.f, dirY = 0.f, yaw = 0.f, pitch = 0.f;
	zx::viewsetup::SpriteView().ViewVector(dirX, dirY);
	zx::viewsetup::SpriteView().ViewAngles(yaw, pitch);
	return TicView{ FIXED2FLOAT(r_ActorFrac), -dirY, dirX, pitch };
}

// [Doruk-Aksoy] A tic piece's stored corners and motion, as its packed sprite holds them.
static void TicPieceCorners(const zx::levelmesh::MeshVertex *src, const zx::levelmesh::MeshPiece &p, zx::hwrender::SpriteCorner corners[4], zx::hwrender::SpriteMotion &m)
{
	for (int c = 0; c < 4; c++)
	{
		const SceneVertex dv = ToSceneVertex(src[p.range.offset + c], 0);
		corners[c] = { dv.x, dv.y, dv.z, dv.u, dv.v };
	}
	m = { p.gpuCx, p.gpuCy, p.gpuPx, p.gpuPy, p.gpuDx, p.gpuDh, p.gpuDy, p.gpuXY };
}

// [Doruk-Aksoy] Whether a reservation now gives any packed tic piece other slots than it was packed with.
static bool TicSlotsMoved(const zx::levelmesh::MeshPiece *pieces, int count)
{
	if ((int)g_tic.slots.size() != count || (int)g_tic.unpacked.size() != count) return true;
	for (int k = 0; k < count; k++)
	{
		if (g_tic.unpacked[k]) continue;
		const PieceSlotSet now = DynamicPieceSlots(k, pieces[k]);
		if (now.mat != g_tic.slots[k].mat || now.bright != g_tic.slots[k].bright) return true;
	}
	return false;
}

// [Doruk-Aksoy] The tic stream packed into the front of the words and refs, one run a sprite.
static void BuildTicRegion(const zx::levelmesh::MeshVertex *src, const zx::levelmesh::MeshPiece *pieces, int count)
{
	ZX_ZONE("scene/tic_region");   // [Doruk-Aksoy] the tic stream packed, once a tic
	static const int kQuadOrder[6] = { 0, 1, 2, 2, 1, 3 };
	static std::vector<unsigned> packed;
	static std::vector<char> ok;
	static std::vector<ScenePieceData> filled;
	static std::vector<int> unfilled;
	packed.resize((size_t)count * zx::hwrender::kSpriteWords);
	ok.assign(count, 0);
	filled.resize(count);
	g_tic.slots.resize(count);
	const int chunks = (count + kDynChunk - 1) / kDynChunk;
	unfilled.assign(chunks, 0);
	auto pack = [&](int chunk, int)
	{
		int missed = 0;
		for (int k = chunk * kDynChunk; k < count && k < (chunk + 1) * kDynChunk; k++)
		{
			const zx::levelmesh::MeshPiece &p = pieces[k];
			g_tic.slots[k] = DynamicPieceSlots(k, p);
			FillPieceDataWith(p, g_tic.slots[k], filled[k], false);
			missed += MaterialSlotsUnfilled((int)filled[k].matSlot, (int)filled[k].brightSlot);
			zx::hwrender::SpriteCorner corners[4];
			zx::hwrender::SpriteMotion m;
			TicPieceCorners(src, p, corners, m);
			ok[k] = p.quad && p.range.count == 4 && zx::hwrender::ComputeSpritePack(filled[k], corners, &packed[(size_t)k * zx::hwrender::kSpriteWords], &m) ? 1 : 0;
		}
		unfilled[chunk] = missed;
	};
	zx::jobs::Pool().ParallelFor(chunks, pack);
	for (int c = 0; c < chunks; c++) { g_stats.dynSlotUnfilled += unfilled[c]; g_stats.dynSlotUnfilledTotal += unfilled[c]; }
	ZX_ZONE("scene/tic_compact");   // [Doruk-Aksoy] the packed sprites laid out with their runs, each at its place among the packed
	static std::vector<int> place;
	place.resize(count);
	g_tic.unpacked.assign(count, 0);
	int sprites = 0;
	for (int k = 0; k < count; k++)
	{
		place[k] = ok[k] ? sprites++ : -1;
		if (!ok[k]) { g_tic.unpacked[k] = 1; ZX_COUNT("ticstream/tic_unpacked", "sprites", 1); }
	}
	g_dynWords.Resize((unsigned)sprites * zx::hwrender::kSpriteWords);
	g_dynRefs.Resize((unsigned)sprites * 6);
	g_tic.runs.Resize((unsigned)sprites);
	g_tic.corners.resize(sprites);
	g_tic.motion.resize(sprites);
	g_tic.sort.resize(sprites);
	auto lay = [&](int lo, int hi, int) { for (int k = lo; k < hi; k++)
	{
		const int j = place[k];
		if (j < 0) continue;
		const zx::levelmesh::MeshPiece &p = pieces[k];
		const unsigned at = (unsigned)j * zx::hwrender::kSpriteWords;
		memcpy(&g_dynWords[at], &packed[(size_t)k * zx::hwrender::kSpriteWords], zx::hwrender::kSpriteWords * sizeof(unsigned));
		for (unsigned v = 0; v < 6; v++) g_dynRefs[(unsigned)j * 6 + v] = zx::hwrender::ComputeSpriteRef(at, kQuadOrder[v]);
		DynRun &r = g_tic.runs[j];
		r.material = p.material; r.first = (unsigned)j * 6; r.count = 6; r.blend = p.blendMode;
		r.translation = p.translation; r.depthBias = p.depthBias; r.redAlpha = p.redToAlpha; r.solid = p.solid;
		r.flatPlane = false; r.hud = p.hud; r.mirrorView = p.mirrorView; r.stackSlot = p.stackSlot; r.onMirror = p.onMirror;
		r.cx = p.sortX; r.cy = p.sortY; r.cz = p.sortZ;
		r.lo = r.hi = 0.f; r.minX = r.maxX = r.minZ = r.maxZ = 0.f;
		r.atlasPage = -1; r.general = -1;
		r.userShader = zx::hwshaders::ComputeUserShaderOf((int)(filled[k].warpStyle + 0.5f));
		r.ticPiece = k; r.hidden = false; r.eligible = true; r.sortWho = p.sortWho;
		TicPieceCorners(src, p, g_tic.corners[j].data(), g_tic.motion[j]);
		g_tic.sort[j] = { p.sortX, p.sortY, p.sortZ };
	} };
	if (count > 0) zx::jobs::Pool().ParallelChunks(count, 256, lay);
	g_tic.words = (unsigned)sprites * zx::hwrender::kSpriteWords;
	g_tic.refs = (unsigned)sprites * 6;
	ZX_COUNT("ticstream/tic_packed", "sprites", sprites);
}

// [Doruk-Aksoy] A tic piece drawn by the CPU where the shader would draw it this frame, an ordinary piece of the frame's.
static void MovedCopy(const zx::levelmesh::MeshVertex *src, const zx::levelmesh::MeshPiece &p, const TicView &view,
	std::vector<zx::levelmesh::MeshVertex> &verts, std::vector<zx::levelmesh::MeshPiece> &pieces)
{
	zx::hwrender::SpriteCorner in[4], out[4];
	zx::hwrender::SpriteMotion m;
	TicPieceCorners(src, p, in, m);
	zx::hwrender::ComputeSpriteMotionCorners(in, m, view.frac, view.perpX, view.perpY, view.pitch, out);
	zx::levelmesh::MeshPiece copy = p;
	copy.gpuMove = false;
	const float back = 1.f - view.frac;
	copy.sortX -= p.gpuDx * back; copy.sortY -= p.gpuDy * back; copy.sortZ -= p.gpuDh * back;
	copy.range.offset = (unsigned)verts.size();
	copy.range.count = 4;
	for (int c = 0; c < 4; c++)
	{
		zx::levelmesh::MeshVertex mv = src[p.range.offset + c];
		mv.x = out[c].x; mv.z = out[c].y; mv.y = out[c].z;
		verts.push_back(mv);
	}
	pieces.push_back(copy);
	ZX_COUNT("ticstream/tic_copied", "sprites", 1);
}

// [Doruk-Aksoy] Each frame, the tic runs hidden as the sprite side says (or forced), their bounds and sort centre where the shader draws them, and fua_dg_gpuview's pick over every run.
static void ApplyTicFrame(const std::vector<char> &forced)
{
	int hiddenCount = 0;
	const unsigned char *hidden = zx::levelmesh::DynTicHidden(hiddenCount);
	const TicView view = TicViewNow();
	const float back = 1.f - view.frac;
	const int pick = fua_dg_gpuview;
	TArray<DynRun> &runs = g_scene.dynRuns;
	const unsigned ticRuns = g_tic.runs.Size() <= runs.Size() ? g_tic.runs.Size() : 0;
	for (unsigned i = ticRuns; i < runs.Size(); i++) runs[i].hidden = (pick == 1 && !runs[i].eligible) || (pick == 2 && runs[i].eligible);
	// [Doruk-Aksoy] One turn for every XY billboard of the frame, worked out once.
	const float ang = -view.pitch * 3.14159265358979323846f / 180.f, cs = cosf(ang), sn = sinf(ang);
	// [Doruk-Aksoy] Each tic run on its own lane's chunk, since they write only themselves.
	auto apply = [&](int lo, int hi, int) { for (int i = lo; i < hi; i++)
	{
		DynRun &r = runs[i];
		r.hidden = (pick == 1 && !r.eligible) || (pick == 2 && r.eligible);
		const int k = r.ticPiece;
		if ((k < hiddenCount && hidden[k]) || (k < (int)forced.size() && forced[k])) r.hidden = true;
		if (r.hidden) continue;
		zx::hwrender::SpriteCorner out[4];
		const zx::hwrender::SpriteMotion &m = g_tic.motion[i];
		zx::hwrender::ComputeSpriteMotionCornersCS(g_tic.corners[i].data(), m, view.frac, view.perpX, view.perpY, cs, sn, out);
		r.lo = r.minX = r.minZ = 1e30f;
		r.hi = r.maxX = r.maxZ = -1e30f;
		for (int c = 0; c < 4; c++)
		{
			r.lo = std::min(r.lo, out[c].y); r.hi = std::max(r.hi, out[c].y);
			r.minX = std::min(r.minX, out[c].x); r.maxX = std::max(r.maxX, out[c].x);
			r.minZ = std::min(r.minZ, out[c].z); r.maxZ = std::max(r.maxZ, out[c].z);
		}
		r.cx = g_tic.sort[i][0] - m.dx * back; r.cy = g_tic.sort[i][1] - m.dy * back; r.cz = g_tic.sort[i][2] - m.dh * back;
	} };
	if (ticRuns > 0) zx::jobs::Pool().ParallelChunks((int)ticRuns, 512, apply);
}

}} // namespace zx::hwrender
// [Doruk-Aksoy] The tic stream's state, so a test can tell the shader path drew what it judged.
DEV_MODE_CCMD(fua_dg_ticstats)
{
	int pieces = 0, hiddenCount = 0, shown = 0, frameDerived = 0;
	zx::levelmesh::DynTicPieces(pieces);
	const unsigned char *hidden = zx::levelmesh::DynTicHidden(hiddenCount);
	int hiddenNow = 0;
	for (int i = 0; i < hiddenCount; i++) hiddenNow += hidden[i] ? 1 : 0;
	zx::sprites::LastTicSplit(shown, frameDerived);
	int unpacked = 0;
	for (char u : zx::hwrender::g_tic.unpacked) unpacked += u ? 1 : 0;
	Printf("ticstats pieces %d packed %u unpacked %d hidden %d shown %d framederived %d cameraticdraws %u builds %u fill %u\n", pieces, zx::hwrender::g_tic.runs.Size(), unpacked, hiddenNow, shown, frameDerived, zx::hwrender::g_cameraTicDraws,
		zx::sprites::TakeTicBuilds(), zx::gamecopy::FillCount());
	zx::hwrender::g_cameraTicDraws = 0;
	int why[12];
	zx::sprites::LastTicWhy(why);
	Printf("ticwhy never %d nosubject %d playerbob %d model %d shape %d sector %d shader %d clip %d glow %d shadowfog %d lane %d pieces %d\n",
		why[0], why[1], why[2], why[3], why[4], why[5], why[6], why[7], why[8], why[9], why[10], why[11]);
}
namespace zx { namespace hwrender {

void BuildDynamic(Diligent::IDeviceContext *ctx)
{
	ZX_ZONE("scene/dynamic");   // [rc4l] the frame's sprites, models and decals laid out and written
	cycle_t dynClock;
	dynClock.Reset();
	dynClock.Clock();
	struct ClockStop { cycle_t &c; ~ClockStop() { c.Unclock(); g_stats.dynBuildMs = c.TimeMS(); } } stopper = { dynClock };
	g_stats.dynDraws = g_stats.dynTris = 0;
	g_stats.blendSubmits = 0;
	g_stats.dynSlotRefused = g_stats.dynSlotNoMaterial = g_stats.dynSlotSeen = g_stats.dynSlotMax = 0;
	g_stats.dynSlotUnfilled = 0;
	g_stats.dynBindless = g_stats.dynPerMaterial = g_stats.dynAtlas = 0;
	g_stats.dynByBlend[0] = g_stats.dynByBlend[1] = g_stats.dynByBlend[2] = g_stats.dynByBlend[3] = 0;
	g_scene.dynReady = false;

	int nverts = 0, npieces = 0, nTicVerts = 0, nTic = 0;
	const zx::levelmesh::MeshVertex *src = zx::levelmesh::DynVertices(nverts);
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::DynPieces(npieces);
	const zx::levelmesh::MeshVertex *ticSrc = zx::levelmesh::DynTicVertices(nTicVerts);
	const zx::levelmesh::MeshPiece *ticPieces = zx::levelmesh::DynTicPieces(nTic);
	// [Doruk-Aksoy] Pieces, not vertices, since a frame of GPU model surfaces alone has none in the stream.
	if (pieces == NULL || npieces <= 0) { npieces = 0; nverts = 0; }
	if (ticSrc == NULL || ticPieces == NULL || nTic <= 0) { nTic = 0; ticPieces = NULL; }
	static std::vector<char> forced;
	if (npieces == 0 && nTic == 0) { g_scene.dynRuns.Clear(); g_scene.dynTicRuns = 0; g_scene.tracedSprites.Clear(); g_scene.tracedSteps.clear(); g_tic.built = false; return; }

	// The dynamic path's piece records, rebuilt with its vertices every frame.

	static TArray<ScenePieceData> dynPieces;
	TArray<DynRun> &runs = g_scene.dynRuns;
	TArray<unsigned> &words = g_dynWords;
	TArray<unsigned> &refs = g_dynRefs;

	// [rc4l] Rebuild and re-upload ONCE per engine frame, not once per draw call.
	//
	// DrawSceneOnce is called many times between frame boundaries by the benchmark, and Vulkan's
	// dynamic heap is only recycled when a frame completes -- so mapping here unconditionally
	// exhausted an 8 MB heap after a few hundred draws ("Space in dynamic heap is exhausted").
	static unsigned int builtGen = 0;
	// [Doruk-Aksoy] Records hold the additive flag fua_dg_premulblend sets, so a change of it fills them again, a paused frame's included.
	static bool builtPremul = false;
	const unsigned int gen = zx::levelmesh::DynGeneration();
	const bool premul = fua_dg_premulblend;
	// [Doruk-Aksoy] The tic region again when its stream, the blend setting, its slots or a buffer it lives in changed.
	bool ticRebuild = !g_tic.built || zx::levelmesh::DynTicGeneration() != g_tic.gen || premul != g_tic.premul || MaterialSlotTableId() != g_tic.table ||
		g_scene.dynDataBuf.RawPtr() != g_tic.wordsBuf || g_dynVB.RawPtr() != g_tic.refsBuf;
	if (!ticRebuild) { ZX_ZONE("scene/tic_slotcheck"); ticRebuild = TicSlotsMoved(ticPieces, nTic); }   // [Doruk-Aksoy] every frame the tic stream stands
	if (gen != builtGen || builtPremul != premul || ticRebuild)
	{
		builtGen = gen;
		builtPremul = premul;
		if (ticRebuild)
		{
			BuildTicRegion(ticSrc, ticPieces, nTic);
			g_tic.gen = zx::levelmesh::DynTicGeneration();
			g_tic.premul = premul;
			g_tic.table = MaterialSlotTableId();
			g_tic.built = true;
		}
		const int framePieces = npieces;   // [Doruk-Aksoy] the frame's own pieces, whose slots the reservation found behind the tic's
		static std::vector<zx::levelmesh::MeshVertex> splitVerts;
		static std::vector<zx::levelmesh::MeshPiece> splitPieces;
		static std::vector<FlatPlaneInfo> flatPlanes;
		static std::vector<char> isFlatPlane;
		{
			ZX_ZONE("scene/flat_split");
			FindFlatPlanes(src, pieces, npieces, flatPlanes, isFlatPlane);
			// [Doruk-Aksoy] Tic pieces no packed sprite carries, and every shown one when a flat plane may cut it, drawn as moved copies among the frame's.
			int hiddenCount = 0;
			const unsigned char *hidden = zx::levelmesh::DynTicHidden(hiddenCount);
			forced.assign(nTic, 0);
			bool anyCopy = false;
			for (int k = 0; k < nTic; k++)
			{
				const bool shown = !(k < hiddenCount && hidden[k]);
				// [Doruk-Aksoy] Not in a camera texture's pass, which draws its own view's sprites and none of the player's tic stream.
				forced[k] = !g_pass.inCameraPass && shown && (g_tic.unpacked[k] || !flatPlanes.empty()) ? 1 : 0;
				anyCopy = anyCopy || forced[k];
			}
			static std::vector<zx::levelmesh::MeshVertex> withCopiesVerts;
			static std::vector<zx::levelmesh::MeshPiece> withCopies;
			if (anyCopy)
			{
				const TicView view = TicViewNow();
				withCopiesVerts.assign(src, src + nverts);
				withCopies.assign(pieces, pieces + npieces);
				for (int k = 0; k < nTic; k++) if (forced[k]) MovedCopy(ticSrc, ticPieces[k], view, withCopiesVerts, withCopies);
				src = withCopiesVerts.data(); nverts = (int)withCopiesVerts.size();
				pieces = withCopies.data(); npieces = (int)withCopies.size();
				isFlatPlane.resize(npieces, 0);
			}
			if (SplitAtFlatPlanes(src, nverts, pieces, npieces, flatPlanes, splitVerts, splitPieces))
			{
				src = splitVerts.data(); nverts = (int)splitVerts.size();
				pieces = splitPieces.data(); npieces = (int)splitPieces.size();
				isFlatPlane.resize(npieces, 0);   // [Doruk-Aksoy] the appended halves are sprites
			}
		}
		// [rc4l] Emptied with each refill, which a frame drawn again without a walk (a screenshot) does not get, so a frame with no sprites keeps none of the last fill's.
		g_scene.tracedSprites.Clear();
		g_scene.tracedSteps.clear();

		// [rc4l] Order decides correctness here, not just batching.
		//
		// Opaque pieces first, grouped by material so they batch. Translucent pieces after, sorted
		// BACK TO FRONT by distance from the camera -- blending is not commutative, so two overlapping
		// translucent sprites drawn in the wrong order composite wrongly. Within the translucent set
		// the sort beats batching, so material grouping is deliberately given up there.
		static TArray<int> order;
		order.Resize(npieces);

		const float cx = FIXED2FLOAT(viewx), cy = FIXED2FLOAT(viewy), cz = FIXED2FLOAT(viewz);
		// [rc4l] n log n, because this ran as a selection sort over every sprite in the frame.
		//
		// The rule is unchanged -- opaque first and grouped by material, then blended back to front --
		// and only the number of comparisons is different. The pair loop was O(n^2): a Sunder MAP10
		// arena with monsters awake puts 1258 sprites in a frame, which is 790,000 comparisons every
		// frame, each dereferencing two pieces. It was 18.5% of the profile, and it is the reason the
		// same view cost twice as much with monsters as without -- the growth is in n, not in what
		// the monsters do.
		//
		// The index breaks ties so the order is DETERMINISTIC. std::sort is not stable, and two
		// sprites at the same distance trading places between frames is a flicker -- the same trap the
		// translucent draw comparator documents one pass later.
		// [rc4l] Each piece read once into a key (pieceorder_compute), since a comparator reading two pieces a comparison was a twentieth of a Complex Doom fight's frame.
		static std::vector<zx::hwrender::PieceOrderKey> orderKeys;
		orderKeys.resize(npieces);
		for (int i = 0; i < npieces; i++)
		{
			const zx::levelmesh::MeshPiece &p = pieces[i];
			const float dx = p.sortX - cx, dy = p.sortY - cy, dz = p.sortZ - cz;
			orderKeys[i].key = zx::hwrender::ComputePieceOrderKey(p.blendMode != 0, (uintptr_t)p.material, dx*dx + dy*dy + dz*dz);
			orderKeys[i].index = i;
		}
		static std::vector<zx::hwrender::PieceOrderKey> orderScratch;
		{
			ZX_ZONE("scene/piece_sort");
			ZX_COUNT("scene/pieces_sorted", "pieces", npieces);
			zx::hwrender::ComputeSortPieceOrder(orderKeys, orderScratch);
		}
		for (int i = 0; i < npieces; i++) order[i] = orderKeys[i].index;

		// [rc4l] Every slot found and every atlas place taken in draw order, on this thread, since placing a sprite the first time uploads it.
		static TArray<unsigned> dynVerts;
		dynVerts.Resize(npieces);
		for (int i = 0; i < npieces; i++) dynVerts[i] = pieces[i].range.count == 0 ? 0u : pieces[i].quad ? 6u : pieces[i].range.count;
		// [Doruk-Aksoy] The GPU model surfaces and their world boxes, here on this thread since their spans' boxes are kept between frames; one not drawable (refused geometry, a bad span) draws nothing.
		static std::vector<char> gpuModel;
		static std::vector<float> gpuBox;
		gpuModel.assign(npieces, 0);
		gpuBox.resize((size_t)npieces * 6);
		{
			int heldMatrices = 0;
			const float *mats = zx::levelmesh::DynMatrices(heldMatrices);
			for (int i = 0; i < npieces; i++)
			{
				const zx::levelmesh::MeshPiece &p = pieces[i];
				if (p.gpuGeometry == NULL || dynVerts[i] == 0) continue;
				if (p.modelMatrix < 0 || p.modelMatrix >= heldMatrices || !ModelSurfaceWorldBox(p, mats + (size_t)p.modelMatrix * zx::hwrender::kModelMatrixWords, &gpuBox[(size_t)i * 6]))
				{
					dynVerts[i] = 0;
					continue;
				}
				gpuModel[i] = 1;
			}
		}
		static std::vector<PieceSlotSet> slots;
		static std::vector<const AtlasEntry *> atlased;
		slots.resize(npieces);
		atlased.assign(npieces, NULL);
		const bool atlas = AtlasActive();
		g_stats.dynMirrorRuns = 0;
		for (int i = 0; i < npieces; i++)
		{
			const int k = order[i];
			if (dynVerts[k] == 0) continue;
			const zx::levelmesh::MeshPiece &p = pieces[k];
			// [Doruk-Aksoy] The reservation numbers the tic's pieces first, then the frame's own; a copy or a cut half has no number and is looked up.
			slots[k] = DynamicPieceSlots(k < framePieces ? nTic + k : -1, p);
			if (atlas) atlased[k] = AtlasFor(p.material, p.translation,
				!gpuModel[k] && zx::hwrender::ComputeAtlasPieceEligible({ p.quad, p.depthBias, p.solid, p.hud, p.mirrorView, p.onMirror, p.fuzz }));
			if (p.mirrorView >= 0) g_stats.dynMirrorRuns++;
		}
		// [rc4l] Every record filled on the pool, and each plain quad packed with its corners (ComputeSpritePack) unless a traced mirror reads its record.
		static std::vector<ScenePieceData> filled;
		static std::vector<unsigned> packedWords;
		static std::vector<char> packedFlag;
		static std::vector<int> unfilled;
		filled.resize(npieces);
		packedWords.resize((size_t)npieces * zx::hwrender::kSpriteWords);
		packedFlag.assign(npieces, 0);
		const int packChunks = (npieces + kDynChunk - 1) / kDynChunk;
		unfilled.assign(packChunks, 0);
		const bool tracing = fua_dg_rtmirrors && fua_dg_rtsprites;
		const bool bindless = BindlessActive();
		auto pack = [&](int chunk, int)
		{
			static const int kQuadOrder[4] = { 0, 1, 2, 3 };
			int missed = 0;
			for (int k = chunk * kDynChunk; k < npieces && k < (chunk + 1) * kDynChunk; k++)
			{
				if (dynVerts[k] == 0) continue;
				const zx::levelmesh::MeshPiece &p = pieces[k];
				ScenePieceData &pd = filled[k];
				FillPieceDataWith(p, slots[k], pd, false);
				if (bindless) missed += MaterialSlotsUnfilled((int)pd.matSlot, (int)pd.brightSlot);
				if (!p.quad || p.range.count != 4) continue;
				if (tracing && zx::hwrender::ComputeTracesRun(6, p.solid, p.depthBias, p.hud, p.stackSlot, p.onMirror, p.mirrorView, p.blendMode)) continue;
				zx::hwrender::SpriteCorner corners[4];
				const AtlasEntry *ae = atlased[k];
				for (int c = 0; c < 4; c++)
				{
					const zx::levelmesh::MeshVertex &mv = src[p.range.offset + kQuadOrder[c]];
					const SceneVertex dv = ToSceneVertex(mv, 0);
					corners[c] = { dv.x, dv.y, dv.z, dv.u, dv.v };
					if (ae != NULL) zx::hwrender::ComputeAtlasUV(ae->x, ae->y, ae->w, ae->h, AtlasPageSize(), AtlasPageSize(), corners[c].u, corners[c].v);
				}
				packedFlag[k] = zx::hwrender::ComputeSpritePack(pd, corners, &packedWords[(size_t)k * zx::hwrender::kSpriteWords]) ? 1 : 0;
			}
			unfilled[chunk] = missed;
		};
		zx::jobs::Pool().ParallelFor(packChunks, pack);
		for (int c = 0; c < packChunks; c++)
		{
			g_stats.dynSlotUnfilled += unfilled[c];
			g_stats.dynSlotUnfilledTotal += unfilled[c];
		}
		const int recordsLeft = g_scene.pieceCapacity > g_scene.pieces.Size() ? int(g_scene.pieceCapacity - g_scene.pieces.Size()) : 0;
		static std::vector<zx::hwrender::DynSlot> layout;
		const zx::hwrender::DynLayoutTotals totals = zx::hwrender::ComputeDynLayout(npieces > 0 ? &order[0] : NULL, npieces, npieces > 0 ? &dynVerts[0] : NULL,
			npieces > 0 ? (const bool *)&packedFlag[0] : NULL, recordsLeft, layout, npieces > 0 ? (const bool *)&gpuModel[0] : NULL);
		int records = 0;
		for (const zx::hwrender::DynSlot &slot : layout) if (slot.record >= 0) records++;
		ZX_COUNT("gpu/sprites_packed", "sprites", totals.sprites);
		ZX_COUNT("gpu/sprites_whole", "pieces", (double)layout.size() - totals.sprites);
		ZX_COUNT("gpu/model_surfaces", "surfaces", totals.models);   // [Doruk-Aksoy] blended and placed by the vertex shader
		// [rc4l] The words: the general vertices first, as SceneVertex, then the packed sprites from a multiple of four.
		// [Doruk-Aksoy] All behind the tic region, from a multiple of twelve so a general vertex starts on a whole one and a sprite on a multiple of four.
		const unsigned frameBase = (g_tic.words + 11) / 12 * 12;
		const unsigned generalBase = frameBase / 6;
		const unsigned spriteBase = (frameBase + totals.general * 6 + 3) & ~3u;
		// [Doruk-Aksoy] ...then the frame's model matrices, which a GLDEFS shader's program reads its ModelMatrix from.
		int nmatrices = 0;
		const float *matrices = zx::levelmesh::DynMatrices(nmatrices);
		const unsigned matBase = spriteBase + (unsigned)totals.sprites * zx::hwrender::kSpriteWords;
		// [Doruk-Aksoy] ...then the GPU model surfaces' records (modelgpu_compute).
		const unsigned modelBase = matBase + (unsigned)nmatrices * zx::hwrender::kModelMatrixWords;
		words.Resize(modelBase + (unsigned)totals.models * zx::levelmesh::kModelRecordWords);
		if (nmatrices > 0) memcpy(&words[matBase], matrices, (size_t)nmatrices * zx::hwrender::kModelMatrixWords * sizeof(unsigned));
		const unsigned refBase = g_tic.refs, runBase = g_tic.runs.Size();
		refs.Resize(refBase + totals.stream);
		runs.Resize(runBase + (unsigned)layout.size());
		{ ZX_ZONE("scene/tic_runs_copy"); for (unsigned i = 0; i < runBase; i++) runs[i] = g_tic.runs[i]; }
		g_scene.dynTicRuns = runBase;
		dynPieces.Resize(records);

		// [rc4l] Every dynamic piece is its own run.
		//
		// Sprites are all drawn in one back-to-front pass now, the way GL does it, so any two
		// merged into a shared run could no longer be ordered against each other. There are a few
		// hundred sprites in a busy frame, so the batching this gives up is not worth having.
		//
		// MEASURED, because "1234 sprites, 1234 draw calls" reads like the whole problem and is
		// not. Reusing the sorted pass's binding between draws that want the same one, and
		// collapsing draws the sort happened to leave adjacent, took a 786-sprite frame from 786
		// submissions and 786 descriptor binds to 95 and 21 -- and moved the frame time by
		// nothing, over four alternating runs. The same scene at a ninth of the pixels went from
		// 30.98 ms to 7.98 ms. This pass is FILL bound, not submission bound, and the sprites
		// cost fragments rather than draw calls.
		//
		// So the lever is overdraw, not batching: these are drawn back-to-front with depth writes
		// off, which is what lets an additive glow show through the alpha-tested sprite in front
		// of it, and also means every overlapping layer is shaded in full. The quads themselves
		// are already trimmed to their opaque borders by FMaterial's TrimBorders, so that easy
		// win is spent.
		// [rc4l] A dynamic piece gets a record too, appended behind the level's own.
		//
		// The static level owns the front of the buffer and never moves; sprites and decals are
		// rebuilt every frame into the back of it. A sprite that overflows the slack draws with the
		// last record rather than reading past the end, which is a wrong colour on one sprite rather
		// than undefined behaviour on the whole frame.
		// [rc4l] The dynamic region starts after ALL of the static records, asked fresh every frame.
		//
		// This used to be a count snapshotted when the scene was last rebuilt, and AppendPiece pushes
		// records past it -- baking the room behind a door that just opened does exactly that. The
		// sprites and decals of the next frame then wrote over those appended records, so a surface
		// that had just come into the world started rendering with some sprite's material. It reads
		// as textures changing at random when you open a door or fire a shot, which is how it was
		// reported.
		// [rc4l] A quad arrives as four CORNERS and becomes six vertices here.
		//
		// It used to arrive as six, with two of the four duplicated on the engine's side and then
		// copied again into this format -- so the duplication was paid for twice. Expanding at the
		// only place that writes SceneVertex costs the same six writes and saves the other pass.
		struct Fill
		{
			const zx::levelmesh::MeshPiece *pieces;
			const zx::levelmesh::MeshVertex *src;
			int count;
			unsigned spriteBase, matBase, generalBase, refBase, runBase, modelBase;
			void operator()(int chunk, int) const
			{
				static const int kQuadOrder[6] = { 0, 1, 2, 2, 1, 3 };
				const unsigned base = g_scene.pieces.Size();
				const unsigned lastRecord = g_scene.pieceCapacity > 0 ? g_scene.pieceCapacity - 1 : 0;
				SceneVertex *general = (SceneVertex *)&g_dynWords[0];
				for (int k = chunk * kDynChunk; k < count && k < (chunk + 1) * kDynChunk; k++)
				{
					const zx::hwrender::DynSlot &slot = layout[k];
					const zx::levelmesh::MeshPiece &p = pieces[slot.piece];
					DynRun &r = g_scene.dynRuns[runBase + k];
					r.material = p.material; r.first = refBase + slot.first; r.count = slot.count; r.blend = p.blendMode;
					r.translation = p.translation;
					r.depthBias = p.depthBias;
					r.redAlpha = p.redToAlpha;
					r.solid = p.solid;
					r.flatPlane = isFlatPlane[slot.piece] != 0;
					r.hud = p.hud;
					r.mirrorView = p.mirrorView;
					r.stackSlot = p.stackSlot;
					r.onMirror = p.onMirror;
					r.cx = p.sortX; r.cy = p.sortY; r.cz = p.sortZ;
					r.lo = 1e30f; r.hi = -1e30f;
					r.minX = r.minZ = 1e30f; r.maxX = r.maxZ = -1e30f;
					r.ticPiece = -1; r.hidden = false; r.eligible = p.gpuEligible; r.sortWho = p.sortWho;
					const AtlasEntry *ae = atlased[slot.piece];
					r.atlasPage = ae != NULL ? ae->page : -1;
					r.general = slot.sprite >= 0 ? -1 : (int)(generalBase + slot.general);
					r.userShader = zx::hwshaders::ComputeUserShaderOf((int)(filled[slot.piece].warpStyle + 0.5f));
					if (slot.sprite >= 0)
					{
						// [rc4l] A packed sprite: its words, and six references to its corners.
						const unsigned at = spriteBase + (unsigned)slot.sprite * zx::hwrender::kSpriteWords;
						memcpy(&g_dynWords[at], &packedWords[(size_t)slot.piece * zx::hwrender::kSpriteWords], zx::hwrender::kSpriteWords * sizeof(unsigned));
						for (unsigned v = 0; v < 6; v++) g_dynRefs[refBase + slot.first + v] = zx::hwrender::ComputeSpriteRef(at, kQuadOrder[v]);
						for (int c = 0; c < 4; c++)
						{
							float x, y, z;
							memcpy(&x, &g_dynWords[at + c * 4], sizeof(x));
							memcpy(&y, &g_dynWords[at + c * 4 + 1], sizeof(y));
							memcpy(&z, &g_dynWords[at + c * 4 + 2], sizeof(z));
							if (y < r.lo) r.lo = y;
							if (y > r.hi) r.hi = y;
							r.minX = std::min(r.minX, x); r.maxX = std::max(r.maxX, x);
							r.minZ = std::min(r.minZ, z); r.maxZ = std::max(r.maxZ, z);
						}
						continue;
					}
					const unsigned int dynPieceIndex = slot.record >= 0 ? base + (unsigned)slot.record : lastRecord;
					if (slot.record >= 0)
					{
						dynPieces[slot.record] = filled[slot.piece];
						dynPieces[slot.record].tintRange[3] = zx::hwrender::ComputeModelMatrixWord(matBase, p.modelMatrix);
					}
					// [Doruk-Aksoy] A GPU model surface: its record, and one reference to it for every corner, which the vertex shader tells apart by gl_VertexIndex.
					if (slot.model >= 0)
					{
						const FModelGeometry *g = p.gpuGeometry;
						const unsigned recordWord = modelBase + (unsigned)slot.model * zx::levelmesh::kModelRecordWords;
						const zx::levelmesh::ModelRecordIn in = { refBase + slot.first, dynPieceIndex, { g->gpuVertWord, g->gpuIndexWord }, g->indexed && g->indices.Size() > 0,
							p.gpuBase1, p.gpuBase2, p.gpuFirstIndex, p.gpuInter, matBase + (unsigned)p.modelMatrix * zx::hwrender::kModelMatrixWords };
						zx::levelmesh::ComputeModelRecord(in, &g_dynWords[recordWord]);
						const unsigned ref = zx::levelmesh::ComputeModelRef(recordWord);
						for (unsigned v = 0; v < slot.count; v++) g_dynRefs[refBase + slot.first + v] = ref;
						const float *b = &gpuBox[(size_t)slot.piece * 6];
						r.lo = b[1]; r.hi = b[4];
						r.minX = b[0]; r.maxX = b[3];
						r.minZ = b[2]; r.maxZ = b[5];
						r.general = -1;
						continue;
					}
					for (unsigned v = 0; v < slot.count; v++)
					{
						const unsigned srcIndex = p.quad ? (unsigned)kQuadOrder[v] : v;
						SceneVertex dv = ToSceneVertex(src[p.range.offset + srcIndex], dynPieceIndex);
						if (ae != NULL) zx::hwrender::ComputeAtlasUV(ae->x, ae->y, ae->w, ae->h, AtlasPageSize(), AtlasPageSize(), dv.u, dv.v);
						general[generalBase + slot.general + v] = dv;
						g_dynRefs[refBase + slot.first + v] = zx::hwrender::ComputeGeneralRef(generalBase + slot.general + v);
						if (dv.y < r.lo) r.lo = dv.y;
						if (dv.y > r.hi) r.hi = dv.y;
						r.minX = std::min(r.minX, dv.x); r.maxX = std::max(r.maxX, dv.x);
						r.minZ = std::min(r.minZ, dv.z); r.maxZ = std::max(r.maxZ, dv.z);
					}
				}
			}
		};
		const int chunks = (int)((layout.size() + kDynChunk - 1) / kDynChunk);
		Fill fill = { pieces, src, (int)layout.size(), spriteBase, matBase, generalBase, refBase, runBase, modelBase };
		zx::jobs::Pool().ParallelFor(chunks, fill);
		if (refs.Size() == 0) return;

		GatherTracedSprites((const SceneVertex *)&words[0], generalBase + totals.general);
		ZX_ZONE("scene/dyn_upload");   // [Doruk-Aksoy] the refs, words and records sent
		// [Doruk-Aksoy] Only the frame's part goes up again when the tic region already sits in these very buffers.
		const bool whole = ticRebuild || refs.Size() > g_dynVBCapacity;
		if (whole) { if (!UploadGrowingVB(ctx, g_dynVB, g_dynVBCapacity, &refs[0], refs.Size(), sizeof(unsigned), 1024, "fua dynamic refs")) return; }
		else if (refs.Size() > refBase) UpdateBufferInPages(ctx, g_dynVB, (unsigned long long)refBase * sizeof(unsigned), (size_t)(refs.Size() - refBase) * sizeof(unsigned), &refs[refBase]);
		// [rc4l] The words EnsureDynamicRecords made room for; a frame past them draws no dynamic piece rather than read past the buffer.
		if (!g_scene.dynDataBuf || words.Size() > g_scene.dynDataCapacity) { ZX_COUNT("gpu/dyn_words_short", "frames", 1); g_tic.built = false; return; }
		const unsigned wordsFrom = whole ? 0u : std::min(frameBase, words.Size());
		if (words.Size() > wordsFrom) UpdateBufferInPages(ctx, g_scene.dynDataBuf, (unsigned long long)wordsFrom * sizeof(unsigned), (size_t)(words.Size() - wordsFrom) * sizeof(unsigned), &words[wordsFrom]);
		g_tic.wordsBuf = g_scene.dynDataBuf.RawPtr();
		g_tic.refsBuf = g_dynVB.RawPtr();
		// ...and the records they index, into the back of the shared piece buffer.
		if (g_scene.pieceBuf && dynPieces.Size() > 0 &&
		    g_scene.pieces.Size() + dynPieces.Size() <= g_scene.pieceCapacity)
			UpdateBufferInPages(ctx, g_scene.pieceBuf, (unsigned long long)g_scene.pieces.Size() * sizeof(ScenePieceData),
				(size_t)dynPieces.Size() * sizeof(ScenePieceData), &dynPieces[0]);
	}
	{ ZX_ZONE("scene/tic_apply"); ApplyTicFrame(forced); }   // [Doruk-Aksoy] each tic sprite's hide flag and exact bounds, every frame
	if (!g_dynVB || g_scene.dynRuns.Size() == 0) return;
	g_scene.dynReady = true;
}


// [rc4l] The away-facing half of a 3D floor used to be dropped HERE, per surface, on the CPU.
//
// That cost a batch per plane -- 240 extra draws on dbab02 -- because whether a horizontal plane
// faces the camera depends on its own height, so planes at different heights cannot share one. It is
// gone: once flats are wound according to their plane's normal, ordinary back-face culling drops
// exactly the same surfaces for nothing, and disabling the CPU test changed the frame by 0.6%.
//
// The dependency is worth stating plainly: with fua_dg_cull 0 the thin-3D-floor z-fighting and the
// double-composited translucent floor both come back, because nothing else removes the far face.

// [rc4l] Everything that blends, world and sprites together, in one back-to-front pass.
//
// They cannot be two passes. A rocket box lying on the lava under a glass 3D floor has to be
// composited BEFORE the floor is; if sprites are drawn afterwards as their own pass it paints over
// the floor and appears to float on top of it, which is precisely what this looked like. The world's
// contribution is a batch in the static vertex buffer and a sprite's is a run in the dynamic one, so
// the merged list carries which buffer it came from and the pass rebinds when it crosses over --
// a few dozen draws, so the extra binds are not worth avoiding.
// [rc4l] The dynamic pieces that are SOLID, drawn with the world instead of in the sorted pass.
//
// GL draws an opaque model in the opaque pass, depth-writing, with culling off -- it only turns
// culling on for translucent ones, to paper over having no depth sort. This is that pass: the
// masked no-cull pipeline is already alpha-tested, already writes depth and already ignores winding,
// which is the exact combination a model wants and the reason it needs no pipeline of its own.
// [rc4l] A dynamic piece's binding on the per-material path: its atlas page's, else its material's.
static Diligent::IShaderResourceBinding *DynSRB(Diligent::IPipelineState *pso, const DynRun &r)
{
	Diligent::IShaderResourceBinding *page = r.atlasPage >= 0 ? AtlasSRB(pso, r.atlasPage) : NULL;
	return page != NULL ? page : GetMaterialSRB(pso, r.material, r.translation);
}

void DrawSolidDyn(Diligent::IDeviceContext *ctx)
{
	if (!g_dynVB || g_scene.dynRuns.Size() == 0 || !g_pso.world[kWpMaskedNoCull]) return;

	Diligent::IBuffer *vbs[] = { g_dynVB };
	const Diligent::Uint64 offsets[] = { 0 };
	bool started = false;
	Diligent::IPipelineState *bound = NULL;
	Diligent::IShaderResourceBinding *committed = NULL;
	zx::hwrender::LoopCommits commits = zx::hwrender::ComputeLoopCommitsStart();
	for (unsigned i = 0; i < g_scene.dynRuns.Size(); i++)
	{
		const DynRun &r = g_scene.dynRuns[i];
		if (r.count == 0 || r.hidden || !r.solid || r.blend != 0 || !zx::levelmesh::ComputeDrawsDynamicPiece(r.mirrorView, r.stackSlot, r.onMirror, g_pass.mirrorView, g_pass.stackSlot, -1)) continue;
		if (!zx::hwrender::ComputePassDrawsRun(i, g_scene.dynTicRuns, g_pass.inCameraPass)) continue;
		if (g_pass.inCameraPass && i < g_scene.dynTicRuns) g_cameraTicDraws++;
		auto *srb = WorldSRB(g_pso.world[kWpMaskedNoCull].RawPtr());
		if (srb) g_stats.dynBindless++;
		else { g_stats.dynPerMaterial++; srb = DynSRB(g_pso.world[kWpMaskedNoCull].RawPtr(), r); }
		if (!srb) continue;
		if (!started)
		{
			ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
				Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
			started = true;
		}
		// [Doruk-Aksoy] A run's own program, committed with the plain pipeline's binding (ScenePSO).
		Diligent::IPipelineState *run = ScenePSO(kWpMaskedNoCull, r.userShader);
		if (SwitchPipeline(ctx, bound, run)) committed = NULL;
		if (srb != committed) { CommitInLoop(ctx, commits, srb); committed = srb; }
		Diligent::DrawAttribs dr;
		dr.NumVertices = r.count;
		dr.StartVertexLocation = r.first;
		dr.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(dr);
		g_stats.dynDraws++;
		g_stats.dynTris += r.count / 3;
	}
}

void DrawMirrorFaceDecals(Diligent::IDeviceContext *ctx, int line)
{
	if (!g_dynVB || !g_scene.dynReady) return;
	bool started = false;
	Diligent::IPipelineState *bound = NULL;
	Diligent::IShaderResourceBinding *committed = NULL;
	zx::hwrender::LoopCommits commits = zx::hwrender::ComputeLoopCommitsStart();
	for (unsigned i = 0; i < g_scene.dynRuns.Size(); i++)
	{
		const DynRun &r = g_scene.dynRuns[i];
		if (r.count == 0 || r.hidden || !zx::levelmesh::ComputeDrawsDynamicPiece(r.mirrorView, r.stackSlot, r.onMirror, -1, -1, line)) continue;
		const int pipeline = ComputeDynamicPipeline(r.blend, r.redAlpha, r.depthBias, r.hud);
		Diligent::IPipelineState *pso = g_pso.world[pipeline].RawPtr();
		if (!pso) continue;
		Diligent::IShaderResourceBinding *srb = WorldSRB(pso);
		if (!srb) srb = DynSRB(pso, r);
		if (!srb) continue;
		if (!started)
		{
			// [rc4l] The surface left its own constants in the scene block, so the player's go back before the first decal.
			{
				Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
				WriteSceneConstants(&cb[0], nullptr, (float)PassWidth(), (float)PassHeight(),
					BindlessActive() ? (float)MaterialSlotCount() : 0.f);
			}
			Diligent::IBuffer *vbs[] = { g_dynVB };
			const Diligent::Uint64 offsets[] = { 0 };
			ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
				Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
			started = true;
		}
		Diligent::IPipelineState *run = ScenePSO(pipeline, r.userShader);
		if (SwitchPipeline(ctx, bound, run)) committed = NULL;
		if (srb != committed) { CommitInLoop(ctx, commits, srb); committed = srb; }
		Diligent::DrawAttribs dr;
		dr.NumVertices = r.count;
		dr.StartVertexLocation = r.first;
		dr.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(dr);
		g_stats.dynDraws++;
	}
}

// [rc4l] How many distinct translucent plane heights split the last translucent pass, for fua_dg_blendorder.
static int g_blendPlaneHeights = 0;

void DrawBlended(Diligent::IDeviceContext *ctx)
{
	// [rc4l] `pipeline` is its world pipeline's index, chosen once where the draw is made and the key a run of draws merges on.
	// [Doruk-Aksoy] `user` is the GLDEFS shader whose program it draws with (ScenePSO), which a run merges on too.
	struct BlendDraw { bool dyn; unsigned first, count; int who, blend, translation, pipeline, user; const void *material;
	                   Diligent::IShaderResourceBinding *srb; float dist; bool bias, red, hud;
	                   float cx, cy, cz;      // [rc4l] the centre too: distance alone cannot tell two
	                                          // marks on ONE spot from two marks on two walls
	                   float lo, hi; int layer; bool plane;   // [rc4l] height span, and its place among the translucent planes
	                   bool flatPlane; zx::hwrender::FlatFootprint area; };   // [Doruk-Aksoy] a flat plane's flag and footprint, for flat planes' order
	static TArray<BlendDraw> list;
	static TArray<const BlendDraw *> sorted;   // [Doruk-Aksoy] list's entries in draw order, pointed at rather than copied
	list.Clear();

	const float cx = FIXED2FLOAT(viewx), cy = FIXED2FLOAT(viewy), cz = FIXED2FLOAT(viewz);

	// [rc4l] A translucent surface this pass's walk did not reach is one the scene shader would move off screen, so it is left out before the layering and the sort, as GL sorts only what its walk drew.
	const int passSlot = fua_dg_blendwalk ? PassWalkSlot() : -1;
	int visWords = 0;
	const unsigned int *visBits = passSlot >= 0 ? zx::levelmesh::StackVisibility(visWords) : NULL;
	const int walkSlots = zx::levelmesh::WalkSlots();
	for (unsigned i = 0; i < g_scene.blendBatches.Size(); i++)
	{
		SceneBatch &b = g_scene.batches[g_scene.blendBatches[i]];
		if (!zx::levelmesh::ComputePassDraws(visBits, visWords, b.walkId, passSlot, walkSlots)) continue;
		// [rc4l] A blended batch with no binding is skipped whether or not bindless is on.
		//
		// Relaxing this looked harmless -- with the array carrying materials a batch needs no binding
		// of its own -- and it is how translucent geometry that the old path deliberately dropped got
		// drawn again, over the whole world, in whatever the array had at its slot.
		// [rc4l] Re-resolved first, as the opaque pass does, or a minimise dropped every translucent surface until the next upload.
		if (b.count == 0 || BatchSRB(b) == NULL) continue;
		const float dx = b.sortX - cx, dy = b.sortY - cy, dz = b.sortZ - cz;
		BlendDraw d;
		d.dyn = false; d.first = b.first; d.count = b.count; d.who = -1; d.blend = b.blend;
		d.translation = 0; d.pipeline = ComputeBatchPipeline(b.blend); d.user = b.userShader; d.material = b.material; d.srb = b.srb;
		d.bias = false; d.red = false; d.hud = false;
		d.cx = b.sortX; d.cy = b.sortY; d.cz = b.sortZ;
		d.lo = b.minY; d.hi = b.maxY;   // [rc4l] SceneVertex y is height
		d.flatPlane = false; d.area = { b.minX, b.maxX, b.minZ, b.maxZ };
		d.dist = dx*dx + dy*dy + dz*dz;
		list.Push(d);
	}

	if (g_scene.dynReady)
	{
		// [Doruk-Aksoy] The last run's pipeline, its state and its bindless binding, which the next run takes when it draws with the same look.
		struct LastLook { int blend, user; bool red, bias, hud; int pipeline; Diligent::IPipelineState *pso; Diligent::IShaderResourceBinding *srb; } last = { -1, 0, false, false, false, 0, NULL, NULL };
		for (unsigned i = 0; i < g_scene.dynRuns.Size(); i++)
		{
			const DynRun &r = g_scene.dynRuns[i];
			if (r.count == 0 || r.hidden || !zx::levelmesh::ComputeDrawsDynamicPiece(r.mirrorView, r.stackSlot, r.onMirror, g_pass.mirrorView, g_pass.stackSlot, -1)) continue;
			if (!zx::hwrender::ComputePassDrawsRun(i, g_scene.dynTicRuns, g_pass.inCameraPass)) continue;
			if (g_pass.inCameraPass && i < g_scene.dynTicRuns) g_cameraTicDraws++;
			// [rc4l] An opaque solid is drawn with the world, not in this list. See DrawSolidDyn.
			if (r.solid && r.blend == 0) continue;
			// [rc4l] blend 0 takes the TRANSLUCENT pipeline, not the masked one.
			//
			// An alpha-1 sprite blends identically to an opaque draw and discards its transparent
			// texels just the same, but it does not write depth -- which is the whole point. GL's
			// sprites live in one sorted list with depth writes off, and splitting them into an
			// opaque depth-writing pass let an alpha-tested impact sprite occlude the additive glow
			// behind it: black holes where a plasma burst's bright core should be.
			if (r.blend != last.blend || r.userShader != last.user || r.redAlpha != last.red || r.depthBias != last.bias || r.hud != last.hud)
			{
				last.blend = r.blend; last.user = r.userShader; last.red = r.redAlpha; last.bias = r.depthBias; last.hud = r.hud;
				last.pipeline = zx::hwrender::ComputePremulPipeline(ComputeDynamicPipeline(r.blend, r.redAlpha, r.depthBias, r.hud), fua_dg_premulblend, r.userShader);
				last.pso = g_pso.world[last.pipeline].RawPtr();
				last.srb = WorldSRB(last.pso);
			}
			const int pipeline = last.pipeline;
			Diligent::IPipelineState *pso = last.pso;
			auto *srb = last.srb;
			if (srb) g_stats.dynBindless++; else { g_stats.dynPerMaterial++; srb = DynSRB(pso, r); }
			if (r.atlasPage >= 0) g_stats.dynAtlas++;
			if (!srb) continue;
			BlendDraw d;
			d.dyn = true; d.first = r.first; d.count = r.count; d.who = r.sortWho; d.blend = r.blend;
			d.translation = r.translation; d.pipeline = pipeline; d.user = r.userShader; d.material = r.material; d.srb = srb;
			d.bias = r.depthBias;
			d.red = r.redAlpha;
			d.hud = r.hud;
			const float dx = r.cx - cx, dy = r.cy - cy, dz = r.cz - cz;
			d.cx = r.cx; d.cy = r.cy; d.cz = r.cz;
			d.lo = r.lo; d.hi = r.hi;
			d.flatPlane = r.flatPlane; d.area = { r.minX, r.maxX, r.minZ, r.maxZ };
			d.dist = dx*dx + dy*dy + dz*dz;
			list.Push(d);
			g_stats.dynDraws++;
			g_stats.dynTris += r.count / 3;
			if (r.blend >= 0 && r.blend < 4) g_stats.dynByBlend[r.blend]++;
		}
	}

	if (list.Size() == 0) return;

	// [rc4l] Which translucent level planes stand between each draw and the eye, as GL's sort splits on them.
	{
		static TArray<zx::hwrender::BlendSpan> spans;
		static TArray<int> layers;
		static TArray<bool> planes;
		spans.Resize(list.Size());
		layers.Resize(list.Size());
		planes.Resize(list.Size());
		for (unsigned i = 0; i < list.Size(); i++)
		{
			spans[i].lo = list[i].lo; spans[i].hi = list[i].hi; spans[i].surface = !list[i].dyn;
		}
		g_blendPlaneHeights = zx::hwrender::ComputeBlendLayers(&spans[0], (int)list.Size(), cz, &layers[0], &planes[0]);
		for (unsigned i = 0; i < list.Size(); i++) { list[i].layer = layers[i]; list[i].plane = planes[i]; }
	}

	// [Doruk-Aksoy] A flat blended model surface goes behind the sprites standing on it, and what lies beyond it behind it, as a floor or ceiling would (flatplane_compute).
	// [Doruk-Aksoy] Only built when a flat plane is among them, since without one the order leaves every distance as it was.
	bool anyPlane = false;
	for (unsigned i = 0; i < list.Size() && !anyPlane; i++) anyPlane = list[i].dyn && !list[i].bias && !list[i].hud && list[i].flatPlane;
	if (anyPlane)
	{
		static std::vector<zx::hwrender::FlatOrderItem> flat;
		static std::vector<unsigned> flatOf;
		flat.clear(); flatOf.clear();
		for (unsigned i = 0; i < list.Size(); i++)
		{
			const BlendDraw &d = list[i];
			if (!d.dyn || d.bias || d.hud) continue;
			zx::hwrender::FlatOrderItem it;
			it.dist = d.dist; it.lo = d.lo; it.hi = d.hi; it.area = d.area; it.plane = d.flatPlane;
			flat.push_back(it);
			flatOf.push_back(i);
		}
		{
			ZX_ZONE("scene/flat_order");
			static zx::hwrender::FlatOrderScratch scratch;
			zx::hwrender::ComputeFlatPlaneOrder(flat.data(), (int)flat.size(), cz, scratch);
			for (size_t k = 0; k < flat.size(); k++) list[flatOf[k]].dist = flat[k].dist;
		}
	}

	// [rc4l] Farthest first, and DETERMINISTIC on a tie.
	//
	// std::sort is not stable, so two draws at the same distance -- a decal and the one a template
	// puts underneath it, landing at the same point -- traded places between frames and flickered
	// through each other. Falling back to the buffer offset makes equal distances resolve the same
	// way every frame.
	// [rc4l] The rule itself lives in surfaceorder_compute, where it is unit-tested.
	//
	// Three layering faults shipped from this comparator -- a scorch over its own glow, a scorch over
	// the impact flash, a decal over a sprite -- and each was fixed by aiming another epsilon at the
	// pair that happened to be on screen. None could be checked without playing the game, because a
	// picture cannot say which of two draws landed second. As a function over stated inputs it can:
	// the rule that required two distances to be EXACTLY equal, and therefore never once fired, fails
	// a test written with the real numbers off a real wall.
	{
		// [rc4l] Sorted as 128-bit keys holding the rule, and the draws then moved once.
		static std::vector<zx::hwrender::TranslucentOrder> keys, keysMoved;
		static std::vector<zx::hwrender::TranslucentHalf> halves, halfScratch;
		keys.resize(list.Size());
		for (unsigned i = 0; i < list.Size(); i++)
		{
			const BlendDraw &d = list[i];
			zx::hwrender::TranslucentDraw k;
			k.distSq = d.dist; k.blend = d.blend; k.decal = d.bias; k.first = d.first; k.who = d.who;
			k.layer = d.layer; k.plane = d.plane;
			keys[i].key = zx::hwrender::ComputeTranslucentSortKey(k);
			keys[i].index = i;
		}
		// [Doruk-Aksoy] A stable radix sort, so equal keys, which the vertex offset ending each key makes rare, keep their place in the list rather than any std::sort chose.
		{
			ZX_ZONE("scene/blend_sort");
			ZX_COUNT("scene/blends_sorted", "draws", keys.size());
			zx::hwrender::ComputeSortTranslucentOrder(keys, halves, halfScratch, keysMoved);
		}
		sorted.Resize(list.Size());
		for (unsigned i = 0; i < keys.size(); i++) sorted[i] = &list[keys[i].index];
	}

	if (g_dumpBlendOrder && g_pass.mainView)
	{
		g_dumpBlendOrder = false;
		// [rc4l] Why static translucent batches were left out, so a pass that lost them all says which test dropped them.
		int empty = 0, unbound = 0;
		for (unsigned i = 0; i < g_scene.blendBatches.Size(); i++)
		{
			const SceneBatch &b = g_scene.batches[g_scene.blendBatches[i]];
			if (b.count == 0) empty++;
			else if (b.srb == NULL) unbound++;
		}
		Printf( "%u blended batches: %d empty, %d with no binding (trans PSO %s, add PSO %s)\n", g_scene.blendBatches.Size(),
			empty, unbound, g_pso.world[kWpTrans] ? "ok" : "NULL", g_pso.world[kWpAdd] ? "ok" : "NULL" );
		Printf( "%d translucent plane heights split the order\n", g_blendPlaneHeights );
		Printf( "translucent pass, in draw order (first drawn = furthest back):\n" );
		const unsigned from = (sorted.Size() > 40 && !g_dumpBlendAtZ) ? sorted.Size() - 40 : 0;   // [rc4l] the NEAREST 40; the far end is scenery
		for (unsigned i = from; i < sorted.Size(); i++)
		{
			const BlendDraw &d = *sorted[i];
			if (g_dumpBlendAtZ && (d.lo > g_dumpBlendZ || d.hi < g_dumpBlendZ)) continue;
			Printf( "  %2u  dist %9.0f  %s  blend %d%s%s  layer %d%s  z %.0f..%.0f  verts %u at %u\n", i, d.dist,
				d.dyn ? "dyn " : "batch", d.blend,
				d.bias ? "  DECAL" : "", d.red ? " redToAlpha" : "", d.layer, d.plane ? " PLANE" : "",
				d.lo, d.hi, d.count, d.first );
		}
		Printf( "  %u draws total\n", sorted.Size() );
		// [Doruk-Aksoy] The pipeline switches the loop below makes over this order, by pair, the cost a merge of two pipelines would save.
		std::vector<zx::hwrender::PipelineKey> keys;
		for (unsigned i = 0; i < sorted.Size(); i++) keys.push_back({ sorted[i]->pipeline, sorted[i]->user });
		const std::vector<zx::hwrender::PipelineSwitchCount> tally = zx::hwrender::ComputePipelineSwitches(keys);
		Printf( "  %d pipeline switches over them, most first:\n", zx::hwrender::ComputeSwitchTotal(tally) );
		for (size_t i = 0; i < tally.size() && i < 8; i++)
			Printf( "    %5d  %s > %s\n", tally[i].count, zx::hwrender::ComputeWorldPipelineDesc(tally[i].from).name,
				zx::hwrender::ComputeWorldPipelineDesc(tally[i].to).name );
	}

	// [rc4l] The sorted order, written once into indices -- see g_blendIB.
	{
		unsigned int total = 0;
		for (unsigned i = 0; i < sorted.Size(); i++) total += sorted[i]->count;
		g_blendIndices.Clear();
		if (total > 0)
		{
			g_blendIndices.Reserve(total);
			unsigned int w = 0;
			for (unsigned i = 0; i < sorted.Size(); i++)
			{
				const BlendDraw &d = *sorted[i];
				for (unsigned k = 0; k < d.count; k++) g_blendIndices[w++] = (Diligent::Uint32)(d.first + k);
			}
			if (EnsureBlendIndexBuffer(GetDevice(), total))
			{
				UpdateBufferInPages(ctx, g_blendIB, 0, (size_t)total * sizeof(Diligent::Uint32), &g_blendIndices[0]);
				ctx->SetIndexBuffer(g_blendIB, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			}
		}
	}
	// Where each entry's indices start, walked alongside the list below.
	unsigned int idxCursor = 0;

	Diligent::IPipelineState *bound = NULL;
	// [rc4l] What is CURRENTLY committed, so the same binding is not committed again per draw.
	//
	// The pass sorts by distance so it cannot batch by material -- but with bindless there is ONE
	// binding per pipeline for the whole level, so consecutive draws in the sorted list almost always
	// want the one already there. Committing per draw costs a descriptor bind per sprite.
	//
	// This was tried once and reverted for measuring nothing, on a scene that turned out to be fill
	// bound: 786 stacked lamps at full screen, where the fragments swamped everything. On Sunder MAP10
	// with GL cut out the balance is the other way -- a ninth of the pixels still leaves 2.9 ms -- and
	// the same change is worth having. The lesson kept is that this must be measured on a CPU-bound
	// frame, because on a fill-bound one it is invisible.
	Diligent::IShaderResourceBinding *boundSRB = NULL;
	zx::hwrender::LoopCommits commits = zx::hwrender::ComputeLoopCommitsStart();
	int boundVB = -1;
	const Diligent::Uint64 offsets[] = { 0 };
	for (unsigned i = 0; i < sorted.Size(); i++)
	{
		const BlendDraw &d = *sorted[i];
		const int want = d.dyn ? 1 : 0;
		if (want != boundVB)
		{
			Diligent::IBuffer *vbs[] = { d.dyn ? g_dynVB.RawPtr() : g_scene.vb.RawPtr() };
			if (!vbs[0]) continue;
			ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
				Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
			boundVB = want;
		}
		Diligent::IPipelineState *pso = g_pso.world[d.pipeline].RawPtr();
		if (!pso) { idxCursor += d.count; continue; }
		// [Doruk-Aksoy] The draw's own program, committed with the plain pipeline's binding (ScenePSO).
		Diligent::IPipelineState *run = ScenePSO(d.pipeline, d.user);
		if (SwitchPipeline(ctx, bound, run)) boundSRB = NULL;
		// The pipeline is only known here -- the pass sorts by distance, not by material -- so this is
		// where the per-pipeline binding can be asked for.
		Diligent::IShaderResourceBinding *const worldSrb = WorldSRB(pso);
		Diligent::IShaderResourceBinding *srb = worldSrb != NULL ? worldSrb : d.srb;
		if (!srb) { idxCursor += d.count; continue; }
		if (srb != boundSRB)
		{
			CommitInLoop(ctx, commits, srb);
			boundSRB = srb;
		}

		// [rc4l] ...and every entry after this one that wants the same state joins the same draw.
		//
		// They do not have to be adjacent in the VERTEX buffer any more -- the index buffer already
		// holds them in sorted order, so a run is contiguous in indices by construction. Nothing is
		// reordered: the run stops at the first entry that wants a different pipeline or binding, and
		// within the run the order is the sort's.
		const unsigned int idxStart = idxCursor;
		unsigned int idxCount = d.count;
		idxCursor += d.count;
		while (i + 1 < sorted.Size())
		{
			const BlendDraw &n = *sorted[i + 1];
			if ((n.dyn ? 1 : 0) != want) break;
			if (n.pipeline != d.pipeline || n.user != d.user) break;
			// Without bindless the binding IS the material, so it has to match too.
			if (worldSrb == NULL && n.srb != srb) break;
			idxCount += n.count;
			idxCursor += n.count;
			i++;
		}

		if (g_blendIB && g_blendIndices.Size() > 0)
		{
			Diligent::DrawIndexedAttribs draw;
			draw.NumIndices = idxCount;
			draw.FirstIndexLocation = idxStart;
			draw.IndexType = Diligent::VT_UINT32;
			draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
			ctx->DrawIndexed(draw);
		}
		else
		{
			// No index buffer this frame: fall back to the span the entry already names.
			Diligent::DrawAttribs draw;
			draw.NumVertices = d.count;
			draw.StartVertexLocation = d.first;
			draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
			ctx->Draw(draw);
		}
		g_stats.blendSubmits++;
	}
}

}} // namespace zx::hwrender
