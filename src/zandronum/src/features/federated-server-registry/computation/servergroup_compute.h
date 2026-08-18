// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether listings claiming one identity really are one dual-stack server, refusing
// anything else because a wrongly merged row is invisible while a duplicate is merely untidy.

#ifndef ZX_SERVERGROUP_COMPUTE_H
#define ZX_SERVERGROUP_COMPUTE_H

namespace zx
{

// [rc4l] Why same-identity listings were or were not treated as one server.
enum class GroupVerdict
{
	Group,			// exactly one IPv4 and one IPv6 on one port, the only shape one socket can produce

	Alone,			// one address, which is every ordinary server

	Collision,		// more than one socket can produce, so two servers share an identity

	PortMismatch,	// right families, wrong ports, which one socket cannot do either
};

// [rc4l] `v4` and `v6` count the listings of each family; `samePort` is whether they share a port.
GroupVerdict DecideServerGroup(int v4, int v6, bool samePort);

// [rc4l] Whether the caller should actually send this group to a launcher.
bool ShouldSendGroup(GroupVerdict verdict);

// [rc4l] Whether the operator should be told, silent on the ordinary cases so the one message that
// matters is not drowned.
bool GroupNeedsReport(GroupVerdict verdict);

} // namespace zx

#endif // ZX_SERVERGROUP_COMPUTE_H
