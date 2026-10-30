// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The world pipelines and what binds to them: the bindless material array and its slots, animated slots, and the per-material bindings.

#include "features/hwshaders/hwshaders.h"   // [rc4l] the GLDEFS shader list
#include "features/hwshaders/computation/hwshader_compute.h"
#include "features/hwrender/computation/materialindex_compute.h"
#include "features/hwrender/computation/slotreport_compute.h"   // [Doruk-Aksoy] fua_dg_slotreport
#include "features/hwrender/computation/slotrecycle_compute.h"   // [Doruk-Aksoy] which slot gives way, which a fill writes
#include "features/hwrender/computation/retiredbindings_compute.h"   // [Doruk-Aksoy] RetiredBindings
#include "features/hwrender/computation/resourcestate_compute.h"   // [Doruk-Aksoy] ComputeBufferReadState
#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/computation/dynlayout_compute.h"   // [rc4l] ComputeDynWordsCapacity
#include "features/hwrender/computation/sectorlitrecord_compute.h"   // [rc4l] ComputeSectorLightCapacity
#include "features/hwrender/diligent/dgscene_glsl.h"   // [rc4l] the world pass's shaders
#include "features/telemetry/telemetry.h"   // [Doruk-Aksoy] load/user_shaders

#include <string.h>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>
#include <set>
#include <string>
#include "w_wad.h"   // [rc4l] the shader lumps

