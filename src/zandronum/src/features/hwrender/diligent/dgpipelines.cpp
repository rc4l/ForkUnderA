// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The world pipelines and what binds to them: the bindless material array and its slots, animated slots, and the per-material bindings.

#include "features/hwshaders/hwshaders.h"   // [rc4l] the GLDEFS shader list
#include "features/hwshaders/computation/hwshader_compute.h"
#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/diligent/dgscene_glsl.h"   // [rc4l] the world pass's shaders

#include <string.h>
#include "w_wad.h"   // [rc4l] the shader lumps

// [rc4l] Off makes every commit in a draw loop check every resource again, the old cost, for an A/B in one session.
CVAR(Bool, fua_dg_loopcommits, true, 0)
EXTERN_CVAR(Float, r_texture_filter_anisotropic)

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

Diligent::IShaderResourceBinding *GetMaterialSRB(Diligent::IPipelineState *pso,
	const void *material, int translation)
{
	if (!pso) return NULL;
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
Diligent::IPipelineState *BatchPSO(int blend)
{
	return g_pso.world[ComputeBatchPipeline(blend)].RawPtr();
}

// [rc4l] A batch's binding, made again for the frame it last resolved to whenever a flush, a resize or a minimise dropped it.
Diligent::IShaderResourceBinding *BatchSRB(SceneBatch &b)
{
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
// Bounded retries for slots that came back white -- see FillMaterialArrayStatic.
static int g_matSlotRetries = 0;
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
// This only answers "which index" without walking. Open addressing, no allocation, cleared with the
// table it indexes -- there are exactly two places that mutate it and both are next to this comment.
struct MatIndexEntry { const void *material; int translation; int slot; };
const int kMatIndexSize = 16384;   // power of two, comfortably over kMaxMaterialSlots
static MatIndexEntry g_matIndex[kMatIndexSize];

static inline unsigned MatIndexHash(const void *m, int t)
{
	unsigned long long h = (unsigned long long)(size_t)m;
	h ^= (unsigned long long)(unsigned)t * 0x9E3779B97F4A7C15ull;
	h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
	return (unsigned)(h & (unsigned)(kMatIndexSize - 1));
}

// slot < 0 marks an empty bucket. NULL is a legitimate key -- it is the white slot -- so emptiness
// cannot be spelled with the material pointer.
static void MatIndexClear()
{
	for (int i = 0; i < kMatIndexSize; i++) g_matIndex[i].slot = -1;
}

static void MatIndexInsert(const void *material, int translation, int slot)
{
	unsigned i = MatIndexHash(material, translation);
	for (int probe = 0; probe < kMatIndexSize; probe++)
	{
		if (g_matIndex[i].slot < 0)
		{
			g_matIndex[i].material = material;
			g_matIndex[i].translation = translation;
			g_matIndex[i].slot = slot;
			return;
		}
		i = (i + 1) & (unsigned)(kMatIndexSize - 1);
	}
}

// -1 when absent.
static int MatIndexFind(const void *material, int translation)
{
	unsigned i = MatIndexHash(material, translation);
	for (int probe = 0; probe < kMatIndexSize; probe++)
	{
		const MatIndexEntry &e = g_matIndex[i];
		if (e.slot < 0) return -1;
		if (e.material == material && e.translation == translation) return e.slot;
		i = (i + 1) & (unsigned)(kMatIndexSize - 1);
	}
	return -1;
}

void ResetMaterialSlots()
{
	g_matSlotTable.Clear();
	MatIndexClear();
	MatSlot white; white.material = NULL; white.translation = 0;
	g_matSlotTable.Push(white);
	MatIndexInsert(white.material, white.translation, 0);
	g_matSlotOverflow = false;
	g_matSlotRetries = 0;
	g_matSlotsDirty = true;
}

// [rc4l] What a slot holds: a material in its translation, or, under kBrightmapTranslation, that material's brightmap (black for none).
static Diligent::IDeviceObject *SlotSRV(const MatSlot &e)
{
	if (e.translation == kBrightmapTranslation) return GetBrightmapSRV(e.material);
	return GetMaterialSRV(e.material, e.translation);
}

static int PushMaterialSlot(const void *material, int translation)
{
	MatSlot e; e.material = material; e.translation = translation;
	g_matSlotTable.Push(e);
	const int slot = (int)g_matSlotTable.Size() - 1;
	MatIndexInsert(material, translation, slot);
	g_matSlotsDirty = true;
	return slot;
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
			a.discrete = defs[i]->AnimType == FAnimDef::ANIM_DiscreteFrames;
			if (a.discrete)
				for (int f = 0; f < defs[i]->NumFrames; f++) a.pics.push_back(defs[i]->Frames[f].FramePic.GetIndex());
			g_anims.push_back(a);
		}
		g_animOfTex = zx::hwrender::ComputeAnimationOfTexture(TexMan.NumTextures(), g_anims);
	}
	const int id = info.textureIndex;
	return (id >= 0 && id < (int)g_animOfTex.size()) ? g_animOfTex[id] : -1;
}

