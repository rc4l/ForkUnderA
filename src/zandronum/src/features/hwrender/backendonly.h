// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A launch with no GL at all (-vulkan): the backend's own frame buffer and renderer, chosen at startup, so no GL context is ever made.

#ifndef ZX_BACKENDONLY_H
#define ZX_BACKENDONLY_H

// [rc4l] Diligent-free like hud2d.h, since the engine's video code includes it and every Diligent header drags in a windows.h that fights the engine's own.

class DFrameBuffer;
struct FRenderer;   // [rc4l] a struct, as r_renderer.h defines it, or MSVC links a different name

#include "features/frame/computation/frameroute_compute.h"

namespace zx { namespace hwrender {

// [rc4l] Whether this launch runs without GL, decided once on first ask from vid_backend, -vulkan and -gl (ComputeWantsBackendOnly) and whether a Vulkan device starts; always false in a build without the backend.
bool BackendOnly();

// [rc4l] Whether the backend's device comes up, saying why on the console when it does not, so the launch falls back to GL (Legacy).
bool BackendDeviceStarts();

// [rc4l] Set the launch up for it before any video exists: the backend switched on, standalone and embedded, and GL's capability flags as a modern card has them.
void BeginBackendOnly();

// [rc4l] The renderer the backend draws the world with, in a launch without GL or beside it.
FRenderer *CreateBackendRenderer();
// [rc4l] The renderer a launch draws with, which every platform's I_CreateRenderer asks for: GL's (from `createGL`, NULL in a build without GL) and the backend's under features/frame's driver, or the backend's alone in a launch without GL.
// [rc4l] The renderer a launch draws with, which every platform's I_CreateRenderer asks for: GL's (from `createGL`) and the backend's under features/frame's driver, or the backend's alone in a launch without GL.
FRenderer *CreateLaunchRenderer(FRenderer *(*createGL)());

// [rc4l] Which renderer draws this frame and whose picture shows, from the launch's renderers and the backend's state and cvars.
zx::frame::FrameRoute FrameRouteNow(bool hasGL, bool hasBackend);

// [rc4l] The frame buffer it draws the screen with, on the engine's own window.
DFrameBuffer *CreateBackendFrameBuffer(void *hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen);

// [rc4l] The palette's brightness, from the backend's frame buffer, which the screen's tint darkens a colormap by.
int BackendPaletteBrightness();

// [rc4l] Bring up the backend's device, window and swapchain at the frame buffer's size; false when Vulkan is not there.
bool OpenBackendOutput(int width, int height);
// [rc4l] A window is about to be destroyed (an SDL frame buffer's, on a mode change): the swapchain made on it goes first, and the next OpenBackendOutput makes one on the new window.
void ForgetBackendWindow(void *window);

// [rc4l] The largest 2D texture the device takes, which camera textures are sized against; 0 before the device is up.
int BackendMaxTextureSize();
// [rc4l] The graphics API the backend's device is made on ("Vulkan" or "WebGPU"); "" in a build without the backend.
const char *BackendApiName();

// [rc4l] The engine drew a view of the world this frame, which the present draws under the 2D layer.
void NoteWorldView();
// [rc4l] Step the level's set-up through at once before its first view, which a launch without GL can since the map bakes the level whole, so that view, and a wipe's new screen, are drawn rather than black.
void SettleLevelScene();

// [rc4l] Advance the level's scene, and with `present` draw and present the frame: the world when a view of it was drawn and the level is baked, else the 2D layer alone.
void PresentBackendOnly(bool present);

// [rc4l] The last presented frame drawn again and read back, as top-down RGB the caller deletes; NULL when there is no device.
unsigned char *ReadBackendFrame(int &width, int &height);
// [rc4l] The player's last view as a w by h savegame picture, `fovDeg` across, read back as top-down RGB the caller deletes; NULL when the scene is not up.
unsigned char *RenderSavePicture(int w, int h, float fovDeg);
// [rc4l] The screen wipe: the last frame kept as the old screen (false for a type there is none of), this frame drawn and kept as the new one with its 2D dropped, a tic's mix drawn under the 2D from then on (true once done), and the end of it.
bool WipeStart(int type);
void WipeEnd();
bool WipeTic(int ticks);
void WipeCleanup();

}} // namespace zx::hwrender

#endif // ZX_BACKENDONLY_H
