// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which structure of an array a pointer to one of its members points into, so a pointer the game keeps into a live sector can be pointed at the same member of its render copy.

#ifndef ZX_MEMBERINDEX_COMPUTE_H
#define ZX_MEMBERINDEX_COMPUTE_H

#include <cstddef>
#include <cstdint>

namespace zx { namespace gamecopy {

// [rc4l] The element of `count` structures `stride` bytes apart from `base` whose member `offset` bytes in `member` points at, -1 when it points at no such member.
inline int ComputeMemberIndex(const void *base, size_t stride, int count, size_t offset, const void *member)
{
	const uintptr_t b = reinterpret_cast<uintptr_t>(base), m = reinterpret_cast<uintptr_t>(member);
	if (base == nullptr || member == nullptr || stride == 0 || count <= 0 || m < b + offset) return -1;
	const uintptr_t into = m - b - offset;
	if (into % stride != 0) return -1;
	const uintptr_t i = into / stride;
	return i < uintptr_t(count) ? int(i) : -1;
}

}} // namespace zx::gamecopy

#endif // ZX_MEMBERINDEX_COMPUTE_H
