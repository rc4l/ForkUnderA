// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Milestones 2 and 3 of the backend swap: a Diligent swapchain that presents, and a triangle
// through a real pipeline.
//
// It runs on its OWN window, deliberately. The engine's window already carries an OpenGL context,
// and a window surface cannot serve both APIs -- so taking over the engine's window is a later
// milestone that has to remove the GL context first. Proving present + shader compilation + pipeline
// state on a separate window derisks all of that without touching the running renderer.
//
// What this establishes, none of which was previously known on this codebase:
//   * a Vulkan swapchain can be created and presented from inside the engine process,
//   * Diligent's shader factory compiles GLSL to SPIR-V here (no external glslangValidator step),
//   * a pipeline state object builds against the swapchain's formats,
//   * all of it coexists with the live GL renderer, frame after frame.

#include "c_dispatch.h"
#include "c_console.h"

#ifdef FUA_DILIGENT

#include "features/hwrender/diligent/dgdevice.h"
#include "features/hwrender/computation/framepacing_compute.h"   // [rc4l] ComputeSwapchainImages
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "RefCntAutoPtr.hpp"
#include "NativeWindow.h"
#include "features/hwrender/diligent/dgplatform.h"
#include "features/hwrender/diligent/dgshared.h"   // [rc4l] ReleaseFrameFence, ReleaseLevelResources, ResetAtlas
#include "features/hwrender/diligent/dgmemory.h"   // [rc4l] ReportHeldDeviceObjects
#include "features/hwrender/diligent/dggputimer.h"   // [rc4l] ReleaseGpuPasses
#include "features/hwrender/backendonly.h"   // [rc4l] ForgetBackendWindow
#include "DebugOutput.h"
#include "i_system.h"   // [rc4l] atterm
#include "c_cvars.h"
#include "v_text.h"   // [Doruk-Aksoy] TEXTCOLOR_ORANGE

EXTERN_CVAR(Bool, vid_vsync)   // [Doruk-Aksoy] picks the swapchain's image count (ComputeSwapchainImages)
// [Doruk-Aksoy] Forces the swapchain's image count when above 0, so a test can bring back the three that stuttered under vsync.
CVAR(Int, fua_debug_swapchain_images, 0, CVAR_FUA_DEV_MODE)

