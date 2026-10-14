// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/framedriver.h"
#include "features/frame/levelrenderer.h"
#include "features/frame/playerview.h"
#include "features/hwrender/backendonly.h"
#include "features/viewsetup/viewsetup.h"   // [rc4l] InstallSpriteView

#include "textures/textures.h"

namespace zx { namespace frame {

namespace {

struct FFrameDriver : public FLevelRenderer
{
	std::vector<DrivenRenderer> renderers;

	explicit FFrameDriver(const std::vector<DrivenRenderer> &r) : renderers(r) {}
	~FFrameDriver()
	{
		for (const DrivenRenderer &d : renderers) delete d.renderer;
	}

	bool Has(int kind) const
	{
		for (const DrivenRenderer &d : renderers) if (d.kind == kind) return true;
		return false;
	}

	// [rc4l] Which kinds draw this frame and whose picture shows.
	FrameRoute Route() const { return zx::hwrender::FrameRouteNow(Has(kRendererGL), Has(kRendererBackend)); }

	// [rc4l] `call` on every renderer, or with a route on those that draw on it.
	template <class Call> void Every(Call call) { for (const DrivenRenderer &d : renderers) call(d.renderer); }
	template <class Call> void Drawing(const FrameRoute &r, Call call)
	{
		for (const DrivenRenderer &d : renderers) if (ComputeKindDraws(r, d.kind)) call(d.renderer);
	}

	void PrecacheTexture(FTexture *tex, int cache) { Every([&](FRenderer *x) { x->PrecacheTexture(tex, cache); }); }

	void RenderView(player_t *player)
	{
		const FrameRoute r = Route();
		Drawing(r, [&](FRenderer *x) { x->BeginFrame(player); });
		BeginPlayerFrame(player);
		Drawing(r, [&](FRenderer *x) { x->RenderView(player); });
	}

	// [rc4l] The picture of what the player sees, from the kind the route names, else the first renderer.
	void WriteSavePic(player_t *player, FILE *file, int width, int height)
	{
		const int kind = ComputeSavePicKind(Route());
		for (const DrivenRenderer &d : renderers)
			if (d.kind == kind) { d.renderer->WriteSavePic(player, file, width, height); return; }
		if (!renderers.empty()) renderers[0].renderer->WriteSavePic(player, file, width, height);
	}

	void RenderTextureView(FCanvasTexture *tex, AActor *viewpoint, int fov)
	{
		Drawing(Route(), [&](FRenderer *x) { x->RenderTextureView(tex, viewpoint, fov); });
	}

	void ClearBuffer(int color) { Drawing(Route(), [&](FRenderer *x) { x->ClearBuffer(color); }); }

	void Init()
	{
		FLevelRenderer::Init();
		zx::viewsetup::InstallSpriteView();
	}

	void RemapVoxels() { Every([](FRenderer *x) { x->RemapVoxels(); }); }
	void DrawRemainingPlayerSprites() { Every([](FRenderer *x) { x->DrawRemainingPlayerSprites(); }); }
	void OnModeSet() { Every([](FRenderer *x) { x->OnModeSet(); }); }
	void ErrorCleanup() { Every([](FRenderer *x) { x->ErrorCleanup(); }); }
	void SetWindow(int windowSize, int fullWidth, int fullHeight, int stHeight, int trueratio)
	{
		Every([&](FRenderer *x) { x->SetWindow(windowSize, fullWidth, fullHeight, stHeight, trueratio); });
	}
	void SetupFrame(player_t *player) { Every([&](FRenderer *x) { x->SetupFrame(player); }); }
	void CopyStackedViewParameters() { Every([](FRenderer *x) { x->CopyStackedViewParameters(); }); }
};

} // namespace

FRenderer *CreateFrameDriver(const std::vector<DrivenRenderer> &renderers) { return new FFrameDriver(renderers); }

}} // namespace zx::frame
