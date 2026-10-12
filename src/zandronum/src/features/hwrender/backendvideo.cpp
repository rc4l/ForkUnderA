// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The frame buffer of a launch without GL: the engine's window as GL's sets it up, minus the context, with every 2D draw recorded for the backend and the frame presented by it.

#include "gl/system/gl_system.h"
#include "features/hwrender/backendonly.h"
#include "features/hwrender/hud2d.h"
#include "features/draw2d/draw2d.h"
#include "features/display/display.h"
#include "features/frame/playerview.h"   // [rc4l] ForgetCamera
#include "features/hwrender/computation/imageresize_compute.h"
#include "features/video-scale/videoscale.h"

#include "c_cvars.h"
#include "i_system.h"   // [rc4l] atterm
#include "m_argv.h"
#include "v_palette.h"
#include "v_video.h"

// [rc4l] GL's capability flags and CPU-side palette and brightmap code, which need no context.
#include "gl/system/gl_interface.h"
#include "gl/textures/gl_texture.h"     // [rc4l] gl_GenerateGlobalBrightmapFromColormap
#include "gl/textures/gl_translate.h"   // [rc4l] GLTranslationPalette

#if defined(_WIN32) && defined(FUA_DILIGENT)
#include "win32gliface.h"
#endif

EXTERN_CVAR(Bool, fua_vulkan)
EXTERN_CVAR(Bool, fua_dg_standalone)
EXTERN_CVAR(Bool, fua_dg_embed)
EXTERN_CVAR(Bool, fua_surface_mapbake_auto)

namespace zx { namespace hwrender {

namespace {
int g_backendOnly = -1;

#if defined(_WIN32) && defined(FUA_DILIGENT)
// [rc4l] The switches as the user's config had them, put back before it is written so a -vulkan launch leaves no trace there.
bool g_savedVulkan, g_savedStandalone, g_savedEmbed, g_savedMapBake;

void RestoreSavedSwitches()
{
	fua_vulkan = g_savedVulkan;
	fua_dg_standalone = g_savedStandalone;
	fua_dg_embed = g_savedEmbed;
	fua_surface_mapbake_auto = g_savedMapBake;
}
#endif
}

bool BackendOnly()
{
#if defined(_WIN32) && defined(FUA_DILIGENT)
	if (g_backendOnly < 0) g_backendOnly = Args->CheckParm("-vulkan") ? 1 : 0;
	return g_backendOnly == 1;
#else
	return false;
#endif
}

void BeginBackendOnly()
{
#if defined(_WIN32) && defined(FUA_DILIGENT)
	g_savedVulkan = fua_vulkan;
	g_savedStandalone = fua_dg_standalone;
	g_savedEmbed = fua_dg_embed;
	g_savedMapBake = fua_surface_mapbake_auto;
	// [rc4l] Registered after the config's own save, so it runs before it.
	atterm(RestoreSavedSwitches);
	fua_vulkan = true;
	fua_dg_standalone = true;
	fua_dg_embed = true;
	// [rc4l] With no GL derivation to fall back on, a moved sector must re-bake from the map.
	fua_surface_mapbake_auto = true;
	// [rc4l] What gl_LoadExtensions would have found on any card the backend runs on; the sprite UVs read it.
	gl.flags |= RFL_NPOT_TEXTURE;
#endif
}

}} // namespace zx::hwrender

#if defined(_WIN32) && defined(FUA_DILIGENT)

// [rc4l] Win32GLFrameBuffer without its context: the window, the modes and the gamma ramp, and nothing GL.
class DiligentFrameBuffer : public Win32GLFrameBuffer
{
	DECLARE_CLASS(DiligentFrameBuffer, Win32GLFrameBuffer)
public:
	DiligentFrameBuffer() {}
	DiligentFrameBuffer(void *hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen)
		: Win32GLFrameBuffer(hMonitor, width, height, bits, refreshHz, fullscreen, false)
	{
		mColors.Load();
		zx::frame::ForgetCamera();
		gl_GenerateGlobalBrightmapFromColormap();
		DoSetGamma();
		Accel2D = true;
		if (!zx::hwrender::OpenBackendOutput(width, height)) I_FatalError("-vulkan: the Vulkan backend could not start.");
		// [rc4l] What gl_LoadExtensions reads from GL, from the device instead.
		gl.max_texturesize = zx::hwrender::BackendMaxTextureSize();
	}

