// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/uploadtally_compute.h"

#include <algorithm>

namespace zx { namespace hwrender {

void ComputeUploadTallyAdd(UploadTally &t, const char *name, double bytes)
{
	t.bytes[name != nullptr && *name ? name : "unnamed"] += bytes;
}

void ComputeUploadTallyFrame(UploadTally &t)
{
	t.frames++;
}

std::vector<std::pair<std::string, double>> ComputeUploadTallyTake(UploadTally &t)
{
	const double frames = t.frames > 0 ? t.frames : 1;
	std::vector<std::pair<std::string, double>> out;
	for (const auto &b : t.bytes) out.push_back({ b.first, b.second / frames });
	std::stable_sort(out.begin(), out.end(), [](const std::pair<std::string, double> &a, const std::pair<std::string, double> &b) { return a.second > b.second; });
	t = UploadTally();
	return out;
}

}} // namespace zx::hwrender
