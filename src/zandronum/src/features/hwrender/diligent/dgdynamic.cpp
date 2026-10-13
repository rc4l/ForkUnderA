// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The per-frame stream: sprites, models and decals, the solid ones drawn after the world and the translucent ones sorted into one pass with the world's.

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/computation/dynlayout_compute.h"   // [rc4l] ComputeDynLayout
#include "features/jobs/jobs.h"                                 // [rc4l] the pool the records are written on

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
CCMD( fua_dg_blendorder )
{
	g_dumpBlendOrder = true;
	g_dumpBlendAtZ = argv.argc( ) > 1;
	g_dumpBlendZ = g_dumpBlendAtZ ? (float)atof( argv[1] ) : 0.f;
	Printf( "fua_dg_blendorder: dumping the next translucent pass\n" );
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
	if (g_blendIB && g_blendIBCapacity >= needed) return true;
	// Grown with slack, because a frame that needs one more index than the last must not reallocate.
	unsigned int cap = g_blendIBCapacity ? g_blendIBCapacity : 4096;
	while (cap < needed) cap *= 2;
	g_blendIB.Release();
	Diligent::BufferDesc bd;
	bd.Name = "fua blend order";
	bd.Size = (Diligent::Uint64)cap * sizeof(Diligent::Uint32);
	bd.BindFlags = Diligent::BIND_INDEX_BUFFER;
	bd.Usage = Diligent::USAGE_DEFAULT;
	dev->CreateBuffer(bd, NULL, &g_blendIB);
	if (!g_blendIB) { g_blendIBCapacity = 0; return false; }
	g_blendIBCapacity = cap;
	return true;
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

void BuildDynamic(Diligent::IDeviceContext *ctx)
{
	cycle_t dynClock;
	dynClock.Reset();
	dynClock.Clock();
	struct ClockStop { cycle_t &c; ~ClockStop() { c.Unclock(); g_stats.dynBuildMs = c.TimeMS(); } } stopper = { dynClock };
	g_stats.dynDraws = g_stats.dynTris = 0;
	g_stats.blendSubmits = 0;
	g_stats.dynSlotRefused = g_stats.dynSlotNoMaterial = g_stats.dynSlotSeen = g_stats.dynSlotMax = 0;
	g_stats.dynSlotUnfilled = 0;
	g_stats.dynBindless = g_stats.dynPerMaterial = 0;
	g_stats.dynByBlend[0] = g_stats.dynByBlend[1] = g_stats.dynByBlend[2] = g_stats.dynByBlend[3] = 0;
	g_scene.dynReady = false;

	int nverts = 0, npieces = 0;
	const zx::levelmesh::MeshVertex *src = zx::levelmesh::DynVertices(nverts);
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::DynPieces(npieces);
	if (src == NULL || nverts <= 0 || pieces == NULL || npieces <= 0) { g_scene.dynRuns.Clear(); return; }

	static TArray<SceneVertex> vb;

	// The dynamic path's piece records, rebuilt with its vertices every frame.

	static TArray<ScenePieceData> dynPieces;
	TArray<DynRun> &runs = g_scene.dynRuns;

	// [rc4l] Rebuild and re-upload ONCE per engine frame, not once per draw call.
	//
	// DrawSceneOnce is called many times between frame boundaries by the benchmark, and Vulkan's
	// dynamic heap is only recycled when a frame completes -- so mapping here unconditionally
	// exhausted an 8 MB heap after a few hundred draws ("Space in dynamic heap is exhausted").
	static unsigned int builtGen = 0;
	const unsigned int gen = zx::levelmesh::DynGeneration();
	if (gen != builtGen)
	{
		builtGen = gen;

		// [rc4l] Order decides correctness here, not just batching.
		//
		// Opaque pieces first, grouped by material so they batch. Translucent pieces after, sorted
		// BACK TO FRONT by distance from the camera -- blending is not commutative, so two overlapping
		// translucent sprites drawn in the wrong order composite wrongly. Within the translucent set
		// the sort beats batching, so material grouping is deliberately given up there.
		static TArray<int> order;
		order.Clear();
		for (int i = 0; i < npieces; i++) order.Push(i);

		const float cx = FIXED2FLOAT(viewx), cy = FIXED2FLOAT(viewy), cz = FIXED2FLOAT(viewz);
		static TArray<float> key;
		key.Clear();
		key.Resize(npieces);
		for (int i = 0; i < npieces; i++)
		{
			const zx::levelmesh::MeshPiece &p = pieces[i];
			const float dx = p.sortX - cx, dy = p.sortY - cy, dz = p.sortZ - cz;
			key[i] = dx*dx + dy*dy + dz*dz;
		}
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
		// `key` has static storage, which a lambda cannot capture; the pointer can.
		const float *keyp = (npieces > 0) ? &key[0] : NULL;
		std::sort(&order[0], &order[0] + npieces,
			[pieces, keyp](int ia, int ib) {
				const zx::levelmesh::MeshPiece &pa = pieces[ia];
				const zx::levelmesh::MeshPiece &pb = pieces[ib];
				const bool ta = pa.blendMode != 0, tb = pb.blendMode != 0;
				if (ta != tb) return !ta;                             // opaque before blended
				if (!ta)
				{
					if (pa.material != pb.material) return pa.material < pb.material;
					return ia < ib;
				}
				if (keyp[ia] != keyp[ib]) return keyp[ia] > keyp[ib]; // blended: far first
				return ia < ib;
			});

		// [rc4l] Laid out first, and every slot found, on this thread; then the records and vertices are written on the pool, each piece into its own place.
		static TArray<unsigned> dynVerts;
		dynVerts.Resize(npieces);
		for (int i = 0; i < npieces; i++) dynVerts[i] = pieces[i].range.count == 0 ? 0u : pieces[i].quad ? 6u : pieces[i].range.count;
		const int recordsLeft = g_scene.pieceCapacity > g_scene.pieces.Size() ? int(g_scene.pieceCapacity - g_scene.pieces.Size()) : 0;
		static std::vector<zx::hwrender::DynSlot> layout;
		const unsigned nv = zx::hwrender::ComputeDynLayout(&order[0], npieces, &dynVerts[0], recordsLeft, layout);
		static std::vector<PieceSlotSet> slots;
		slots.resize(layout.size());
		g_stats.dynMirrorRuns = 0;
		int records = 0;
		for (size_t k = 0; k < layout.size(); k++)
		{
			const zx::levelmesh::MeshPiece &p = pieces[layout[k].piece];
			slots[k] = PieceSlots(p, p.translation, false);
			if (p.mirrorLine >= 0) g_stats.dynMirrorRuns++;
			if (layout[k].record >= 0) records++;
		}
		vb.Resize(nv);
		runs.Resize((unsigned)layout.size());
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
			std::vector<int> *unfilled;
			void operator()(int chunk, int) const
			{
				static const int kQuadOrder[6] = { 0, 1, 2, 2, 1, 3 };
				const unsigned base = g_scene.pieces.Size();
				const unsigned lastRecord = g_scene.pieceCapacity > 0 ? g_scene.pieceCapacity - 1 : 0;
				int missed = 0;
				for (int k = chunk * kDynChunk; k < count && k < (chunk + 1) * kDynChunk; k++)
				{
					const zx::hwrender::DynSlot &slot = layout[k];
					const zx::levelmesh::MeshPiece &p = pieces[slot.piece];
					DynRun &r = g_scene.dynRuns[k];
					r.material = p.material; r.first = slot.first; r.count = slot.count; r.blend = p.blendMode;
					r.translation = p.translation;
					r.depthBias = p.depthBias;
					r.redAlpha = p.redToAlpha;
					r.solid = p.solid;
					r.mirrorLine = p.mirrorLine;
					r.stackSlot = p.stackSlot;
					r.cx = p.sortX; r.cy = p.sortY; r.cz = p.sortZ;
					r.lo = 1e30f; r.hi = -1e30f;
					const unsigned int dynPieceIndex = slot.record >= 0 ? base + (unsigned)slot.record : lastRecord;
					if (slot.record >= 0)
					{
						ScenePieceData &pd = dynPieces[slot.record];
						FillPieceDataWith(p, slots[k], pd, false);
						if (!fua_dg_bindless_dyn) pd.matSlot = pd.brightSlot = 0.f;   // back to the bound texture and brightmap
						else if (BindlessActive()) missed += zx::hwrender::ComputeSlotsUnfilled((int)pd.matSlot, (int)pd.brightSlot, (int)FilledSlotCount());
					}
					for (unsigned v = 0; v < slot.count; v++)
					{
						const unsigned srcIndex = p.quad ? (unsigned)kQuadOrder[v] : v;
						const SceneVertex dv = ToSceneVertex(src[p.range.offset + srcIndex], dynPieceIndex);
						vb[slot.first + v] = dv;
						if (dv.y < r.lo) r.lo = dv.y;
						if (dv.y > r.hi) r.hi = dv.y;
					}
				}
				(*unfilled)[chunk] = missed;
			}
		};
		static std::vector<int> unfilled;
		const int chunks = (int)((layout.size() + kDynChunk - 1) / kDynChunk);
		unfilled.assign(chunks, 0);
		Fill fill = { pieces, src, (int)layout.size(), &unfilled };
		zx::jobs::Pool().ParallelFor(chunks, fill);
		for (int c = 0; c < chunks; c++)
		{
			g_stats.dynSlotUnfilled += unfilled[c];
			g_stats.dynSlotUnfilledTotal += unfilled[c];
		}
	if (vb.Size() == 0) return;

		if (!UploadGrowingVB(ctx, g_dynVB, g_dynVBCapacity, &vb[0], vb.Size(), sizeof(SceneVertex), 1024, "fua dynamic VB")) return;
		// ...and the records they index, into the back of the shared piece buffer.
		if (g_scene.pieceBuf && dynPieces.Size() > 0 &&
		    g_scene.pieces.Size() + dynPieces.Size() <= g_scene.pieceCapacity)
			ctx->UpdateBuffer(g_scene.pieceBuf,
				(Diligent::Uint64)g_scene.pieces.Size() * sizeof(ScenePieceData),
				(Diligent::Uint64)dynPieces.Size() * sizeof(ScenePieceData), &dynPieces[0],
				Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	if (vb.Size() == 0 || !g_dynVB) return;
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
void DrawSolidDyn(Diligent::IDeviceContext *ctx)
{
	if (!g_dynVB || g_scene.dynRuns.Size() == 0 || !g_pso.world[kWpMaskedNoCull]) return;

	Diligent::IBuffer *vbs[] = { g_dynVB };
	const Diligent::Uint64 offsets[] = { 0 };
	bool bound = false;
	Diligent::IShaderResourceBinding *committed = NULL;
	zx::hwrender::LoopCommits commits = zx::hwrender::ComputeLoopCommitsStart();
	for (unsigned i = 0; i < g_scene.dynRuns.Size(); i++)
	{
		const DynRun &r = g_scene.dynRuns[i];
		if (r.count == 0 || !r.solid || r.blend != 0 || !zx::levelmesh::ComputeDrawsDynamicPiece(r.mirrorLine, r.stackSlot, g_pass.lineMirror, g_pass.stackSlot)) continue;
		auto *srb = WorldSRB(g_pso.world[kWpMaskedNoCull].RawPtr());
		if (srb) g_stats.dynBindless++;
		else { g_stats.dynPerMaterial++; srb = GetMaterialSRB(g_pso.world[kWpMaskedNoCull].RawPtr(), r.material, r.translation); }
		if (!srb) continue;
		if (!bound)
		{
			ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
				Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
			ctx->SetPipelineState(g_pso.world[kWpMaskedNoCull]);
			bound = true;
		}
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

// [rc4l] How many distinct translucent plane heights split the last translucent pass, for fua_dg_blendorder.
static int g_blendPlaneHeights = 0;

void DrawBlended(Diligent::IDeviceContext *ctx)
{
	struct BlendDraw { bool dyn; unsigned first, count; int blend, translation; const void *material;
	                   Diligent::IShaderResourceBinding *srb; float dist; bool bias, red;
	                   float cx, cy, cz;      // [rc4l] the centre too: distance alone cannot tell two
	                                          // marks on ONE spot from two marks on two walls
	                   float lo, hi; int layer; bool plane; };   // [rc4l] height span, and its place among the translucent planes
	static TArray<BlendDraw> list;
	list.Clear();

	const float cx = FIXED2FLOAT(viewx), cy = FIXED2FLOAT(viewy), cz = FIXED2FLOAT(viewz);

	for (unsigned i = 0; i < g_scene.blendBatches.Size(); i++)
	{
		SceneBatch &b = g_scene.batches[g_scene.blendBatches[i]];
		// [rc4l] A blended batch with no binding is skipped whether or not bindless is on.
		//
		// Relaxing this looked harmless -- with the array carrying materials a batch needs no binding
		// of its own -- and it is how translucent geometry that the old path deliberately dropped got
		// drawn again, over the whole world, in whatever the array had at its slot.
		// [rc4l] Re-resolved first, as the opaque pass does, or a minimise dropped every translucent surface until the next upload.
		if (b.count == 0 || BatchSRB(b) == NULL) continue;
		const float dx = b.sortX - cx, dy = b.sortY - cy, dz = b.sortZ - cz;
		BlendDraw d;
		d.dyn = false; d.first = b.first; d.count = b.count; d.blend = b.blend;
		d.translation = 0; d.material = b.material; d.srb = b.srb;
		d.bias = false; d.red = false;
		d.cx = b.sortX; d.cy = b.sortY; d.cz = b.sortZ;
		d.lo = b.minY; d.hi = b.maxY;   // [rc4l] SceneVertex y is height
		d.dist = dx*dx + dy*dy + dz*dz;
		list.Push(d);
	}

	if (g_scene.dynReady)
	{
		for (unsigned i = 0; i < g_scene.dynRuns.Size(); i++)
		{
			const DynRun &r = g_scene.dynRuns[i];
			if (r.count == 0 || !zx::levelmesh::ComputeDrawsDynamicPiece(r.mirrorLine, r.stackSlot, g_pass.lineMirror, g_pass.stackSlot)) continue;
			// [rc4l] An opaque solid is drawn with the world, not in this list. See DrawSolidDyn.
			if (r.solid && r.blend == 0) continue;
			// Blend mode 3 is the fuzz shadow; the engine draws it as a dark near-opaque overlay, and
			// normal translucency is a fair stand-in until the fuzz shaders are ported.
			// [rc4l] blend 0 takes the TRANSLUCENT pipeline, not the masked one.
			//
			// An alpha-1 sprite blends identically to an opaque draw and discards its transparent
			// texels just the same, but it does not write depth -- which is the whole point. GL's
			// sprites live in one sorted list with depth writes off, and splitting them into an
			// opaque depth-writing pass let an alpha-tested impact sprite occlude the additive glow
			// behind it: black holes where a plasma burst's bright core should be.
			Diligent::IPipelineState *pso = g_pso.world[ComputeDynamicPipeline(r.blend, r.redAlpha, r.depthBias)].RawPtr();
			auto *srb = WorldSRB(pso);
			if (srb) g_stats.dynBindless++; else { g_stats.dynPerMaterial++; srb = GetMaterialSRB(pso, r.material, r.translation); }
			if (!srb) continue;
			BlendDraw d;
			d.dyn = true; d.first = r.first; d.count = r.count; d.blend = r.blend;
			d.translation = r.translation; d.material = r.material; d.srb = srb;
			d.bias = r.depthBias;
			d.red = r.redAlpha;
			const float dx = r.cx - cx, dy = r.cy - cy, dz = r.cz - cz;
			d.cx = r.cx; d.cy = r.cy; d.cz = r.cz;
			d.lo = r.lo; d.hi = r.hi;
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
		struct SortKey { zx::hwrender::TranslucentSortKey key; unsigned index; };
		static TArray<SortKey> keys;
		static TArray<BlendDraw> sorted;
		keys.Resize(list.Size());
		for (unsigned i = 0; i < list.Size(); i++)
		{
			const BlendDraw &d = list[i];
			zx::hwrender::TranslucentDraw k;
			k.distSq = d.dist; k.blend = d.blend; k.decal = d.bias; k.first = d.first;
			k.layer = d.layer; k.plane = d.plane;
			keys[i].key = zx::hwrender::ComputeTranslucentSortKey(k);
			keys[i].index = i;
		}
		std::sort(&keys[0], &keys[0] + keys.Size(), [](const SortKey &a, const SortKey &b) { return a.key < b.key; });
		sorted.Resize(list.Size());
		for (unsigned i = 0; i < keys.Size(); i++) sorted[i] = list[keys[i].index];
		for (unsigned i = 0; i < sorted.Size(); i++) list[i] = sorted[i];
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
		const unsigned from = (list.Size() > 40 && !g_dumpBlendAtZ) ? list.Size() - 40 : 0;   // [rc4l] the NEAREST 40; the far end is scenery
		for (unsigned i = from; i < list.Size(); i++)
		{
			const BlendDraw &d = list[i];
			if (g_dumpBlendAtZ && (d.lo > g_dumpBlendZ || d.hi < g_dumpBlendZ)) continue;
			Printf( "  %2u  dist %9.0f  %s  blend %d%s%s  layer %d%s  z %.0f..%.0f  verts %u at %u\n", i, d.dist,
				d.dyn ? "dyn " : "batch", d.blend,
				d.bias ? "  DECAL" : "", d.red ? " redToAlpha" : "", d.layer, d.plane ? " PLANE" : "",
				d.lo, d.hi, d.count, d.first );
		}
		Printf( "  %u draws total\n", list.Size() );
	}

	// [rc4l] The sorted order, written once into indices -- see g_blendIB.
	{
		unsigned int total = 0;
		for (unsigned i = 0; i < list.Size(); i++) total += list[i].count;
		g_blendIndices.Clear();
		if (total > 0)
		{
			g_blendIndices.Reserve(total);
			unsigned int w = 0;
			for (unsigned i = 0; i < list.Size(); i++)
			{
				const BlendDraw &d = list[i];
				for (unsigned k = 0; k < d.count; k++) g_blendIndices[w++] = (Diligent::Uint32)(d.first + k);
			}
			if (EnsureBlendIndexBuffer(GetDevice(), total))
			{
				ctx->UpdateBuffer(g_blendIB, 0, (Diligent::Uint64)total * sizeof(Diligent::Uint32),
					&g_blendIndices[0], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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
	// bound: 786 stacked lamps at 1920x1200, where the fragments swamped everything. On Sunder MAP10
	// with GL cut out the balance is the other way -- a ninth of the pixels still leaves 2.9 ms -- and
	// the same change is worth having. The lesson kept is that this must be measured on a CPU-bound
	// frame, because on a fill-bound one it is invisible.
	Diligent::IShaderResourceBinding *boundSRB = NULL;
	zx::hwrender::LoopCommits commits = zx::hwrender::ComputeLoopCommitsStart();
	int boundVB = -1;
	const Diligent::Uint64 offsets[] = { 0 };
	for (unsigned i = 0; i < list.Size(); i++)
	{
		const BlendDraw &d = list[i];
		const int want = d.dyn ? 1 : 0;
		if (want != boundVB)
		{
			Diligent::IBuffer *vbs[] = { d.dyn ? g_dynVB.RawPtr() : g_scene.vb.RawPtr() };
			if (!vbs[0]) continue;
			ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
				Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
			boundVB = want;
		}
		Diligent::IPipelineState *pso = !d.dyn
			? BatchPSO(d.blend)
			: g_pso.world[ComputeDynamicPipeline(d.blend, d.red, d.bias)].RawPtr();
		if (!pso) { idxCursor += d.count; continue; }
		if (pso != bound) { ctx->SetPipelineState(pso); bound = pso; boundSRB = NULL; }
		// The pipeline is only known here -- the pass sorts by distance, not by material -- so this is
		// where the per-pipeline binding can be asked for.
		Diligent::IShaderResourceBinding *srb = WorldSRB(pso);
		if (!srb) srb = d.srb;
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
		while (i + 1 < list.Size())
		{
			const BlendDraw &n = list[i + 1];
			if ((n.dyn ? 1 : 0) != want) break;
			if (n.blend != d.blend || n.red != d.red || n.bias != d.bias) break;
			// Without bindless the binding IS the material, so it has to match too.
			if (!WorldSRB(pso) && n.srb != srb) break;
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