namespace zx { namespace hwrender {

bool ProbeVulkan(FString &report);   // dgprobe.cpp
Diligent::IRenderDevice  *GetDevice();
Diligent::IDeviceContext *GetContext();

static void                                          *g_hwnd = NULL;
static int g_wantW = 0, g_wantH = 0;

// [rc4l] Set by the caller before the window exists; see EnsureWindow.
void Fua_SetBackendWindowSize(int w, int h) { g_wantW = w; g_wantH = h; }
static Diligent::RefCntAutoPtr<Diligent::ISwapChain>  g_swap;
static int                                            g_swapImages = 0;

static bool EnsureWindow(FString &err)
{
	if (g_hwnd) return true;
	// [rc4l] Match the engine's own screen, so the 2D layer maps 1:1 and a screenshot pair is
	// directly comparable. Set by the caller, which knows about `screen`; falls back to 640x480.
	g_hwnd = Fua_CreateBackendWindow("ForkUnderA - Diligent (Vulkan) backend",
		g_wantW > 0 ? g_wantW : 640, g_wantH > 0 ? g_wantH : 480);
	if (!g_hwnd) { err = "backend window creation failed"; return false; }
	return true;
}

// [rc4l] At exit, ahead of the static destructors (atterm runs first) and while the window and the console are still there: the swapchain and the frame fence released and Diligent's messages kept off the console, since what is left is destroyed after both are gone; a quit crashed printing the fence's message to a freed console.
static void ShutdownBackend()
{
	// [rc4l] What is still held once the level's resources are gone is the backend's own (targets, pipelines, constants) or a leak, which the console log keeps.
	ReleaseLevelResources();
	ResetAtlas();
	ReleasePostProcess();
	ReleaseShaderCache();
	ReportHeldDeviceObjects("at shutdown, after the level's resources were released");
	ReleaseGpuPasses();
	ReleaseGpuLoad();
	ForgetBackendWindow(g_hwnd);
	ReleaseFrameFence();
	Diligent::SetDebugMessageCallback(nullptr);
}

static int WantedSwapchainImages()
{
	return ComputeSwapchainImages(vid_vsync, PagePaced(), fua_debug_swapchain_images);
}

static bool CreateSwapChain(int images, FString &report)
{
	Diligent::SwapChainDesc scd;
	// [rc4l] Match the engine's own framebuffer: plain UNORM, not Diligent's default sRGB.
	//
	// With an sRGB target the hardware encodes the shader's output on write, so the stored bytes
	// are numerically much brighter than what was written. The GL renderer writes to a plain
	// UNORM buffer whose contents are treated as already-sRGB. Both end up looking the same on
	// screen, but their BUFFERS differ -- which made every readback comparison against GL wrong,
	// and sent a long hunt after a lighting bug that was really a screenshot bug.
	scd.ColorBufferFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;
	// [Doruk-Aksoy] Two with vsync and three for mailbox (ComputeSwapchainImages).
	scd.BufferCount = (Diligent::Uint32)images;
	// [rc4l] The window as this platform's Vulkan surface takes it: an HWND, an X11 window or a Wayland surface, or a Metal-layered view.
	BackendWindowHandles handles;
	if (!Fua_BackendWindowHandles(g_hwnd, handles)) { report = "the backend window is on a window system Vulkan cannot draw to here"; return false; }
	Diligent::NativeWindow win;
#if PLATFORM_WIN32
	win.hWnd = handles.hwnd;
#elif PLATFORM_LINUX
	win.WindowId = (Diligent::Uint32)handles.x11Window;
	win.pDisplay = handles.x11Display != NULL ? handles.x11Display : handles.waylandDisplay;
	win.pWaylandSurface = handles.waylandSurface;
#elif PLATFORM_MACOS
	win.pNSView = handles.nsView;
#elif PLATFORM_WEB
	win.pCanvasId = handles.canvasId;
#endif
	ChosenDeviceApi()->createSwapChain(GetDevice(), GetContext(), scd, win, &g_swap);
	if (!g_swap) { report = "swapchain creation failed"; return false; }
	g_swapImages = images;
	static bool shutdownSet = false;
	if (!shutdownSet) { atterm(ShutdownBackend); shutdownSet = true; }
	// [rc4l] Say what depth format we actually got. Z-fighting the GL renderer does not have is
	// first a question about depth precision, and "Diligent probably defaults to D32" is not an
	// answer -- a 16-bit depth buffer against a 5..65536 frustum would fight everywhere.
	Printf("Diligent swapchain: color %d, depth %d (%dx%d), %d images\n",
		(int)g_swap->GetDesc().ColorBufferFormat, (int)g_swap->GetDesc().DepthBufferFormat,
		(int)g_swap->GetDesc().Width, (int)g_swap->GetDesc().Height, (int)g_swap->GetDesc().BufferCount);
	return true;
}

bool DiligentShowWindow(FString &report)
{
	FString err;
	if (!ProbeVulkan(err)) { report = err; return false; }
	if (!EnsureWindow(err)) { report = err; return false; }
	if (!g_swap && !CreateSwapChain(WantedSwapchainImages(), report)) return false;

	report.Format("Diligent swapchain %dx%d",
		(int)g_swap->GetDesc().Width, (int)g_swap->GetDesc().Height);
	return true;
}

void ForgetBackendWindow(void *window)
{
	if (window == NULL || window != g_hwnd) return;
	// [rc4l] The device is done with the swapchain's images before the surface under them goes; a browser's canvas outlives its swapchain, and its GPU cannot be waited on.
	if (GetDevice() != NULL && CanWaitForGPU()) GetDevice()->IdleGPU();
	g_swap.Release();
	g_hwnd = NULL;
}

// [rc4l] Accessors for dgscene.cpp -- one swapchain and one window, shared.
Diligent::ISwapChain *GetSwapChain() { return g_swap; }

// [Doruk-Aksoy] Diligent keeps a swapchain's image count for its life, so a vsync toggle that wants another count remakes it, keeping the old count if the new one cannot be made.
static void FollowBackendVsync()
{
	const int images = WantedSwapchainImages();
	if (!g_swap || images == g_swapImages) return;
	const int had = g_swapImages;
	g_swap.Release();
	FString report;
	if (!CreateSwapChain(images, report) && !CreateSwapChain(had, report))
		Printf(TEXTCOLOR_ORANGE "Diligent swapchain could not be remade: %s\n", report.GetChars());
}

void FollowBackendSurface()
{
	FollowBackendVsync();
	int w = 0, h = 0;
	if (!g_swap || !Fua_BackendSurfaceSize(g_hwnd, w, h)) return;
	const Diligent::SwapChainDesc &d = g_swap->GetDesc();
	if ((int)d.Width != w || (int)d.Height != h) g_swap->Resize((Diligent::Uint32)w, (Diligent::Uint32)h);
}
void *GetBackendWindow() { return g_hwnd; }

}} // namespace zx::hwrender

// [rc4l] fuactl's shot asks this when GL's frame comes back blank, the tell of a minimized window.
DEV_MODE_CCMD( fua_window_restore ) { Printf( "%s\n", zx::hwrender::Fua_RestoreEngineWindow( ) ? "restored" : "not minimized" ); }

#else // !FUA_DILIGENT

DEV_MODE_CCMD( fua_window_restore )  { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }

#endif // FUA_DILIGENT
