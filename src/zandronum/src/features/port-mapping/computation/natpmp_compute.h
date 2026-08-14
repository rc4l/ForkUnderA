// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The other way to ask, for the routers that do not speak the first.
//
// NAT-PMP is what Apple's base stations use and what a fair number of other routers implement
// alongside UPnP. It is worth having because it is nearly free: twelve bytes out, sixteen back, no
// discovery step -- the gateway is the default route, and you simply ask it.
//
// It is also the more pleasant protocol to be strict about. Everything is fixed-width and
// big-endian, so a reply either has the right shape or does not, and there is no prose to
// misinterpret. That makes this unit almost entirely a matter of not trusting the wire: a sixteen
// byte buffer that arrived from the network is checked for length, version, and the opcode
// actually being a response to what we asked, before a single field is read out of it.
//
// A short read that is treated as a valid reply is how a mapping gets "confirmed" against a router
// that said nothing of the sort.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_NATPMP_COMPUTE_H
#define ZX_NATPMP_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// The port every NAT-PMP gateway listens on.
extern const int kNatPmpPort;

// Sizes on the wire. Named because a magic 12 and 16 in the socket code is how a length check drifts
// away from the thing it is checking.
extern const size_t kNatPmpRequestSize;
extern const size_t kNatPmpResponseSize;

// A mapping request: opcode 1 for UDP, 2 for TCP.
std::vector<unsigned char> BuildNatPmpMapRequest(int internalPort, int externalPort, bool tcp,
	int lifetimeSeconds);

// The external-address request, which is also the cheapest way to ask "are you there".
std::vector<unsigned char> BuildNatPmpAddressRequest();

// What came back.
struct NatPmpReply
{
	int resultCode;			// 0 is success
	int internalPort;
	int externalPort;
	int lifetimeSeconds;
	bool valid;				// false means the bytes were not a reply we can read

	NatPmpReply() : resultCode(-1), internalPort(0), externalPort(0), lifetimeSeconds(0),
		valid(false) {}
};

// [rc4l] Read a mapping reply, refusing anything that is not one.
//
// `tcp` is what we ASKED for: the response opcode must be the request opcode plus 128, so a UDP
// answer to a TCP question is rejected rather than quietly accepted. Routers under load do reorder
// and duplicate datagrams, and the two mappings we make differ only by protocol.
NatPmpReply ReadNatPmpMapReply(const std::vector<unsigned char> &bytes, bool tcp);

// A human sentence for a result code. Never empty.
const char *NatPmpResultText(int resultCode);

} // namespace zx

#endif // ZX_NATPMP_COMPUTE_H
