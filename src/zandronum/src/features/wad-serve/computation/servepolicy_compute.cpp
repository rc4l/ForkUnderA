// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-serve/computation/servepolicy_compute.h"

#include "features/wad-download/computation/iwadallow_compute.h"

namespace zx
{

namespace
{

// The longest Retry-After we will ask for. Past a few minutes a client stops treating it as a queue
// and starts treating it as a failure, which loses us the transfer entirely.
const int kMaxRetryAfterSeconds = 300;

char LowerAscii(char c)
{
	if ((c >= 'A') && (c <= 'Z'))
		return static_cast<char>(c - 'A' + 'a');
	return c;
}

bool NamesMatch(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (LowerAscii(a[i]) != LowerAscii(b[i]))
			return false;
	}
	return true;
}

} // namespace

const char *ServeVerdictReason(ServeVerdict verdict)
{
	switch (verdict)
	{
	case ServeVerdict::Allowed:
		return "allowed";
	case ServeVerdict::Disabled:
		return "file serving is turned off on this server";
	case ServeVerdict::NotLoaded:
		return "this server has no such file loaded";
	case ServeVerdict::ProtectedIwad:
		return "that IWAD is not one we can confirm is free to redistribute";
	case ServeVerdict::TooLarge:
		return "that file is larger than this server will serve";
	}
	return "refused";
}

int ServeVerdictStatus(ServeVerdict verdict)
{
	switch (verdict)
	{
	case ServeVerdict::Allowed:
		return 200;
	case ServeVerdict::Disabled:
	case ServeVerdict::NotLoaded:
		// Both 404. A server with serving switched off should look like one that does not have the
		// file, rather than advertising a feature it then declines to provide.
		return 404;
	case ServeVerdict::ProtectedIwad:
	case ServeVerdict::TooLarge:
		return 403;
	}
	return 403;
}

int FindServableFile(const std::vector<ServableFile> &loaded, const std::string &requested)
{
	for (size_t i = 0; i < loaded.size(); ++i)
	{
		if (NamesMatch(loaded[i].name, requested))
			return static_cast<int>(i);
	}
	return -1;
}

ServeVerdict ClassifyServeRequest(const std::vector<ServableFile> &loaded,
	const std::string &requested, bool enabled, long long maxFileBytes, int &outIndex)
{
	outIndex = -1;

	if (!enabled)
		return ServeVerdict::Disabled;

	const int index = FindServableFile(loaded, requested);
	if (index < 0)
		return ServeVerdict::NotLoaded;

	const ServableFile &file = loaded[static_cast<size_t>(index)];

	// Deny-by-default, exactly as on the client: every IWAD is assumed to be a game someone sells
	// unless it is one we shipped a name for.
	if (file.isIwad && !IsFreeIwadName(file.name))
		return ServeVerdict::ProtectedIwad;

	if ((maxFileBytes > 0) && (file.size > maxFileBytes))
		return ServeVerdict::TooLarge;

	outIndex = index;
	return ServeVerdict::Allowed;
}

bool ComputeAdmitTransfer(int activeTotal, int maxSlots, int activeFromAddress, int maxPerAddress)
{
	if (maxSlots <= 0)
		return false;
	if (activeTotal >= maxSlots)
		return false;

	// One peer opening twenty connections would otherwise hold every slot -- a denial of service
	// that costs the attacker nothing and the operator everything.
	if ((maxPerAddress > 0) && (activeFromAddress >= maxPerAddress))
		return false;

	return true;
}

int ComputeRetryAfterSeconds(int waitingAhead, int maxSlots, int typicalTransferSeconds)
{
	int slots = maxSlots;
	if (slots < 1)
		slots = 1;

	int perTransfer = typicalTransferSeconds;
	if (perTransfer < 1)
		perTransfer = 1;

	int ahead = waitingAhead;
	if (ahead < 0)
		ahead = 0;

	// Queues drain in waves of `slots`, so the client ahead of position `slots` waits two transfers,
	// and so on. Always at least one wave: a refused request should never be told to retry instantly.
	const long long waves = (ahead / slots) + 1;
	const long long seconds = waves * perTransfer;

	if (seconds > kMaxRetryAfterSeconds)
		return kMaxRetryAfterSeconds;
	return static_cast<int>(seconds);
}

} // namespace zx
