// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/registrystatus_compute.h"

namespace zx
{

RegistryTone RegistryToneFor(RegistryStatus status)
{
	switch (status)
	{
	case RegistryStatus::Pending:		return RegistryTone::Waiting;
	case RegistryStatus::Ok:			return RegistryTone::Good;
	case RegistryStatus::Throttled:		return RegistryTone::Warn;

	case RegistryStatus::LookupFailed:
	case RegistryStatus::NoAnswer:
	case RegistryStatus::Banned:
	case RegistryStatus::Version:
		break;
	}

	return RegistryTone::Bad;
}

bool RegistryStatusIsFinished(RegistryStatus status)
{
	return status != RegistryStatus::Pending;
}

const char *RegistryStatusCode(RegistryStatus status)
{
	switch (status)
	{
	case RegistryStatus::Pending:		return "REG_PENDING";
	case RegistryStatus::Ok:			return "REG_OK";
	case RegistryStatus::LookupFailed:	return "REG_LOOKUP_FAILED";
	case RegistryStatus::NoAnswer:		return "REG_NO_ANSWER";
	case RegistryStatus::Throttled:		return "REG_THROTTLED";
	case RegistryStatus::Banned:		return "REG_BANNED";
	case RegistryStatus::Version:		break;
	}

	return "REG_VERSION";
}

const char *RegistryStatusText(RegistryStatus status)
{
	switch (status)
	{
	case RegistryStatus::Pending:		return "Waiting for a response...";
	case RegistryStatus::Ok:			return "Got a valid response!";

	// [rc4l] Says which half failed, because the two have different fixes: correct the address, or go
	// and find out why that machine is not talking. The first says nothing about typing because the
	// name never became an address at all, so no packet was ever sent to be ignored.
	case RegistryStatus::LookupFailed:	return "DNS couldn't find an address for this name. Check it for typos.";
	case RegistryStatus::NoAnswer:		return "Request sent but received no response.";

	case RegistryStatus::Throttled:		return "Too many requests! Please try again in a bit.";
	case RegistryStatus::Banned:		return "You are banned from this server registry :(";
	case RegistryStatus::Version:		break;
	}

	return "This server registry requires a different version of the game.";
}

std::string RegistryTooltip(const std::string &host, int port, RegistryStatus status)
{
	std::string out = host.empty() ? std::string("(unnamed registry)") : host;

	if (port > 0)
	{
		// Hand-built rather than via a stream, to keep this unit free of <sstream> for one number.
		char buffer[16];
		int n = 0;
		int value = port;
		while ((value > 0) && (n < 15))
		{
			buffer[n++] = static_cast<char>('0' + (value % 10));
			value /= 10;
		}

		out += ':';
		while (n > 0)
			out += buffer[--n];
	}

	// Three lines: which registry, the code, then what it means. The code is worth reading on its own
	// when someone is quoting it back at us, so it does not share a line with the sentence.
	out += '\n';
	out += RegistryStatusCode(status);
	out += '\n';
	out += RegistryStatusText(status);

	return out;
}

RegistryStatus AgeRegistryStatus(RegistryStatus current, int msSinceRecorded, int throttleClearMs)
{
	if (current != RegistryStatus::Throttled)
		return current;

	// A non-positive window means "do not decay", which lets a caller pin the state deliberately.
	if (throttleClearMs <= 0)
		return current;

	// A negative age is a clock that moved, not a fresh reading. Left alone rather than treated as
	// old, because guessing in either direction on a broken clock is worse than waiting.
	if (msSinceRecorded < 0)
		return current;

	if (msSinceRecorded < throttleClearMs)
		return current;

	return RegistryStatus::Pending;
}

RegistryStatus ComputeKnownStatus(RegistryStatus current, RegistryStatus prior)
{
	return (current == RegistryStatus::Pending) ? prior : current;
}

RegistryStatus ComputeRecordedStatus(RegistryStatus current, RegistryStatus incoming)
{
	// A throttle never unseats proof that the registry answered. Note it does not unseat a FAILURE
	// either -- it replaces one, because a registry that can refuse us is a registry that is up, and
	// that is better news than the silence we had recorded.
	if ((incoming == RegistryStatus::Throttled) && (current == RegistryStatus::Ok))
		return RegistryStatus::Ok;

	return incoming;
}

} // namespace zx
