// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/hwrender/computation/resourcestate_compute.h"

#include <algorithm>

namespace zx { namespace hwrender {

unsigned ComputeBufferReadState(unsigned bindFlags)
{
	unsigned state = 0;
	if (bindFlags & kBindVertexBuffer) state |= kStateVertexBuffer;
	if (bindFlags & kBindIndexBuffer) state |= kStateIndexBuffer;
	if (bindFlags & kBindUniformBuffer) state |= kStateConstantBuffer;
	if (bindFlags & kBindShaderResource) state |= kStateShaderResource;
	if (bindFlags & kBindIndirectDrawArgs) state |= kStateIndirectArgument;
	return state;
}

bool ComputeNoteOnce(std::vector<const void *> &list, const void *item)
{
	if (std::find(list.begin(), list.end(), item) != list.end()) return false;
	list.push_back(item);
	return true;
}

}} // namespace zx::hwrender
