// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The frame buffer of a launch without GL: the engine's window as GL's sets it up, minus the context, with every 2D draw recorded for the backend and the frame presented by it.

#include "features/hwrender/backendonly.h"
#include "features/draw2d/record2d.h"   // [rc4l] zx::draw2d::Record2DCanvas, the 2D recorded rather than drawn
#include "features/draw2d/draw2d.h"
#include "features/display/display.h"
#include "features/viewsetup/viewsetup.h"   // [rc4l] ForgetCamera
#include <vector>
#include "features/hwrender/computation/imageresize_compute.h"
#include "features/hwrender/computation/backendchoice_compute.h"
#include "features/video-scale/videoscale.h"

#include "c_cvars.h"
#include "i_system.h"   // [rc4l] atterm
#include "m_argv.h"
#include "v_palette.h"
#include "v_video.h"

#include "features/materials/materials.h"   // [rc4l] the texture system's setup without GL

#if defined(FUA_DILIGENT)
#ifdef _WIN32
#include "gl/system/gl_system.h"   // [rc4l] before win32gliface.h, which leans on its Windows headers
#include "win32gliface.h"
// [rc4l] Each platform's frame buffer without its GL context: the window, the modes and the gamma ramp.
typedef Win32GLFrameBuffer BackendFrameBufferBase;
#else
#include "backendfb.h"   // [rc4l] posix/sdl's or posix/cocoa's, whichever backend the build uses
#endif
#endif

#ifdef FUA_DILIGENT_VULKAN
static const bool kVulkanBuilt = true;
#else
static const bool kVulkanBuilt = false;
#endif
#ifdef FUA_WEBGPU
static const bool kWebGPUBuilt = true;
#else
static const bool kWebGPUBuilt = false;
#endif

// [rc4l] The renderer a launch draws with (ComputeWantsBackendOnly): 1 Vulkan (Recommended), 0 GL (Legacy), 2 the backend on WebGPU where built; -vulkan, -webgpu and -gl override it for one launch, and ComputeVidBackendFor keeps it to what the build can honour.
CUSTOM_CVAR(Int, vid_backend, zx::hwrender::ComputeVidBackendFor(zx::hwrender::kVidBackendVulkan, kVulkanBuilt, kWebGPUBuilt), CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	const int honoured = zx::hwrender::ComputeVidBackendFor(self, kVulkanBuilt, kWebGPUBuilt);
	if (honoured != self) self = honoured;
}

// [rc4l] Whether the video menu's renderer can be changed, which a browser's build, drawing only with WebGPU, greys out.
CVAR(Bool, vid_backend_choosable, zx::hwrender::ComputeVidBackendChoosable(kVulkanBuilt, kWebGPUBuilt), CVAR_NOSET)

// [rc4l] The vid_backend this launch's renderer was chosen from, which the video menu compares against to say a change waits for a restart.
CVAR(Int, vid_backend_launched, -1, CVAR_NOSET)

EXTERN_CVAR(Bool, fua_vulkan)
EXTERN_CVAR(Bool, fua_dg_standalone)
EXTERN_CVAR(Bool, fua_dg_embed)

namespace zx { namespace hwrender {

namespace {
int g_backendOnly = -1;

#if defined(FUA_DILIGENT)
// [rc4l] The switches as the user's config had them, put back before it is written so a -vulkan launch leaves no trace there.
bool g_savedVulkan, g_savedStandalone, g_savedEmbed;

void RestoreSavedSwitches()
{
	fua_vulkan = g_savedVulkan;
	fua_dg_standalone = g_savedStandalone;
	fua_dg_embed = g_savedEmbed;
}
#endif
}


bool BackendOnly()
{
	if (g_backendOnly < 0)
	{
#if defined(FUA_DILIGENT)
		const bool built = true;
#else
		const bool built = false;
#endif
		const bool argBackend = Args->CheckParm("-vulkan") != 0 || Args->CheckParm("-webgpu") != 0;
		const bool wants = ComputeWantsBackendOnly(vid_backend, argBackend, Args->CheckParm("-gl") != 0, built);
		g_backendOnly = (wants && BackendDeviceStarts()) ? 1 : 0;
		UCVarValue chosen;
		chosen.Int = vid_backend;
		vid_backend_launched.ForceSet(chosen, CVAR_Int);
	}
	return g_backendOnly == 1;
}

void BeginBackendOnly()
{
#if defined(FUA_DILIGENT)
	g_savedVulkan = fua_vulkan;
	g_savedStandalone = fua_dg_standalone;
	g_savedEmbed = fua_dg_embed;
	// [rc4l] Registered after the config's own save, so it runs before it.
	atterm(RestoreSavedSwitches);
	fua_vulkan = true;
	fua_dg_standalone = true;
	fua_dg_embed = true;
	// [rc4l] What gl_LoadExtensions would have found on any card the backend runs on; the sprite UVs read it.
	zx::materials::AssumeBackendTextures(0);
#endif
}

}} // namespace zx::hwrender

