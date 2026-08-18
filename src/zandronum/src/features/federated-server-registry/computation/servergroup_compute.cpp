// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/federated-server-registry/computation/servergroup_compute.h"

namespace zx
{

GroupVerdict DecideServerGroup(int v4, int v6, bool samePort)
{
	const int total = v4 + v6;

	// Negative counts cannot happen from a caller that counted a list, and treating them as a
	// collision is the safe reading of a caller that has gone wrong.
	if ((v4 < 0) || (v6 < 0))
		return GroupVerdict::Collision;

	if (total <= 1)
		return GroupVerdict::Alone;

	// More than two, or two of one family: one socket cannot produce either, so more than one server
	// is claiming this identity.
	if ((v4 != 1) || (v6 != 1))
		return GroupVerdict::Collision;

	// Right families, wrong ports. Also impossible from one socket, and reported separately because
	// it points at a different cause: the same key reused on a second server rather than duplicated
	// wholesale.
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
	// Alone is every ordinary server and Group is the success. Only the two impossible shapes are
	// worth a line, because from the operator's side a wrongly merged server looks like nothing at
	// all: their listing simply is not there.
	return (verdict == GroupVerdict::Collision) || (verdict == GroupVerdict::PortMismatch);
}

} // namespace zx
