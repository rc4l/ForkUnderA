// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The renderer of a launch without GL: the engine's view through features/frame, on the level, light and fog half both renderers share.

#include "gl/system/gl_system.h"
#include "features/hwrender/backendonly.h"
#include "features/frame/levelrenderer.h"
#include "features/frame/playerview.h"
#include "features/viewsetup/viewsetup.h"   // [rc4l] InstallSpriteView

#include "d_player.h"
#include "m_png.h"
#include "textures/textures.h"
#include "gl/textures/gl_material.h"

namespace {

struct FBackendRenderer : public zx::frame::FLevelRenderer
{
	// [rc4l] Textures go to the backend as it first draws them, so there is nothing to warm.
	void PrecacheTexture(FTexture *, int) {}

	void RenderView(player_t *player) { zx::frame::RenderPlayerView(player, zx::hwrender::BackendPaletteBrightness()); }

	// [rc4l] No picture of the view yet: the save is written with the placeholder the software path writes when it has none.
	void WriteSavePic(player_t *, FILE *file, int, int) { M_CreateDummyPNG(file); }

	// [rc4l] A camera texture is the backend's own view into its own target, as GL's RenderTextureView hands it over.
	void RenderTextureView(FCanvasTexture *tex, AActor *viewpoint, int fov)
	{
		zx::frame::RenderBackendCamera(FMaterial::ValidateTexture(tex, false), viewpoint, fov);
		tex->SetUpdated();
	}

	// [rc4l] The backend clears its own target every frame.
	void ClearBuffer(int) {}

	// [rc4l] GLDEFS, and the sprite derivation answered from features/viewsetup as GL's interface answers it from itself.
	void Init()
	{
		FLevelRenderer::Init();
		zx::viewsetup::InstallSpriteView();
	}
};

} // namespace

namespace zx { namespace hwrender {

FRenderer *CreateBackendRenderer() { return new FBackendRenderer; }

}} // namespace zx::hwrender