int MaterialSlotFor(const void *material, int translation)
{
	if (g_matSlotTable.Size() == 0) ResetMaterialSlots();
	{
		const int found = MatIndexFind(material, translation);
		if (found >= 0) return found;
	}
	if (g_matSlotTable.Size() >= MaterialSlotCapacity())
	{
		// Loud, once: a level past the slot count draws its extra materials white, and white walls
		// with no message is the kind of thing that gets diagnosed as a texture-loading fault.
		if (!g_matSlotOverflow)
			Printf("Diligent bindless: more than %u materials on this level -- falling back to a binding per material"
				"\n", MaterialSlotCapacity());
		g_matSlotOverflow = true;
		return 0;
	}
	const int slot = PushMaterialSlot(material, translation);
	// [rc4l] Every frame of an animated texture gets a slot right behind it, so SlotRemap can point at any frame without a new binding.
	const int anim = AnimationOf(material);
	if (anim >= 0)
		for (const int frame : zx::hwrender::ComputeAnimationFrames(g_anims[anim]))
		{
			const void *fm = zx::materials::MaterialForTextureIndex(frame);
			if (fm != NULL && MatIndexFind(fm, translation) < 0 && g_matSlotTable.Size() < MaterialSlotCapacity())
				PushMaterialSlot(fm, translation);
		}
	return slot;
}

int MaterialSlotCount() { return (int)g_matSlotTable.Size(); }
// Force the array to be built again, for telling a stale fill from a wrong index.
void DirtyMaterialSlots() { g_matSlotsDirty = true; g_matSlotRetries = 0; }

// [rc4l] ONE binding per pipeline, which is the whole point.
//
// A STATIC shader variable is copied into every SRB made from the pipeline, and this backend makes
// one SRB per (pipeline, material) -- so a 256-slot array bound the old way would cost 256 times the
// material count in descriptors and take the device down. Bindless replaces those bindings rather
// than joining them: with the array in place there is nothing left that varies per material, so
// one binding per world pipeline serves the whole world. See docs/bindless-attempt-notes.md.
static Diligent::IShaderResourceBinding *g_worldSRBs[kWorldPipelines] = { 0 };
static bool g_bindlessReady = false;
// [rc4l] What the array was last filled FROM, so a rebuild that changes nothing rebuilds nothing.
//
// A scene rebuild resets the slot table and re-emits every vertex, and on a level with moving
// sectors that happens constantly -- dbab04 rebuilds the scene about two hundred times in a minute.
// The table it produces is almost always IDENTICAL to the one before it, so taking the reset as a
// reason to rebuild thirteen shader resource bindings meant rebuilding them every frame, and
// releasing a binding the GPU is still reading out of the previous frame's command buffer.
static TArray<MatSlot> g_filledFrom;

static bool MaterialSlotsMatchFill()
{
	if (g_filledFrom.Size() != g_matSlotTable.Size()) return false;
	for (unsigned i = 0; i < g_matSlotTable.Size(); i++)
		if (g_filledFrom[i].material != g_matSlotTable[i].material ||
		    g_filledFrom[i].translation != g_matSlotTable[i].translation) return false;
	return true;
}