	~DiligentFrameBuffer() { ReleaseScreenshotBuffer(); }

	void Update()
	{
		// [rc4l] An inactive window is not presented to, as GL's is not, but its level is still set up and baked.
		if (!CanUpdate())
		{
			zx::hwrender::PresentBackendOnly(false);
			return;
		}
		Begin2D(false);
		DrawRateStuff();
		zx::hwrender::PresentBackendOnly(true);
		Unlock();
		// [rc4l] After Unlock, as GL's, since resizing reallocates the canvas a lock aliases.
		FollowClientSize();
	}

	bool SetGamma(float) { DoSetGamma(); return true; }
	bool SetBrightness(float) { DoSetGamma(); return true; }
	bool SetContrast(float) { DoSetGamma(); return true; }

	// [rc4l] The palette, its brightness and the flash are features/display's, as GL's frame buffer keeps them.
	void UpdatePalette() { mColors.UpdateBrightness(); }
	void GetFlashedPalette(PalEntry pal[256]) { memcpy(pal, mColors.palette, 256 * sizeof(PalEntry)); }
	PalEntry *GetPalette() { return mColors.palette; }
	bool SetFlash(PalEntry rgb, int amount) { mColors.SetFlash(rgb, amount); return true; }
	void GetFlash(PalEntry &rgb, int &amount) { mColors.GetFlash(rgb, amount); }
	int GetPageCount() { return 1; }
	// [rc4l] A game restart reloads the palette and the brightmap colormap, as GL's does.
	void GameRestart()
	{
		mColors.Load();
		zx::frame::ForgetCamera();
		gl_GenerateGlobalBrightmapFromColormap();
	}

	// [rc4l] 2D is recorded rather than drawn, so there is no state to set up for it.
	bool Begin2D(bool) { return true; }

	void STACK_ARGS DrawTextureV(FTexture *img, double x, double y, uint32 tag, va_list tags)
	{
		DrawParms parms;
		if (!ParseDrawTextureTags(img, x, y, tag, tags, &parms, true)) return;
		zx::draw2d::TextureDraw d;
		zx::draw2d::PrepareTexture(img, parms, d);
		zx::hwrender::Record2D(d.quad);
	}
	void DrawLine(int x1, int y1, int x2, int y2, int palcolor, uint32 color)
	{
		zx::hwrender::Record2D(zx::draw2d::PrepareLine(x1, y1, x2, y2, zx::draw2d::DrawColor(palcolor, color)));
	}
	void DrawPixel(int x, int y, int palcolor, uint32 color)
	{
		zx::hwrender::Record2D(zx::draw2d::PreparePixel(x, y, zx::draw2d::DrawColor(palcolor, color)));
	}
	void Clear(int left, int top, int right, int bottom, int palcolor, uint32 color)
	{
		zx::hwrender::Record2D(zx::draw2d::PrepareClear(left, top, right, bottom, zx::draw2d::DrawColor(palcolor, color)));
	}
	// [rc4l] As GL's: the view's blend belongs to the view, so the screen dim ignores the colour.
	void Dim(PalEntry) { Super::Dim(0); }
	void Dim(PalEntry color, float damount, int x1, int y1, int w, int h)
	{
		zx::hwrender::Record2D(zx::draw2d::PrepareDim(color, damount, x1, y1, w, h));
	}
	void FlatFill(int left, int top, int right, int bottom, FTexture *src, bool local_origin)
	{
		zx::draw2d::PrepareFlatFill(left, top, right, bottom, src, local_origin, mFlatQuads);
		for (size_t i = 0; i < mFlatQuads.size(); i++) zx::hwrender::Record2D(mFlatQuads[i]);
	}
	// [rc4l] Not recorded for the backend yet, on GL's launch either: the textured automap.
	void FillSimplePoly(FTexture *, FVector2 *, int, double, double, double, double, angle_t, FDynamicColormap *, int) {}

