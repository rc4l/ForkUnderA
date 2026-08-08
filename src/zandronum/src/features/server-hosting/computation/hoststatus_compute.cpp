// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-hosting/computation/hoststatus_compute.h"

#include <cstdio>

namespace zx
{

namespace
{

std::string IntToString(int value)
{
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%d", value);
	return std::string(buffer);
}

} // namespace

HostTone HostToneFor(HostStatus status)
{
	switch (status)
	{
	case HostStatus::Open:		return HostTone::Good;
	case HostStatus::Checking:	return HostTone::Waiting;
	case HostStatus::LanOnly:	return HostTone::Info;
	case HostStatus::NoReply:	return HostTone::Bad;
	}

	return HostTone::Waiting;
}

const char *HostStatusCode(HostStatus status)
{
	switch (status)
	{
	case HostStatus::Open:		return "HOST_OPEN";
	case HostStatus::Checking:	return "HOST_CHECKING";
	case HostStatus::LanOnly:	return "HOST_LAN_ONLY";
	case HostStatus::NoReply:	return "HOST_NO_REPLY";
	}

	return "HOST_CHECKING";
}

const char *HostStatusText(HostStatus status)
{
	switch (status)
	{
	case HostStatus::Open:
		return "The internet can reach this server. Anyone can join it.";

	case HostStatus::Checking:
		return "Listed publicly. Waiting to hear whether the internet can reach it.";

	case HostStatus::LanOnly:
		return "Visible on this network only. Players elsewhere cannot see it.";

	case HostStatus::NoReply:
		return "Nothing has reached this server from outside.";
	}

	return "";
}

std::string HostStatusTooltip(HostStatus status, int port, const std::string &router)
{
	std::string out = HostStatusCode(status);
	out += "\n";
	out += HostStatusText(status);

	// [rc4l] Only where there is something to be done. Telling someone how to forward a port while we
	// are still waiting to find out whether they need to is how a check becomes an instruction.
	if (status == HostStatus::NoReply)
	{
		out += "\nIt still works for players on this network.";

		if (port > 0)
		{
			out += "\nTo open it to everyone, forward TCP and UDP port ";
			out += IntToString(port);
			out += " on your router.";
		}
	}

	// Whatever the router said, last, because it is the detail that explains the rest rather than
	// the answer itself. Left off entirely when nothing was attempted.
	if (!router.empty())
	{
		out += "\n";
		out += router;
	}

	return out;
}

} // namespace zx
