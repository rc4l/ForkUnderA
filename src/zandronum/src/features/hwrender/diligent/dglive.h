// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The one entry point the engine calls into the Diligent backend per frame.
//
// Kept in its own header with no Diligent types in it on purpose. Every Diligent header drags in a
// reshaped windows.h that fights with the engine's own (DWORD gets redefined, _BitScanForward gets
// a second declaration with C++ linkage); d_main.cpp must never be exposed to that. A single void
// function is the whole surface the engine needs, and it is a no-op in a build without the backend.

#ifndef ZX_DGLIVE_H
#define ZX_DGLIVE_H

namespace zx { namespace hwrender {

// Draw one frame into the backend window from the CURRENT camera. No-op unless the scene has been
// uploaded (fua_diligent_scene) and fua_diligent_live is on.
void LiveFrame();

}} // namespace zx::hwrender

#endif // ZX_DGLIVE_H
