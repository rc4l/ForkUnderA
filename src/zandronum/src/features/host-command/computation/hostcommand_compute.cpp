// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/host-command/computation/hostcommand_compute.h"

#include <cstdlib>

namespace zx
{

namespace
{

// A port or a player count that is not a plain positive number is a typo, and a typo that reaches a
// command line becomes an argument the server reads as something else.
bool ParsePositive(const std::string &text, int &out)
{
	if (text.empty())
		return false;

	for (size_t i = 0; i < text.size(); ++i)
	{
		if ((text[i] < '0') || (text[i] > '9'))
			return false;
	}

	out = atoi(text.c_str());
	return (out > 0);
}

} // namespace

bool ParseHostCommand(const std::vector<std::string> &args, HostConfig &out, std::string &error)
{
	// [rc4l] Built locally and published only on success, so a refused line cannot leave the caller
	// holding half a config. A caller that ignores the return value then gets nothing rather than
	// something that looks usable and is not what anybody typed.
	HostConfig built;

	out = HostConfig();
	error.clear();

	if (args.empty())
	{
		error = "a map to start on is the one thing this cannot guess";
		return false;
	}

	// The map is the first word and is checked as a lump name: stricter than a general argument,
	// because a map name has a shape and anything outside it is a mistake rather than a preference.
	if (IsLumpName(args[0]) == false)
	{
		error = "that is not a map name";
		return false;
	}

	built.map = args[0];
	built.hostName = "FUA Host";

	for (size_t i = 1; i < args.size(); ++i)
	{
		const std::string &key = args[i];

		// Every option takes exactly one value, so a trailing key with nothing after it is a line
		// the player did not finish typing rather than a default they meant.
		if (i + 1 >= args.size())
		{
			error = "'" + key + "' needs a value";
			return false;
		}

		const std::string &value = args[++i];

		if (key == "name")
		{
			if (IsSafeArgValue(value) == false)
			{
				error = "that server name cannot be put on a command line";
				return false;
			}

			built.hostName = value;
		}
		else if (key == "file")
		{
			if (IsBareFileName(value) == false)
			{
				error = "'" + value + "' is not a plain file name";
				return false;
			}

			built.pwads.push_back(value);
		}
		else if (key == "port")
		{
			if (ParsePositive(value, built.port) == false)
			{
				error = "port must be a positive number";
				return false;
			}
		}
		else if (key == "players")
		{
			if (ParsePositive(value, built.maxPlayers) == false)
			{
				error = "players must be a positive number";
				return false;
			}
		}
		else
		{
			error = "unknown option '" + key + "'";
			return false;
		}
	}

	out = built;
	return true;
}

} // namespace zx
