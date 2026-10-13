// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/framedriver.h"
#include "features/frame/levelrenderer.h"
#include "features/frame/playerview.h"
#include "features/hwrender/backendonly.h"
#include "features/hwrender/hud2d.h"   // [rc4l] StandaloneActive, whether the backend's scene is up
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

	bool BackendDraws() const { return backend != NULL && zx::hwrender::BackendDrawsFrames(); }
	// [rc4l] GL draws while the backend does not, while the backend's scene is not up yet (its window then stands aside), or when GL's own view is wanted.
	bool GLDraws() const
	{
		return gl != NULL && (!BackendDraws() || !zx::hwrender::StandaloneActive() || zx::hwrender::GLViewWanted());
	}

	void PrecacheTexture(FTexture *tex, int cache)
	{
		if (gl != NULL) gl->PrecacheTexture(tex, cache);
	}

	void RenderView(player_t *player)
	{
		const bool glDraws = GLDraws(), backendDraws = BackendDraws();
		if (glDraws) gl->BeginFrame(player);
		BeginPlayerFrame(player);
		if (glDraws) gl->RenderView(player);
		if (backendDraws) backend->RenderView(player);
	}

	// [rc4l] The picture of what the player sees: the backend's while it presents its scene, else GL's.
	void WriteSavePic(player_t *player, FILE *file, int width, int height)
	{
		if (gl == NULL || (BackendDraws() && zx::hwrender::StandaloneActive())) backend->WriteSavePic(player, file, width, height);
		else gl->WriteSavePic(player, file, width, height);
	}

	void RenderTextureView(FCanvasTexture *tex, AActor *viewpoint, int fov)
	{
		if (GLDraws()) gl->RenderTextureView(tex, viewpoint, fov);
		if (BackendDraws()) backend->RenderTextureView(tex, viewpoint, fov);
	}

	void ClearBuffer(int color)
	{
		if (GLDraws()) gl->ClearBuffer(color);
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
