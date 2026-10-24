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

std::vector<UploadRun> ComputeChangedRuns(const unsigned *was, const unsigned *now, unsigned entries, unsigned stride, unsigned bridge)
{
	std::vector<UploadRun> runs;
	for (unsigned i = 0; i < entries; i++)
	{
		if (std::memcmp(was + (size_t)i * stride, now + (size_t)i * stride, stride * sizeof(unsigned)) == 0) continue;
		if (!runs.empty() && i - (runs.back().first + runs.back().count) < bridge) runs.back().count = i + 1 - runs.back().first;
		else runs.push_back({ i, 1 });
	}
	return runs;
}

}} // namespace zx::hwrender
