// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The standalone scene, driven without GL: the frame's upkeep, the visibility walks, the re-bake of what moved, and the actors in view.

#ifndef ZX_STANDALONEFRAME_H
#define ZX_STANDALONEFRAME_H

namespace zx { namespace frame {

// [rc4l] The scene for the view set up now, once the backend's scene is up: the frame's upkeep, the walks with what they reach, the re-bake of what moved among it, and the actors in view.
void StandaloneScene();

}} // namespace zx::frame

#endif // ZX_STANDALONEFRAME_H
