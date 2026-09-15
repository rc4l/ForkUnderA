// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/computation/continuestatus_compute.h"

namespace zx
{

namespace
{

// [rc4l] An IWAD we do not have and are not allowed to fetch is the one fault with no way forward,
// so it outranks everything else a row could be wrong about.
bool IwadIsUnobtainable(const ContinueStatusInputs &in)
{
	return (in.iwadPresent == false) && (in.iwadIsFree == false);
}

bool ServerIsUnreachable(const ContinueStatusInputs &in)
{
	if (in.kind != ContinueKind::Server)
		return false;

	// Asked and not answered. Never asked is not the same thing; see the header.
	return (in.probe == ServerProbe::Gone) || (in.versionCompatible == false);
}

bool SomethingCanBeFetched(const ContinueStatusInputs &in)
{
	if (in.missingMods > 0)
		return true;

	// Missing but downloadable -- IwadIsUnobtainable has already taken the other case.
	if (in.iwadPresent == false)
		return true;

	// Up, and holding a different set than we wrote down: joining means fetching the difference.
	return (in.kind == ContinueKind::Server) && (in.probe == ServerProbe::WadsDiffer);
}

} // namespace

ContinueStatus DecideContinueStatus(const ContinueStatusInputs &in)
{
	// Red first, because a row can be both -- a dead server whose mod is also missing is still a dead
	// server, and telling the player to wait for a download would be the wrong instruction.
	if (IwadIsUnobtainable(in) || ServerIsUnreachable(in))
		return ContinueStatus::Broken;

	if (SomethingCanBeFetched(in))
		return ContinueStatus::Fixable;

	return ContinueStatus::Ready;
}

const char *ContinueStatusReason(const ContinueStatusInputs &in)
{
	// Same order as the verdict, or the words would explain a colour the row is not.
	if (IwadIsUnobtainable(in))
		return "the game it needs is not on this machine, and cannot be downloaded";

	if (in.kind == ContinueKind::Server)
	{
		if (in.versionCompatible == false)
			return "that server is running a version this build cannot join";
		if (in.probe == ServerProbe::Gone)
			return "that server is not answering";
		if (in.probe == ServerProbe::WadsDiffer)
			return "that server is running different files now, so joining means downloading";
	}

	if (in.missingMods > 0)
		return "some of its files are missing and will be downloaded";

	if (in.iwadPresent == false)
		return "the game it needs will be downloaded";

	return "";
}

} // namespace zx
