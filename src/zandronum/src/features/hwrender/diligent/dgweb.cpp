// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's window in a browser: the page's canvas, which SDL's window already is and the WebGPU swapchain draws on by its selector.

#ifdef FUA_DILIGENT

#include <string.h>
#include <SDL.h>

#include "features/hwrender/diligent/dgplatform.h"

// [rc4l] The window the engine's frame buffer made (posix/sdl/sdlvideo.cpp), NULL before it has one.
SDL_Window *I_EngineWindow();

namespace zx { namespace hwrender {

// [rc4l] A page has one canvas, the engine's window, which a build without GL hands to the swapchain whole.
void *Fua_CreateBackendWindow(const char *, int, int)
{
	return I_EngineWindow();
}

bool Fua_BackendWindowHandles(void *window, BackendWindowHandles &out)
{
	memset(&out, 0, sizeof(out));
	if (window == NULL) return false;
	// [rc4l] Emscripten's SDL draws on Module.canvas, which the page gives this id.
	out.canvasId = "#canvas";
	return true;
}

void Fua_SyncBackendWindowToParent(void *) {}
void Fua_ShowBackendWindow(void *, int) {}

// [rc4l] The browser delivers the canvas's events to SDL, which the engine already pumps.
void Fua_PumpBackendWindow(void *) {}

bool Fua_RestoreEngineWindow() { return false; }

// [rc4l] There is no Vulkan in a browser.
void Fua_PrepareVulkanLoader() {}

}} // namespace zx::hwrender

#endif // FUA_DILIGENT
