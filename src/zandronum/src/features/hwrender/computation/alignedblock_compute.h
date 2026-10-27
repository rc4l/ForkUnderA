// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Where an aligned block starts inside a larger one taken from an allocator that only promises its own alignment, with room before it for a header that finds the larger block again.

#ifndef ZX_HWRENDER_ALIGNEDBLOCK_COMPUTE_H
#define ZX_HWRENDER_ALIGNEDBLOCK_COMPUTE_H

#include <cstddef>
#include <cstdint>

namespace zx { namespace hwrender {

// [Doruk-Aksoy] The bytes to take for `size` bytes at `alignment` (a power of two) after a `header`-byte header.
size_t ComputeAlignedBlockBytes(size_t size, size_t alignment, size_t header);

// [Doruk-Aksoy] The first address at or past base + header that is a multiple of `alignment`.
uintptr_t ComputeAlignedBlockStart(uintptr_t base, size_t alignment, size_t header);

}} // namespace zx::hwrender

#endif // ZX_HWRENDER_ALIGNEDBLOCK_COMPUTE_H
