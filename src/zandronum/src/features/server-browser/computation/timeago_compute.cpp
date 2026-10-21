// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "timeago_compute.h"

#include <cstdio>

namespace
{

std::string Count(int value, const char *singular, const char *plural)
{
	char buf[64];
	std::snprintf(buf, sizeof buf, "%d %s ago", value, (value == 1) ? singular : plural);
	return buf;
}

} // namespace

namespace zx
{

std::string TimeAgo(int seconds)
{
	// A clock that moved backwards is not evidence that something happened in the future.
	if (seconds < 0)
		return "at an unknown time";

	if (seconds < 1)
		return "just now";

	if (seconds < 60)
		return Count(seconds, "sec", "secs");

	if (seconds < 3600)
		return Count(seconds / 60, "min", "mins");

	if (seconds < 86400)
		return Count(seconds / 3600, "hour", "hours");

	return Count(seconds / 86400, "day", "days");
}

std::string LastRefreshedLine(bool everRefreshed, int seconds)
{
	if (!everRefreshed)
		return "Last refreshed: never";

	return "Last refreshed: " + TimeAgo(seconds);
}

} // namespace zx
