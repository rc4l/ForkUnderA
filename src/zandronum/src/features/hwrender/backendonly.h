// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A launch with no GL at all (-vulkan): the backend's own frame buffer and renderer, chosen at startup, so no GL context is ever made.

#ifndef ZX_BACKENDONLY_H
#define ZX_BACKENDONLY_H

// [rc4l] Diligent-free like hud2d.h, since the engine's video code includes it and every Diligent header drags in a windows.h that fights the engine's own.

class DFrameBuffer;
class FRenderer;

namespace zx { namespace hwrender {

// [rc4l] Whether this launch runs without GL, decided once from the command line (-vulkan) on first ask; always false in a build without the backend.
bool BackendOnly();

// [rc4l] Set the launch up for it before any video exists: the backend switched on, standalone and embedded, and GL's capability flags as a modern card has them.
void BeginBackendOnly();

// [rc4l] The renderer a launch without GL draws the world with.
FRenderer *CreateBackendRenderer();

// [rc4l] The frame buffer it draws the screen with, on the engine's own window.
DFrameBuffer *CreateBackendFrameBuffer(void *hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen);

// [rc4l] The palette's brightness, from the backend's frame buffer, which the screen's tint darkens a colormap by.
int BackendPaletteBrightness();

// [rc4l] Bring up the backend's device, window and swapchain at the frame buffer's size; false when Vulkan is not there.
bool OpenBackendOutput(int width, int height);

// [rc4l] The largest 2D texture the device takes, which camera textures are sized against; 0 before the device is up.
int BackendMaxTextureSize();

// [rc4l] The engine drew a view of the world this frame, which the present draws under the 2D layer.
void NoteWorldView();

// [rc4l] Advance the level's scene, and with `present` draw and present the frame: the world when a view of it was drawn and the level is baked, else the 2D layer alone.
void PresentBackendOnly(bool present);

// [rc4l] The last presented frame drawn again and read back, as top-down RGB the caller deletes; NULL when there is no device.
unsigned char *ReadBackendFrame(int &width, int &height);

}} // namespace zx::hwrender

#endif // ZX_BACKENDONLY_H
