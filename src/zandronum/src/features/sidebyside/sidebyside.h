// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL and the backend on one game state: the one module that knows both renderers, and composes them for a launch that has GL.

#ifndef ZX_SIDEBYSIDE_H
#define ZX_SIDEBYSIDE_H

class DFrameBuffer;

namespace zx { namespace sidebyside {

// [rc4l] The frame buffer of a launch with GL: GL's own, which also records every 2D draw for the backend when the backend is built in.
DFrameBuffer *CreateGLFrameBuffer(void *hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen);

// [rc4l] GL reports its walk and draw to the checks against it while one is on (fua_vis_check, fua_sprite_check, fua_psprite_check), and to nothing otherwise.
void WatchGLForChecks();

}} // namespace zx::sidebyside

#endif // ZX_SIDEBYSIDE_H
