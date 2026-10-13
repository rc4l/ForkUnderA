// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The renderer of a launch without GL: the engine's view through features/frame, on the level, light and fog half both renderers share.

#include "features/hwrender/backendonly.h"
#include "features/frame/levelrenderer.h"
#include "features/frame/playerview.h"
#include "features/frame/standaloneframe.h"   // [rc4l] the camera's own scene
#include "features/levelmesh/flatmesh.h"      // [rc4l] EnterScene, LeaveScene
#include "features/hwrender/hud2d.h"          // [rc4l] StandaloneActive
#include "features/viewsetup/viewsetup.h"   // [rc4l] InstallSpriteView
#include "features/draw2d/screenblend.h"     // [rc4l] the screen's tint over the save picture
#include "features/draw2d/computation/screenblend_compute.h"
#include "templates.h"                       // [rc4l] clamp, which gl_lightdata.h uses without including
#include "features/rendersettings/rendersettings.h"   // [rc4l] the render settings it reads
#include "r_utility.h"                       // [rc4l] viewsector, FieldOfView

#include "d_player.h"
#include "m_png.h"
#include "textures/textures.h"
#include "features/materials/materials.h"

namespace {

struct FBackendRenderer : public zx::frame::FLevelRenderer
{
	// [rc4l] Textures go to the backend as it first draws them, so there is nothing to warm.
	void PrecacheTexture(FTexture *, int) {}

	void RenderView(player_t *player)
	{
		zx::hwrender::SettleLevelScene();
		zx::frame::RenderPlayerView(player, zx::hwrender::BackendPaletteBrightness());
	}

	// [rc4l] The player's view drawn at the picture's size with the screen's tint over it, as GL's WriteSavePic draws it; the placeholder only when there is no scene.
	void WriteSavePic(player_t *, FILE *file, int width, int height)
	{
		unsigned char *rgb = zx::hwrender::RenderSavePicture(width, height, FieldOfView * 360.0f / FINEANGLES);
		if (rgb == NULL)
		{
			M_CreateDummyPNG(file);
			return;
		}
		zx::draw2d::ScreenFill fills[2];
		const int n = zx::draw2d::ScreenBlends(viewsector, in_area, gl_fixedcolormap != 0, zx::hwrender::BackendPaletteBrightness(), fills);
		for (int i = 0; i < n; i++)
			zx::draw2d::ComputeApplyScreenFill(fills[i].r, fills[i].g, fills[i].b, fills[i].a, fills[i].multiply, rgb, width * height);
		M_CreatePNG(file, rgb, NULL, SS_RGB, width, height, width * 3);
		delete[] rgb;
	}

	// [rc4l] A camera texture is the backend's own view into its own target: the camera's viewpoint set up and its scene swept, as GL's RenderViewpoint does for it, then drawn.
	void RenderTextureView(FCanvasTexture *tex, AActor *viewpoint, int fov)
	{
		const void *mat = zx::materials::MaterialForTexture(tex, false);
		if (mat != NULL && viewpoint != NULL)
		{
			int w, h;
			zx::materials::MaterialRenderSize(mat, w, h);
			const float ratio = (float)w / h;
			const bool savedMain = zx::frame::BeginViewpoint(viewpoint, (float)fov, ratio, false, false);
			if (zx::hwrender::StandaloneActive())
			{
				zx::levelmesh::EnterScene();
				zx::frame::StandaloneScene();
				zx::levelmesh::LeaveScene();
			}
			zx::frame::RenderBackendCamera(mat, viewpoint, fov);
			zx::frame::EndViewpoint(savedMain);
		}
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