	FNativePalette *CreatePalette(FRemapTable *remap) { return GLTranslationPalette::CreatePalette(remap); }

	// [rc4l] No wipe yet: a screen change cuts straight over.
	bool WipeStartScreen(int) { return false; }
	void WipeEndScreen() {}
	bool WipeDo(int) { return true; }
	void WipeCleanup() {}
	bool Is8BitMode() { return false; }

	int PaletteBrightness() const { return mColors.brightness; }

	// [rc4l] The screenshot is the backend's frame, read back and brought to the screen's size when the window it is drawn for is another (video scaling, a windowed mode change).
	void GetScreenshotBuffer(const BYTE *&buffer, int &pitch, ESSType &color_type)
	{
		ReleaseScreenshotBuffer();
		buffer = NULL;
		int w = 0, h = 0;
		unsigned char *rgb = zx::hwrender::ReadBackendFrame(w, h);
		if (rgb == NULL) return;
		const int sw = GetWidth(), sh = GetHeight();
		if (w != sw || h != sh)
		{
			BYTE *scaled = new BYTE[(size_t)sw * sh * 3];
			zx::ComputeNearestResize(w, h, rgb, sw, sh, scaled, 3);
			delete[] rgb;
			rgb = scaled;
		}
		mShot = rgb;
		buffer = mShot;
		pitch = sw * 3;
		color_type = SS_RGB;
	}
	void ReleaseScreenshotBuffer()
	{
		delete[] mShot;
		mShot = NULL;
	}

private:
	// [rc4l] GL's per-frame reconcile without its scale buffer: the screen follows the window's client area and the scale cvars, and the backend draws at the window's size.
	void FollowClientSize()
	{
		int cw = 0, ch = 0;
		// [rc4l] A minimized window has no client area; the last good size is kept until it comes back.
		if (!ClientSize(cw, ch) || cw <= 0 || ch <= 0) return;
		int wantW = 0, wantH = 0;
		if (zx::ReconcileRenderSize(cw, ch, GetWidth(), GetHeight(), mClientW, mClientH, wantW, wantH) == zx::SCALE_RECONCILE_RESIZE)
			zx::ResizeScreenInPlace(this, wantW, wantH);
		mClientW = cw;
		mClientH = ch;
	}

	void DoSetGamma()
	{
		if (!m_supportsGamma) return;
		WORD ramp[768];
		zx::display::CurrentGammaRamp(ramp);
		SetGammaTable(ramp);
	}

	zx::display::ScreenColors mColors;
	BYTE *mShot = NULL;
	int mClientW = 0, mClientH = 0;   // [rc4l] the client size last reconciled
	std::vector<zx::hwrender::Quad2D> mFlatQuads;   // [rc4l] kept, so a fill settles at its high-water mark
};

IMPLEMENT_CLASS(DiligentFrameBuffer)

namespace zx { namespace hwrender {

DFrameBuffer *CreateBackendFrameBuffer(void *hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen)
{
	return new DiligentFrameBuffer(hMonitor, width, height, bits, refreshHz, fullscreen);
}

// [rc4l] The palette brightness the screen's tint darkens a colormap by, from the frame buffer that has it.
int BackendPaletteBrightness()
{
	if (screen == NULL || !screen->IsKindOf(RUNTIME_CLASS(DiligentFrameBuffer))) return 0;
	return static_cast<DiligentFrameBuffer *>(screen)->PaletteBrightness();
}

}} // namespace zx::hwrender

#endif   // [rc4l] a build without the backend takes these from backendstubs.cpp
