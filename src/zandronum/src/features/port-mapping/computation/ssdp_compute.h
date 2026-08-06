// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Finding the router, and deciding whether to believe what answers.
//
// UPnP discovery is a shout into the dark: an M-SEARCH goes out to a multicast address and whatever
// is on the network answers with a URL. That URL is then fetched. So the whole of this unit exists
// around one uncomfortable fact --
//
//   ANYTHING ON THE LAN CAN ANSWER, AND WE ARE ABOUT TO FETCH WHAT IT SAYS.
//
// A hostile or merely broken device can reply with any URL it likes, and a client that fetches it
// unconditionally is a request-forger sitting inside somebody's network: point it at
// http://192.168.1.1/admin?reset=1 and the game does it. Wanting a port open is not a reason to
// become that.
//
// So a location is only accepted when it is http, on a plausible port, at an address in a PRIVATE
// range -- the router is by definition on the same network, and a discovery reply naming a public
// address is either broken or lying. None of these checks is clever. They do not need to be: the set
// of URLs worth fetching here is tiny, and everything outside it is refused rather than reasoned
// about.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_SSDP_COMPUTE_H
#define ZX_SSDP_COMPUTE_H

#include <string>

namespace zx
{

// The multicast group and port every IGD listens on. Fixed by the specification.
extern const char *const kSsdpAddress;
extern const int kSsdpPort;

// The M-SEARCH datagram, asking for `searchTarget`. `mx` is how many seconds routers may stagger
// their replies over -- small, because a player is waiting and one straggler is not worth the wait.
std::string BuildSsdpSearch(const std::string &searchTarget, int mx);

// The value of `name` in an HTTP-style header block, or "". Case-insensitive on the name, because
// SSDP replies are written by router firmware and every vendor picks its own capitalisation.
std::string HeaderValue(const std::string &response, const std::string &name);

// One URL, split. `port` is 80 when the URL does not say.
struct HttpUrl
{
	std::string host;
	std::string path;
	int port;
	bool valid;

	HttpUrl() : port(0), valid(false) {}
};

// Parse an absolute http:// URL. Anything else -- https, a scheme we do not know, a missing host --
// comes back invalid rather than half-parsed.
HttpUrl ParseHttpUrl(const std::string &url);

// Whether a dotted-quad is in a private range: 10/8, 172.16/12, 192.168/16, or 169.254/16 for
// link-local. The router we are asking to forward a port is on our own network by definition.
bool IsPrivateIPv4(const std::string &host);

// [rc4l] Whether a discovered location may be fetched. See the header comment: this is the boundary
// between "a device on the LAN said something" and "the game acts on it".
bool IsAcceptableLocation(const std::string &url);

// The LOCATION out of an SSDP reply, or "" if there is none or it fails the check above.
std::string LocationFromSsdpReply(const std::string &response);

// Resolve `reference` against the URL it was found in. Control URLs in a device description are
// usually root-relative ("/ctl/IPConn"), sometimes absolute, and occasionally relative to the
// description's own directory -- all three appear in shipped firmware.
std::string ResolveUrl(const std::string &baseUrl, const std::string &reference);

} // namespace zx

#endif // ZX_SSDP_COMPUTE_H
