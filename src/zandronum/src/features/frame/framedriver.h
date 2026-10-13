// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The engine's renderer: the level half once, each frame set up once, then GL's view and the backend's, each when it is drawn, so neither renderer drives the other.

#ifndef ZX_FRAMEDRIVER_H
#define ZX_FRAMEDRIVER_H

class FRenderer;

namespace zx { namespace frame {

// [rc4l] The driver over GL's renderer and the backend's, either of which may be NULL; it owns both.
FRenderer *CreateFrameDriver(FRenderer *gl, FRenderer *backend);

}} // namespace zx::frame

#endif // ZX_FRAMEDRIVER_H
