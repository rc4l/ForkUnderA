// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-hosting/computation/hostlifecycle_compute.h"

#include <cstdio>

namespace zx
{

// 30 seconds. A cold disk loading a large WAD set takes real time, and cutting a slow but healthy
// start short would produce a failure message that is simply untrue.
const int kReadyTimeoutMs = 30000;

// 5 seconds to exit politely. Past that the process is not going to, and holding the port matters
// more than the manners.
const int kStopTimeoutMs = 5000;

namespace
{
// Lowercased haystack search. The engine's messages are not written to a fixed case and we are
// matching prose, not a protocol.
char Lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool ContainsNoCase(const std::string &haystack, const std::string &needle)
{
	if (needle.empty() || haystack.size() < needle.size())
		return false;

	for (size_t i = 0; i + needle.size() <= haystack.size(); ++i)
	{
		size_t j = 0;
		while ((j < needle.size()) && (Lower(haystack[i + j]) == Lower(needle[j])))
			++j;

		if (j == needle.size())
			return true;
	}

	return false;
}

HostLifecycle Enter(HostState state, const std::string &reason)
{
	HostLifecycle out;
	out.state = state;
	out.reason = reason;
	out.elapsedMs = 0;
	return out;
}
} // namespace

bool IsHostFinished(HostState state)
{
	return (state == HostState::Stopped) || (state == HostState::Failed) || (state == HostState::Idle);
}

bool HostHoldsProcess(HostState state)
{
	// Stopping counts. The process is still there until it is not, and teardown that skipped it
	// would leave exactly the orphan this whole unit exists to prevent.
	return (state == HostState::Starting) || (state == HostState::Running)
		|| (state == HostState::Stopping);
}

bool HostAcceptsClients(HostState state)
{
	return state == HostState::Running;
}

HostLifecycle StepHostLifecycle(const HostLifecycle &host, HostEvent event, const std::string &detail)
{
	// [rc4l] Terminal is terminal. Events keep arriving after a host has ended -- a pipe drains, an
	// exit code lands after we already gave up -- and none of them may resurrect it.
	if ((host.state == HostState::Stopped) || (host.state == HostState::Failed))
		return host;

	switch (event)
	{
	case HostEvent::Spawned:
		if (host.state == HostState::Idle)
			return Enter(HostState::Starting, "");
		break;

	case HostEvent::ReadyObserved:
		if (host.state == HostState::Starting)
			return Enter(HostState::Running, "");
		break;

	case HostEvent::SpawnFailed:
		return Enter(HostState::Failed, detail.empty() ? "The server could not be started." : detail);

	case HostEvent::ChildExited:
		// Dying during Stopping is what we asked for; dying at any other time is not.
		if (host.state == HostState::Stopping)
			return Enter(HostState::Stopped, "");
		if (host.state == HostState::Idle)
			break;
		return Enter(HostState::Failed,
			detail.empty() ? "The server stopped unexpectedly." : detail);

	case HostEvent::StopRequested:
		// Both arms return, and there is no third case: the terminal states left at the top of this
		// function, which leaves only the three that hold a process and Idle. A `break` under here
		// would be a line no input can reach -- a guarded case that is really an untestable claim
		// about the enum.
		if (HostHoldsProcess(host.state))
			return Enter(HostState::Stopping, "");
		return Enter(HostState::Stopped, "");

	case HostEvent::Timeout:
		if (host.state == HostState::Starting)
		{
			return Enter(HostState::Failed,
				detail.empty() ? "The server did not finish starting." : detail);
		}
		// A stop that times out still counts as stopped -- the caller kills the process on this
		// same signal, so claiming otherwise would leave the UI stuck on a process that is gone.
		if (host.state == HostState::Stopping)
			return Enter(HostState::Stopped, "");
		break;
	}

	return host;
}

HostLifecycle TickHostLifecycle(const HostLifecycle &host, int deltaMs)
{
	HostLifecycle out = host;

	if (deltaMs > 0)
		out.elapsedMs += deltaMs;

	if ((out.state == HostState::Starting) && (out.elapsedMs >= kReadyTimeoutMs))
		return StepHostLifecycle(out, HostEvent::Timeout, "");

	if ((out.state == HostState::Stopping) && (out.elapsedMs >= kStopTimeoutMs))
		return StepHostLifecycle(out, HostEvent::Timeout, "");

	return out;
}

const char *HostStateSummary(HostState state)
{
	switch (state)
	{
	case HostState::Idle:		return "Not hosting";
	case HostState::Starting:	return "Starting the server";
	case HostState::Running:	return "Server running";
	case HostState::Stopping:	return "Shutting the server down";
	case HostState::Stopped:	return "Server stopped";
	case HostState::Failed:		return "Server failed";
	}

	return "Not hosting";
}

std::string ExplainHostFailure(const std::string &childOutput, int exitCode)
{
	// Ordered most specific first. A missing WAD also mentions the word "file", so asking the
	// broader questions first would answer the narrow ones wrongly.
	if (ContainsNoCase(childOutput, "bind") || ContainsNoCase(childOutput, "address already in use")
		|| ContainsNoCase(childOutput, "EADDRINUSE"))
	{
		return "That port is already in use. Something else is listening on it -- another server, "
			"most likely. Try a different port.";
	}

	if (ContainsNoCase(childOutput, "Could not find IWAD")
		|| ContainsNoCase(childOutput, "Cannot find a game IWAD")
		|| ContainsNoCase(childOutput, "game data"))
	{
		return "The server could not find the game data it was told to load.";
	}

	if (ContainsNoCase(childOutput, "Could not open") || ContainsNoCase(childOutput, "not find")
		|| ContainsNoCase(childOutput, "No such file"))
	{
		return "The server could not open one of the files it was told to load.";
	}

	if (ContainsNoCase(childOutput, "Unknown map") || ContainsNoCase(childOutput, "not a valid map"))
	{
		return "That map is not in the files the server loaded.";
	}

	// Nothing recognised. The child's own last words beat anything generic we could invent, so they
	// are passed through -- trimmed, because a wall of startup banner is not an explanation.
	std::string trimmed;
	size_t start = childOutput.find_last_not_of(" \t\r\n");
	if (start != std::string::npos)
	{
		const size_t lineStart = childOutput.find_last_of('\n', start);
		const size_t from = (lineStart == std::string::npos) ? 0 : lineStart + 1;
		trimmed = childOutput.substr(from, start - from + 1);
	}

	if (!trimmed.empty())
		return trimmed;

	if (exitCode != 0)
	{
		char buffer[80];
		std::snprintf(buffer, sizeof(buffer),
			"The server stopped without saying why (exit code %d).", exitCode);
		return std::string(buffer);
	}

	return "The server stopped without saying why.";
}

} // namespace zx
