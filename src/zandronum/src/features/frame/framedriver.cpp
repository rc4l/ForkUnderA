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
	FRenderer *gl, *backend;

	FFrameDriver(FRenderer *g, FRenderer *b) : gl(g), backend(b) {}
	~FFrameDriver()
	{
		delete gl;
		delete backend;
	}

	// [rc4l] Which renderer draws this frame and whose picture shows.
	FrameRoute Route() const { return zx::hwrender::FrameRouteNow(gl != NULL, backend != NULL); }

	void PrecacheTexture(FTexture *tex, int cache)
	{
		if (gl != NULL) gl->PrecacheTexture(tex, cache);
	}

	void RenderView(player_t *player)
	{
		const FrameRoute r = Route();
		if (r.glDraws) gl->BeginFrame(player);
		BeginPlayerFrame(player);
		if (r.glDraws) gl->RenderView(player);
		if (r.backendDraws) backend->RenderView(player);
	}

	// [rc4l] The picture of what the player sees: the backend's while it presents its scene, else GL's.
	void WriteSavePic(player_t *player, FILE *file, int width, int height)
	{
		if (Route().savePicFromBackend) backend->WriteSavePic(player, file, width, height);
		else gl->WriteSavePic(player, file, width, height);
	}

	void RenderTextureView(FCanvasTexture *tex, AActor *viewpoint, int fov)
	{
		const FrameRoute r = Route();
		if (r.glDraws) gl->RenderTextureView(tex, viewpoint, fov);
		if (r.backendDraws) backend->RenderTextureView(tex, viewpoint, fov);
	}

	void ClearBuffer(int color)
	{
		if (Route().glDraws) gl->ClearBuffer(color);
	}

	void Init()
	{
		FLevelRenderer::Init();
		zx::viewsetup::InstallSpriteView();
	}

	void RemapVoxels() { if (gl != NULL) gl->RemapVoxels(); }
	void DrawRemainingPlayerSprites() { if (gl != NULL) gl->DrawRemainingPlayerSprites(); }
	void OnModeSet() { if (gl != NULL) gl->OnModeSet(); }
	void ErrorCleanup() { if (gl != NULL) gl->ErrorCleanup(); }
	void SetWindow(int windowSize, int fullWidth, int fullHeight, int stHeight, int trueratio)
	{
		if (gl != NULL) gl->SetWindow(windowSize, fullWidth, fullHeight, stHeight, trueratio);
	}
	void SetupFrame(player_t *player) { if (gl != NULL) gl->SetupFrame(player); }
	void CopyStackedViewParameters() { if (gl != NULL) gl->CopyStackedViewParameters(); }
};

} // namespace

FRenderer *CreateFrameDriver(FRenderer *gl, FRenderer *backend) { return new FFrameDriver(gl, backend); }

}} // namespace zx::frame
