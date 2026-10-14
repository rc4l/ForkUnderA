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
#include "m_misc.h"                          // [rc4l] M_WriteFile
#include "features/hwrender/computation/savepicture_compute.h"
#include <string>
#include <vector>
#include "textures/textures.h"
#include "features/materials/materials.h"

namespace {

// [rc4l] The screen's tint over a picture, as GL's WriteSavePic puts it there.
static void TintPicture(const std::vector<zx::draw2d::ScreenFill> &fills, unsigned char *rgb, int pixels)
{
	for (const zx::draw2d::ScreenFill &f : fills)
		zx::draw2d::ComputeApplyScreenFill(f.r, f.g, f.b, f.a, f.multiply, rgb, pixels);
}

// [rc4l] An open file's bytes from its start; false when it reads short.
static bool ReadWhole(FILE *file, std::vector<unsigned char> &out)
{
	if (fseek(file, 0, SEEK_END) != 0) return false;
	const long size = ftell(file);
	if (size < 0 || fseek(file, 0, SEEK_SET) != 0) return false;
	out.resize((size_t)size);
	return fread(out.data(), 1, out.size(), file) == out.size();
}

// [rc4l] The save picture a device reads back only after its save is written: the request's number, the save it goes in once G_DoSaveGame names it, and the screen's tint at the save.
struct LaterSavePicture
{
	unsigned serial = 0;
	bool pending = false;
	std::string path;
	std::vector<zx::draw2d::ScreenFill> fills;
};
static LaterSavePicture g_laterPicture;

// [rc4l] The picture is back: tinted, made a PNG and put in place of the save's placeholder, unless a newer save asked since or this one was never written.
static void FinishLaterPicture(unsigned serial, const unsigned char *rgb, int w, int h)
{
	if (!g_laterPicture.pending || g_laterPicture.serial != serial || g_laterPicture.path.empty()) return;
	g_laterPicture.pending = false;
	std::vector<unsigned char> pixels(rgb, rgb + (size_t)w * h * 3), picture, save, out;
	TintPicture(g_laterPicture.fills, pixels.data(), w * h);
	FILE *png = tmpfile();
	const bool made = png != NULL && M_CreatePNG(png, pixels.data(), NULL, SS_RGB, w, h, w * 3) && ReadWhole(png, picture);
	if (png != NULL) fclose(png);
	FILE *file = made ? fopen(g_laterPicture.path.c_str(), "rb") : NULL;
	const bool read = file != NULL && ReadWhole(file, save);
	if (file != NULL) fclose(file);
	if (!read || !zx::ComputeSpliceSavePicture(save, picture, out) || !M_WriteFile(g_laterPicture.path.c_str(), out.data(), (int)out.size()))
		Printf("save picture: could not put it in %s\n", g_laterPicture.path.c_str());
}

struct FBackendRenderer : public zx::frame::FLevelRenderer
{
	// [rc4l] Textures go to the backend as it first draws them, so there is nothing to warm.
	void PrecacheTexture(FTexture *, int) {}

	void RenderView(player_t *player)
	{
		zx::hwrender::SettleLevelScene();
		zx::frame::RenderPlayerView(player, zx::hwrender::BackendPaletteBrightness());
	}

	// [rc4l] The player's view drawn at the picture's size with the screen's tint over it, as GL's WriteSavePic draws it; the placeholder when there is no scene, and for now on a device that reads it back only later.
	void WriteSavePic(player_t *player, FILE *file, int width, int height)
	{
		if (player == NULL || player->camera == NULL)
		{
			M_CreateDummyPNG(file);
			return;
		}
		// [rc4l] The view set up from the player's camera first, as GL's RenderViewpoint does, since a save can come before any frame has set it, as quitting straight after a level loads does.
		const float fov = FieldOfView * 360.0f / FINEANGLES;
		// [rc4l] A 1.6 ratio both ways, as GL's WriteSavePic hands RenderViewpoint.
		const FViewLens lens = { fov, 1.6f, 1.6f };
		const bool savedMain = zx::frame::BeginViewpoint(player->camera, lens, false, false);
		WriteSavePicFromView(file, width, height, fov);
		zx::frame::EndViewpoint(savedMain);
	}

	static void WriteSavePicFromView(FILE *file, int width, int height, float fov)
	{
		const unsigned serial = g_laterPicture.serial + 1;
		if (zx::hwrender::RenderSavePictureLater(width, height, fov, [serial](const unsigned char *rgb, int w, int h) { FinishLaterPicture(serial, rgb, w, h); }))
		{
			g_laterPicture.serial = serial;
			g_laterPicture.pending = true;
			g_laterPicture.path.clear();
			g_laterPicture.fills = ScreenFills();
			M_CreateDummyPNG(file);
			return;
		}
		unsigned char *rgb = zx::hwrender::RenderSavePicture(width, height, fov);
		if (rgb == NULL)
		{
			M_CreateDummyPNG(file);
			return;
		}
		TintPicture(ScreenFills(), rgb, width * height);
		M_CreatePNG(file, rgb, NULL, SS_RGB, width, height, width * 3);
		delete[] rgb;
	}

	// [rc4l] The screen's tint where the view stands, which only a view set up for the picture can say.
	static std::vector<zx::draw2d::ScreenFill> ScreenFills()
	{
		std::vector<zx::draw2d::ScreenFill> fills(2);
		fills.resize(zx::draw2d::ScreenBlends(viewsector, in_area, gl_fixedcolormap != 0, zx::hwrender::BackendPaletteBrightness(), fills.data()));
		return fills;
	}

	// [rc4l] The save a picture still being read back belongs in.
	void SaveWritten(const char *path)
	{
		if (g_laterPicture.pending && g_laterPicture.path.empty()) g_laterPicture.path = path;
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
			// [MobiusU9B] The texture's own shape for both ratios, as GL's RenderTextureView hands RenderViewpoint.
			const FViewLens lens = { (float)fov, ratio, ratio };
			const bool savedMain = zx::frame::BeginViewpoint(viewpoint, lens, false, false);
			zx::frame::StandaloneScene();
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
