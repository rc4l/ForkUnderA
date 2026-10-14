// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's window on each platform (dgwin32.cpp, dgsdl.cpp, dgcocoa.mm), kept free of Diligent's headers so those files never include them.

#ifndef ZX_DGPLATFORM_H
#define ZX_DGPLATFORM_H

namespace zx { namespace hwrender {

// [rc4l] What a swapchain is made on: an HWND on Windows; an X11 window and display, or a Wayland surface and display, on Linux; a Metal-layered NSView on macOS; a canvas's CSS selector in a browser.
struct BackendWindowHandles
{
	void *hwnd;
	unsigned long x11Window;
	void *x11Display;
	void *waylandSurface;
	void *waylandDisplay;
	void *nsView;
	const char *canvasId;
};

// [rc4l] A window for the backend `w` by `h`: the engine's own when GL does not draw into it, inside it where the platform can nest one, else beside it; NULL when none could be made.
void *Fua_CreateBackendWindow(const char *title, int w, int h);

// [rc4l] The window's handles for the swapchain; false when its window system is one the backend cannot draw to.
bool Fua_BackendWindowHandles(void *window, BackendWindowHandles &out);

// [rc4l] Keep a nested window over the engine's client area as it resizes.
void Fua_SyncBackendWindowToParent(void *window);

// [rc4l] Show or hide a window of the backend's own, so turning it off reveals GL's picture; the engine's own window is left alone.
void Fua_ShowBackendWindow(void *window, int visible);

// [rc4l] Service a window the engine's event loop does not, once a presented frame.
void Fua_PumpBackendWindow(void *window);

// [rc4l] Un-minimize the engine's window without taking focus; true if it was minimized.
bool Fua_RestoreEngineWindow();

// [rc4l] Make the Vulkan loader findable before the device is made, as a bundle that carries its own must.
void Fua_PrepareVulkanLoader();

// [rc4l] Whether the backend draws inside the engine's window (fua_dg_embed), from dgframe.cpp.
bool Fua_WantEmbeddedWindow();

}} // namespace zx::hwrender

#endif // ZX_DGPLATFORM_H
