// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The scene buffer: built from the level mesh sorted by material, and patched in place as pieces move, repaint or appear.

#include "features/hwshaders/computation/hwshader_compute.h"   // [rc4l] ComputePieceShader
#include "features/hwrender/computation/growbuffer_compute.h"   // [rc4l] ComputePieceCapacity
#include "features/telemetry/telemetry.h"
#include <string.h>   // [rc4l] memcmp
#include "features/hwrender/computation/dynlayout_compute.h"   // [rc4l] ComputeDynWordsBound
#include "features/hwrender/computation/sectorlitrecord_compute.h"   // [rc4l] ComputeSectorLitRecord
#include "features/hwrender/computation/uploadguard_compute.h"   // [rc4l] ComputeChangedRuns
#include "features/levelmesh/captureshading.h"   // [rc4l] SectorLightEntryOf
#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/decals/decals.h"   // [rc4l] SurfaceRefusesMarks, the G-buffer's no-marks bit
#include "r_state.h"   // [rc4l] sides, numsides, numsubsectors
#include "c_dispatch.h"   // [rc4l] fua_dg_sectorlit_report
#include "features/hwrender/computation/texflags_compute.h"   // [rc4l] kTexFlagSectorLit

namespace zx { namespace hwrender {

// [rc4l] The material and brightmap slots a piece samples, made if it has none yet; a baked piece on an animated texture keys them on its untranslated base, which SlotRemap maps to the frame showing, since the bake froze whichever frame it saw.
PieceSlotSet PieceSlots(const zx::levelmesh::MeshPiece &p, int translation, bool inMesh)
{
	const void *base = inMesh ? zx::materials::MaterialForTexture(p.baseTex) : NULL;
	PieceSlotSet out;
	out.animates = AnimationOf(base) >= 0;
	const void *slotMat = out.animates ? base : p.material;
	zx::materials::MaterialInfo info;
	const bool described = zx::materials::DescribeMaterial(p.material, info);
	out.mat = (float)MaterialSlotFor(slotMat, translation);
	// [rc4l] An animated one always takes a brightmap slot, since another frame may have a brightmap where this one has none.
	out.bright = (out.animates || (described && info.brightmap)) ? (float)MaterialSlotFor(slotMat, kBrightmapTranslation) : 0.f;
	return out;
}

// [rc4l] This frame's sprites, models and decals given their slots before RefreshBindless fills the array, since BuildDynamic runs after it and a slot first made there sampled the white placeholder for a frame.
void ReserveDynamicSlots()
{
	static unsigned int reservedGen = 0;
	const unsigned int gen = zx::levelmesh::DynGeneration();
	if (gen == reservedGen) return;
	reservedGen = gen;
	int npieces = 0;
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::DynPieces(npieces);
	for (int i = 0; i < npieces; i++) PieceSlots(pieces[i], pieces[i].translation, false);
}

// [rc4l] Level surfaces their sector's light reproduces are lit from the sector light table (fuaSectorLit); 0 bakes every surface's shading into its record, to compare against.
CVAR(Bool, fua_dg_sectorlit, true, 0)

// [rc4l] The piece's own record, written once. See ScenePieceData.
void FillPieceData(const zx::levelmesh::MeshPiece &p, int translation, ScenePieceData &pd, bool inMesh)
{
	FillPieceDataWith(p, PieceSlots(p, translation, inMesh), pd, inMesh);
}

void FillPieceDataWith(const zx::levelmesh::MeshPiece &p, const PieceSlotSet &slots, ScenePieceData &pd, bool inMesh)
{
	pd.r = p.colorR; pd.g = p.colorG; pd.b = p.colorB;
	pd.softLight = p.softLight;
	pd.fogDensity = p.fogDensity;
	pd.alpha = p.alpha;
	pd.fogMode = (float)p.fogMode;
	// [rc4l] The material SLOT. It held a dynamic light index, then a batch index for the traced
	// mirror, and it is a material slot because that is the one of the three that both the raster and
	// the traced path want: a batch index only identifies a texture by accident, since a level has
	// more batches than materials and the mirror had to mask it to 127 and hope.
	pd.matSlot = slots.mat;
	pd.brightSlot = slots.bright;
	zx::levelmesh::ComputeUnpackRGB(p.fogColor, pd.fogR, pd.fogG, pd.fogB);
	// [rc4l] A warp texture ripples in the shader, as GL's func_warp does; the upload already holds its unwarped source.
	zx::materials::MaterialInfo info;
	const bool described = zx::materials::DescribeMaterial(p.material, info);
	// [rc4l] A warp, or a GLDEFS hardware shader from 3 up, in the slot the shaders read the piece's effect from.
	pd.warpStyle = (float)zx::hwshaders::ComputePieceShader(zx::hwrender::ComputeWarpStyle(described ? info.warp : 0), described ? info.userShader : -1);
	pd.nx = p.normX; pd.ny = p.normY; pd.nz = p.normZ;
	pd.warpSpeed = described ? info.shaderSpeed : 1.f;
	pd.floodUx = p.floodUV[0]; pd.floodUy = p.floodUV[1]; pd.floodU0 = p.floodUV[2]; pd.floodZ = p.floodZ;
	pd.floodVx = p.floodUV[3]; pd.floodVy = p.floodUV[4]; pd.floodV0 = p.floodUV[5]; pd.flood = p.flood ? 1.f : 0.f;
	pd.actorR = p.dynR; pd.actorG = p.dynG; pd.actorB = p.dynB; pd.cpuLight = p.cpuLight ? 1.f : 0.f;
	pd.stackId = inMesh ? (float)(zx::levelmesh::PieceStackId(p) + 1) : 0.f;
	pd.animates = slots.animates ? 1.f : 0.f;
	pd.plain = p.plain ? 1.f : 0.f;
	pd.desaturation = zx::hwrender::ComputeDesaturationFactor(p.desaturation);
	for (int i = 0; i < 4; i++)
	{
		pd.glowTop[i] = p.glowTop[i]; pd.glowBottom[i] = p.glowBottom[i];
		pd.glowTopPlane[i] = p.glowTopPlane[i]; pd.glowBottomPlane[i] = p.glowBottomPlane[i];
	}
	pd.tint[0] = p.tint.r; pd.tint[1] = p.tint.g; pd.tint[2] = p.tint.b; pd.tint[3] = p.tint.strength;
	pd.tintRange[0] = p.tint.top; pd.tintRange[1] = p.tint.bottom; pd.tintRange[2] = p.tint.glow ? 1.f : 0.f; pd.tintRange[3] = 0.f;
	pd.scrollU = p.scrollU; pd.scrollV = p.scrollV;
	// [rc4l] features/decals' surface rule for a level piece, by its base texture and, for a wall, its side's flags as the copy holds them.
	const FTexture *base = static_cast<const FTexture *>(p.baseTex);
	const bool wallPiece = p.linkId >= numsubsectors && p.linkId - numsubsectors < numsides;
	const bool noMarks = inMesh && base != NULL &&
		zx::decals::SurfaceRefusesMarks(base->id, wallPiece, wallPiece ? sides[p.linkId - numsubsectors].rc.Flags : 0);
	const zx::hwrender::PieceTexInputs tex = { p.stencil, p.alphaTest, p.hud, inMesh, p.inverse, p.colorBlend, p.fogLayer, p.fuzz, p.fogBoundary, p.cullFace, p.fogPass, noMarks };
	pd.texFlags = (float)zx::hwrender::ComputePieceTexFlags(tex);
	// [rc4l] A flicker then rewrites its sector's entry in the table rather than this record.
	if (inMesh && fua_dg_sectorlit && p.sectorLight.sector >= 0) zx::hwrender::ComputeSectorLitRecord(pd, p.sectorLight, p.sectorLitNoFog);
}

// [rc4l] How many of the level's surfaces are lit from the sector light table, and how many sectors it holds.
CCMD(fua_dg_sectorlit_report)
{
	unsigned lit = 0;
	for (unsigned i = 0; i < g_scene.pieces.Size(); i++)
		if ((unsigned)g_scene.pieces[i].texFlags & zx::hwrender::kTexFlagSectorLit) lit++;
	Printf("sector light table: %u of %u level surfaces lit from it, %d sectors (room for %u), %s\n", lit, g_scene.pieces.Size(),
		numsectors, g_scene.sectorLightCapacity, fua_dg_sectorlit ? "on" : "off (fua_dg_sectorlit 0)");
}

void UpdateSectorLightTable(Diligent::IDeviceContext *ctx)
{
	ZX_ZONE("scene/sectorlight");   // [rc4l] the sector light table read from the render copy and its changes sent
	if (!g_scene.sectorLightBuf || ctx == NULL || numsectors <= 0 || (unsigned)numsectors > g_scene.sectorLightCapacity) return;
	static TArray<unsigned> now;
	const unsigned words = (unsigned)numsectors * zx::levelmesh::kSectorLightWords;
	now.Resize(words);
	for (int i = 0; i < numsectors; i++)
		zx::levelmesh::ComputeSectorLightWords(zx::levelmesh::SectorLightEntryOf(i), &now[(unsigned)i * zx::levelmesh::kSectorLightWords]);
	TArray<unsigned> &was = g_scene.sectorLightWords;
	if (was.Size() != words)
	{
		was = now;
		UpdateBufferInPages(ctx, g_scene.sectorLightBuf, 0, (size_t)words * sizeof(unsigned), &now[0]);
		return;
	}
	const unsigned stride = zx::levelmesh::kSectorLightWords;
	for (const zx::hwrender::UploadRun &run : zx::hwrender::ComputeChangedRuns(&was[0], &now[0], (unsigned)numsectors, stride, 64))
	{
		memcpy(&was[run.first * stride], &now[run.first * stride], (size_t)run.count * stride * sizeof(unsigned));
		UpdateBufferInPages(ctx, g_scene.sectorLightBuf, (unsigned long long)run.first * stride * sizeof(unsigned),
			(size_t)run.count * stride * sizeof(unsigned), &now[run.first * stride]);
	}
}

// [rc4l] Where each piece's vertices ended up, so a moved one can be patched instead of rebuilt.
struct VBSlot
{
	unsigned int meshOffset;   // key: the piece's range in the level mesh
	unsigned int count;
	// [rc4l] How much room the slot HAS, which is not the same as how much it is drawing.
	//
	// RetireSlot zeroes `count` when a surface goes away, and without a record of the space it left
	// behind, a surface that comes BACK could only be put on the end of the buffer -- abandoning its
	// old vertices every time. A door's upper exists when the door is shut and not when it is open,
	// several times a second, so wdiex01 leaked ~1,060 slots every twelve seconds and walked from
	// 3.25 ms to a flat 33.9 ms. Keeping the capacity lets the piece go back where it was.
	unsigned int capacity;
	unsigned int vbStart;      // where those vertices live in the scene buffer
	unsigned int batch;
	// [rc4l] Where this piece's record lives. Stable across a patch: a moved sector rewrites the
	// record in place rather than taking a new index, so the vertices never have to be renumbered.
	unsigned int pieceIndex;
	bool         blended;
};
static TArray<VBSlot> g_vbSlots;
// [rc4l] Offset -> slot, because appending breaks the sortedness a binary search needs and
// re-sorting 115,000 slots to place one new piece would defeat the point of appending.
static TMap<unsigned int, unsigned int> g_slotByOffset;

// [rc4l] The mesh revisions this scene buffer was built at -- see RefreshMovedGeometry.
static unsigned int g_builtRebatchRev = 0, g_builtRepaintRev = 0;
// Repaints waiting for a frame with room for them -- see where they are drained.
static TArray<unsigned int> g_pendingRepaints;
// [rc4l] The repaint budget is per CALL of the dynamic build, not per frame, and MEASURING says
// keep it that way.
//
// BuildDynamic runs about three times a frame, so "512" drains up to ~1,536 -- and the breakdown
// showed exactly 1,536 on a p99 frame. That looks like the spreading failing, and tightening it to a
// true per-frame 512 was tried: p99 went 4.55 -> 10.44 ms and hitches 6.9% -> 28.8%. The reason is
// that the drain only runs when the dynamic build runs, which is on the tic frames -- so a per-frame
// cap starves the queue on the seven quiet frames, the backlog crosses the flush-all threshold, and
// the work lands in BIGGER lumps than before.
//
// Real spreading needs the drain lifted out of BuildDynamic so it runs on quiet frames too. Until
// then ~1,536 a tic matches the arrival rate, which is the stable answer.   // how many UpdateBuffer calls those repaints were coalesced into
// Batches the last sorted build produced -- the figure appends are measured against.
static int g_builtBatchCount = 0;


// [rc4l] The piece records made again with room for want and the level's own written in, the old buffer kept when the new one cannot be had; the caller makes the pipelines again, since a static binding cannot follow a new buffer.
static bool MakePieceBuffer(Diligent::IDeviceContext *ctx, unsigned int want)
{
	Diligent::RefCntAutoPtr<Diligent::IBuffer> made;
	if (!CreateStructuredBuffer("fua scene piece data", sizeof(ScenePieceData), want, NULL, made)) return false;
	g_scene.pieceBuf = made;
	g_scene.pieceCapacity = want;
	if (ctx != NULL && g_scene.pieces.Size() > 0) UpdateBufferInPages(ctx, g_scene.pieceBuf, 0, g_scene.pieces.Size() * sizeof(ScenePieceData), &g_scene.pieces[0]);
	return true;
}

// [rc4l] The piece buffer made bigger before this frame's dynamic pieces are laid out, when they outnumber the records behind the level's: past them each sprite drew with the last record's material, and nuts.wad's 10,600 monsters showed as rows of one wrong monster.
void EnsureDynamicRecords()
{
	int count = 0, vertices = 0;
	zx::levelmesh::DynPieces(count);
	zx::levelmesh::DynVertices(vertices);
	const unsigned int dynamic = (unsigned int)count;
	if (dynamic > g_scene.dynPeak) g_scene.dynPeak = dynamic;
	const unsigned int words = zx::hwrender::ComputeDynWordsBound(dynamic, (unsigned int)vertices);
	if (words > g_scene.dynWordsPeak) g_scene.dynWordsPeak = words;
	auto *ctx = GetContext();
	if (!g_scene.pieceBuf || ctx == NULL) return;
	bool remade = false;
	if (!zx::hwrender::ComputeBufferFits(true, g_scene.pieceCapacity, g_scene.pieces.Size() + dynamic))
		remade = MakePieceBuffer(ctx, zx::hwrender::ComputePieceCapacity(g_scene.pieces.Size(), g_scene.dynPeak));
	// [rc4l] The dynamic words too, which EnsureScenePipeline makes again at the peak.
	if (g_scene.dynDataBuf && words > g_scene.dynDataCapacity)
	{
		g_scene.dynDataBuf.Release();
		g_scene.dynDataCapacity = 0;
		remade = true;
	}
	if (!remade) return;
	// [rc4l] A new buffer object, so the pipelines binding it statically are made again, as a level's upload does.
	ReleaseScenePipelines();
	FString err;
	EnsureScenePipeline(err);
}

// [rc4l] Build the backend vertex buffer from the level mesh, and upload it.
//
// Called at upload AND whenever geometry moves. A door does not merely nudge its vertices: the wall
// cache re-bakes the seg, the piece can change vertex count, and MeshStore then hands it a NEW range
// at the top of the arena. Any map from old mesh offsets to buffer slots is stale the moment that
// happens -- which is why patching pieces in place left the shut door painted across an open doorway.
//
// So the whole thing is rebuilt. That is only affordable because the material sort is no longer
// quadratic; it runs for the handful of frames a door is actually moving, and not at all otherwise.
static bool BuildSceneBuffer(FString &err)
{
	ZX_COUNT("scene/rebuilds", "rebuilds", 1);   // [rc4l] whole scene buffer builds, which a level with moving sectors makes in play
	int srcCount = 0;
	const zx::levelmesh::MeshVertex *src = zx::levelmesh::MeshVertexData(srcCount);
	int npieces = 0;
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces(npieces);
	if (src == NULL || srcCount <= 0 || pieces == NULL || npieces <= 0)
	{
		err = "no baked geometry -- set gl_wallmesh 1, walk the level, then retry";
		return false;
	}

	// [rc4l] Order the pieces by material, then emit their vertices in that order. The result is one
	// contiguous run per material, so each material draws once -- instead of one draw per piece.
	//
	// std::sort, not the selection sort this used to be. At 18000 pieces the quadratic version was
	// ~160 million comparisons -- several seconds, tolerable exactly once at upload and completely
	// prohibitive the moment a door made this have to run again mid-game.
	static TArray<int> order;
	order.Clear();
	order.Resize(npieces);
	for (int i = 0; i < npieces; i++) order[i] = i;
	// [rc4l] Opaque first, then blended. Blending is not commutative, so a translucent surface has to
	// be drawn after everything it shows through -- a 3D floor of grating over a lava pit reads as
	// solid grating otherwise, which is what dbab01 looked like until this existed.
	//
	// The secondary key is baseTex, and it is not cosmetic. Two surfaces with DIFFERENT base textures
	// can resolve to the SAME material at bake time, because an animation is at some frame when the
	// mesh is built: a lava floor showing frame 2 and another animated flat also showing frame 2 are
	// one FMaterial. Merged into one batch, they then get re-resolved every frame from whichever
	// baseTex the batch happened to record, and the other surface is repainted with a texture that has
	// nothing to do with it -- which is a lava floor turning into green foliage, at dbab02-flatswap.
	// [rc4l] The rule is in surfaceorder_compute, beside the translucent one, because one of its
	// three clauses is correctness and two are batching -- see ComputePiecesBefore.
	std::sort(&order[0], &order[0] + npieces, [pieces](int a, int b) {
		zx::hwrender::ScenePiece pa, pb;
		pa.blendMode = pieces[a].blendMode; pa.material = pieces[a].material; pa.baseTex = pieces[a].baseTex;
		pb.blendMode = pieces[b].blendMode; pb.material = pieces[b].material; pb.baseTex = pieces[b].baseTex;
		return zx::hwrender::ComputePiecesBefore(pa, pb);
	});

	g_scene.cpuVerts.Clear();
	g_scene.pieces.Clear();
	g_scene.batches.Clear();
	g_scene.batchesGen++;
	g_scene.blendBatches.Clear();
	g_vbSlots.Clear();
	// [rc4l] The slot table is NOT reset here, and that is the whole point of it.
	//
	// A scene rebuild re-emits every vertex, and on a level with moving sectors it happens
	// constantly -- dbab04 rebuilds about two hundred times in a minute. Resetting the table each
	// time renumbers the slots from the new emit order, so a material that was slot 3 becomes slot
	// 7, and every binding built from the old numbering is describing a different level. The
	// symptom is the whole world wearing the wrong textures with the geometry perfectly intact.
	//
	// Keeping it means a slot number is stable for as long as the level is: MaterialSlotFor finds
	// the existing entry, the indices never move, and the array only has to be built again when a
	// genuinely new material appears. The table is reset where it belongs -- on a level change,
	// in AutoSetupForLevel, next to everything else keyed on a pointer the old level owned.
	const void *cur = (const void *)(size_t)-1;
	const void *curBase = (const void *)(size_t)-1;
	int curBlend = -1;
	for (int i = 0; i < npieces; i++)
	{
		const zx::levelmesh::MeshPiece &p = pieces[order[i]];
		if (p.range.count == 0) continue;

		// A blended piece never merges with its neighbour: the translucent pass reorders batches per
		// frame, and two pieces sharing a batch could not then be separated. A batch also breaks on
		// baseTex, because the animation pass re-resolves the whole batch from that one pointer.
		if (p.material != cur || p.baseTex != curBase || p.blendMode != curBlend || p.blendMode != 0 ||
		    false)
		{
			SceneBatch b;
			b.material = p.material;
			b.first = (unsigned int)g_scene.cpuVerts.Size();
			b.count = 0;
			b.masked = MaterialIsMasked(p.material);
			b.srb = NULL;   // filled once the batch list is final
			b.mirrorSector = p.mirrorSector;
			b.mirrorCeiling = p.mirrorCeiling;
			b.mirrorSlot = -1;
			b.baseTex = p.baseTex;
			b.resolved = p.material;
			b.blend = (p.blendMode == 2) ? 2 : (p.blendMode != 0 ? 1 : 0);
			b.sortX = b.sortY = b.sortZ = 0.f;
			b.normX = p.normX; b.normY = p.normY; b.normZ = p.normZ;
			b.minX = b.minY = b.minZ =  1e30f;
			b.maxX = b.maxY = b.maxZ = -1e30f;
			b.walkId = -1;
			g_scene.batches.Push(b);
			if (b.blend != 0) g_scene.blendBatches.Push((int)g_scene.batches.Size() - 1);
			cur = p.material;
			curBase = p.baseTex;
			curBlend = p.blendMode;
		}

		// [rc4l] Straight from the mesh: these are the values the engine's own gl_SetColor/gl_SetFog
		// produced for this surface at bake time. The backend re-derived them once and drifted --
		// see CaptureShading in staticmesh.cpp.
		const unsigned int vbStart = g_scene.cpuVerts.Size();
		const unsigned int pieceIdx = g_scene.pieces.Size();
		{
			ScenePieceData pd;
			FillPieceData(p, 0, pd, true);
			g_scene.pieces.Push(pd);
			// [rc4l] A blended piece is a batch of its own, so the batch can be skipped by the walk that reached the piece.
			if (p.blendMode != 0) g_scene.batches[g_scene.batches.Size() - 1].walkId = (int)(pd.stackId + 0.5f) - 1;
		}
		for (unsigned int v = 0; v < p.range.count; v++)
		{
			g_scene.cpuVerts.Push(ToSceneVertex(src[p.range.offset + v], pieceIdx));
		}
		// The batch grows to hold this piece.
		{
			SceneBatch &nbb = g_scene.batches[g_scene.batches.Size() - 1];
			for (unsigned int v = 0; v < p.range.count; v++)
			{
				const SceneVertex &dv = g_scene.cpuVerts[vbStart + v];
				if (dv.x < nbb.minX) nbb.minX = dv.x;
				if (dv.y < nbb.minY) nbb.minY = dv.y;
				if (dv.z < nbb.minZ) nbb.minZ = dv.z;
				if (dv.x > nbb.maxX) nbb.maxX = dv.x;
				if (dv.y > nbb.maxY) nbb.maxY = dv.y;
				if (dv.z > nbb.maxZ) nbb.maxZ = dv.z;
			}
		}
		{
			VBSlot slot;
			slot.meshOffset = p.range.offset;
			slot.count = p.range.count;
			slot.capacity = p.range.count;
			slot.vbStart = vbStart;
			slot.batch = g_scene.batches.Size() - 1;
			slot.pieceIndex = pieceIdx;
			slot.blended = (p.blendMode != 0);
			g_vbSlots.Push(slot);
		}
		SceneBatch &nb = g_scene.batches[g_scene.batches.Size() - 1];
		nb.count += p.range.count;
		// The centroid the translucent pass sorts on. Averaged from the piece's own vertices rather
		// than taken from MeshPiece::sortX, which only the dynamic path fills in.
		if (nb.blend != 0)
			zx::levelmesh::ComputeCentroid(src, (unsigned)srcCount, p.range.offset, p.range.count, nb.sortX, nb.sortY, nb.sortZ);
	}

	if (g_scene.cpuVerts.Size() == 0) { err = "no drawable pieces"; return false; }

	// [rc4l] The layout this buffer was built for, and the slots to patch it by. Sorted by mesh
	// offset so a dirty range can be walked with a binary search rather than a scan of 60,000 pieces.
	std::sort(&g_vbSlots[0], &g_vbSlots[0] + g_vbSlots.Size(),
		[](const VBSlot &x, const VBSlot &y) { return x.meshOffset < y.meshOffset; });
	g_scene.builtLayoutGen = zx::levelmesh::MeshLayoutGeneration();
	g_scene.appendedVerts = 0;
	g_builtBatchCount = (int)g_scene.batches.Size();
	g_slotByOffset.Clear();
	for (unsigned i = 0; i < g_vbSlots.Size(); i++) g_slotByOffset.Insert(g_vbSlots[i].meshOffset, i);

	// [rc4l] The vertex buffer is kept while the level still fits it, as the piece records are: a rebuild made it again each time, three 18 MB buffers in one Sunder load.
	const bool vbKept = g_scene.vb && g_scene.cpuVerts.Size() <= g_scene.vbCapacity;
	if (!vbKept)
	{
		Diligent::BufferDesc bd;
		bd.Name = "fua scene VB";
		// [rc4l] A quarter more room than the level needs, so a piece that appears mid-game is appended rather than costing a whole rebuild (Sunder MAP16 made 21 a tic without it).
		g_scene.vbCapacity = (unsigned int)(g_scene.cpuVerts.Size() + g_scene.cpuVerts.Size() / 4 + 1024);
		bd.Size = (Diligent::Uint64)g_scene.vbCapacity * sizeof(SceneVertex);
		// [rc4l] USAGE_DEFAULT rather than IMMUTABLE, since doors, lifts and crushers re-bake their segs and an immutable buffer froze every moving thing in the backend's view.
		bd.Usage = Diligent::USAGE_DEFAULT;
		// [rc4l] BIND_RAY_TRACING as well when the device has it, since a buffer BuildBLAS takes must declare that use at creation or the process dies on a later submit with nothing in the log.
		bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
		if (RayTracingAvailable())
		{
			// [rc4l] Readable as storage too, as a float array, since a ray query returns a primitive index whose vertices the shader fetches itself.
			bd.BindFlags |= Diligent::BIND_RAY_TRACING | Diligent::BIND_SHADER_RESOURCE;
			bd.Mode = Diligent::BUFFER_MODE_STRUCTURED;
			bd.ElementByteStride = sizeof(float);
		}
		// [rc4l] Created empty and then filled, since Diligent refuses initial data smaller than the buffer and the buffer keeps slack for appended pieces.
		g_scene.vb.Release();
		CreateDeviceBuffer(bd, nullptr, &g_scene.vb);
		if (!g_scene.vb) { err = "vertex buffer creation failed"; return false; }
	}

	// [rc4l] The per-piece records, sized for this level with the same slack the vertices get.
	//
	// Appending a piece must not have to resize this: a bigger buffer is a different object, and a
	// static shader variable cannot be re-pointed once its pipeline has handed out a binding. So the
	// slack is the budget, AppendPiece refuses when it runs out, and a rebuild sizes it again.
	const bool pieceBufferKept = g_scene.pieceBuf && g_scene.pieces.Size() <= g_scene.pieceCapacity;
	if (!pieceBufferKept)
	{
		if (!MakePieceBuffer(GetContext(), zx::hwrender::ComputePieceCapacity(g_scene.pieces.Size(), g_scene.dynPeak)))
		{ err = "piece data buffer creation failed"; return false; }
	}
	else if (auto *ctx3 = GetContext())
	{
		// [rc4l] Same buffer, new contents, and no new pipelines.
		//
		// Rebuilding the scene is not rare: a level with moving sectors does it a couple of hundred
		// times a minute, and making thirteen pipelines again each time is a stutter you can feel with
		// the mouse. A buffer that still fits is kept, and only a bigger one costs a rebuild.
		if (g_scene.pieces.Size() > 0)
			UpdateBufferInPages(ctx3, g_scene.pieceBuf, 0, g_scene.pieces.Size() * sizeof(ScenePieceData), &g_scene.pieces[0]);
	}
	// [rc4l] The stacked views' bits follow the same rule: kept while this level's subsectors fit.
	const unsigned int visWords = (unsigned int)zx::levelmesh::ComputeStackVisWords(numsubsectors + numsides, zx::levelmesh::WalkSlots());
	const bool stackVisKept = g_scene.stackVisBuf && visWords <= g_scene.stackVisCapacity;
	if (!stackVisKept)
	{
		const unsigned int want = zx::hwrender::ComputeGrownCapacity(visWords, 64);
		if (!CreateStructuredBuffer("fua stacked view subsectors", sizeof(unsigned int), want, NULL, g_scene.stackVisBuf))
		{ err = "stacked view buffer creation failed"; return false; }
		g_scene.stackVisCapacity = want;
	}
	// A NEW buffer object means the pipelines have to be made again to bind it -- the same
	// static-resource rule that governs the material array.
	if (!pieceBufferKept || !stackVisKept)
	{
		ReleaseScenePipelines();
		FString perr;
		if (!EnsureScenePipeline(perr)) { err = perr; return false; }
	}
	if (auto *ctx = GetContext())
		UpdateBufferInPages(ctx, g_scene.vb, 0, g_scene.cpuVerts.Size() * sizeof(SceneVertex), &g_scene.cpuVerts[0]);

	// [rc4l] Give every batch its own SRB now that the list is final. Sized once, so the RefCntAutoPtrs
	// never move and the raw pointers handed to SceneBatch stay valid.
	// [rc4l] NOW the table describes the level whose vertices are on the GPU, and not one moment
	// earlier.
	//
	// Setting this where the table is reset looks equivalent and is not: a rebuild can still fail
	// after that point -- "no drawable pieces" is one of several ways -- and it leaves the slot
	// table describing the NEW level while the buffer the GPU draws still holds the OLD one. Every
	// surface then samples whatever material happens to sit at its index, which is exactly what
	// dbab04 did, and only when reached by a map change rather than loaded directly.
	MarkMaterialSlotsCurrent();
	DirtyMaterialSlots();

	ReleaseBatchSRBs();
	for (unsigned i = 0; i < g_scene.batches.Size(); i++) BatchSRB(g_scene.batches[i]);
	// [rc4l] The decal pass's pipelines go whenever the scene's do (ReleaseScenePipelines), so they are made with the scene: made by the first floor or ceiling mark instead, they stalled that frame some 18 ms on every level.
	EnsureDecalPipelines();
	return true;
}
// See ReleaseMaterialSRBs. The batches keep raw copies of bindings they do not own.
void ForgetBatchSRBs()
{
	for (unsigned i = 0; i < g_scene.batches.Size(); i++) g_scene.batches[i].srb = NULL;
}

bool SceneUpload(FString &report)
{
	FString err;
	// [rc4l] Size the backend's client area to the engine's screen BEFORE the window exists, so the
	// 2D layer maps 1:1 and a GL/Vulkan screenshot pair is directly comparable. A 624x361 client area
	// against a 640x480 2D layer squashed the HUD to 75% height and point-sampled it into mush.
	if (screen != NULL) Fua_SetBackendWindowSize(screen->GetWidth(), screen->GetHeight());
	if (!DiligentShowWindow(err)) { report = err; return false; }
	if (!EnsureScenePipeline(err)) { report = err; return false; }
	// [rc4l] The level goes up in one frame, and a browser's device would keep a page for every 8 MB of it; asked once the window has made the device.
	UploadFlushing flushing;
	if (!BuildSceneBuffer(err)) { report = err; return false; }

	g_scene.verts = (int)g_scene.cpuVerts.Size();
	// [rc4l] The acceleration structure is built from the same buffer the raster path draws, right
	// after it is uploaded, so there is never a version of the level that one path can see and the
	// other cannot.
	// Only when something will actually trace against it: an acceleration structure over 17k
	// triangles is not free to build, and nothing else in the frame reads it.
	if (fua_dg_rtmirrors && RayTracingAvailable())
	{
		EnsureAccelerationStructure();
		// [rc4l] And the mirror bindings, here rather than mid-frame: creating them touches textures
		// and acceleration structures, and neither is valid inside a render pass.
		EnsureMirrorResources();
	}
	BuildMVP(g_mvp);

	// [rc4l] The shading inputs themselves, not just which code path they take.
	//
	// The Vulkan render came out uniformly brighter and less tinted than GL's, and three rounds of
	// reading pixels off screenshots produced three different theories. The values are right here at
	// bake time; printing them settles in one run what guessing did not settle in an hour.
	int npieces = 0;
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces(npieces);
	{
		int lmin = 999, lmax = -1;
		double lsum = 0;
		unsigned int lc = 0, fc = 0; int lcN = 0, fcN = 0;
		for (int i = 0; i < npieces; i++)
		{
			const zx::levelmesh::MeshPiece &p = pieces[i];
			if (p.range.count == 0) continue;
			if (p.lightLevel < lmin) lmin = p.lightLevel;
			if (p.lightLevel > lmax) lmax = p.lightLevel;
			lsum += p.lightLevel;
			if (lcN == 0 || p.lightColor != lc) { lc = p.lightColor; lcN++; }
			if (fcN == 0 || p.fadeColor != fc) { fc = p.fadeColor; fcN++; }
		}
		Printf("  mesh light: level %d..%d (mean %.1f), lightColor %08x, fadeColor %08x, "
			"fogdensity(first) %.2f\n",
			lmin, lmax, npieces ? lsum / npieces : 0.0, lc, fc,
			zx::levelmesh::ComputeFogDensity(lmin, fc, RenderShadingEnv(false)));
	}

	// [rc4l] Which lighting path is live decides how the picture should be read: lightmode 8 takes
	// R_DoomLightingEquation, everything else takes fog diminishing. Printing it beats inferring it
	// from a screenshot.
	int softPieces = 0, fogPieces = 0;
	// Counted over the PIECES now rather than their vertices, which is what the numbers always
	// meant -- and one record per piece instead of one per vertex is the whole point of the shrink.
	for (unsigned int i = 0; i < g_scene.pieces.Size(); i++)
	{
		if (g_scene.pieces[i].softLight >= 0.f) softPieces++;
		if (g_scene.pieces[i].fogMode != 0.f) fogPieces++;
	}
	report.Format("uploaded %d verts (%.2f MB) + %d piece records (%.2f MB), %d pieces -> %d material batches (%d translucent) "
		"[lightmode %d, fogmode %d, %d%% soft-lit, %d%% fogged]",
		g_scene.verts, (double)g_scene.cpuVerts.Size() * sizeof(SceneVertex) / (1024.0 * 1024.0),
		(int)g_scene.pieces.Size(),
		(double)g_scene.pieces.Size() * sizeof(ScenePieceData) / (1024.0 * 1024.0),
		npieces, (int)g_scene.batches.Size(), (int)g_scene.blendBatches.Size(),
		glset.lightmode, (int)r_fogmode,
		g_scene.pieces.Size() ? softPieces * 100 / (int)g_scene.pieces.Size() : 0,
		g_scene.pieces.Size() ? fogPieces * 100 / (int)g_scene.pieces.Size() : 0);
	return true;
}

static bool AppendPiece(Diligent::IDeviceContext *ctx, const zx::levelmesh::MeshPiece &p,
	const zx::levelmesh::MeshVertex *src)
{
	if (p.blendMode != 0 || p.range.count == 0) return false;
	if ((unsigned int)(g_scene.cpuVerts.Size() + p.range.count) > g_scene.vbCapacity) return false;

	SceneBatch b;
	b.material = p.material;
	b.first = (unsigned int)g_scene.cpuVerts.Size();
	b.count = p.range.count;
	b.masked = MaterialIsMasked(p.material);
	b.baseTex = p.baseTex;
	b.resolved = p.material;
	b.blend = 0;
	b.mirrorSector = -1;
	b.mirrorCeiling = false;
	b.mirrorSlot = -1;
	b.sortX = b.sortY = b.sortZ = 0.f;
	b.normX = p.normX; b.normY = p.normY; b.normZ = p.normZ;
	b.minX = b.minY = b.minZ =  1e30f;
	b.maxX = b.maxY = b.maxZ = -1e30f;
	b.srb = NULL;
	b.walkId = -1;
	if (BatchSRB(b) == NULL) return false;   // [rc4l] no binding, no draw -- and a rebuild will do it properly
	g_scene.batches.Push(b);
	g_scene.batchesGen++;

	VBSlot slot;
	slot.meshOffset = p.range.offset;
	slot.count = p.range.count;
	slot.capacity = p.range.count;
	slot.vbStart = b.first;
	slot.batch = g_scene.batches.Size() - 1;
	// [rc4l] An appended piece needs a record as well as vertices, and the record has to reach the
	// GPU before the vertices that index it do -- a vertex pointing at a slot nobody has written is
	// undefined shading, and it would show up as one surface out of thousands looking wrong.
	slot.pieceIndex = g_scene.pieces.Size();
	if (slot.pieceIndex >= g_scene.pieceCapacity) return false;   // out of record space: let a rebuild size it
	{
		ScenePieceData pd;
		FillPieceData(p, 0, pd, true);
		g_scene.pieces.Push(pd);
		SendToBuffer(ctx, g_scene.pieceBuf, (Diligent::Uint64)slot.pieceIndex * sizeof(ScenePieceData),
			sizeof(ScenePieceData), &pd);
	}
	slot.blended = false;

	for (unsigned int v = 0; v < p.range.count; v++)
	{
		const SceneVertex dv = ToSceneVertex(src[p.range.offset + v], slot.pieceIndex);
		SceneBatch &nb = g_scene.batches[slot.batch];
		if (dv.x < nb.minX) nb.minX = dv.x;
		if (dv.y < nb.minY) nb.minY = dv.y;
		if (dv.z < nb.minZ) nb.minZ = dv.z;
		if (dv.x > nb.maxX) nb.maxX = dv.x;
		if (dv.y > nb.maxY) nb.maxY = dv.y;
		if (dv.z > nb.maxZ) nb.maxZ = dv.z;
		g_scene.cpuVerts.Push(dv);
	}
	SendToBuffer(ctx, g_scene.vb, (Diligent::Uint64)slot.vbStart * sizeof(SceneVertex),
		(Diligent::Uint64)slot.count * sizeof(SceneVertex), &g_scene.cpuVerts[slot.vbStart]);

	const unsigned int idx = g_vbSlots.Push(slot);
	g_slotByOffset.Insert(slot.meshOffset, idx);
	g_scene.appendedVerts += (int)slot.count;
	g_stats.geomAppends++;
	return true;
}

// [rc4l] Collapse a slot the world no longer uses, without moving anything after it.
//
// A piece that was re-baked at a different size lives somewhere else now, and its old vertices are
// still in the buffer being drawn. Compacting them out would move every slot behind them; giving
// them zero area costs one upload and the rasteriser discards them. The waste is reclaimed at the
// next full rebuild, which is what the slack budget above is for.
static void RetireSlot(Diligent::IDeviceContext *ctx, VBSlot &slot)
{
	for (unsigned int v = 0; v < slot.count; v++)
	{
		SceneVertex &dv = g_scene.cpuVerts[slot.vbStart + v];
		dv.x = dv.y = dv.z = 0.f;
	}
	SendToBuffer(ctx, g_scene.vb, (Diligent::Uint64)slot.vbStart * sizeof(SceneVertex),
		(Diligent::Uint64)slot.count * sizeof(SceneVertex), &g_scene.cpuVerts[slot.vbStart]);
	slot.count = 0;   // nothing here to patch again
}

// [rc4l] Put a returning surface back where it was, instead of on the end of the buffer.
//
// RetireSlot leaves the slot allocated with count 0. A piece that comes back is otherwise "new" as
// far as g_slotByOffset is concerned -- its slot exists but draws nothing -- so it was appended, and
// the space it used to occupy was abandoned for the life of the level. That is the whole of the
// wdiex01 leak: 100% of its appends were this case, none of them a genuine size change.
//
// Refuses when the piece no longer fits the room it left, or when its material has moved it to a
// different batch -- reviving into a batch that names another texture is the bug that left a pressed
// switch looking unpressed. Both fall through to the append, which is always correct.
static bool ReviveSlot(Diligent::IDeviceContext *ctx, VBSlot &slot,
	const zx::levelmesh::MeshPiece &p, const zx::levelmesh::MeshVertex *src)
{
	if (slot.count != 0 || slot.capacity < p.range.count || p.range.count == 0) return false;
	if (slot.batch >= g_scene.batches.Size()) return false;
	if (p.material != g_scene.batches[slot.batch].material) return false;
	if (slot.pieceIndex >= g_scene.pieces.Size()) return false;

	FillPieceData(p, 0, g_scene.pieces[slot.pieceIndex], true);
	SendToBuffer(ctx, g_scene.pieceBuf, (Diligent::Uint64)slot.pieceIndex * sizeof(ScenePieceData),
		sizeof(ScenePieceData), &g_scene.pieces[slot.pieceIndex]);

	SceneBatch &nb = g_scene.batches[slot.batch];
	for (unsigned int v = 0; v < p.range.count; v++)
	{
		SceneVertex &dv = g_scene.cpuVerts[slot.vbStart + v];
		dv = ToSceneVertex(src[p.range.offset + v], slot.pieceIndex);
		if (dv.x < nb.minX) nb.minX = dv.x;
		if (dv.y < nb.minY) nb.minY = dv.y;
		if (dv.z < nb.minZ) nb.minZ = dv.z;
		if (dv.x > nb.maxX) nb.maxX = dv.x;
		if (dv.y > nb.maxY) nb.maxY = dv.y;
		if (dv.z > nb.maxZ) nb.maxZ = dv.z;
	}
	// Anything past what the piece needs stays collapsed, so the slot keeps its capacity.
	for (unsigned int v = p.range.count; v < slot.capacity; v++)
	{
		SceneVertex &dv = g_scene.cpuVerts[slot.vbStart + v];
		dv.x = dv.y = dv.z = 0.f;
	}
	SendToBuffer(ctx, g_scene.vb, (Diligent::Uint64)slot.vbStart * sizeof(SceneVertex),
		(Diligent::Uint64)slot.capacity * sizeof(SceneVertex), &g_scene.cpuVerts[slot.vbStart]);

	slot.count = p.range.count;
	slot.meshOffset = p.range.offset;
	slot.blended = (p.blendMode != 0);
	return true;
}

// [rc4l] A piece that changed material or pass taken out of the batch it was built into and put on the end in one of its own, false when it cannot be appended and only a rebuild will do.
static bool MovePieceToNewBatch(Diligent::IDeviceContext *ctx, const zx::levelmesh::MeshPiece &p,
	const zx::levelmesh::MeshVertex *src)
{
	unsigned int *slotIdx = g_slotByOffset.CheckKey(p.range.offset);
	if (slotIdx != NULL && *slotIdx < g_vbSlots.Size() && g_vbSlots[*slotIdx].count > 0) RetireSlot(ctx, g_vbSlots[*slotIdx]);
	if (p.range.count == 0) return true;
	if (!AppendPiece(ctx, p, src)) return false;
	g_stats.appendRebatched++;
	return true;
}

static void NoteBail(const zx::levelmesh::MeshPiece &p)
{
	// [rc4l] Where it is in map units, so the line it came from can be looked up.
	int count = 0;
	const zx::levelmesh::MeshVertex *v = zx::levelmesh::MeshVertexData(count);
	float cx = 0.f, cy = 0.f, cz = 0.f;
	zx::levelmesh::ComputeCentroid(v, (unsigned)count, p.range.offset, p.range.count, cx, cy, cz);
	g_stats.bailLast.Format("%s at %u (map %.0f, %.0f, z %.0f), blend %d", zx::materials::MaterialName(p.material),
		p.range.offset, cx, cy, cz, p.blendMode);
}

void RefreshMovedGeometry(Diligent::IDeviceContext *ctx)
{
	// [rc4l] First in the frame's preparation, and a rebuild sends the level's piece records again in one frame.
	UploadFlushing flushing;
	if (!g_scene.vb || g_scene.cpuVerts.Size() == 0) return;
	// [rc4l] fua_dg_sectorlit changes how every level record is written, so its turn writes them all again.
	static int sectorLitWritten = -1;
	if (sectorLitWritten != (int)fua_dg_sectorlit)
	{
		const bool first = sectorLitWritten < 0;
		sectorLitWritten = (int)fua_dg_sectorlit;
		FString err;
		if (!first && BuildSceneBuffer(err))
		{
			g_scene.verts = (int)g_scene.cpuVerts.Size();
			g_pendingRepaints.Clear();
			zx::levelmesh::MeshClearRepaints();
			zx::levelmesh::MeshClearDirtyRanges();
			return;
		}
	}

	// [rc4l] Two questions, both answered by the mesh, neither of them "did a vertex move".
	//
	// A surface can change completely without moving: pressing a switch swaps a sidedef's texture,
	// and a door's light level changes the shading of everything it passes. This function used to
	// return on the dirty-vertex line below before asking anything else, so those changes arrived
	// only if they happened to coincide with geometry that moved. The switch never looked pressed.
	//
	// MeshRegisterPiece decides which of the two it was, once, for walls and floors alike -- see
	// features/surfaces/computation/surfacechange_compute.h.
	{
		const unsigned int rebatch = zx::levelmesh::MeshRebatchRevision();
		if (rebatch != g_builtRebatchRev)
		{
			// [rc4l] A surface that changed material or pass (a switch, a door, a flat swap) is moved to a batch of its own, since rebuilding the whole scene for it was a 57 ms frame on Sunder MAP16.
			g_builtRebatchRev = rebatch;
			const unsigned int *list = NULL;
			int count = 0, np = 0, translucent = 0;
			bool overflowed = false;
			zx::levelmesh::MeshTakeRebatches(list, count, overflowed);
			const zx::levelmesh::MeshPiece *all = zx::levelmesh::MeshPieces(np);
			for (int i = 0; i < count; i++) if ((int)list[i] < np && all[list[i]].blendMode != 0) translucent++;
			const ::hwrender::RebatchPlan plan = ::hwrender::ComputeRebatchPlan(count, overflowed, translucent);
			bool rebuild = (plan == ::hwrender::RebatchPlan::Rebuild);
			if (plan == ::hwrender::RebatchPlan::Append)
			{
				int srcCount = 0;
				const zx::levelmesh::MeshVertex *verts = zx::levelmesh::MeshVertexData(srcCount);
				for (int i = 0; i < count && !rebuild; i++)
					if ((int)list[i] < np && !MovePieceToNewBatch(ctx, all[list[i]], verts)) rebuild = true;
			}
			zx::levelmesh::MeshClearRebatches();
			FString err;
			if (rebuild && BuildSceneBuffer(err))
			{
				g_stats.geomRebuilds++;
				g_scene.verts = (int)g_scene.cpuVerts.Size();
				// A full rebuild uploaded every piece record, so anything queued is already applied.
				// Left in place it would repaint by INDEX into a piece list the rebuild reshuffled.
				g_pendingRepaints.Clear();
				zx::levelmesh::MeshClearRepaints();
				zx::levelmesh::MeshClearDirtyRanges();
				return;
			}
		}
	}
	// A repaint is the surface in the right place with stale shading baked into its record. Since the
	// shading moved out of the vertices and into that record, this is a sixty-four byte upload each.
	{
		// [rc4l] SPREAD the repaints, because they all arrive on the same frame.
		//
		// Doom's sim runs at 35 Hz and the renderer here runs at 400. Every light effect in the level
		// changes its sector on the tic, so one frame in eight is handed ~1,500 surfaces to re-shade
		// and the other seven are handed none. Even with the uploads coalesced that is a ~3 ms lump
		// landing 35 times a second -- which is what choppy movement IS, and why a healthy median
		// frame time never showed it.
		//
		// A repaint is a shading change, not a position change: a surface carrying last frame's light
		// for another two milliseconds is not visible, whereas a surface in last frame's PLACE would
		// be. So only these are deferred, never the geometry patch below, and the queue drains within
		// a couple of frames at any sane frame rate.
		const unsigned int repaint = zx::levelmesh::MeshRepaintRevision();
		if (repaint != g_builtRepaintRev)
		{
			g_builtRepaintRev = repaint;
			const unsigned int *list = NULL; int count = 0;
			zx::levelmesh::MeshTakeRepaints(list, count);
			for (int i = 0; i < count; i++) g_pendingRepaints.Push(list[i]);
			zx::levelmesh::MeshClearRepaints();
		}
		if (g_pendingRepaints.Size() > 0)
		{
			int np = 0;
			const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces(np);
			// Enough to drain a tic's worth in two or three frames. If the backlog ever runs away --
			// a level load, a frame rate collapse -- take the lot rather than let it grow without
			// bound; a one-off lump is better than shading that never catches up.
			// [rc4l] Per CALL, and that is not a slip -- see g_repaintBudget.
			const unsigned int kPerCall = 512, kFlushAll = 8192;
			unsigned int budget = (g_pendingRepaints.Size() > kFlushAll)
				? g_pendingRepaints.Size() : kPerCall;
			if (budget > g_pendingRepaints.Size()) budget = g_pendingRepaints.Size();
			const unsigned int *list = &g_pendingRepaints[0];
			const int count = (int)budget;
			// [rc4l] Fill the mirror first, upload in RUNS -- one UpdateBuffer per repaint was the
			// stutter.
			//
			// Doom's light effects run every tic, so on the frame after a tic a level with animated
			// lighting repaints a thousand-odd surfaces at once. Each one was a separate 64-byte
			// UpdateBuffer, and 1,548 driver calls do not cost 1,548 times nothing: it put a ~5 ms
			// spike on one frame in eight at 35 Hz, which reads as choppy mouse movement however
			// healthy the median looks. The frame-time MEDIAN never showed it -- that is exactly the
			// thing rc4l said the metrics were missing.
			//
			// g_scene.pieces is a full CPU mirror of the buffer, so any span of it is safe to
			// upload whether or not every entry in the span changed. That makes near-misses
			// mergeable: pieces baked together are allocated together, so a sector's surfaces land in
			// a handful of runs rather than a thousand scattered writes.
			static TArray<unsigned int> s_repaintSlots;
			s_repaintSlots.Clear();
			for (int i = 0; i < count && pieces != NULL; i++)
			{
				if ((int)list[i] >= np) continue;
				const zx::levelmesh::MeshPiece &p = pieces[list[i]];
				unsigned int *slotIdx = g_slotByOffset.CheckKey(p.range.offset);
				if (slotIdx == NULL || *slotIdx >= g_vbSlots.Size()) continue;
				const VBSlot &slot = g_vbSlots[*slotIdx];
				if (slot.count == 0 || slot.pieceIndex >= g_scene.pieces.Size()) continue;
				// [rc4l] A record that comes out as the GPU already holds it, as an animation frame's does when SlotRemap carries the frame, is not sent again.
				ScenePieceData pd;
				FillPieceData(p, 0, pd, true);
				if (memcmp(&pd, &g_scene.pieces[slot.pieceIndex], sizeof(pd)) == 0)
				{
					ZX_COUNT("gpu/repaint_same", "pieces", 1);
					continue;
				}
				g_scene.pieces[slot.pieceIndex] = pd;
				s_repaintSlots.Push(slot.pieceIndex);
				g_stats.geomRepaints++;
				g_stats.fRepaints++;
			}
			if (g_scene.pieceBuf && s_repaintSlots.Size() > 0)
			{
				std::sort(&s_repaintSlots[0], &s_repaintSlots[0] + s_repaintSlots.Size());
				// Bridging a gap re-sends entries that did not change. 32 of them is 2 KB of slack
				// against a whole driver call, which is the trade every time.
				const unsigned int kBridge = 32;
				unsigned int runStart = s_repaintSlots[0], runEnd = runStart;
				for (unsigned int i = 1; i <= s_repaintSlots.Size(); i++)
				{
					const bool end = (i == s_repaintSlots.Size());
					if (!end && s_repaintSlots[i] <= runEnd + kBridge)
					{
						if (s_repaintSlots[i] > runEnd) runEnd = s_repaintSlots[i];
						continue;
					}
					SendToBuffer(ctx, g_scene.pieceBuf,
						(Diligent::Uint64)runStart * sizeof(ScenePieceData),
						(Diligent::Uint64)(runEnd - runStart + 1) * sizeof(ScenePieceData),
						&g_scene.pieces[runStart]);
					ZX_COUNT("gpu/repaint_sent", "pieces", runEnd - runStart + 1);
					g_stats.repaintRuns++;
					g_stats.fRuns++;
					if (!end) { runStart = runEnd = s_repaintSlots[i]; }
				}
			}
			// Drop what was just applied; whatever is left rides to the next frame.
			g_pendingRepaints.Delete(0, (int)budget);
		}
	}
	unsigned int lo = 0, hi = 0;
	zx::levelmesh::MeshTakeDirty(lo, hi);
	// [rc4l] Clear the itemised list HERE too, or it accumulates across every quiet frame until
	// it overflows -- and an overflowed list reports "assume everything moved", which sent this
	// straight to a full rebuild every frame while claiming the layout was stable. 0 patches, 188
	// rebuilds, and a generation that matched the one the buffer was built for.
	if (hi <= lo) { zx::levelmesh::MeshClearDirtyRanges(); g_stats.geomUpdates = 0; return; }
	g_stats.lastDirtyLo = lo; g_stats.lastDirtyHi = hi;

	int srcCount = 0;
	const zx::levelmesh::MeshVertex *src = zx::levelmesh::MeshVertexData(srcCount);
	int npieces = 0;
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces(npieces);
	if (src == NULL || pieces == NULL) return;

	// [rc4l] Patch what moved; rebuild only when the LAYOUT moved.
	//
	// This used to call BuildSceneBuffer unconditionally, which sorts every piece in the level and
	// re-uploads the whole vertex buffer. The comment justifying that said it "only happens on the
	// frames something actually moves" -- true on a small map, and on Sunder MAP16 that is every
	// frame, because something is always being baked, animating or lit differently. 6506 full
	// rebuilds in a couple of hundred tics, and the backend cost 4.5 ms a frame while its actual
	// drawing measured 0.23.
	//
	// Patching in place was tried before and abandoned for a real reason: a re-baked piece can change
	// vertex count, MeshStore then gives it a NEW range, and every offset recorded at upload time is
	// stale from that moment -- the shut door stayed painted across an open doorway. The answer is not
	// to give up on patching but to NOTICE: a signature over every piece's offset, size, material,
	// base texture and blend mode says whether the layout is the one this buffer was built for. When
	// it is not, the full rebuild still happens, exactly as before.
	// [rc4l] A layout change no longer means a rebuild.
	//
	// It used to: the generation moved, the whole buffer was sorted and re-uploaded, and on Sunder
	// MAP16 that happened every frame because ~21 pieces a tic are baked or re-baked at a new size.
	// Rebuilds went 6506 -> 22 by patching what moved, and the frame did not shift at all, because
	// the 22 that remained were each 17 ms.
	//
	// So the three things that can happen to a piece are handled where they happen: patched in place
	// if it is the size its slot expects, appended at the end if it is new or has outgrown its slot,
	// and collapsed to zero area if the world has stopped using it. A full rebuild is what happens
	// when the slack runs out, which also reclaims everything the collapses left behind.
	if (g_vbSlots.Size() > 0 && g_scene.vb)
	{
		int ndirty = 0;
		const zx::levelmesh::MeshRange *dirty = zx::levelmesh::MeshTakeDirtyRanges(ndirty);
		if (dirty != NULL)
		{
			bool bailed = false;
			int patched = 0;
			for (int d = 0; d < ndirty && !bailed; d++)
			{
				const unsigned int dl = dirty[d].offset;

				// The piece that owns this range now, which may be a different one from last frame.
				const zx::levelmesh::MeshPiece *p = zx::levelmesh::MeshPieceByOffset(dl);
				unsigned int *slotIdx = g_slotByOffset.CheckKey(dl);

				if (p == NULL || p->range.count == 0)
				{
					// The world dropped it. Collapse whatever is still being drawn there.
					if (slotIdx != NULL && *slotIdx < g_vbSlots.Size() && g_vbSlots[*slotIdx].count > 0)
						RetireSlot(ctx, g_vbSlots[*slotIdx]);
					continue;
				}

				if (slotIdx != NULL && *slotIdx < g_vbSlots.Size() &&
					g_vbSlots[*slotIdx].count == p->range.count)
				{
					const VBSlot &slot = g_vbSlots[*slotIdx];
					// [rc4l] A translucent piece is sorted by where it is, so it is patched only in its own blend mode and where it stood (a scrolling waterfall), and anything that moved is rebuilt.
					static TArray<SceneVertex> moved;
					moved.Resize(slot.count);
					for (unsigned int v = 0; v < slot.count; v++) moved[v] = ToSceneVertex(src[slot.meshOffset + v], slot.pieceIndex);
					const bool blendedNow = (p->blendMode != 0);
					if (blendedNow || slot.blended)
					{
						const bool sameBlend = slot.blended == blendedNow && slot.batch < g_scene.batches.Size() &&
							g_scene.batches[slot.batch].blend == p->blendMode;
						if (!sameBlend) { g_stats.bailBlendedSlot++; NoteBail(*p); bailed = true; break; }
						if (!zx::hwrender::ComputeSamePositions(&g_scene.cpuVerts[slot.vbStart], &moved[0], slot.count))
							{ g_stats.bailBlend++; NoteBail(*p); bailed = true; break; }
					}
					// [rc4l] A piece whose MATERIAL changed cannot be patched in place.
					//
					// A batch is a run of pieces that share a material and, without bindless, one
					// binding. Rewriting a piece's vertices where it lies leaves it in a batch that
					// still names the old texture, so it goes on being drawn with it -- which is why
					// pressing a switch left the switch looking unpressed. The seg re-bakes correctly
					// (side_t::SetTexture bumps fua_dirty); it is the batching that has to be redone,
					// and a rebuild is the only thing that can move a piece between batches.
					if (slot.batch < g_scene.batches.Size() && !::hwrender::ComputeStaysInBatch(p->material, p->baseTex,
							g_scene.batches[slot.batch].material, g_scene.batches[slot.batch].baseTex))
					{
						// [rc4l] An opaque one moves to a batch of its own; a translucent one is sorted by distance and still needs the rebuild.
						if (blendedNow || slot.blended || !MovePieceToNewBatch(ctx, *p, src))
							{ g_stats.bailMaterial++; NoteBail(*p); bailed = true; break; }
						patched++;
						continue;
					}
					// [rc4l] The record is rewritten in place and the index does not move.
					//
					// A door's shading changes as it moves -- its light level, and on a slope its normal --
					// so patching the vertices alone would leave the surface lit for where it used to be.
					if (slot.pieceIndex < g_scene.pieces.Size())
					{
						FillPieceData(*p, 0, g_scene.pieces[slot.pieceIndex], true);
						SendToBuffer(ctx, g_scene.pieceBuf,
							(Diligent::Uint64)slot.pieceIndex * sizeof(ScenePieceData), sizeof(ScenePieceData),
							&g_scene.pieces[slot.pieceIndex]);
					}
					for (unsigned int v = 0; v < slot.count; v++) g_scene.cpuVerts[slot.vbStart + v] = moved[v];
					SendToBuffer(ctx, g_scene.vb, (Diligent::Uint64)slot.vbStart * sizeof(SceneVertex),
						(Diligent::Uint64)slot.count * sizeof(SceneVertex), &g_scene.cpuVerts[slot.vbStart]);
					g_stats.geomPatchVerts += (int)slot.count;
					g_stats.fPatchVerts += (int)slot.count;
					patched++;
					continue;
				}

				// [rc4l] A translucent piece with no slot of its size cannot be appended into the sorted pass, so it is rebuilt.
				if (p->blendMode != 0) { g_stats.bailBlend++; NoteBail(*p); bailed = true; break; }
				// New, or no longer the size its slot holds: the old vertices stop being drawn and the
				// piece goes on the end.
				// A slot that is merely EMPTY can take the piece back -- see ReviveSlot.
				if (slotIdx != NULL && *slotIdx < g_vbSlots.Size() &&
					ReviveSlot(ctx, g_vbSlots[*slotIdx], *p, src))
				{
					g_stats.slotRevived++;
					patched++;
					continue;
				}
				if (slotIdx != NULL && *slotIdx < g_vbSlots.Size() && g_vbSlots[*slotIdx].count > 0)
				{
					g_stats.appendResized++;
					RetireSlot(ctx, g_vbSlots[*slotIdx]);
				}
				else g_stats.appendNoSlot++;
				if (!AppendPiece(ctx, *p, src)) { bailed = true; break; }
			}

			// [rc4l] Rebuild when the slack is spent, not when the layout changes. The collapsed slots
			// and the extra batches are both reclaimed by the sort, so this is compaction rather than
			// failure -- and it happens on the order of once per level rather than once per frame.
			// [rc4l] Compact when the BATCH LIST has bloated, not only when the buffer has.
			//
			// An appended piece gets a batch of its own, and a batch costs a material binding and a draw
			// call every frame forever. Sunder MAP16 fully baked reached 2348 batches where the sorted
			// build produces 168, and submit time went 0.23 ms to 0.54 with it. The sort is what merges
			// them back together, so the sort is worth running again once enough have piled up.
			//
			// Half again as many as the last sorted build: often enough to keep the cost near the
			// compacted figure, rare enough that a rebuild is not what the frame is doing.
			const bool batchesBloated = (g_builtBatchCount > 0) &&
				((int)g_scene.batches.Size() > g_builtBatchCount + g_builtBatchCount / 2 + 32);
			const bool slackSpent = (g_scene.appendedVerts > (int)(g_scene.vbCapacity / 8)) || batchesBloated;
			if (!bailed && !slackSpent)
			{
				if (patched > 0 || g_stats.geomAppends > 0) g_stats.geomPatches++;
				zx::levelmesh::MeshClearDirtyRanges();
				g_scene.builtLayoutGen = zx::levelmesh::MeshLayoutGeneration();
				g_scene.verts = (int)g_scene.cpuVerts.Size();
				g_stats.geomUpdates = 0;
				return;
			}
		}
	}

	// [rc4l] The layout changed -- or a translucent surface moved, or a piece resized under its own
	// offset. A full rebuild costs a sort and an upload, and is now what happens when it has to.
	FString err;
	g_stats.geomUpdates = BuildSceneBuffer(err) ? 1 : 0;
	g_scene.verts = (int)g_scene.cpuVerts.Size();
	g_stats.geomRebuilds += g_stats.geomUpdates;
	zx::levelmesh::MeshClearDirtyRanges();
	(void)src; (void)pieces; (void)npieces;
}

}} // namespace zx::hwrender