#if defined(FUA_DILIGENT)

// [rc4l] The platform's frame buffer without its context: the window, the modes and the gamma ramp, and nothing GL.
class DiligentFrameBuffer : public BackendFrameBufferBase
{
	DECLARE_CLASS(DiligentFrameBuffer, BackendFrameBufferBase)
public:
	DiligentFrameBuffer() {}
	DiligentFrameBuffer(void *hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen)
		: BackendFrameBufferBase(hMonitor, width, height, bits, refreshHz, fullscreen, false)
	{
		mColors.Load();
		zx::viewsetup::ForgetCamera();
		zx::materials::GenerateGlobalBrightmap();
		DoSetGamma();
		Accel2D = true;
		if (!zx::hwrender::OpenBackendOutput(width, height)) I_FatalError("-vulkan: the Vulkan backend could not start.");
		// [rc4l] What gl_LoadExtensions reads from GL, from the device instead.
		zx::materials::AssumeBackendTextures(zx::hwrender::BackendMaxTextureSize());
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
		zx::viewsetup::ForgetCamera();
		zx::materials::GenerateGlobalBrightmap();
	}

	// [rc4l] Window pixels to the render pixels the menu hit-tests in, as GL's frame buffer maps them, since a scaled screen is stretched over the whole client.
	void ScaleCoordsFromWindow(int &x, int &y) { zx::ScaleWindowPointToRender(mClientW, mClientH, GetWidth(), GetHeight(), x, y); }

	// [rc4l] 2D is recorded rather than drawn, so there is no state to set up for it.
	bool Begin2D(bool) { return true; }

	void STACK_ARGS DrawTextureV(FTexture *img, double x, double y, uint32 tag, va_list tags)
	{
		DrawParms parms;
		if (ParseDrawTextureTags(img, x, y, tag, tags, &parms, true)) m2D.Texture(img, parms);
	}
	void DrawLine(int x1, int y1, int x2, int y2, int palcolor, uint32 color) { m2D.Line(x1, y1, x2, y2, palcolor, color); }
	void DrawPixel(int x, int y, int palcolor, uint32 color) { m2D.Pixel(x, y, palcolor, color); }
	void Clear(int left, int top, int right, int bottom, int palcolor, uint32 color) { m2D.Clear(left, top, right, bottom, palcolor, color); }
	// [rc4l] As GL's: the view's blend belongs to the view, so the screen dim ignores the colour.
	void Dim(PalEntry) { Super::Dim(0); }
	void Dim(PalEntry color, float damount, int x1, int y1, int w, int h) { m2D.Dim(color, damount, x1, y1, w, h); }
	void FlatFill(int left, int top, int right, int bottom, FTexture *src, bool local_origin) { m2D.FlatFill(left, top, right, bottom, src, local_origin); }
	// [rc4l] The textured automap's floors, recorded as the fan GL draws.
	void FillSimplePoly(FTexture *tex, FVector2 *points, int npoints, double originx, double originy, double scalex, double scaley,
		angle_t rotation, FDynamicColormap *colormap, int lightlevel)
	{
		m2D.Poly(tex, points, npoints, originx, originy, scalex, scaley, rotation, colormap, lightlevel);
	}

	FNativePalette *CreatePalette(FRemapTable *remap) { return zx::materials::CreateTranslationPalette(remap); }

	// [rc4l] The screen wipes, drawn by the backend from the last frame and the new one, locked and unlocked as GL's frame buffer is.
	bool WipeStartScreen(int type) { return zx::hwrender::WipeStart(type); }
	void WipeEndScreen()
	{
		zx::hwrender::WipeEnd();
		Unlock();
	}
	bool WipeDo(int ticks)
	{
		Lock(true);
		return zx::hwrender::WipeTic(ticks);
	}
	void WipeCleanup() { zx::hwrender::WipeCleanup(); }
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
	zx::draw2d::Record2DCanvas m2D;   // [rc4l] the 2D, recorded rather than drawn
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
