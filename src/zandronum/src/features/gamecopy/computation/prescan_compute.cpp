// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/computation/prescan_compute.h"

#include <algorithm>

namespace zx { namespace gamecopy {

bool ComputePrescanFresh(const PrescanStamp &found, const PrescanStamp &now, bool client)
{
	return found.found && !client && found.fill == now.fill && found.level == now.level && found.effects == now.effects &&
		found.sectors == now.sectors && found.sides == now.sides && found.lines == now.lines;
}

bool ComputeActorScanFresh(const PrescanStamp &found, const PrescanStamp &now, bool client)
{
	return ComputePrescanFresh(found, now, client) && found.lists == now.lists;
}

void ComputeTakeCandidates(const std::vector<int> &changed, const std::vector<int> &blended, const std::vector<int> &seen, int count,
	std::vector<int> &out)
{
	out.clear();
	for (const std::vector<int> *list : { &changed, &blended, &seen })
		for (int i : *list) if (i >= 0 && i < count) out.push_back(i);
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
}

}} // namespace zx::gamecopy
