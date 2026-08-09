// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-hosting/computation/hostport_compute.h"

namespace zx
{

int PortToCheck(int runningPort, int configuredPort)
{
	// A running server is the only thing that settles which port matters. Zero means we hold none,
	// so the form field is still the question being asked.
	if (runningPort > 0)
		return runningPort;

	return configuredPort;
}

bool PortDriftNeedsWarning(int runningPort, int configuredPort)
{
	// Nothing running, nothing to have drifted from.
	if (runningPort <= 0)
		return false;

	// A configured port we cannot read is not something to accuse the server of missing. Callers
	// clamp the field to a sane default, so this is belt and braces rather than a live case.
	if (configuredPort <= 0)
		return false;

	return (runningPort != configuredPort);
}

} // namespace zx
