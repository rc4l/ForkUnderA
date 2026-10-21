// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/cameraseen_compute.h"

#include <algorithm>

namespace zx { namespace frame {

void ComputeCamerasSeen(const std::vector<int> &cameras, std::vector<int> &textures, std::vector<unsigned char> &seen)
{
	std::sort(textures.begin(), textures.end());
	seen.assign(cameras.size(), 0);
	for (size_t i = 0; i < cameras.size(); i++)
		seen[i] = std::binary_search(textures.begin(), textures.end(), cameras[i]) ? 1 : 0;
}

}} // namespace zx::frame
