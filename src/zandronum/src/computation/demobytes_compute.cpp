// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/demobytes_compute.h"

namespace zx {

size_t ComputeDemoBytes(size_t fixedBytes, std::initializer_list<size_t> stringLengths)
{
	size_t bytes = fixedBytes;
	for (size_t length : stringLengths)
		bytes += length + 1;
	return bytes;
}

} // namespace zx
