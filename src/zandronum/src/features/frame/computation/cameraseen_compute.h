// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which camera textures a frame's walk saw on a wall or a plane, so each is drawn again next frame as GL's binding it would have marked it.

#ifndef ZX_CAMERASEEN_COMPUTE_H
#define ZX_CAMERASEEN_COMPUTE_H

#include <vector>

namespace zx { namespace frame {

// [rc4l] seen[i] is 1 when cameras[i] is among `textures` (any order, repeats allowed), else 0; `textures` is sorted in place.
void ComputeCamerasSeen(const std::vector<int> &cameras, std::vector<int> &textures, std::vector<unsigned char> &seen);

}} // namespace zx::frame

#endif // ZX_CAMERASEEN_COMPUTE_H
