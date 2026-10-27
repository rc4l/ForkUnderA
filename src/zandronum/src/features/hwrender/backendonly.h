// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A launch with no GL at all (-vulkan): the backend's own frame buffer and renderer, chosen at startup, so no GL context is ever made.

#ifndef ZX_BACKENDONLY_H
#define ZX_BACKENDONLY_H

// [rc4l] Diligent-free, since the engine's video code includes it and every Diligent header drags in a windows.h that fights the engine's own.

class DFrameBuffer;
struct FRenderer;   // [rc4l] a struct, as r_renderer.h defines it, or MSVC links a different name

#include "basictypes.h"   // [rc4l] fixed_t
#include "features/frame/computation/frameroute_compute.h"
#include <functional>

namespace zx { namespace hwrender {

// [rc4l] Whether this launch runs without GL, decided once on first ask from vid_backend, -vulkan and -gl (ComputeWantsBackendOnly) and whether a Vulkan device starts; always false in a build without the backend.
bool BackendOnly();

// [rc4l] Whether the backend's device comes up, saying why on the console when it does not, so the launch falls back to GL (Legacy).
bool BackendDeviceStarts();

// [rc4l] What Diligent's own threads reported, put on the console by the main thread; each frame does it.
void PrintDiligentMessages();

// [rc4l] Set the launch up for it before any video exists: the backend switched on, standalone and embedded, and GL's capability flags as a modern card has them.
void BeginBackendOnly();

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
// [Doruk-Aksoy] Every GLDEFS shader's programs compiled now, at launch once GLDEFS is read, as GL compiles its own at startup; a no-op before the device is up, once done, or with fua_dg_precompile_shaders off.
void PrecompileUserShaders();

// [rc4l] The largest 2D texture the device takes, which camera textures are sized against; 0 before the device is up.
int BackendMaxTextureSize();
// [rc4l] The graphics API the backend's device is made on ("Vulkan" or "WebGPU"); "" in a build without the backend.
const char *BackendApiName();

// [rc4l] The engine drew a view of the world this frame, which the present draws under the 2D layer.
void NoteWorldView();
// [rc4l] Drops the backend's caches keyed on textures and materials, which every start (a wad_reload's too) makes again at addresses the old ones may have had.
void ForgetBackendTextures();
// [rc4l] A texture the engine marked for this level's precache with its hit bits, which the backend makes when it sets the level up (precache_compute).
void QueueMaterialPrecache(const void *texture, int hits);
// [rc4l] Step the level's set-up through at once before its first view, which a launch without GL can since the map bakes the level whole, so that view, and a wipe's new screen, are drawn rather than black.
void SettleLevelScene();

// [rc4l] Advance the level's scene, and with `present` draw and present the frame: the world when a view of it was drawn and the level is baked, else the 2D layer alone.
void PresentBackendOnly(bool present);

// [rc4l] The last presented frame drawn again and read back, as top-down RGB the caller deletes; NULL when there is no device.
unsigned char *ReadBackendFrame(int &width, int &height);
// [rc4l] The player's last view as a w by h savegame picture, `fovDeg` across, read back as top-down RGB the caller deletes; NULL when the scene is not up.
unsigned char *RenderSavePicture(int w, int h, float fovDeg);
// [rc4l] The same for a device that reads back only later (a browser's WebGPU): drawn now, and done called with the picture as RGB on a later frame; false where RenderSavePicture serves.
bool RenderSavePictureLater(int w, int h, float fovDeg, std::function<void(const unsigned char *rgb, int w, int h)> done);
// [rc4l] The screen wipe: the last frame kept as the old screen (false for a type there is none of), this frame drawn and kept as the new one with its 2D dropped, a tic's mix drawn under the 2D from then on (true once done), and the end of it.
bool WipeStart(int type);
void WipeEnd();
bool WipeTic(int ticks);
void WipeCleanup();

// [rc4l] vid_fps per renderer: GL's frame runs from D_Display's start to the backend's LiveFrame, the backend's from there until it has drawn.
void FrameTimingBegin();
void GLFrameDone();
void BackendFrameDone();
// [rc4l] Whether the backend drew within the last second, and a renderer's last whole second as vid_fps shows it.
bool BackendRateLive();
void RendererRateShown(bool backend, int &ms, int &fps);

// [rc4l] The backend's frame, drawn after GL's once the engine has drawn its own (D_Display).
void LiveFrame();

// [rc4l] One camera texture, the world from another viewpoint into the backend's own target, w by h in pixels framed to aspect.
void RenderCameraTexture(const void *material, int w, int h, float aspect,
                         fixed_t px, fixed_t py, fixed_t pz, unsigned int pangle, int ppitch, float fovDeg);

// [rc4l] True once the backend's scene is up (switched on, the level baked and uploaded), whatever fua_dg_standalone says.
bool StandaloneActive();

// [rc4l] Whether the backend's window is the picture the player sees, which decides who presents, where StandaloneActive decides who builds.
bool BackendPresenting();

// [rc4l] The camera the backend draws with, column-major, so a standalone frame culls against the same matrix rather than deriving the camera again.
const float *SceneMVP();

}} // namespace zx::hwrender

#endif // ZX_BACKENDONLY_H
