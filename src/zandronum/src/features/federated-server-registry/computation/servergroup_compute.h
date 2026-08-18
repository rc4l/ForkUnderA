// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether a set of listings claiming one identity really is ONE server.
//
// A server announces once per family from a single socket, so a dual-stack host produces exactly two
// entries: one IPv4, one IPv6, on the same port. That shape is the entire test, and everything else
// claiming a shared identity is refused.
//
// WHY REFUSE RATHER THAN TRUST. The identity is derived from a server's own secret, which makes it
// unguessable but not unforgeable-by-accident: copy a machine image and the key file goes with it, so
// a clone on the same port derives the same identity honestly and innocently. Somebody who observed
// an announce could replay one deliberately. Either way, merging the listings would make one of the
// servers disappear from the browser, and its operator would have no way to see why.
//
// So the failure direction matters more than the accuracy: a duplicate row is untidy, a wrongly
// merged row is invisible. This says no whenever it is not certain.
//
// Header-pure by the features/ rules: no engine types, no sockets.

#ifndef ZX_SERVERGROUP_COMPUTE_H
#define ZX_SERVERGROUP_COMPUTE_H

namespace zx
{

// Why a set of same-identity listings was or was not treated as one server.
enum class GroupVerdict
{
	// Exactly one IPv4 and one IPv6 on one port. The only shape a dual-stack server can produce.
	Group,

	// One address, which is every ordinary server. Not a fault and not worth a packet to say.
	Alone,

	// More listings than one socket can produce, so at least two servers share an identity.
	Collision,

	// Two families but different ports, which one socket cannot do either.
	PortMismatch,
};

// `v4` and `v6` count the listings of each family; `samePort` is whether they all share a port.
GroupVerdict DecideServerGroup(int v4, int v6, bool samePort);

// Whether the caller should actually send this group to a launcher.
bool ShouldSendGroup(GroupVerdict verdict);

// Whether the operator should be told. Silence on the ordinary cases, because a registry logging a
// line per server per query drowns the one message that matters.
bool GroupNeedsReport(GroupVerdict verdict);

} // namespace zx

#endif // ZX_SERVERGROUP_COMPUTE_H
