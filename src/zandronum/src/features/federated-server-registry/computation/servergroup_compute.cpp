// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/federated-server-registry/computation/servergroup_compute.h"

namespace zx
{

GroupVerdict DecideServerGroup(int v4, int v6, bool samePort)
{
	const int total = v4 + v6;

	// [rc4l] A caller that has gone wrong must not be answered with "merge these".
	if ((v4 < 0) || (v6 < 0))
		return GroupVerdict::Collision;

	if (total <= 1)
		return GroupVerdict::Alone;

	// [rc4l] One socket cannot produce either shape, so more than one server claims this identity.
	if ((v4 != 1) || (v6 != 1))
		return GroupVerdict::Collision;

	// [rc4l] Reported separately because it points at one key reused rather than a whole clone.
	if (!samePort)
		return GroupVerdict::PortMismatch;

	return GroupVerdict::Group;
}

bool ShouldSendGroup(GroupVerdict verdict)
{
	return (verdict == GroupVerdict::Group);
}

bool GroupNeedsReport(GroupVerdict verdict)
{
	// [rc4l] Only the impossible shapes are worth a line, since a wrongly merged server looks like
	// nothing at all from its operator's side.
	return (verdict == GroupVerdict::Collision) || (verdict == GroupVerdict::PortMismatch);
}

} // namespace zx
