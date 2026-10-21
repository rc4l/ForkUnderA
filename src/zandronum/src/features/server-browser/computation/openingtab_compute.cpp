// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/openingtab_compute.h"

namespace zx
{

OpeningTab ComputeOpeningTab(int knownServers, bool listHasAnswered)
{
	if (knownServers > 0)
		return OpeningTab::Browse;

	// Still looking. An empty list we have not finished filling is not evidence of anything.
	if (!listHasAnswered)
		return OpeningTab::Browse;

	return OpeningTab::Host;
}

} // namespace zx
