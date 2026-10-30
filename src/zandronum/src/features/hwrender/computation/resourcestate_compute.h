// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Where a written resource goes back to so a draw can read it without the commit checking every resource the binding holds.

#ifndef ZX_RESOURCESTATE_COMPUTE_H
#define ZX_RESOURCESTATE_COMPUTE_H

#include <vector>

namespace zx { namespace hwrender {

// [Doruk-Aksoy] A buffer's bind flags and resource states as Diligent numbers them (BIND_FLAGS, RESOURCE_STATE), which the backend holds equal at compile time.
enum : unsigned
{
	kBindVertexBuffer = 1u << 0, kBindIndexBuffer = 1u << 1, kBindUniformBuffer = 1u << 2, kBindShaderResource = 1u << 3,
	kBindUnorderedAccess = 1u << 7, kBindIndirectDrawArgs = 1u << 8, kBindRayTracing = 1u << 10,
};
enum : unsigned
{
	kStateVertexBuffer = 1u << 1, kStateConstantBuffer = 1u << 2, kStateIndexBuffer = 1u << 3, kStateShaderResource = 1u << 8,
	kStateIndirectArgument = 1u << 10,
};

// [Doruk-Aksoy] The state a buffer goes back to after a write: every read it is bound for combined, leaving out its writes and ray-tracing builds, whose users check their own; 0 for a buffer nothing reads.
unsigned ComputeBufferReadState(unsigned bindFlags);

// [Doruk-Aksoy] Adds `item` to `list` unless it is there already, and says whether it was added.
bool ComputeNoteOnce(std::vector<const void *> &list, const void *item);

}} // namespace zx::hwrender

#endif // ZX_RESOURCESTATE_COMPUTE_H