// [rc4l] Off makes every commit in a draw loop check every resource again, the old cost, for an A/B in one session.
CVAR(Bool, fua_dg_loopcommits, true, 0)
// [Doruk-Aksoy] A world binding's resources checked once after each fill rather than once per draw loop, what changed since made ready before it is next committed; 0 checks them per loop, to bisect or compare.
CVAR(Bool, fua_dg_checkonce, true, 0)
// [Doruk-Aksoy] Translucent and additive sprites drawn by one premultiplied pipeline, each by its own blend, so the sorted pass switches less; a colour differs by at most one level from off, which the Video Mode menu offers.
CVAR(Bool, fua_dg_premulblend, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
EXTERN_CVAR(Float, r_texture_filter_anisotropic)
EXTERN_CVAR(Int, fua_dg_spirv_optimize)
// [Doruk-Aksoy] Off makes pipelines one after another, and a frame first drawing with a GLDEFS shader's pipeline wait for it rather than draw that piece plain until it is ready.
CVAR(Bool, fua_dg_async_pipelines, true, CVAR_ARCHIVE)

namespace zx { namespace hwrender {

// [rc4l] One SRB per material, not one SRB re-pointed per draw.
//
// The draw loop used to do `var->Set(srv); CommitShaderResources(srb); Draw();` in a loop over a
// single shared SRB, which is a natural-looking way to write it and is wrong. A MUTABLE shader
// variable is baked into the SRB's descriptor set when the SRB is committed; re-setting it between
// draws inside one command buffer does not produce a new descriptor set, so every batch in the frame
// ends up sampling whichever texture the set last held. The whole world rendered in a single
// repeating texture -- and it looked *plausible*, because Doom levels reuse textures heavily.
//
// Ownership is explicit: ZDoom's TArray moves its storage with realloc, which is not safe for a
// refcounting smart pointer, so the SRBs are held as raw AddRef'd pointers and released by hand on
// re-upload. (Storing RefCntAutoPtr in a TArray crashed on the first upload.)
static TArray<Diligent::IShaderResourceBinding *> g_batchSRBs;

void ReleaseBatchSRBs()
{
	for (unsigned i = 0; i < g_batchSRBs.Size(); i++)
		if (g_batchSRBs[i]) g_batchSRBs[i]->Release();
	g_batchSRBs.Clear();
}

// [rc4l] Material -> SRB, cached for the life of the level.
//
// The static path can build its SRBs once at upload, but the dynamic path (sprites) sees a different
// set of materials every frame, and creating SRBs per frame would allocate descriptor sets at 60 Hz.
// Keyed on the FMaterial pointer, which is stable per level.
// [rc4l] Keyed on (PSO, material), not material alone: an SRB is created from a pipeline and is only
// valid with it, so the translucent and additive passes need their own even for the same texture.
struct MatSRB { const void *material; const void *pso; int translation; Diligent::IShaderResourceBinding *srb; };
static TArray<MatSRB> g_matSRBs;

// [Doruk-Aksoy] The key the slot table and the bindings file a translation under, so redefined colours get their own slot or binding rather than the old one's.
static int SlotKey(int translation)
{
	return zx::hwrender::ComputeSlotTranslation(translation, zx::materials::MaterialTranslation(translation));
}

Diligent::IShaderResourceBinding *GetMaterialSRB(Diligent::IPipelineState *pso,
	const void *material, int translation)
{
	if (!pso) return NULL;
	translation = SlotKey(translation);
	for (unsigned i = 0; i < g_matSRBs.Size(); i++)
		if (g_matSRBs[i].material == material && g_matSRBs[i].pso == pso &&
		    g_matSRBs[i].translation == translation) return g_matSRBs[i].srb;

	MatSRB e;
	e.material = material;
	e.pso = pso;
	e.translation = translation;
	e.srb = NULL;
	pso->CreateShaderResourceBinding(&e.srb, true);
	if (e.srb)
	{
		if (auto *v = e.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
			v->Set(GetMaterialSRV(material, translation));
		// [rc4l] Every material gets a brightmap, black when it has none, so the shader needs no
		// flag and no second pipeline permutation. See GetBrightmapSRV.
		//
		// Reported once rather than assumed: an unbound sampler reads UNDEFINED, which in practice is
		// whatever texture was bound last, and adding that to the fragment saturates every bright
		// surface to white. "The variable was not found" and "the black texture failed to create"
		// both produce exactly that, and neither says anything on its own.
		{
			auto *v = e.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uBrightmap");
			Diligent::ITextureView *bmv = GetBrightmapSRV(material);
			static bool reported = false;
			if (!reported)
			{
				reported = true;
				Printf("Diligent brightmap: variable %s, black texture %s\n",
					v ? "FOUND" : "MISSING (sampler will be unbound)",
					bmv ? "ok" : "FAILED TO CREATE");
			}
			if (v && bmv) v->Set(bmv);
		}
	}
	g_matSRBs.Push(e);
	return e.srb;
}

// [rc4l] The scene pipeline a static batch draws with.
void SetBlendEquation(Diligent::RenderTargetBlendDesc &rt, int blend)
{
	static const Diligent::BLEND_FACTOR kFactor[] = { Diligent::BLEND_FACTOR_SRC_ALPHA, Diligent::BLEND_FACTOR_INV_SRC_ALPHA,
		Diligent::BLEND_FACTOR_ONE, Diligent::BLEND_FACTOR_DEST_COLOR };
	static const Diligent::BLEND_OPERATION kOp[] = { Diligent::BLEND_OPERATION_ADD, Diligent::BLEND_OPERATION_SUBTRACT,
		Diligent::BLEND_OPERATION_REV_SUBTRACT };
	const zx::hwrender::BlendEquation e = zx::hwrender::ComputeBlendEquation(blend);
	rt.BlendEnable = true;
	rt.SrcBlend = kFactor[e.src];
	rt.DestBlend = kFactor[e.dst];
	rt.BlendOp = kOp[e.op];
	rt.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
	rt.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
	rt.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
}

Diligent::IPipelineState *BatchPSO(int blend)
{
	return g_pso.world[ComputeBatchPipeline(blend)].RawPtr();
}

// [rc4l] A batch's binding, made again for the frame it last resolved to whenever a flush, a resize or a minimise dropped it.
Diligent::IShaderResourceBinding *BatchSRB(SceneBatch &b)
{
	// [Doruk-Aksoy] While the material array carries the world a batch draws with the world binding, so none of its own is made, each of which holds the whole array.
	if (BindlessActive()) return WorldSRB(BatchPSO(b.blend));
	if (b.srb == NULL) b.srb = GetMaterialSRB(BatchPSO(b.blend), b.resolved);
	return b.srb;
}

// [rc4l] The material slot table: the level's textures, numbered.
//
// Slot 0 is always the white placeholder, so an overflow or an unresolvable material draws white
// rather than drawing whatever happens to be in slot 0.
//
// Keyed on (material, translation) for the same reason GetMaterialSRV is: a translated sprite is a
// different image of the same material, and giving them one slot paints the marine's armour on his
// twin.
// [rc4l] 512, and the number is not arbitrary.
//
// Sunder MAP16 uses 198 materials, so 256 is close enough to the cap to be a trap. The cost is
// slots x 13 pipelines = 6656 combined-image-sampler descriptors, and the ceiling this hit before
// -- 64 slots x 149 per-material bindings = 9536 worked, 128 x 149 = 19072 killed the device --
// puts the limit above 9.5k. 6656 sits under it with room, and does not move when a level has more
// materials, which is the whole difference between this shape and the one that failed.
// [rc4l] How many materials one bindless array holds -- and 512 was not enough for real content.
//
// Sunder and the eon maps fit in 512 comfortably, so every measurement in this work was taken with
// bindless ON and nothing suggested the cap mattered. A nine-PWAD Whodunit load wants 835, and past
// the cap MaterialSlotFor gives up and returns slot 0, which turns bindless OFF for the whole level:
// the world went from ONE draw call to 670, sprites from bindless to a descriptor binding each, and
// submit from 2.85 ms to 3.82. It is the loudest failure mode in the backend and it is a constant.
//
// 4096 is chosen to be past what content plausibly asks for rather than tuned to 835. The cost is a
// larger descriptor array, not a larger frame -- the array is sized once and indexed per pixel.
//
// [rc4l] 4096 is the most it is given: SizeMaterialSlots (dgprobe.cpp) sizes it from the device's own
// descriptor limits, which the Vulkan minimum puts far lower than a desktop driver, and a device left
// with too few draws with a binding per material instead of failing pipeline creation.

// [rc4l] `resolved` is what the slot actually POINTS AT, which is not always what it is keyed on.
//
// An animated texture keeps one FMaterial as its identity and swaps which image that resolves to
// every eight tics. The old path re-pointed the batch's binding; here the slot's contents change
// and its key does not, so the geometry never has to know that nukage flows.
static TArray<MatSlot> g_matSlotTable;
// [rc4l] Per slot, the slot showing its texture's current ANIMDEFS frame, on the GPU as SlotRemap; made once, since a static binding cannot follow a replaced buffer.
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_slotRemapBuf;
static std::vector<unsigned> g_slotRemap;
static bool g_matSlotOverflow = false;
// The static array copied into the pipelines is only as fresh as the last fill. Anything that
// changes what a slot should point at -- a new material, an animated texture reaching its next
// frame -- raises this, and the next frame refills and rebuilds the world bindings.
static bool g_matSlotsDirty = true;
// [rc4l] The level this table describes. Nothing is filled from it until the scene has been rebuilt
// for that level, because filling means calling GetMaterialSRV -- which CACHES what it makes, keyed
// on the FMaterial address. Doing that with the previous level's pointers, in the window before the
// new level's geometry arrives, teaches the cache things the new level then believes.
static int g_bindlessGen = -1;

// [rc4l] A side index over the slot table, because the table is SCANNED ONCE PER PIECE.
//
// MaterialSlotFor was a linear walk of the table, and every dynamic piece calls it to find out which
// texture it draws with. Sunder MAP16 has 3053 sprites a frame and 335 slots, so that is up to a
// million pointer comparisons a frame -- and it measured as 0.239 ms of the dynamic build's 0.493,
// half of it, and ten times what writing all the vertices costs.
//
// The table stays the source of truth: slot INDICES are what the shader reads, so they cannot move.
// This only answers "which index" without walking. Open addressing (computation/materialindex_compute), cleared with the
// table it indexes -- there are exactly two places that mutate it and both are next to this comment.
static zx::hwrender::MaterialIndex g_matIndex;

static void MatIndexClear()
{
	zx::hwrender::ComputeMaterialIndexClear(g_matIndex);
}

static void MatIndexInsert(const void *material, int translation, int slot)
{
	zx::hwrender::ComputeMaterialIndexInsert(g_matIndex, material, translation, slot);
}

// -1 when absent.
static int MatIndexFind(const void *material, int translation)
{
	return zx::hwrender::ComputeMaterialIndexFind(g_matIndex, material, translation);
}

// [Doruk-Aksoy] Every key the level asked for, each once, and when a dynamic piece last asked for each, so fua_dg_slotreport counts keys rather than slots a recycled one has held for several.
static TArray<MatSlot> g_seenKeys;
static zx::hwrender::MaterialIndex g_seenIndex;
static std::vector<zx::hwrender::SlotStamp> g_keyStamps;
static std::vector<int> g_workingSet, g_workingPeaks;
static int g_keysMeasured = 0, g_recycledMeasured = 0;
static const std::vector<double> kWorkingWindowsMs = { 1000, 2000, 5000 };

// [Doruk-Aksoy] The key's place among those seen, added the first time it is asked for.
static int SeenKey(const void *material, int translation)
{
	const int at = zx::hwrender::ComputeMaterialIndexFind(g_seenIndex, material, translation);
	if (at >= 0) return at;
	zx::hwrender::ComputeMaterialIndexInsert(g_seenIndex, material, translation, (int)g_seenKeys.Size());
	MatSlot e; e.material = material; e.translation = translation;
	g_seenKeys.Push(e);
	return (int)g_seenKeys.Size() - 1;
}

// [Doruk-Aksoy] Per slot beside the table: pinned (the white one, or a baked piece's, whose record froze its number), whether it is a baked animated base SlotRemap follows, the dynamic reservation that last used it, its tries at a texture that came back white, and its key's place among those seen (-1 until something asks for the key, since an ANIMDEFS frame takes a slot unasked).
struct SlotState { bool pinned; bool animated; uint32_t reserved; int whiteTries; int seen; };
static std::vector<SlotState> g_slotState;
// [Doruk-Aksoy] Per slot, the table generation that last changed what it holds, which a fill compares with its own.
static std::vector<uint32_t> g_slotChanged;
static uint32_t g_slotGen = 0;
// [Doruk-Aksoy] Counts the dynamic reservations, so a slot either of the last two used, whose records may still be drawn, never gives way.
static uint32_t g_reserveSerial = 1;
// [Doruk-Aksoy] The frame whose fill is made, until whose end no slot may change under the binding it draws with; none between frames.
static uint64_t g_sealedFrame = ~0ull;
// [Doruk-Aksoy] The slots that may give way, least recently reserved first, made the first time a reservation needs one.
static std::vector<int> g_recycleOrder;
static size_t g_recycleNext = 0;
static uint32_t g_recycleOrderFor = 0;
// [Doruk-Aksoy] Slots given to new keys since the level began, and keys refused once none could give way.
static int g_slotsRecycled = 0, g_keysRefused = 0;
// [Doruk-Aksoy] Every slot written at the next fill, for when the textures behind the slots changed rather than the table.
static bool g_forceFill = true;
static const int kWhiteTries = 120;

static void NoteSlotChanged(int slot)
{
	g_slotChanged[slot] = ++g_slotGen;
	g_slotState[slot].whiteTries = 0;
	g_matSlotsDirty = true;
}

static void Stamp(std::vector<zx::hwrender::SlotStamp> &stamps, int at)
{
	// [Doruk-Aksoy] The clock read once a frame, not once a sprite, since every dynamic piece stamps a frame and the windows need no finer time.
	static long long clockFrame = -1;
	static double clockMs = 0;
	const long long frame = (long long)FrameBeingBuilt();
	if (frame != clockFrame) { clockFrame = frame; clockMs = zx::telemetry::NowNs() / 1e6; }
	if ((int)stamps.size() <= at) stamps.resize(at + 1);
	stamps[at] = { frame, clockMs };
}

// [Doruk-Aksoy] The place among those seen of the key slot holds, looked up the first time anything asks for that key there, or every time for a key left on white slot 0.
static int SeenOf(int slot, const void *material, int key)
{
	if (slot <= 0) return SeenKey(material, key);
	int &seen = g_slotState[slot].seen;
	if (seen < 0) seen = SeenKey(material, key);
	return seen;
}

void NoteDynamicSlotUse(int slot, const void *material, int translation)
{
	if (material == NULL) return;
	const int seen = slot > 0 ? g_slotState[slot].seen : -1;
	Stamp(g_keyStamps, seen >= 0 ? seen : SeenOf(slot, material, SlotKey(translation)));
}

void MeasureSlotWorkingSet()
{
	const long long frame = (long long)FrameBeingBuilt() - 1;
	const double now = zx::telemetry::NowNs() / 1e6;
	g_workingSet.clear();
	zx::hwrender::ComputeWorkingSet(g_keyStamps, frame, now, kWorkingWindowsMs, g_workingSet);
	zx::hwrender::ComputeKeepPeaks(g_workingPeaks, g_workingSet);
	const int keys = (int)g_seenKeys.Size();
	ZX_COUNT("gpu/slots/frame_set", "keys", g_workingSet[0]);
	ZX_COUNT("gpu/slots/window_2s", "keys", g_workingSet[2]);
	ZX_COUNT("gpu/slots/new", "keys", keys > g_keysMeasured ? keys - g_keysMeasured : 0);
	ZX_COUNT("gpu/slots/recycled", "slots", g_slotsRecycled - g_recycledMeasured);
	g_keysMeasured = keys;
	g_recycledMeasured = g_slotsRecycled;
}

// [Doruk-Aksoy] Counts the table's resets, so slots kept for a sprite list are never read against a table made since.
static unsigned int g_slotTableId = 0;
unsigned int MaterialSlotTableId() { return g_slotTableId; }

void ResetMaterialSlots()
{
	g_slotTableId++;
	g_matSlotTable.Clear();
	MatIndexClear();
	MatSlot white; white.material = NULL; white.translation = 0;
	g_matSlotTable.Push(white);
	MatIndexInsert(white.material, white.translation, 0);
	g_slotState.assign(1, SlotState{ true, false, 0, 0, -1 });
	g_slotChanged.assign(1, ++g_slotGen);
	g_seenKeys.Clear();
	zx::hwrender::ComputeMaterialIndexClear(g_seenIndex);
	g_keyStamps.clear();
	g_workingPeaks.clear();
	g_keysMeasured = 0;
	g_recycledMeasured = 0;
	g_slotsRecycled = 0;
	g_keysRefused = 0;
	g_recycleOrder.clear();
	g_recycleNext = 0;
	g_recycleOrderFor = 0;
	g_sealedFrame = ~0ull;
	g_matSlotOverflow = false;
	g_matSlotsDirty = true;
	g_forceFill = true;
}

// [rc4l] What a slot holds: a material in its translation, or, under kBrightmapTranslation, that material's brightmap (black for none).
Diligent::IDeviceObject *MaterialSlotSRV(const MatSlot &e)
{
	switch (zx::hwrender::ComputeSlotKind(e.material, e.translation))
	{
	case zx::hwrender::kSlotBrightmap: return GetBrightmapSRV(e.material);
	case zx::hwrender::kSlotMaterial: return GetMaterialSRV(e.material, e.translation);
	default: return GetMaterialSRV(NULL, 0);
	}
}

static int PushMaterialSlot(const void *material, int translation, bool pin)
{
	MatSlot e; e.material = material; e.translation = translation;
	g_matSlotTable.Push(e);
	const int slot = (int)g_matSlotTable.Size() - 1;
	MatIndexInsert(material, translation, slot);
	// [Doruk-Aksoy] Reserved by the reservation running, so a key asked for right after it (the piece's own brightmap) cannot take it back.
	g_slotState.push_back(SlotState{ pin, false, g_reserveSerial, 0, -1 });
	g_slotChanged.push_back(0);
	NoteSlotChanged(slot);
	return slot;
}

// [Doruk-Aksoy] The slot that gives way to a new key in a full table, or -1 when none may.
static int RecycleVictim()
{
	if (g_recycleOrderFor != g_reserveSerial)
	{
		std::vector<zx::hwrender::SlotTenure> uses(g_slotState.size());
		for (size_t i = 0; i < uses.size(); i++) uses[i] = { g_slotState[i].pinned, g_slotState[i].reserved };
		g_recycleOrder = zx::hwrender::ComputeRecycleOrder(uses, g_reserveSerial);
		g_recycleNext = 0;
		g_recycleOrderFor = g_reserveSerial;
	}
	while (g_recycleNext < g_recycleOrder.size())
	{
		const int slot = g_recycleOrder[g_recycleNext++];
		if (zx::hwrender::ComputeMayRecycle({ g_slotState[slot].pinned, g_slotState[slot].reserved }, g_reserveSerial)) return slot;
	}
	return -1;
}

// [Doruk-Aksoy] Slot `slot` given to a new key: the old key forgotten, its texture left in the texture cache.
static int RecycleSlot(int slot, const void *material, int translation, bool pin)
{
	MatSlot &e = g_matSlotTable[slot];
	zx::hwrender::ComputeMaterialIndexErase(g_matIndex, e.material, e.translation);
	e.material = material;
	e.translation = translation;
	MatIndexInsert(material, translation, slot);
	g_slotState[slot] = SlotState{ pin, false, g_reserveSerial, 0, -1 };
	NoteSlotChanged(slot);
	g_slotsRecycled++;
	return slot;
}

static bool SlotsSealed() { return g_sealedFrame == FrameBeingBuilt(); }

// [Doruk-Aksoy] A slot for a key the table lacks: a new one while there is room, else one that gives way, else 0 (white), for this frame while the table is sealed and for the level when none may give way.
static int NewMaterialSlot(const void *material, int translation, bool pin)
{
	if (g_matSlotTable.Size() < MaterialSlotCapacity()) return PushMaterialSlot(material, translation, pin);
	if (SlotsSealed()) return 0;
	const int victim = MaterialSlotCapacity() > 0 ? RecycleVictim() : -1;
	if (victim > 0) return RecycleSlot(victim, material, translation, pin);
	// Loud, once: a level past the slot count draws its extra materials white, and white walls
	// with no message is the kind of thing that gets diagnosed as a texture-loading fault.
	if (!g_matSlotOverflow)
		Printf("Diligent bindless: more than %u materials in use at once on this level -- falling back to a binding per material"
			"\n", MaterialSlotCapacity());
	g_matSlotOverflow = true;
	g_keysRefused++;
	return 0;
}

// [rc4l] The engine's animations as plain records and the one each texture index belongs to, rebuilt when ANIMDEFS or the texture list changes size.
static std::vector<zx::hwrender::AnimationDef> g_anims;
static std::vector<int> g_animOfTex;

int AnimationOf(const void *material)
{
	zx::materials::MaterialInfo info;
	if (!zx::materials::DescribeMaterial(material, info)) return -1;
	const TArray<FAnimDef *> &defs = TexMan.GetAnimations();
	if (g_anims.size() != defs.Size() || g_animOfTex.size() != (size_t)TexMan.NumTextures())
	{
		g_anims.clear();
		for (unsigned i = 0; i < defs.Size(); i++)
		{
			zx::hwrender::AnimationDef a;
			a.basePic = defs[i]->BasePic.GetIndex();
			a.numFrames = defs[i]->NumFrames;
			a.discrete = defs[i]->bDiscrete;
			if (a.discrete)
				for (int f = 0; f < defs[i]->NumFrames; f++) a.pics.push_back(defs[i]->Frames[f].FramePic.GetIndex());
			g_anims.push_back(a);
		}
		g_animOfTex = zx::hwrender::ComputeAnimationOfTexture(TexMan.NumTextures(), g_anims);
	}
	const int id = info.textureIndex;
	return (id >= 0 && id < (int)g_animOfTex.size()) ? g_animOfTex[id] : -1;
}

// [Doruk-Aksoy] A baked piece's slot pinned, with every ANIMDEFS frame SlotRemap may show in its place, given a slot when missing.
// [rc4l] Every frame of an animated texture gets a slot, so SlotRemap can point at any frame without a new binding.
static void PinWithFrames(int slot, const void *material, int translation)
{
	g_slotState[slot].pinned = true;
	if (g_slotState[slot].animated) return;
	const int anim = AnimationOf(material);
	if (anim < 0) return;
	g_slotState[slot].animated = true;
	for (const int frame : zx::hwrender::ComputeAnimationFrames(g_anims[anim]))
	{
		const void *fm = zx::materials::MaterialForTextureIndex(frame);
		if (fm == NULL) continue;
		int at = MatIndexFind(fm, translation);
		if (at < 0) at = NewMaterialSlot(fm, translation, true);
		if (at > 0) g_slotState[at].pinned = true;
	}
}

int MaterialSlotFor(const void *material, int translation, bool inMesh)
{
	if (g_matSlotTable.Size() == 0) ResetMaterialSlots();
	translation = SlotKey(translation);
	int slot = MatIndexFind(material, translation);
	if (slot < 0) slot = NewMaterialSlot(material, translation, inMesh);
	else if (!inMesh) g_slotState[slot].reserved = g_reserveSerial;
	SeenOf(slot, material, translation);
	// [Doruk-Aksoy] Only a baked key's ANIMDEFS frames take slots, since a dynamic piece is given the frame it shows and never reads SlotRemap.
	if (slot > 0 && inMesh) PinWithFrames(slot, material, translation);
	return slot;
}

int FindReservedSlot(const void *material, int translation)
{
	const int slot = MatIndexFind(material, SlotKey(translation));
	if (slot > 0) g_slotState[slot].reserved = g_reserveSerial;
	return slot;
}

void BeginSlotReservation() { g_reserveSerial++; }
void OpenMaterialSlots() { g_sealedFrame = ~0ull; }
void SealMaterialSlots() { g_sealedFrame = FrameBeingBuilt(); }

void UnpinMaterialSlots()
{
	for (size_t i = 1; i < g_slotState.size(); i++) g_slotState[i].pinned = g_slotState[i].animated = false;
	g_recycleOrderFor = 0;
}

int MaterialSlotCount() { return (int)g_matSlotTable.Size(); }
// Force the array to be built again, for telling a stale fill from a wrong index.
void DirtyMaterialSlots() { g_matSlotsDirty = true; }
void ForceMaterialRefill() { g_forceFill = true; g_matSlotsDirty = true; }

// [rc4l] ONE binding per pipeline, which is the whole point.
//
// A STATIC shader variable is copied into every SRB made from the pipeline, and this backend makes
// one SRB per (pipeline, material) -- so a 256-slot array bound the old way would cost 256 times the
// material count in descriptors and take the device down. Bindless replaces those bindings rather
// than joining them: with the array in place there is nothing left that varies per material, so
// one binding per world pipeline serves the whole world. See docs/bindless-attempt-notes.md.
static Diligent::IShaderResourceBinding *g_worldSRBs[kWorldPipelines] = { 0 };
// [Doruk-Aksoy] Resources written since the last world commit, each made ready to read before the next one, since that commit checks nothing.
static std::vector<const void *> g_writtenSeen;
static std::vector<Diligent::RefCntAutoPtr<Diligent::IBuffer>> g_writtenBuffers;
static std::vector<Diligent::RefCntAutoPtr<Diligent::ITexture>> g_writtenTextures;
// [Doruk-Aksoy] The world bindings whose resources were checked since they were last filled.
static std::vector<const void *> g_worldChecked;
static bool g_bindlessReady = false;

// [Doruk-Aksoy] One fill's bindings, a pointer per world pipeline, a shared one holding a reference of its own.
// [Doruk-Aksoy] And the table generation it was filled at.
struct WorldBindings { Diligent::IShaderResourceBinding *srbs[kWorldPipelines]; uint32_t filledGen; };
// [Doruk-Aksoy] The generation the current fill holds, and the live slots that came back white in it.
static uint32_t g_worldFilledGen = 0;
static std::vector<int> g_whiteSlots;
static std::vector<unsigned> g_slotsToWrite;

static void ReleaseBindings(WorldBindings &b)
{
	for (Diligent::IShaderResourceBinding *&srb : b.srbs)
		if (srb) { srb->Release(); srb = NULL; }
}

// [Doruk-Aksoy] Fills a refill replaced, filled again once the GPU has finished with them, since each new one is a descriptor set whose memory NVIDIA's driver keeps once freed.
static zx::hwrender::RetiredBindings<WorldBindings> g_retiredWorld;
static const size_t kRetiredWorldKept = 4;
// [Doruk-Aksoy] A fill is kept while it holds the table's generation, which only a change to what a slot holds moves, so a scene rebuild asking for the same table (dbab04 rebuilds about two hundred times a minute) refills nothing.

// What the scene pixel shader said it declares, recorded at creation so the question can be asked
// later -- pipeline setup happens before anything is listening on the console.
static FString g_scenePSResources;

void ReleaseWorldSRBs()
{
	for (int i = 0; i < kWorldPipelines; i++)
		if (g_worldSRBs[i]) { g_worldSRBs[i]->Release(); g_worldSRBs[i] = NULL; }
	g_worldChecked.clear();
	// [Doruk-Aksoy] The retired fills go too, as they were made from the same pipelines.
	WorldBindings retired = {};
	while (zx::hwrender::ComputeTakeRetiredBindings(g_retiredWorld, ~Diligent::Uint64{ 0 }, retired)) ReleaseBindings(retired);
}

// [Doruk-Aksoy] The current fill moved to the retired ones, last drawn in the frame being built; past kRetiredWorldKept the oldest is let go.
static void RetireWorldSRBs()
{
	WorldBindings current = {};
	current.filledGen = g_worldFilledGen;
	bool any = false;
	for (int i = 0; i < kWorldPipelines; i++)
	{
		current.srbs[i] = g_worldSRBs[i];
		any = any || current.srbs[i] != NULL;
		g_worldSRBs[i] = NULL;
	}
	WorldBindings dropped = {};
	if (any && zx::hwrender::ComputeRetireBindings(g_retiredWorld, FrameBeingBuilt(), current, kRetiredWorldKept, dropped)) ReleaseBindings(dropped);
}

// [rc4l] Fill the material array on a BINDING.
//
// Every element, always: an unassigned element is not "mostly bound", and the white placeholder
// covers the slots past the live ones as well as anything whose own upload failed.
// [Doruk-Aksoy] `flags` lets a retired fill's array be written again, and only the slots changed since its `filledGen` are, unless it is `fresh`.
static bool FillMaterialArraySRB(Diligent::IShaderResourceBinding *srb, Diligent::SET_SHADER_RESOURCE_FLAGS flags, bool fresh, uint32_t filledGen)
{
	if (!srb) return false;
	auto *v = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uMaterials");
	Diligent::IDeviceObject *white = GetMaterialSRV(NULL, 0);
	if (v == NULL || white == NULL)
	{
		static bool said = false;
		if (!said)
		{
			said = true;
			Printf("Diligent bindless: %s; the shader declared:%s" "\n",
				v ? "no white placeholder texture" : "uMaterials is not on the binding",
				g_scenePSResources.GetChars());
		}
		return false;
	}
	g_stats.matSlotsWhite = 0;
	zx::hwrender::ComputeSlotsToWrite(g_slotChanged, filledGen, fresh, MaterialSlotCapacity(), g_slotsToWrite);
	for (const unsigned i : g_slotsToWrite)
	{
		Diligent::IDeviceObject *obj = NULL;
		if (i < g_matSlotTable.Size())
			obj = MaterialSlotSRV(g_matSlotTable[i]);
		const bool live = (i < g_matSlotTable.Size()) && g_matSlotTable[i].material != NULL;
		if (obj == NULL) obj = white;
		// [rc4l] A slot that came back WHITE for a real material is not filled, it is wrong.
		//
		// GetMaterialSRV falls back to white when it cannot produce a texture yet -- a sprite whose
		// image has not been uploaded on the frame the array happens to be built. Filled once and
		// cached, that fallback is permanent, so a white answer for a live material asks for the
		// array to be built again, with a bound on the retries.
		if (live && obj == white)
		{
			g_stats.matSlotsWhite++;
			g_whiteSlots.push_back((int)i);
		}
		if (i < 8 && live)
		{
			g_stats.fillNames[i] = zx::materials::MaterialName(g_matSlotTable[i].material);
			g_stats.fillViews[i] = (const void *)obj;
		}
		v->SetArray(&obj, i, 1, flags);
	}
	ZX_COUNT("gpu/slots/written", "writes", (int)g_slotsToWrite.size());
	return true;
}

// Declared here because refreshing the array can require the pipelines to be made again -- see
// the comment inside RefreshBindless for why that is not optional.

void RefreshBindless()
{
	if (zx::levelmesh::LevelGeneration() != g_bindlessGen)
	{
		ReleaseWorldSRBs();
		g_bindlessReady = false;
		return;
	}
	// [rc4l] Animated textures move through SlotRemap, since Diligent ignores a second write to a bound mutable descriptor and rewriting one the GPU still reads is undefined.
	// [Doruk-Aksoy] Whether or not the last fill is ready, since a recycle may have moved a frame's slot; only a baked animated base is followed.
	if (fua_dg_animate && g_slotRemapBuf)
	{
		const bool moved = zx::hwrender::ComputeSlotRemap(g_matSlotTable.Size(), [](unsigned i) -> int {
			if (!g_slotState[i].animated) return -1;
			const MatSlot &e = g_matSlotTable[i];
			const void *now = zx::materials::MaterialCurrentFrame(e.material);
			return (now == NULL || now == e.material) ? -1 : MatIndexFind(now, e.translation);
		}, g_slotRemap);
		auto *ctx = GetContext();
		if (moved && ctx)
		{
			SendToBuffer(ctx, g_slotRemapBuf, 0, (Diligent::Uint64)g_slotRemap.size() * sizeof(unsigned), &g_slotRemap[0]);
			g_stats.remapUploads++;
		}
	}
	if (!g_matSlotsDirty) return;
	g_matSlotsDirty = false;
	// A table that has not changed needs no new bindings -- see g_worldFilledGen.
	if (g_bindlessReady && g_worldFilledGen == g_slotGen && !g_forceFill) return;
	ZX_ZONE("draw/bindless_refill");
	g_bindlessReady = false;
	g_whiteSlots.clear();
	// [Doruk-Aksoy] The fill being replaced waits for the GPU to finish with it, and a retired one it has finished with is filled again rather than new bindings made.
	RetireWorldSRBs();
	WorldBindings reused = {};
	zx::hwrender::ComputeTakeRetiredBindings(g_retiredWorld, FrameFinishedOnGpu(), reused);
	// [rc4l] '-' for a pipeline the fill has not reached.
	memset(g_stats.fillState, '-', kWorldPipelines);
	for (int i = 0; i < kWorldPipelines; i++)
	{
		if (!g_pso.world[i]) { ReleaseBindings(reused); return; }
		// [Doruk-Aksoy] A pipeline taking the same resources as an earlier one commits that one's binding, so a fill makes 2 material arrays rather than 22.
		const int shared = zx::hwrender::ComputeSharedWorldBinding(i, [i](int earlier) { return g_pso.world[i]->IsCompatibleWith(g_pso.world[earlier]); });
		if (shared >= 0 && g_worldSRBs[shared])
		{
			g_worldSRBs[i] = g_worldSRBs[shared];
			g_worldSRBs[i]->AddRef();
			g_stats.fillState[i] = 's';
			continue;
		}
		Diligent::SET_SHADER_RESOURCE_FLAGS flags = Diligent::SET_SHADER_RESOURCE_FLAG_NONE;
		bool fresh = true;
		if (reused.srbs[i])
		{
			fresh = g_forceFill;
			g_worldSRBs[i] = reused.srbs[i];
			reused.srbs[i] = NULL;
			flags = Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE;
			g_stats.fillState[i] = 'r';
		}
		else
		{
			g_pso.world[i]->CreateShaderResourceBinding(&g_worldSRBs[i], true);
			if (!g_worldSRBs[i]) { ReleaseBindings(reused); ReleaseWorldSRBs(); return; }
			g_stats.fillState[i] = 'b';
		}
		// uTex and uBrightmap are still declared and still mutable. The shader does not read them
		// while the array is live, but an unassigned sampler is undefined rather than unused.
		Diligent::IDeviceObject *white = GetMaterialSRV(NULL, 0);
		if (auto *v = g_worldSRBs[i]->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
			v->Set(white, flags);
		if (auto *v = g_worldSRBs[i]->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uBrightmap"))
		{
			Diligent::IDeviceObject *bm = GetBrightmapSRV(NULL);
			v->Set(bm ? bm : white, flags);
		}
		if (!FillMaterialArraySRB(g_worldSRBs[i], flags, fresh, reused.filledGen))
		{
			ReleaseBindings(reused);
			ReleaseWorldSRBs();
			static bool said = false;
			if (!said) { said = true; Printf("Diligent bindless: staying on per-material bindings" "\n"); }
			return;
		}
	}
	// [Doruk-Aksoy] What a filled-again fill still holds: its shared pipelines' references.
	ReleaseBindings(reused);
	g_bindlessReady = true;
	// [Doruk-Aksoy] Each binding's next commit checks every resource, the new slots' textures among them.
	g_worldChecked.clear();
	g_stats.fillCount++;
	g_worldFilledGen = g_slotGen;
	g_forceFill = false;
	// [Doruk-Aksoy] A live slot that came back white is written again at the next fill, up to its own bound, reset when its key changes.
	std::sort(g_whiteSlots.begin(), g_whiteSlots.end());
	g_whiteSlots.erase(std::unique(g_whiteSlots.begin(), g_whiteSlots.end()), g_whiteSlots.end());
	bool again = false;
	for (const int slot : g_whiteSlots)
		if (zx::hwrender::ComputeRetryWhite(g_slotState[slot].whiteTries, kWhiteTries)) { g_slotChanged[slot] = g_slotGen + 1; again = true; }
	if (again) { g_slotGen++; g_matSlotsDirty = true; }
	static bool announced = false;
	if (!announced)
	{
		announced = true;
		Printf("Diligent bindless: %d materials in %d slots, %d bindings for the whole world" "\n",
			MaterialSlotCount(), (int)MaterialSlotCapacity(), (int)kWorldPipelines);
	}
}

// [rc4l] A level with more materials than slots gives the whole thing up, rather than drawing the
// extras white.
//
// The overflow path has to be the SAFE one, because it is the one nobody tests: a map big enough to
// reach it is by definition a map that was not to hand. Falling back to per-material bindings costs
// a little submit time on one level; drawing white walls costs a bug report that looks like a
// texture-loading fault.
// See the CCMD: drop everything keyed on an FMaterial address and let it all be made again.
void FlushTextureCache()
{
	// [rc4l] The engine's pixels changed (hqresize, hires), so the atlas's copies of them go too.
	ResetAtlas();
	ReleaseBatchSRBs();
	ReleaseMaterialSRBs();
	for (unsigned b = 0; b < g_scene.batches.Size(); b++) g_scene.batches[b].srb = NULL;
	ReleaseWorldSRBs();
	ReleaseMaterials();
	g_bindlessReady = false;
	ForceMaterialRefill();
}

bool BindlessWanted() { return fua_dg_bindless && MaterialSlotCapacity() > 0 && !g_matSlotOverflow; }
bool BindlessActive() { return BindlessWanted() && g_bindlessReady; }


static_assert(zx::hwrender::kBindVertexBuffer == Diligent::BIND_VERTEX_BUFFER && zx::hwrender::kBindIndexBuffer == Diligent::BIND_INDEX_BUFFER &&
	zx::hwrender::kBindUniformBuffer == Diligent::BIND_UNIFORM_BUFFER && zx::hwrender::kBindShaderResource == Diligent::BIND_SHADER_RESOURCE &&
	zx::hwrender::kBindUnorderedAccess == Diligent::BIND_UNORDERED_ACCESS && zx::hwrender::kBindIndirectDrawArgs == Diligent::BIND_INDIRECT_DRAW_ARGS &&
	zx::hwrender::kBindRayTracing == Diligent::BIND_RAY_TRACING, "resourcestate_compute's bind flags are Diligent's");
static_assert(zx::hwrender::kStateVertexBuffer == Diligent::RESOURCE_STATE_VERTEX_BUFFER && zx::hwrender::kStateIndexBuffer == Diligent::RESOURCE_STATE_INDEX_BUFFER &&
	zx::hwrender::kStateConstantBuffer == Diligent::RESOURCE_STATE_CONSTANT_BUFFER && zx::hwrender::kStateShaderResource == Diligent::RESOURCE_STATE_SHADER_RESOURCE &&
	zx::hwrender::kStateIndirectArgument == Diligent::RESOURCE_STATE_INDIRECT_ARGUMENT, "resourcestate_compute's states are Diligent's");


// [rc4l] Whether writes are tracked, letting go of what was held before fua_dg_checkonce went off, since no commit will.
static bool WritesTracked()
{
	if (zx::hwrender::ComputeTracksWrites(fua_dg_checkonce, MaterialSlotCapacity())) return true;
	g_writtenSeen.clear();
	g_writtenBuffers.clear();
	g_writtenTextures.clear();
	return false;
}

void NoteBufferWritten(Diligent::IBuffer *buffer)
{
	if (buffer == NULL || !WritesTracked() || zx::hwrender::ComputeBufferReadState(buffer->GetDesc().BindFlags) == 0) return;
	if (zx::hwrender::ComputeNoteOnce(g_writtenSeen, buffer)) g_writtenBuffers.emplace_back(buffer);
}

void NoteTextureWritten(Diligent::ITexture *texture)
{
	if (texture != NULL && WritesTracked() && zx::hwrender::ComputeNoteOnce(g_writtenSeen, texture)) g_writtenTextures.emplace_back(texture);
}

static void ReadyWrittenResources(Diligent::IDeviceContext *ctx)
{
	if (g_writtenSeen.empty()) return;
	std::vector<Diligent::StateTransitionDesc> barriers;
	for (auto &b : g_writtenBuffers)
		barriers.emplace_back(b.RawPtr(), Diligent::RESOURCE_STATE_UNKNOWN,
			(Diligent::RESOURCE_STATE)zx::hwrender::ComputeBufferReadState(b->GetDesc().BindFlags), Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
	for (auto &t : g_writtenTextures)
		barriers.emplace_back(t.RawPtr(), Diligent::RESOURCE_STATE_UNKNOWN, Diligent::RESOURCE_STATE_SHADER_RESOURCE, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
	ctx->TransitionResourceStates((Diligent::Uint32)barriers.size(), barriers.data());
	g_writtenSeen.clear();
	g_writtenBuffers.clear();
	g_writtenTextures.clear();
}

static bool IsWorldSRB(const Diligent::IShaderResourceBinding *srb)
{
	for (int i = 0; i < kWorldPipelines; i++)
		if (g_worldSRBs[i] == srb) return true;
	return false;
}

// [rc4l] Commit srb inside one draw loop, checking its resources' states only the first time the loop commits it.
// [Doruk-Aksoy] A world binding is checked only on its first commit since its fill, under fua_dg_checkonce, what was written since made ready before it is committed.
void CommitInLoop(Diligent::IDeviceContext *ctx, zx::hwrender::LoopCommits &loop, Diligent::IShaderResourceBinding *srb)
{
	const bool world = fua_dg_checkonce && IsWorldSRB(srb);
	if (world) ReadyWrittenResources(ctx);
	const bool transition = world ? zx::hwrender::ComputeNoteOnce(g_worldChecked, srb)
		: !fua_dg_loopcommits || zx::hwrender::ComputeCommitTransitions(loop, srb);
	ctx->CommitShaderResources(srb, transition ? Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION
		: Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
	ZX_COUNT("gpu/commits_checked", "commits", transition ? 1 : 0);
}

bool SwitchPipeline(Diligent::IDeviceContext *ctx, Diligent::IPipelineState *&bound, Diligent::IPipelineState *run)
{
	if (run == bound) return false;
	ctx->SetPipelineState(run);
	bound = run;
	ZX_COUNT("gpu/pipeline_switches", "switches", 1);
	return true;
}

// The binding to commit for a draw on this pipeline, or NULL if bindless is not carrying it.
Diligent::IShaderResourceBinding *WorldSRB(Diligent::IPipelineState *pso)
{
	if (!BindlessActive()) return NULL;
	for (int i = 0; i < kWorldPipelines; i++)
		if (g_pso.world[i].RawPtr() == pso)
		{
			// [rc4l] Sprites and decals are on the array too, and the switch stays for bisecting.
			//
			// They used to draw as flat white rectangles, which looked like a second bug and was the same
			// one: a sprite whose material joined the table after the array had been filled could not be
			// added to it, because a static variable cannot be rewritten once its pipeline has handed out
			// a binding. Rebuilding the pipelines fixed both at once.
			if (!ComputeWorldPipelineDesc(i).level && !fua_dg_bindless_dyn) return NULL;
			return g_worldSRBs[i];
		}
	return NULL;
}

// [rc4l] Freeing these means forgetting every RAW copy of them as well.
//
// Each scene batch caches the pointer its material resolved to, once, when the batch list is built
// -- so the batches outlive this call and go on pointing at bindings that have been released.
// Committing one is a use-after-free, and it is reached by resizing or MINIMISING the window: that
// resizes the swapchain, which recreates the scene depth and normal textures, which comes through
// here. The engine died in CommitShaderResources every time, from inside the ordinary opaque loop,
// which reads as a driver fault rather than as our own dangling pointer.
//
// Nulling them is enough because the draw loop re-resolves a batch whose binding has gone.

// [rc4l] How many SRBs are alive, for fua_dg_srbcost -- see the CCMD for why the number matters.
void MaterialSRBCounts(int &cached, int &batch)
{
	cached = (int)g_matSRBs.Size();
	batch = (int)g_batchSRBs.Size();
}

void ForgetMaterialSRBs(const void *material)
{
	for (unsigned i = g_matSRBs.Size(); i-- > 0;)
		if (g_matSRBs[i].material == material)
		{
			if (g_matSRBs[i].srb) g_matSRBs[i].srb->Release();
			g_matSRBs.Delete(i);
		}
	for (unsigned b = 0; b < g_scene.batches.Size(); b++)
		if (g_scene.batches[b].resolved == material) g_scene.batches[b].srb = NULL;
}

void ReleaseMaterialSRBs()
{
	ReleaseAtlasSRBs();
	ForgetBatchSRBs();
	for (unsigned i = 0; i < g_matSRBs.Size(); i++)
		if (g_matSRBs[i].srb) g_matSRBs[i].srb->Release();
	g_matSRBs.Clear();
}

static bool g_pieceBindFailed = false;
// [rc4l] Drop the pipelines and everything bound to them, so the next upload rebuilds with current
// settings. SRBs are created from a PSO and cannot outlive it.
static int g_builtCull = -1;
static int g_builtOptimize = -1;   // [Doruk-Aksoy] fua_dg_spirv_optimize as the pipelines' shaders were compiled
static Diligent::TEXTURE_FORMAT g_builtSceneFormat = Diligent::TEX_FORMAT_UNKNOWN;   // [Doruk-Aksoy] the scene's format the pipelines draw in
static unsigned g_builtSlots = 0;   // [rc4l] the material array size the pipelines were built for
static int g_builtPremul = -1;   // [Doruk-Aksoy] fua_dg_premulblend as the sprite translucent pipeline was made
static float g_builtAniso = 0.f;    // [rc4l] the anisotropy the samplers were built with
static int g_builtFilter = -1;

// [Doruk-Aksoy] Each GLDEFS shader's world pipelines, by (pipeline, shader), empty for one that could not be made; and every pair ever asked for, which EnsureScenePipeline makes again with the plain ones.
static UserPipelineTable g_userPSOs;
static std::set<std::pair<int, int>> g_userWanted;

void ReleaseScenePipelines()
{
	// [rc4l] The world bindings go first, and forgetting them is a use-after-free.
	//
	// A shader resource binding is created FROM a pipeline and does not outlive it. Releasing the
	// pipelines while g_worldSRBs still held theirs took the process down on the first re-upload
	// after this function started being called for reasons other than a filter change -- and it did
	// it inside CommitShaderResources, which reads as a driver fault.
	ReleaseWorldSRBs();
	ReleasePlaneMirrors();
	ReleaseLineMirrorPipeline();
	ReleaseStackPortals();
	g_bindlessReady = false;
	ReleaseBatchSRBs();
	ReleaseMaterialSRBs();
	// [rc4l] Their samplers come from the engine's filter too, so they are made again with the rest.
	ReleaseHorizonPipeline();
	ReleaseDecalPipelines();
	for (unsigned i = 0; i < g_scene.batches.Size(); i++) g_scene.batches[i].srb = NULL;
	ReleaseSkyPipelines();
	g_userPSOs.made.clear();
	for (auto &p : g_pso.world) p.Release();
}

int UserPipelineStatus(Diligent::RefCntAutoPtr<Diligent::IPipelineState> &pso, bool wait)
{
	const Diligent::PIPELINE_STATE_STATUS s = pso->GetStatus(wait);
	if (s == Diligent::PIPELINE_STATE_STATUS_READY) return kUserPipelineReady;
	return s == Diligent::PIPELINE_STATE_STATUS_COMPILING ? kUserPipelineCompiling : kUserPipelineFailed;
}

// [rc4l] Sample textures the way the engine's own renderer is configured to, not the way that looks
// best in isolation.
//
// This used to be hard-wired trilinear-with-point-mag, under a comment claiming that was "the same
// shape the GL renderer uses". It is not: r_texture_filter defaults to 0, which is GL_NEAREST with
// mipmapping off -- Doom's crunchy look. Filtering the Vulkan view instead put a difference on every
// texture detail edge in the frame, which is diffuse rather than localised and therefore reads as
// noise rather than a bug. It dominated the GL-vs-Vulkan measurement and hid whatever real feature
// gaps sit underneath it.
//
// The five modes below are TexFilter[] in gl_texture.cpp, entry for entry. Modes 0 and 2 have
// mipmapping off, which a sampler expresses by refusing to sample past mip 0.
void FillSamplerFromEngine(Diligent::SamplerDesc &samp)
{
	const zx::hwrender::TextureFilter f = zx::hwrender::ComputeTextureFilter((int)r_texture_filter);
	samp.MinFilter = f.minLinear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT;
	samp.MagFilter = f.magLinear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT;
	samp.MipFilter = f.mipLinear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT;
	samp.MaxLOD = f.mipmapped ? 1000.0f : 0.0f;
	// [rc4l] r_texture_filter_anisotropic, as GL's samplers take it.
	const unsigned aniso = zx::hwrender::ComputeAnisotropy(f, r_texture_filter_anisotropic);
	if (aniso > 0)
	{
		samp.MinFilter = samp.MagFilter = samp.MipFilter = Diligent::FILTER_TYPE_ANISOTROPIC;
		samp.MaxAnisotropy = aniso;
	}
}

// [Doruk-Aksoy] A GLDEFS shader's source as its program takes it, read once per lump name, and whether its program could not be made, which is noted once.
struct UserShaderSource { std::string lump, adapted; bool newStyle = false, broken = false; };
static std::vector<UserShaderSource> g_userSources;

static UserShaderSource &UserSource(int shader)
{
	if (g_userSources.size() <= (size_t)shader) g_userSources.resize(shader + 1);
	UserShaderSource &s = g_userSources[shader];
	const char *lump = zx::hwshaders::UserShaderLump(shader);
	if (s.lump == lump) return s;
	s = UserShaderSource();
	s.lump = lump;
	const int num = Wads.CheckNumForFullName(lump);
	if (num >= 0)
	{
		FMemLump data = Wads.ReadLump(num);
		s.newStyle = zx::hwshaders::ComputeAdaptUserShader(data.GetString().GetChars(), shader, s.adapted);
	}
	else s.broken = true;
	return s;
}

// [Doruk-Aksoy] Leave a shader out for good, with the console note GL prints for one of its own.
static void BreakUserShader(UserShaderSource &s, const char *why)
{
	if (s.broken) return;
	s.broken = true;
	Printf("%s", zx::hwshaders::ComputeUserShaderSkipNote("vulkan", s.lump, why).c_str());
}

std::string ProgramBlock(zx::hwshaders::ProgramKind kind, int userShader)
{
	if (userShader < 0 || userShader >= zx::hwshaders::UserShaderCount()) return zx::hwshaders::ComputePlainProgramBlock(kind);
	const UserShaderSource &s = UserSource(userShader);
	return zx::hwshaders::ComputeUserProgramBlock(kind, s.adapted, s.newStyle, userShader);
}

bool UserShaderUsable(int shader)
{
	return shader >= 0 && shader < zx::hwshaders::UserShaderCount() && !UserSource(shader).broken;
}

bool KeepUserPipeline(int shader, Diligent::IPipelineState *plain, Diligent::RefCntAutoPtr<Diligent::IPipelineState> &made)
{
	UserShaderSource &src = UserSource(shader);
	if (!made) { BreakUserShader(src, ""); return false; }
	// [Doruk-Aksoy] Its draws commit the plain pipeline's bindings, so it must take the same resources, as a shader declaring its own cannot.
	if (!made->IsCompatibleWith(plain)) { made.Release(); BreakUserShader(src, "its program does not take the plain pipeline's bindings"); return false; }
	return true;
}

const char *UserShaderName(int shader)
{
	return UserSource(shader).lump.c_str();
}

// [rc4l] `false`, not Diligent::False -- something in the reshaped DXSDK headers defines False as
// a macro, so the qualified name does not survive the preprocessor here.
static const Diligent::LayoutElement kSceneLayout[] = {
	Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},   // position
	Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},   // uv
	Diligent::LayoutElement{2, 0, 1, Diligent::VT_FLOAT32, false},   // piece index
};
// [rc4l] The dynamic stream's one attribute, a reference into its words (ComputeSpriteRef, ComputeGeneralRef).
static const Diligent::LayoutElement kDynLayout[] = {
	Diligent::LayoutElement{0, 0, 1, Diligent::VT_UINT32, false},
};

static const Diligent::ShaderResourceVariableDesc kSceneVars[] = {
	{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	// [rc4l] The material array is MUTABLE, which is to say it lives on the BINDING.
	//
	// It was static, and static means Diligent stores it on the pipeline and copies it into
	// every binding at the moment that binding is made -- after which it cannot be rewritten.
	// Following a texture to its next animation frame therefore meant making all thirteen
	// pipelines again, shader compiles and all, twenty times a second. That was reported from
	// play as mouse stutter and as a lowering floor that could not decide what texture it was.
	//
	// On the binding, the same update is one SetArray call on the slots that actually changed.
	{ Diligent::SHADER_TYPE_PIXEL, "uMaterials", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
};

// [Doruk-Aksoy] The world samplers as the last EnsureScenePipeline made them, so a GLDEFS shader's pipeline made later has the same and shares the plain one's bindings.
static Diligent::SamplerDesc g_sceneSampler;

// [Doruk-Aksoy] The scene fragment stages by kWpStage*, and their names.
static const char *const kStageSources[] = { kScenePSOpaque, kScenePS, kScenePSTrans, kScenePSRedAlpha, kScenePSGBuffer };
static const char *const kStageNames[] = { "fua scene PS opaque", "fua scene PS masked", "fua scene PS translucent", "fua scene PS red alpha", "fua scene PS masked + gbuffer" };

// [rc4l] Every one has its statics bound, sprites' and decals' included: a pipeline created with none bound died on launch with "No resource is assigned to static shader variable 'Constants'".
static void BindWorldStatics(Diligent::IPipelineState *pso)
{
	BindSceneConstants(pso);
	BindLightBuffers(pso);

	// [rc4l] The per-piece records, read by the VERTEX stage. Shared by every world pass, so it is
	// static like the others -- and it is why a new piece buffer means new pipelines: a static
	// variable cannot be re-pointed once its pipeline has handed out a binding.
	if (g_scene.pieceBuf)
	{
		auto *pv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Pieces");
		if (pv) pv->Set(g_scene.pieceBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
		else g_pieceBindFailed = true;
	}
	if (g_scene.stackVisBuf)
		if (auto *sv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "StackVis"))
			sv->Set(g_scene.stackVisBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
	if (auto *rv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "SlotRemap"))
		rv->Set(g_slotRemapBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
	if (auto *dv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "DynData"))
		dv->Set(g_scene.dynDataBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
	if (auto *lv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "SectorLight"))
		lv->Set(g_scene.sectorLightBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
	if (auto *hv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "SkyHoles"))
		hv->Set(g_scene.skyHoleBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
}

// [rc4l] A world pipeline over one vertex layout and set of resources, as ComputeWorldPipelineDesc describes it, the blended ones never writing depth.
// [Doruk-Aksoy] Made on the device's pool when fua_dg_async_pipelines is on, so its statics are bound by the caller once it is ready (BindWorldStatics).
// [Doruk-Aksoy] `premul` blends ps's colour as already multiplied by its alpha (kWpBlendPremul), for the premultiplied sprite pipeline.
static void MakeWorldPipeline(int pass, Diligent::IShader *vs, Diligent::IShader *ps, const char *name, Diligent::RefCntAutoPtr<Diligent::IPipelineState> &out, bool premul = false)
{
	const WorldPipelineDesc d = ComputeWorldPipelineDesc(pass);
	const bool gbuffer = d.stage == kWpStageGBuffer;
	const bool blended = d.blend != kWpBlendNone;
	auto *swap = GetSwapChain();
	if (swap == NULL) return;

	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = name;
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = gbuffer ? 2 : 1;
	pci.GraphicsPipeline.RTVFormats[0] = SceneColorFormat();
	if (gbuffer) pci.GraphicsPipeline.RTVFormats[1] = Diligent::TEX_FORMAT_RGBA8_UNORM;
	pci.GraphicsPipeline.DSVFormat = swap->GetDesc().DepthBufferFormat;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = !d.level ? Diligent::CULL_MODE_NONE :
		(fua_dg_cull == 1) ? Diligent::CULL_MODE_BACK :
		(fua_dg_cull == 2) ? Diligent::CULL_MODE_FRONT : Diligent::CULL_MODE_NONE;
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = !blended || d.depthWrite;
	ClampDepthLikeGL(pci.GraphicsPipeline);
	if (d.depthBias)
	{
		// [rc4l] GL's glPolygonOffset(-1, -128): -1 times the slope and -128 resolvable steps, which on a float buffer scale with the primitive's depth; swapped, a decal seen square on had no slope to lift it and fought its wall.
		pci.GraphicsPipeline.RasterizerDesc.DepthBias = -128;
		pci.GraphicsPipeline.RasterizerDesc.SlopeScaledDepthBias = -1.f;
	}
	if (blended) SetBlendEquation(pci.GraphicsPipeline.BlendDesc.RenderTargets[0], premul ? kWpBlendPremul : d.blend);
	// [rc4l] The level's pipelines read its vertices; the others draw the dynamic stream.
	pci.GraphicsPipeline.InputLayout.LayoutElements = d.level ? kSceneLayout : kDynLayout;
	pci.GraphicsPipeline.InputLayout.NumElements = d.level ? 3 : 1;
	pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
	// [rc4l] Per call, never static: the filter is the engine's at this rebuild, and a static array would keep the first one forever.
	const Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", g_sceneSampler },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", g_sceneSampler },
		// [rc4l] The material array's sampler, uMaterials_sampler, is immutable as well, or it is a variable nothing binds.
		{ Diligent::SHADER_TYPE_PIXEL, "uMaterials", g_sceneSampler },
	};
	// [rc4l] The material array is the last of each, left out where the shaders have none (FUA_BINDLESS).
	const Diligent::Uint32 noArray = MaterialSlotCapacity() > 0 ? 0 : 1;
	pci.PSODesc.ResourceLayout.Variables = kSceneVars;
	pci.PSODesc.ResourceLayout.NumVariables = (Diligent::Uint32)(sizeof(kSceneVars) / sizeof(kSceneVars[0])) - noArray;
	pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
	pci.PSODesc.ResourceLayout.NumImmutableSamplers = (Diligent::Uint32)(sizeof(samplers) / sizeof(samplers[0])) - noArray;
	pci.pVS = vs;
	pci.pPS = ps;
	if (AsyncPipelines()) pci.Flags |= Diligent::PSO_CREATE_FLAG_ASYNCHRONOUS;
	CreatePipeline(pci, &out);
}

// [Doruk-Aksoy] A GLDEFS shader's world pipeline once ready: its statics bound and kept only if it takes the plain one's bindings.
static bool FinishUserScenePSO(const std::pair<int, int> &key, Diligent::RefCntAutoPtr<Diligent::IPipelineState> &pso)
{
	if (pso) BindWorldStatics(pso);
	return KeepUserPipeline(key.second, g_pso.world[key.first], pso);
}

// [Doruk-Aksoy] Every GLDEFS shader world pipeline started and not yet ready, waited for.
static void LandUserScenePSOs()
{
	ComputeLandAllUserPipelines(g_userPSOs, UserPipelineStatus, FinishUserScenePSO);
}

// [Doruk-Aksoy] GLDEFS shader `userShader`'s twin of world pipeline `pipeline`, started the first time it is asked for, or the plain one while it is made and for good once left out; `wait` waits for it instead.
static Diligent::IPipelineState *UserScenePSO(int pipeline, int userShader, bool wait)
{
	if (pipeline < 0 || pipeline >= kWorldPipelines) return NULL;
	Diligent::IPipelineState *plain = g_pso.world[pipeline];
	if (plain == NULL || !UserShaderUsable(userShader) || !ComputeWorldPipelineTakesUserShader(pipeline)) return plain;
	const std::pair<int, int> key(pipeline, userShader);
	auto start = [&]() {
		g_userWanted.insert(key);
		Diligent::RefCntAutoPtr<Diligent::IPipelineState> made;
		const WorldPipelineDesc d = ComputeWorldPipelineDesc(pipeline);
		// [Doruk-Aksoy] One vertex shader serves every GLDEFS shader's program, since only its fragment stage differs.
		auto vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, d.level ? "fua scene VS user" : "fua dynamic VS user", d.level ? kSceneVS.c_str() : kDynamicVS.c_str(), userShader);
		FString name;
		name.Format("%s %s", kStageNames[d.stage], UserShaderName(userShader));
		auto ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, name.GetChars(), kStageSources[d.stage], userShader);
		name.Format("%s %s", d.name, UserShaderName(userShader));
		if (vs && ps) MakeWorldPipeline(pipeline, vs, ps, name.GetChars(), made);
		return made;
	};
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> *made = ComputeUserPipeline(g_userPSOs, key, wait, start, UserPipelineStatus, FinishUserScenePSO);
	return made != NULL ? made->RawPtr() : plain;
}

Diligent::IPipelineState *ScenePSO(int pipeline, int userShader)
{
	return UserScenePSO(pipeline, userShader, !AsyncPipelines());
}

void PrewarmUserPipelines()
{
	const size_t before = g_userPSOs.made.size();
	cycle_t clock;
	clock.Reset();
	clock.Clock();
	// [Doruk-Aksoy] Every one the level's surfaces take started first, so the pool makes them side by side, then all waited for.
	for (unsigned i = 0; i < g_scene.batches.Size(); i++)
	{
		const SceneBatch &b = g_scene.batches[i];
		if (b.userShader < 0) continue;
		if (b.blend != 0) { UserScenePSO(ComputeBatchPipeline(b.blend), b.userShader, false); continue; }
		UserScenePSO(kWpMasked, b.userShader, false);
		UserScenePSO(kWpMaskedGBuffer, b.userShader, false);
	}
	LandUserScenePSOs();
	clock.Unclock();
	if (g_userPSOs.made.size() > before)
		Printf("vulkan: %d GLDEFS shader pipelines made ahead in %.0f ms\n", (int)(g_userPSOs.made.size() - before), clock.TimeMS());
}

void UserShaderReport(FString &out)
{
	const int count = zx::hwshaders::UserShaderCount();
	int ran = 0;
	FString lines;
	// [Doruk-Aksoy] Timed, as what a pipeline costs beyond its shaders is the driver's part of the bill.
	const size_t before = g_userPSOs.made.size();
	cycle_t clock;
	clock.Reset();
	clock.Clock();
	// [Doruk-Aksoy] A level surface's program and a sprite's translucent one, the two fragment stages a shader's pieces mostly take, all started before any is waited for.
	for (int i = 0; i < count && g_pso.world[kWpMasked]; i++)
	{
		UserScenePSO(kWpMasked, i, false);
		UserScenePSO(kWpTransNoCull, i, false);
	}
	LandUserScenePSOs();
	clock.Unclock();
	for (int i = 0; i < count && g_pso.world[kWpMasked]; i++)
	{
		const bool ok = !UserSource(i).broken;
		ran += ok ? 1 : 0;
		lines.AppendFormat("  %s  %s\n", ok ? "runs" : "out ", zx::hwshaders::UserShaderLump(i));
	}
	out.Format("%d GLDEFS hardware shaders, %d running; %d pipelines made in %.1f ms\n%s", count, ran,
		(int)(g_userPSOs.made.size() - before), clock.TimeMS(), lines.GetChars());
	if (!g_pso.world[kWpMasked] && count > 0) out += "  (no level is up, so none was made)\n";
}

// [Doruk-Aksoy] Off leaves each GLDEFS shader's programs to the frame that first draws with one, as before, for timing a launch against.
CVAR(Bool, fua_dg_precompile_shaders, true, CVAR_ARCHIVE)
// [Doruk-Aksoy] Off compiles them one after another on this thread, for timing the device's pool against.
CVAR(Bool, fua_dg_precompile_async, true, CVAR_ARCHIVE)

// [Doruk-Aksoy] The fragment stages a GLDEFS shader's pieces draw with (ComputeWorldPipelineDesc's, the opaque one never asked for), compiled ahead with the 2D layer's.
static const int kUserStages[] = { kWpStageMasked, kWpStageTrans, kWpStageGBuffer };

// [Doruk-Aksoy] Each fragment program a usable GLDEFS shader is compiled into ahead of its first draw: its scene stages and its 2D one, by name and source.
template <class Each> static void ForEachUserProgram(Each each)
{
	for (int i = 0; i < zx::hwshaders::UserShaderCount(); i++)
	{
		if (!UserShaderUsable(i)) continue;
		FString name;
		for (const int stage : kUserStages)
		{
			name.Format("%s %s", kStageNames[stage], UserShaderName(i));
			each(i, name.GetChars(), kStageSources[stage]);
		}
		name.Format("fua 2D PS %s", UserShaderName(i));
		each(i, name.GetChars(), Ps2DSource());
	}
}

void PrecompileUserShaders()
{
	// [Doruk-Aksoy] features/postprocess's programs start at launch with these, each on its own guard.
	PrecompilePostShaders();
	static int doneFor = -1;
	const int count = zx::hwshaders::UserShaderCount();
	if (!fua_dg_precompile_shaders || GetDevice() == NULL || count == doneFor) return;
	doneFor = count;
	if (count == 0) return;
	ZX_ZONE("load/user_shaders");   // [Doruk-Aksoy] every GLDEFS shader's programs compiled at launch
	cycle_t clock;
	clock.Reset();
	clock.Clock();
	auto compile = fua_dg_precompile_async ? CompileShaderAsync : CompileShader;
	compile(Diligent::SHADER_TYPE_VERTEX, "fua scene VS user", kSceneVS.c_str(), 0);
	compile(Diligent::SHADER_TYPE_VERTEX, "fua dynamic VS user", kDynamicVS.c_str(), 0);
	// [Doruk-Aksoy] Every program asked for first, so the pool has them all at once, then each waited for in turn.
	std::vector<std::pair<int, Diligent::RefCntAutoPtr<Diligent::IShader>>> made;
	int leftOut = 0;
	for (int i = 0; i < count; i++) leftOut += UserShaderUsable(i) ? 0 : 1;
	ForEachUserProgram([&](int shader, const char *name, const char *source) {
		made.emplace_back(shader, compile(Diligent::SHADER_TYPE_PIXEL, name, source, shader));
	});
	int programs = 0;
	for (auto &m : made)
	{
		const bool ok = m.second && m.second->GetStatus(true) == Diligent::SHADER_STATUS_READY;
		programs += ok ? 1 : 0;
		// [Doruk-Aksoy] Noted now, at launch, as GL notes its own.
		if (!ok && UserShaderUsable(m.first)) { BreakUserShader(UserSource(m.first), ""); leftOut++; }
	}
	clock.Unclock();
	Printf("%s", zx::hwshaders::ComputePrecompileNote(count, programs, leftOut, clock.TimeMS()).c_str());
}

void ShaderCompileBench(int inFlight, FString &out)
{
	static int run = 0;
	run++;
	// [Doruk-Aksoy] Each text made new by a trailing comment, so the cache answers none of them and each is compiled in full.
	std::vector<std::string> texts;
	ForEachUserProgram([&](int shader, const char *, const char *source) {
		texts.push_back(ShaderText(source, shader) + "// fua_dg_shaderbench " + std::to_string(run) + "\n");
	});
	if (texts.empty()) { out = "no GLDEFS shader to compile\n"; return; }
	inFlight = std::max(1, inFlight);
	cycle_t clock;
	clock.Reset();
	clock.Clock();
	std::deque<Diligent::RefCntAutoPtr<Diligent::IShader>> flying;
	int failed = 0;
	auto land = [&]() {
		failed += flying.front() && flying.front()->GetStatus(true) == Diligent::SHADER_STATUS_READY ? 0 : 1;
		flying.pop_front();
	};
	for (const std::string &text : texts)
	{
		if ((int)flying.size() >= inFlight) land();
		flying.push_back(CompileShaderText(Diligent::SHADER_TYPE_PIXEL, "fua shader bench", text, true));
	}
	while (!flying.empty()) land();
	clock.Unclock();
	out.Format("%d programs, at most %d at a time: %.0f ms, %.1f ms each%s\n", (int)texts.size(), inFlight, clock.TimeMS(),
		clock.TimeMS() / texts.size(), failed ? " (some failed)" : "");
}

bool EnsureScenePipeline(FString &err)
{
	// The sampler is immutable in the PSO, so a filter change means a new PSO. Cheap and rare.
	if (g_pso.world[kWpOpaque] && (g_builtFilter != (int)r_texture_filter || g_builtAniso != (float)r_texture_filter_anisotropic)) ReleaseScenePipelines();
	g_builtFilter = (int)r_texture_filter;
	g_builtAniso = r_texture_filter_anisotropic;
	if (g_pso.world[kWpOpaque] && g_builtCull != (int)fua_dg_cull) ReleaseScenePipelines();
	g_builtCull = (int)fua_dg_cull;
	if (g_pso.world[kWpOpaque] && g_builtPremul != (int)fua_dg_premulblend) ReleaseScenePipelines();
	g_builtPremul = (int)fua_dg_premulblend;
	// [Doruk-Aksoy] The kept shaders were compiled the other way, so they go with the pipelines holding them.
	if (g_pso.world[kWpOpaque] && g_builtOptimize != (int)fua_dg_spirv_optimize) { ReleaseScenePipelines(); ReleasePostProcess(); ReleaseShaderCache(); }
	g_builtOptimize = (int)fua_dg_spirv_optimize;
	// [Doruk-Aksoy] Every pipeline and view target of the 3D is in the scene's format, so a change of fua_dg_scene16 makes them again, the cameras' pictures with them.
	if (g_pso.world[kWpOpaque] && g_builtSceneFormat != SceneColorFormat()) { ReleaseScenePipelines(); ReleaseCameraTargets(); }
	g_builtSceneFormat = SceneColorFormat();
	// [rc4l] The shaders declare the array at its size, so a new size is new pipelines.
	// [Doruk-Aksoy] One cut below the table mid-level turns bindless off for the level, since records hold slots past the new size, which a binding per material never reads.
	if (g_pso.world[kWpOpaque] && g_builtSlots != MaterialSlotCapacity())
	{
		ReleaseScenePipelines();
		if (g_matSlotTable.Size() > MaterialSlotCapacity()) g_matSlotOverflow = true;
	}
	// [rc4l] A level with more sectors than the sector light table holds makes it again, and the pipelines binding it.
	if (g_pso.world[kWpOpaque] && (unsigned)numsectors > g_scene.sectorLightCapacity) ReleaseScenePipelines();
	g_builtSlots = MaterialSlotCapacity();
	if (g_pso.world[kWpOpaque]) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) { err = "no device/swapchain"; return false; }

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, dynVS, stages[kWpStageGBuffer + 1];
	vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua scene VS", kSceneVS.c_str());
	dynVS = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua dynamic VS", kDynamicVS.c_str());
	bool staged = true;
	for (int s = 0; s <= kWpStageGBuffer; s++)
	{
		stages[s] = CompileShader(Diligent::SHADER_TYPE_PIXEL, kStageNames[s], kStageSources[s]);
		staged = staged && stages[s];
	}
	// [Doruk-Aksoy] The translucent stage once more with FUA_PREMUL for the premultiplied sprite pipeline, under fua_dg_premulblend.
	Diligent::RefCntAutoPtr<Diligent::IShader> premulPS;
	if (fua_dg_premulblend)
	{
		std::string source = kScenePSTrans;
		source.insert(source.find('\n') + 1, "#define FUA_PREMUL 1\n");
		premulPS = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua scene PS translucent premultiplied", source.c_str());
		staged = staged && premulPS;
	}
	Diligent::IShader *psMasked = stages[kWpStageMasked];
	// [rc4l] Ask the SHADER what it declared, before any pipeline has an opinion about it.
	// "Not in the pipeline resource list" has two very different causes -- the shader never declared
	// it, or the layout classified it somewhere else -- and only the shader can tell them apart.
	if (psMasked)
	{
		FString names;
		for (Diligent::Uint32 r = 0; r < psMasked->GetResourceCount(); r++)
		{
			Diligent::ShaderResourceDesc d;
			psMasked->GetResourceDesc(r, d);
			names += " "; names += d.Name;
			if (d.ArraySize > 1) { names += "["; names.AppendFormat("%u", d.ArraySize); names += "]"; }
		}
		g_scenePSResources = names;
		Printf("Diligent scene PS declares (%u):%s\n", psMasked->GetResourceCount(), names.GetChars());
	}
	if (!vs || !dynVS || !staged) { err = "scene shader compilation failed"; return false; }

	// [rc4l] Created once and never replaced, since pipelines that outlive a level change bind it statically and a replaced one is never mapped again.
	if (!g_cb)
	{
		Diligent::BufferDesc cbd;
		cbd.Name = "fua scene constants";
		cbd.Size = sizeof(float) * zx::hwrender::kSceneConstantFloats;
		cbd.Usage = Diligent::USAGE_DYNAMIC;
		cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
		cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		CreateDeviceBuffer(cbd, nullptr, &g_cb);
	}
	if (!g_cb) { err = "constant buffer creation failed"; return false; }

	if (!g_slotRemapBuf)
	{
		std::vector<unsigned> identity(kMaxMaterialSlots);
		for (unsigned i = 0; i < identity.size(); i++) identity[i] = i;
		if (!CreateStructuredBuffer("fua slot remap", sizeof(unsigned), kMaxMaterialSlots, &identity[0], g_slotRemapBuf))
		{ err = "slot remap buffer creation failed"; return false; }
	}
	if (!EnsureLightBuffers(err)) return false;
	// [rc4l] The player's sky holes, two vec4s each, created once at kMaxSkyHoles for the same reason: every world pipeline binds them statically.
	if (!g_scene.skyHoleBuf && !CreateStructuredBuffer("fua sky holes", 16, zx::levelmesh::kMaxSkyHoles * 2, NULL, g_scene.skyHoleBuf))
	{ err = "sky hole buffer creation failed"; return false; }
	if (!g_scene.sectorLightBuf || (unsigned)numsectors > g_scene.sectorLightCapacity)
	{
		const unsigned sectorsHeld = zx::hwrender::ComputeSectorLightCapacity(numsectors);
		g_scene.sectorLightBuf.Release();
		g_scene.sectorLightWords.Clear();
		if (!CreateStructuredBuffer("fua sector light", 4, sectorsHeld * zx::levelmesh::kSectorLightWords, NULL, g_scene.sectorLightBuf))
		{ err = "sector light buffer creation failed"; return false; }
		g_scene.sectorLightCapacity = sectorsHeld;
	}
	// [rc4l] The dynamic stream's words at the most a frame has wanted, which EnsureDynamicRecords grows with the pipelines.
	if (!g_scene.dynDataBuf)
	{
		const unsigned words = zx::hwrender::ComputeDynWordsCapacity(g_scene.dynWordsPeak);
		if (!CreateStructuredBuffer("fua dynamic data", 4, words, NULL, g_scene.dynDataBuf)) { err = "dynamic data buffer creation failed"; return false; }
		g_scene.dynDataCapacity = words;
	}

	// [rc4l] Per call, never static: the filter is the engine's at this rebuild, and a static array would keep the first one forever.
	g_sceneSampler = Diligent::SamplerDesc();
	FillSamplerFromEngine(g_sceneSampler);
	g_sceneSampler.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
	g_sceneSampler.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
	cycle_t clock;
	clock.Reset();
	clock.Clock();
	for (int pass = 0; pass < kWorldPipelines; pass++)
	{
		const WorldPipelineDesc d = ComputeWorldPipelineDesc(pass);
		const bool premul = premulPS && pass == kWpTransNoCull;
		MakeWorldPipeline(pass, d.level ? vs : dynVS, premul ? premulPS.RawPtr() : stages[d.stage], d.name, g_pso.world[pass], premul);
	}
	// [Doruk-Aksoy] All started before any is waited for, so the pool makes them side by side; a failed one leaves none, for the next frame to try again.
	for (auto &p : g_pso.world)
	{
		if (!p || p->GetStatus(true) != Diligent::PIPELINE_STATE_STATUS_READY)
		{
			for (auto &q : g_pso.world) q.Release();
			err = "scene pipeline creation failed";
			return false;
		}
		BindWorldStatics(p);
	}
	ReleaseWorldSRBs();
	g_matSlotsDirty = true;
	// [rc4l] Nothing to pre-fill: the material array is mutable, so it belongs to each binding and is
	// filled when that binding is made. A pipeline can hand out as many as it likes without the array
	// ever being read from it -- which is exactly the property that was missing when it was static.
	// [Doruk-Aksoy] Every GLDEFS shader's pipeline asked for before is made again with them, so the frame that next shows one does not wait on it.
	for (const std::pair<int, int> &want : g_userWanted) UserScenePSO(want.first, want.second, false);
	LandUserScenePSOs();
	clock.Unclock();
	FString again;
	if (!g_userPSOs.made.empty()) again.Format(", %d GLDEFS shader ones among them made again", (int)g_userPSOs.made.size());
	Printf("vulkan: %d world pipelines made in %.0f ms%s\n", kWorldPipelines + (int)g_userPSOs.made.size(), clock.TimeMS(), again.GetChars());
	return true;
}

// [rc4l] What the last dynamic pass actually drew, split by blend mode.
void SlotReport(int n, FString &out)
{
	out.Format("slots %u, batches %u" "\n", g_matSlotTable.Size(), g_scene.batches.Size());
	for (int i = 0; i < n && (unsigned)i < g_scene.batches.Size(); i++)
	{
		const SceneBatch &b = g_scene.batches[i];
		int slot = -1;
		for (unsigned k = 0; k < g_matSlotTable.Size(); k++)
			if (g_matSlotTable[k].material == b.material && g_matSlotTable[k].translation == 0) { slot = (int)k; break; }
		const char *bname = zx::materials::MaterialName(b.material);
		const char *sname = (slot > 0 && g_matSlotTable[slot].material) ? zx::materials::MaterialName(g_matSlotTable[slot].material) : "-";
		// [rc4l] ...and what the VERTEX actually says, which is the only number the GPU sees.
		// A lookup that agrees with the table proves nothing if the buffer was written against a
		// different table.
		const unsigned int pidx = (b.first < g_scene.cpuVerts.Size()) ?
			(unsigned int)(g_scene.cpuVerts[b.first].pieceIndex + 0.5f) : 0xffffffffu;
		const int inVB = (pidx < g_scene.pieces.Size()) ?
			(int)(g_scene.pieces[pidx].matSlot + 0.5f) : -1;
		// The view itself, because two slots resolving to ONE view is the signature of a cache keyed
		// on an address that two different materials have held.
		const void *srv = (slot > 0) ? (const void *)GetMaterialSRV(g_matSlotTable[slot].material,
			g_matSlotTable[slot].translation) : NULL;
		const char *vname = (inVB > 0 && (unsigned)inVB < g_matSlotTable.Size() && g_matSlotTable[inVB].material) ?
			zx::materials::MaterialName(g_matSlotTable[inVB].material) : "-";
		out.AppendFormat("  batch %d verts %u..%u material %s -> slot %d holding %s (view %p) | vertex says %d = %s" "\n",
			i, b.first, b.first + b.count, bname, slot, sname, srv, inVB, vname);
	}
}

// [Doruk-Aksoy] A slot's facts for the slot report, a brightmap slot's from the texture it belongs to.
static zx::hwrender::SlotFacts FactsOf(const MatSlot &e)
{
	static_assert((int)zx::materials::kMaterialUseWorld == (int)zx::hwrender::kSlotUseWorld &&
		(int)zx::materials::kMaterialUseOther == (int)zx::hwrender::kSlotUseOther, "SlotUse follows MaterialUse");
	zx::hwrender::SlotFacts f;
	f.brightmap = e.translation == kBrightmapTranslation;
	f.translated = !f.brightmap && e.translation != 0;
	zx::materials::MaterialInfo info;
	if (!zx::materials::DescribeMaterial(e.material, info)) return f;
	f.use = info.use;
	f.shaded = info.userShader >= 0;
	f.special = info.canvas || info.warp != 0;
	f.width = info.width;
	f.height = info.height;
	f.name = info.name != NULL ? info.name : "";
	f.hasBrightmap = !f.brightmap && info.brightmap;
	return f;
}

// [Doruk-Aksoy] How many slots are pinned, the white one among them.
static int PinnedSlotCount()
{
	int n = 0;
	for (const SlotState &st : g_slotState) n += st.pinned ? 1 : 0;
	return n;
}

void SlotDemandReport(int top, FString &out)
{
	std::vector<zx::hwrender::SlotFacts> facts;
	for (unsigned i = 0; i < g_seenKeys.Size(); i++)
		if (g_seenKeys[i].material != NULL) facts.push_back(FactsOf(g_seenKeys[i]));
	zx::hwrender::SlotTally r;
	zx::hwrender::ComputeSlotReport(facts, AtlasPageSize(), AtlasMaxSide(), r);
	const zx::hwrender::SlotTableFacts table = { (int)g_matSlotTable.Size() - 1, (int)MaterialSlotCapacity(), PinnedSlotCount() - 1, g_slotsRecycled, g_keysRefused };
	out = zx::hwrender::ComputeSlotReportText(r, table, AtlasPageSize(), AtlasMaxSide(), top).c_str();
	out += "\n";
	out += zx::hwrender::ComputeWorkingSetText(g_workingSet, g_workingPeaks, kWorkingWindowsMs, (int)facts.size()).c_str();
	int cached = 0, batch = 0;
	MaterialSRBCounts(cached, batch);
	out.AppendFormat("\n  bindless %s, %d material bindings live", BindlessActive() ? "on" : "off", cached + batch);
}

// [rc4l] The slot table, for a pass that fills a material array of its own.
const TArray<MatSlot> &MaterialSlots() { return g_matSlotTable; }

int MaterialSlotsUnfilled(int matSlot, int brightSlot)
{
	return zx::hwrender::ComputeSlotsChangedAfterFill(g_slotChanged, matSlot, brightSlot, g_worldFilledGen);
}

uint32_t MaterialSlotGeneration() { return g_slotGen; }

void MaterialSlotsToWrite(uint32_t filledGen, bool fresh, std::vector<unsigned> &out)
{
	zx::hwrender::ComputeSlotsToWrite(g_slotChanged, filledGen, fresh, MaterialSlotCapacity(), out);
}

void MaterialSlotStats(FString &out)
{
	out.Format("material slots: %d of %u in the table, %d pinned, %d recycled since load, bindless %s",
		MaterialSlotCount(), MaterialSlotCapacity(), PinnedSlotCount(), g_slotsRecycled, BindlessActive() ? "on" : "off");
}

// [rc4l] The slot table now describes the level being uploaded, so the array may be filled from it.
void MarkMaterialSlotsCurrent() { g_bindlessGen = zx::levelmesh::LevelGeneration(); }

}} // namespace zx::hwrender
