// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Skies other than the level's that a pass's sky surfaces show (a line's, sky2), each drawn into a picture before its view and painted onto its surfaces by screen position.

#include "features/hwrender/diligent/dgsky_internal.h"
#include <memory>   // [rc4l] unique_ptr, the views' sky look sets
#include <vector>

EXTERN_CVAR(Bool, fua_dg_sky)

namespace zx { namespace hwrender {

// [rc4l] One sky's picture, as GL draws each GLSkyInfo through a portal of its own: its target, the binding that paints surfaces from it, and its size.
struct SkyLookPicture
{
	Diligent::RefCntAutoPtr<Diligent::ITexture> color;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
	int w = 0, h = 0;
};
static const int kSkyLookPictures = 4;
// [rc4l] A sky key a pass's sky surfaces carry, and the picture it paints from, -1 for the level's sky.
struct SkyKeyPicture { int key, picture; };
// [rc4l] One pass's pictures, their looks and the one depth buffer they are drawn with in turn, the sky lists its surfaces come from, and which picture each key paints from.
struct SkyLookSet
{
	SkyLookPicture pictures[kSkyLookPictures];
	zx::hwrender::SkyLook looks[kSkyLookPictures];
	Diligent::RefCntAutoPtr<Diligent::ITexture> depth;
	int count = 0;
	TArray<SkyKeyPicture> keys;
	int lists[2] = { zx::levelmesh::kSkyCaptureNone, zx::levelmesh::kSkyCaptureNone };
	int listCount = 0;
	int w = 0, h = 0;
};
// [rc4l] The player's view's set, and the other views' (stacked views, mirrors, camera textures), one per target size, each made just before its view.
static SkyLookSet g_mainLooks;
static std::vector<std::unique_ptr<SkyLookSet> > g_viewLooks;
static SkyLookSet *g_activeViewLooks = NULL;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_lookSurfPSO;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_lookSurfVB;
static unsigned int g_lookSurfVBCapacity = 0;

void ReleaseSkyLookPictures()
{
	g_mainLooks = SkyLookSet();
	g_viewLooks.clear();
	g_activeViewLooks = NULL;
	g_lookSurfPSO.Release();
}

// [rc4l] Where key stands in a set, or -1 when none of its surfaces carries it.
static int FindSkyKey(const SkyLookSet &set, int key)
{
	for (unsigned i = 0; i < set.keys.Size(); i++) if (set.keys[i].key == key) return (int)i;
	return -1;
}

// [rc4l] The looks other than the level's that `lists` show, drawn into `set`'s pictures at w by h from the eye in g_mvp; false when there are none.
static bool RenderLookSet(Diligent::IDeviceContext *ctx, SkyLookSet &set, const int *lists, int listCount, int w, int h, bool screenView)
{
	// [rc4l] A look of the screen draws in the scene's pixels at the top left of its w by h picture, as the pass that paints it reads it.
	const bool scaled = screenView && w == FrameWidth() && h == FrameHeight();
	const int viewW = scaled ? SceneWidth() : w, viewH = scaled ? SceneHeight() : h;
	set.count = 0;
	set.keys.Clear();
	set.listCount = listCount;
	for (int l = 0; l < listCount; l++) set.lists[l] = lists[l];
	if (!fua_dg_sky || SkyPortalActive() || !EnsureSkyPipeline()) return false;
	const zx::hwrender::SkyLook level = LookOfSky(0);
	for (int l = 0; l < listCount; l++)
	{
		int nverts = 0;
		const zx::levelmesh::MeshVertex *q = zx::levelmesh::SkyQuadVertices(nverts, lists[l]);
		for (int i = 0; q != NULL && i + 5 < nverts; i += 6)
		{
			const int key = (int)q[i].u;
			if (key == 0 || FindSkyKey(set, key) >= 0) continue;
			const zx::hwrender::SkyLook look = LookOfSky(zx::hwrender::ComputeSkyFromQuadKey(key));
			const SkyKeyPicture kp = { key, zx::hwrender::ComputeSkyLookPicture(set.looks, set.count, kSkyLookPictures, look, level) };
			set.keys.Push(kp);
		}
	}
	if (set.count == 0) return false;
	if (!g_lookSurfPSO) g_lookSurfPSO = CreateScreenSurfacePSO("fua sky look surface", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
	if (!g_lookSurfPSO) { set.count = 0; return false; }

	// [rc4l] The pass's matrix and the frame's colormap, which the dome shaders read from the scene block.
	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		WriteSceneConstants(&cb[0], nullptr, (float)viewW, (float)viewH, BindlessActive() ? (float)MaterialSlotCount() : 0.f);
	}
	const bool depthFits = set.depth && (int)set.depth->GetDesc().Width == w && (int)set.depth->GetDesc().Height == h;
	if (!depthFits && !CreateDepthTarget(w, h, "fua sky look", set.depth)) { set.count = 0; return false; }
	for (int p = 0; p < set.count; p++)
	{
		SkyLookPicture &pic = set.pictures[p];
		if (!pic.color || pic.w != w || pic.h != h)
		{
			if (!CreateColorTarget(w, h, "fua sky look", pic.color) ||
				!CreatePictureBinding(g_lookSurfPSO, "uSky", pic.color, pic.srb)) { set.count = p; break; }
			pic.w = w; pic.h = h;
		}
		auto *prtv = pic.color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
		auto *pdsv = set.depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
		ctx->SetRenderTargets(1, &prtv, pdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		Diligent::Viewport vp;
		vp.Width = (float)viewW; vp.Height = (float)viewH;
		vp.MinDepth = 0.f; vp.MaxDepth = 1.f;
		ctx->SetViewports(1, &vp, (Diligent::Uint32)w, (Diligent::Uint32)h);
		const float clearCol[4] = { 0.f, 0.f, 0.f, 1.f };
		ctx->ClearRenderTarget(prtv, clearCol, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		const zx::hwrender::SkyLook &look = set.looks[p];
		DrawLook(ctx, look, DomeFor(look.texture), look.secondTexture >= 0 ? DomeFor(look.secondTexture) : NULL);
	}
	return set.count > 0;
}

// [rc4l] Each picture of a set changes hands here, outside the frame's render pass, for the reason RenderSkyPortal gives.
static void HandOverLookSet(Diligent::IDeviceContext *ctx, const SkyLookSet &set)
{
	for (int p = 0; p < set.count; p++)
	{
		Diligent::StateTransitionDesc barrier(set.pictures[p].color, Diligent::RESOURCE_STATE_UNKNOWN,
			Diligent::RESOURCE_STATE_SHADER_RESOURCE, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
		ctx->TransitionResourceStates(1, &barrier);
	}
}

void RenderSkyLooks(Diligent::IDeviceContext *ctx)
{
	const int lists[1] = { zx::levelmesh::kSkyCaptureMain };
	if (!RenderLookSet(ctx, g_mainLooks, lists, 1, FrameWidth(), FrameHeight(), true)) return;
	auto *brtv = FrameRTV();
	Diligent::ITextureView *bdsv = EnsureSceneDepth();
	if (!bdsv) bdsv = FrameDSV();
	ctx->SetRenderTargets(1, &brtv, bdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	HandOverLookSet(ctx, g_mainLooks);
}

void RenderViewSkyLooks(Diligent::IDeviceContext *ctx, bool cameraPass, int stackSlot, bool mirrorPass, int w, int h)
{
	g_activeViewLooks = NULL;
	SkyLookSet *set = NULL;
	for (size_t i = 0; i < g_viewLooks.size() && set == NULL; i++)
		if (g_viewLooks[i]->w == w && g_viewLooks[i]->h == h) set = g_viewLooks[i].get();
	if (set == NULL)
	{
		g_viewLooks.emplace_back(new SkyLookSet);
		set = g_viewLooks.back().get();
		set->w = w; set->h = h;
	}
	int lists[2];
	const int n = zx::levelmesh::ComputeSkyLookLists(cameraPass, stackSlot, mirrorPass, lists);
	if (!RenderLookSet(ctx, *set, lists, n, w, h, !cameraPass)) return;
	ctx->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	HandOverLookSet(ctx, *set);
	g_activeViewLooks = set;
}

void ForgetViewSkyLooks()
{
	g_activeViewLooks = NULL;
}

// [rc4l] See dgsky_internal.h: each run painted from its look's picture, the player's view from its own set and any other view from the one made for it.
bool DrawSkyLookSurfaces(Diligent::IDeviceContext *ctx)
{
	if (g_pass.inSkyPortal || !g_lookSurfPSO) return false;
	const bool playerPass = g_pass.mainView && g_pass.lineMirror < 0 && g_pass.stackSlot < 0 && !g_pass.inCameraPass;
	const SkyLookSet *set = playerPass ? &g_mainLooks : g_activeViewLooks;
	if (set == NULL || set->count == 0) return false;
	static TArray<SceneVertex> sv;
	unsigned int first[kSkyLookPictures], count[kSkyLookPictures];
	sv.Clear();
	for (int p = 0; p < set->count; p++)
	{
		first[p] = sv.Size();
		for (int l = 0; l < set->listCount; l++)
		{
			int nverts = 0;
			const zx::levelmesh::MeshVertex *q = zx::levelmesh::SkyQuadVertices(nverts, set->lists[l]);
			for (int i = 0; q != NULL && i + 5 < nverts; i += 6)
			{
				const int at = FindSkyKey(*set, (int)q[i].u);
				if (at < 0 || set->keys[at].picture != p) continue;
				for (int k = 0; k < 6; k++) sv.Push(ToSceneVertex(q[i + k], 0));
			}
		}
		count[p] = sv.Size() - first[p];
	}
	if (!BindScreenSurfaces(ctx, g_lookSurfVB, g_lookSurfVBCapacity, sv, g_lookSurfPSO, "fua sky look surface VB")) return false;
	for (int p = 0; p < set->count; p++)
		if (count[p] > 0 && set->pictures[p].srb) DrawScreenSurfaceRun(ctx, set->pictures[p].srb, first[p], count[p]);
	return true;
}

}} // namespace zx::hwrender
