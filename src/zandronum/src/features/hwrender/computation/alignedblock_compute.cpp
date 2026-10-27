// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/hwrender/computation/alignedblock_compute.h"

namespace zx { namespace hwrender {

size_t ComputeAlignedBlockBytes(size_t size, size_t alignment, size_t header)
{
	return size + header + (alignment > 1 ? alignment - 1 : 0);
}

uintptr_t ComputeAlignedBlockStart(uintptr_t base, size_t alignment, size_t header)
{
	const uintptr_t at = base + header;
	if (alignment <= 1) return at;
	return (at + alignment - 1) & ~(uintptr_t)(alignment - 1);
}

}} // namespace zx::hwrender
