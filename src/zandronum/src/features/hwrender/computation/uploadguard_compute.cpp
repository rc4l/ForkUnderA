// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "uploadguard_compute.h"

#include <cstring>

namespace zx { namespace hwrender {

bool ComputeUploadChanged(UploadShadow &s, long long object, const void *data, size_t bytes)
{
	if (s.object == object && s.bytes.size() == bytes && (bytes == 0 || std::memcmp(s.bytes.data(), data, bytes) == 0)) return false;
	s.object = object;
	const unsigned char *from = (const unsigned char *)data;
	s.bytes.assign(from, from + bytes);
	return true;
}

}} // namespace zx::hwrender
