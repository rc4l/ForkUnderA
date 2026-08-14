// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "headerreach_compute.h"

namespace zx
{

HeaderReach ComputeHeaderReach(const ReachIn &in)
{
	// Proof first. One answer settles the question even while others are outstanding.
	if (in.anyRegistryAnswered)
		return HeaderReach::Internet;

	// Still asking. Must be answered before the offline branches, or the first few seconds of every
	// session accuse the player's network of being down.
	if (in.anyRegistryPending)
		return HeaderReach::Checking;

	if (in.haveLocalNetwork)
		return HeaderReach::LanOnly;

	return HeaderReach::Offline;
}

ReachTint HeaderReachTint(HeaderReach reach)
{
	switch (reach)
	{
	case HeaderReach::Internet:
		return ReachTint::Green;

	case HeaderReach::LanOnly:
		return ReachTint::Orange;

	case HeaderReach::Offline:
		return ReachTint::Grey;

	default:
		return ReachTint::Neutral;
	}
}

const char *HeaderReachTooltip(HeaderReach reach)
{
	switch (reach)
	{
	case HeaderReach::Internet:
		return "Browse and host games online";

	case HeaderReach::LanOnly:
		return "Local network only - no server list could be reached";

	case HeaderReach::Offline:
		return "No network connection was found";

	default:
		return "Checking your connection...";
	}
}

bool PlayOnlineSelectable(HeaderReach reach)
{
	return reach != HeaderReach::Offline;
}

} // namespace zx
