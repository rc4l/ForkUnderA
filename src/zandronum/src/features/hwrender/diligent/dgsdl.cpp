// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's window on Linux, through SDL2: the engine's own window in a launch without GL, else one beside it, and its X11 or Wayland handles for the Vulkan surface.

#ifdef FUA_DILIGENT

#include <string.h>
#include <SDL.h>
#include <SDL_syswm.h>

#include "features/hwrender/diligent/dgplatform.h"
#include "features/hwrender/backendonly.h"   // [rc4l] BackendOnly

// [rc4l] The window the engine's frame buffer made (posix/sdl/sdlglvideo.cpp), NULL before it has one.
SDL_Window *I_EngineWindow();

namespace zx { namespace hwrender {

void *Fua_CreateBackendWindow(const char *title, int w, int h)
{
	// [rc4l] A launch without GL made the engine's window without a GL context, so the swapchain takes it over whole.
	if (BackendOnly() && I_EngineWindow() != NULL) return I_EngineWindow();
	// [rc4l] Beside GL a window of its own, since SDL nests no window in another and the engine's carries GL's context.
	return SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
}

bool Fua_BackendWindowHandles(void *window, BackendWindowHandles &out)
{
	memset(&out, 0, sizeof(out));
	if (window == NULL) return false;
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo((SDL_Window *)window, &info)) return false;
	switch (info.subsystem)
	{
#if defined(SDL_VIDEO_DRIVER_X11)
	case SDL_SYSWM_X11:
		out.x11Window = (unsigned long)info.info.x11.window;
		out.x11Display = info.info.x11.display;
		return true;
#endif
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
	case SDL_SYSWM_WAYLAND:
		out.waylandSurface = info.info.wl.surface;
		out.waylandDisplay = info.info.wl.display;
		return true;
#endif
	default:
		return false;
	}
}

// [rc4l] Nothing is nested in the engine's window here, so nothing follows it.
void Fua_SyncBackendWindowToParent(void *) {}

void Fua_ShowBackendWindow(void *window, int visible)
{
	if (window == NULL || window == I_EngineWindow()) return;
	if (visible) SDL_ShowWindow((SDL_Window *)window);
	else SDL_HideWindow((SDL_Window *)window);
}

// [rc4l] SDL has one event queue for every window, which the engine already pumps.
void Fua_PumpBackendWindow(void *) {}

bool Fua_RestoreEngineWindow()
{
	SDL_Window *engine = I_EngineWindow();
	if (engine == NULL || !(SDL_GetWindowFlags(engine) & SDL_WINDOW_MINIMIZED)) return false;
	SDL_RestoreWindow(engine);
	return true;
}

// [rc4l] libvulkan.so.1 is the system's, which volk finds itself.
bool Fua_PrepareVulkanLoader() { return true; }

}} // namespace zx::hwrender

#endif // FUA_DILIGENT