// What the scene pixel shader said it declares, recorded at creation so the question can be asked
// later -- pipeline setup happens before anything is listening on the console.
static FString g_scenePSResources;

void ReleaseWorldSRBs()
{
	for (int i = 0; i < kWorldPipelines; i++)
		if (g_worldSRBs[i]) { g_worldSRBs[i]->Release(); g_worldSRBs[i] = NULL; }
}

// [rc4l] Fill the material array on a BINDING.
//
// Every element, always: an unassigned element is not "mostly bound", and the white placeholder
// covers the slots past the live ones as well as anything whose own upload failed.
static bool FillMaterialArraySRB(Diligent::IShaderResourceBinding *srb)
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
	for (Diligent::Uint32 i = 0; i < (Diligent::Uint32)MaterialSlotCapacity(); i++)
	{
		Diligent::IDeviceObject *obj = NULL;
		if (i < g_matSlotTable.Size())
			obj = SlotSRV(g_matSlotTable[i]);
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
			if (g_matSlotRetries < 120) { g_matSlotsDirty = true; g_matSlotRetries++; }
		}
		if (i < 8 && live)
		{
			g_stats.fillNames[i] = zx::materials::MaterialName(g_matSlotTable[i].material);
			g_stats.fillViews[i] = (const void *)obj;
		}
		v->SetArray(&obj, i, 1);
	}
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
	if (fua_dg_animate && g_bindlessReady && g_slotRemapBuf)
	{
		const bool moved = zx::hwrender::ComputeSlotRemap(g_matSlotTable.Size(), [](unsigned i) -> int {
			const MatSlot &e = g_matSlotTable[i];
			const void *now = zx::materials::MaterialCurrentFrame(e.material);
			return (now == NULL || now == e.material) ? -1 : MatIndexFind(now, e.translation);
		}, g_slotRemap);
		auto *ctx = GetContext();
		if (moved && ctx)
		{
			ctx->UpdateBuffer(g_slotRemapBuf, 0, (Diligent::Uint64)g_slotRemap.size() * sizeof(unsigned), &g_slotRemap[0],
				Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			g_stats.remapUploads++;
		}
	}
	if (!g_matSlotsDirty) return;
	g_matSlotsDirty = false;
	// A table that has not changed needs no new bindings -- see g_filledFrom.
	// [rc4l] Unless the last fill left a live material white, which is what the retry that dirtied it is for.
	if (zx::hwrender::ComputeKeepsMaterialFill(g_bindlessReady, MaterialSlotsMatchFill(), g_stats.matSlotsWhite)) return;
	g_bindlessReady = false;
	g_filledFrom.Clear();
	ReleaseWorldSRBs();
	// [rc4l] '-' for a pipeline the fill has not reached.
	memset(g_stats.fillState, '-', kWorldPipelines);
	for (int i = 0; i < kWorldPipelines; i++)
	{
		if (!g_pso.world[i]) return;
		g_pso.world[i]->CreateShaderResourceBinding(&g_worldSRBs[i], true);
		if (!g_worldSRBs[i]) { ReleaseWorldSRBs(); return; }
		g_stats.fillState[i] = 'b';
		// uTex and uBrightmap are still declared and still mutable. The shader does not read them
		// while the array is live, but an unassigned sampler is undefined rather than unused.
		Diligent::IDeviceObject *white = GetMaterialSRV(NULL, 0);
		if (auto *v = g_worldSRBs[i]->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
			v->Set(white);
		if (auto *v = g_worldSRBs[i]->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uBrightmap"))
		{
			Diligent::IDeviceObject *bm = GetBrightmapSRV(NULL);
			v->Set(bm ? bm : white);
		}
		if (!FillMaterialArraySRB(g_worldSRBs[i]))
		{
			ReleaseWorldSRBs();
			static bool said = false;
			if (!said) { said = true; Printf("Diligent bindless: staying on per-material bindings" "\n"); }
			return;
		}
	}
	g_bindlessReady = true;
	g_stats.fillCount++;
	g_filledFrom = g_matSlotTable;
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
	ReleaseBatchSRBs();
	ReleaseMaterialSRBs();
	for (unsigned b = 0; b < g_scene.batches.Size(); b++) g_scene.batches[b].srb = NULL;
	ReleaseWorldSRBs();
	ReleaseMaterials();
	g_bindlessReady = false;
	DirtyMaterialSlots();
}

bool BindlessActive() { return fua_dg_bindless && MaterialSlotCapacity() > 0 && g_bindlessReady && !g_matSlotOverflow; }


// [rc4l] Commit srb inside one draw loop, checking its resources' states only the first time the loop commits it.
void CommitInLoop(Diligent::IDeviceContext *ctx, zx::hwrender::LoopCommits &loop, Diligent::IShaderResourceBinding *srb)
{
	const bool transition = !fua_dg_loopcommits || zx::hwrender::ComputeCommitTransitions(loop, srb);
	ctx->CommitShaderResources(srb, transition ? Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION
		: Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
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

void ReleaseMaterialSRBs()
{
	ForgetBatchSRBs();
	for (unsigned i = 0; i < g_matSRBs.Size(); i++)
		if (g_matSRBs[i].srb) g_matSRBs[i].srb->Release();
	g_matSRBs.Clear();
}

static bool g_pieceBindFailed = false;
// [rc4l] Drop the pipelines and everything bound to them, so the next upload rebuilds with current
// settings. SRBs are created from a PSO and cannot outlive it.
static int g_builtCull = -1;
static unsigned g_builtSlots = 0;   // [rc4l] the material array size the pipelines were built for
static float g_builtAniso = 0.f;    // [rc4l] the anisotropy the samplers were built with
static int g_builtFilter = -1;

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
	for (auto &p : g_pso.world) p.Release();
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

// [rc4l] Off once the scene shaders failed with the hardware shaders in them, so the world still draws, plainly.
static bool g_userShadersOff = false;

const std::string &UserShaderBlock()
{
	static std::string block;
	static int builtFor = -1;
	static bool builtOff = false;
	const int count = g_userShadersOff ? 0 : zx::hwshaders::UserShaderCount();
	if (builtFor == count && builtOff == g_userShadersOff) return block;
	builtFor = count;
	builtOff = g_userShadersOff;
	std::vector<std::string> adapted;
	std::vector<bool> styles;
	for (int i = 0; i < count; i++)
	{
		std::string code, probeCode;
		bool style = false;
		const int lump = Wads.CheckNumForFullName(zx::hwshaders::UserShaderLump(i));
		if (lump >= 0)
		{
			FMemLump data = Wads.ReadLump(lump);
			const std::string src = data.GetString().GetChars();
			style = zx::hwshaders::ComputeAdaptUserShader(src, i, code);
			// [rc4l] Alone, against stand-ins for what the scene shader gives it, so one that leans on more of GL's main.fp than the backend offers is left out rather than taking the world down.
			const bool probeStyle = zx::hwshaders::ComputeAdaptUserShader(src, 0, probeCode);
			const std::string probe = std::string("#version 450\n"
				"layout(location = 0) in vec2 vUV;\n"
				"layout(location = 1) flat in vec2 vWarp;\n"
				"layout(binding = 0) uniform Probe { vec4 uFrame; };\n"
				"layout(binding = 1) uniform sampler2D uTex;\n"
				"layout(location = 0) out vec4 outColor;\n"
				"vec4 fuaTexel(vec2 uv) { return texture(uTex, uv); }\n"
				"vec4 fuaTexMode(vec4 t) { return t; }\n") +
				zx::hwshaders::ComputeUserShaderBlock({ probeCode }, { probeStyle }) +
				"void main() { outColor = fuaSurfaceTexel(vUV); }\n";
			if (!CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua hardware shader probe", probe.c_str())) code.clear();
		}
		if (code.empty()) Printf("vulkan: hardware shader %s does not compile here; its textures draw without it\n", zx::hwshaders::UserShaderLump(i));
		adapted.push_back(code);
		styles.push_back(style);
	}
	block = zx::hwshaders::ComputeUserShaderBlock(adapted, styles);
	return block;
}

bool EnsureScenePipeline(FString &err)
{
	// The sampler is immutable in the PSO, so a filter change means a new PSO. Cheap and rare.
	if (g_pso.world[kWpOpaque] && (g_builtFilter != (int)r_texture_filter || g_builtAniso != (float)r_texture_filter_anisotropic)) ReleaseScenePipelines();
	g_builtFilter = (int)r_texture_filter;
	g_builtAniso = r_texture_filter_anisotropic;
	if (g_pso.world[kWpOpaque] && g_builtCull != (int)fua_dg_cull) ReleaseScenePipelines();
	g_builtCull = (int)fua_dg_cull;
	// [rc4l] The shaders declare the array at its size, so a new size is new pipelines.
	if (g_pso.world[kWpOpaque] && g_builtSlots != MaterialSlotCapacity()) ReleaseScenePipelines();
	g_builtSlots = MaterialSlotCapacity();
	if (g_pso.world[kWpOpaque]) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) { err = "no device/swapchain"; return false; }

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, psOpaque, psMasked, psTrans, psRedAlpha, psGBuffer;
	vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua scene VS", kSceneVS);
	psOpaque = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua scene PS opaque", kScenePSOpaque);
	psMasked = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua scene PS masked", kScenePS);
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
	psGBuffer = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua scene PS masked + gbuffer", kScenePSGBuffer);
	psTrans = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua scene PS translucent", kScenePSTrans);
	psRedAlpha = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua scene PS red alpha", kScenePSRedAlpha);
	if (!vs || !psOpaque || !psMasked || !psTrans || !psRedAlpha)
	{
		// [rc4l] Shaders that compiled alone can still clash together, over a helper both define, so the world is drawn without them rather than not at all.
		if (!g_userShadersOff && zx::hwshaders::UserShaderCount() > 0)
		{
			Printf("vulkan: the hardware shaders together break the scene shaders; drawing without them\n");
			g_userShadersOff = true;
			return EnsureScenePipeline(err);
		}
		err = "scene shader compilation failed"; return false;
	}

	// [rc4l] Created once and never replaced, since pipelines that outlive a level change bind it statically and a replaced one is never mapped again.
	if (!g_cb)
	{
		Diligent::BufferDesc cbd;
		cbd.Name = "fua scene constants";
		cbd.Size = sizeof(float) * zx::hwrender::kSceneConstantFloats;
		cbd.Usage = Diligent::USAGE_DYNAMIC;
		cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
		cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		dev->CreateBuffer(cbd, nullptr, &g_cb);
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

	// [rc4l] `false`, not Diligent::False -- something in the reshaped DXSDK headers defines False as
	// a macro, so the qualified name does not survive the preprocessor here.
	Diligent::LayoutElement layout[] = {
		Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},   // position
		Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},   // uv
		Diligent::LayoutElement{2, 0, 1, Diligent::VT_FLOAT32, false},   // piece index
	};

	static Diligent::ShaderResourceVariableDesc vars[] = {
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
	// [rc4l] Per call, never static: the filter is the engine's at this rebuild, and a static array would keep the first one forever.
	Diligent::SamplerDesc samp;
	FillSamplerFromEngine(samp);
	samp.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
	const Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", samp },
		// [rc4l] The material array needs its own, and this is not optional.
		//
		// A GLSL sampler2D is a COMBINED image sampler, so an ARRAY of them needs a sampler
		// declared for the array too. Without one the variable does not reach the pipeline's
		// resource list at all -- GetStaticVariableByName returns null and the pixel statics are
		// Constants, LightBuffer and the two cluster buffers, with no sign that a sampler array
		// was ever declared. That reads as the shader having dropped it.
		{ Diligent::SHADER_TYPE_PIXEL, "uMaterials", samp },
	};

	// [rc4l] Every world pipeline over one vertex layout and set of resources, as ComputeWorldPipelineDesc describes it, the blended ones never writing depth.
	Diligent::IShader *stages[] = { psOpaque, psMasked, psTrans, psRedAlpha, psGBuffer };
	for (int pass = 0; pass < kWorldPipelines; pass++)
	{
		const WorldPipelineDesc d = ComputeWorldPipelineDesc(pass);
		const bool gbuffer = d.stage == kWpStageGBuffer;
		const bool blended = d.blend != kWpBlendNone;

		Diligent::GraphicsPipelineStateCreateInfo pci;
		pci.PSODesc.Name = d.name;
		pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
		pci.GraphicsPipeline.NumRenderTargets = gbuffer ? 2 : 1;
		pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
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
		if (blended)
		{
			auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
			rt.BlendEnable = true;
			// [rc4l] Fuzz is GL's fallback for an op it has no equation for, the screen times the source plus the screen the source leaves.
			rt.SrcBlend  = (d.blend == kWpBlendFuzz) ? Diligent::BLEND_FACTOR_DEST_COLOR : Diligent::BLEND_FACTOR_SRC_ALPHA;
			// [rc4l] Additive and the subtract styles take the source onto the screen whole, as STYLE_Add and STYLE_Subtract's ONE does.
			rt.DestBlend = (d.blend == kWpBlendAlpha || d.blend == kWpBlendFuzz) ? Diligent::BLEND_FACTOR_INV_SRC_ALPHA : Diligent::BLEND_FACTOR_ONE;
			rt.BlendOp = (d.blend == kWpBlendRevSub) ? Diligent::BLEND_OPERATION_REV_SUBTRACT :
				(d.blend == kWpBlendSub) ? Diligent::BLEND_OPERATION_SUBTRACT : Diligent::BLEND_OPERATION_ADD;
			rt.SrcBlendAlpha  = Diligent::BLEND_FACTOR_ONE;
			rt.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
			rt.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
		}
		pci.GraphicsPipeline.InputLayout.LayoutElements = layout;
		pci.GraphicsPipeline.InputLayout.NumElements = 3;
		pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		pci.PSODesc.ResourceLayout.Variables = vars;
		pci.PSODesc.ResourceLayout.NumVariables =
			(Diligent::Uint32)(sizeof(vars) / sizeof(vars[0]));
		pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers =
			(Diligent::Uint32)(sizeof(samplers) / sizeof(samplers[0]));
		pci.pVS = vs;
		pci.pPS = stages[d.stage];
		dev->CreateGraphicsPipelineState(pci, &g_pso.world[pass]);
		if (!g_pso.world[pass]) { err = "scene pipeline creation failed"; return false; }
	}

	// [rc4l] Every one has its statics bound, sprites' and decals' included: a pipeline created with none bound died on launch with "No resource is assigned to static shader variable 'Constants'".
	ReleaseWorldSRBs();
	g_matSlotsDirty = true;
	// [rc4l] Nothing to pre-fill: the material array is mutable, so it belongs to each binding and is
	// filled when that binding is made. A pipeline can hand out as many as it likes without the array
	// ever being read from it -- which is exactly the property that was missing when it was static.
	for (int i = 0; i < kWorldPipelines; i++)
	{
		Diligent::IPipelineState *pso = g_pso.world[i];
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
		if (auto *hv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "SkyHoles"))
			hv->Set(g_scene.skyHoleBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));

	}
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

// [rc4l] The slot table, for a pass that fills a material array of its own.
const TArray<MatSlot> &MaterialSlots() { return g_matSlotTable; }

// [rc4l] Slots the material array was last filled from, past which a piece samples white until the next fill.
int FilledSlotCount() { return (int)g_filledFrom.Size(); }

// [rc4l] The slot table now describes the level being uploaded, so the array may be filled from it.
void MarkMaterialSlotsCurrent() { g_bindlessGen = zx::levelmesh::LevelGeneration(); }

}} // namespace zx::hwrender
