// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Asking the router for a port, in the language it expects.
//
// The device description is XML and the requests are SOAP, and neither is parsed properly here. That
// is deliberate: a real XML parser is a large dependency and a large attack surface for a document
// we need exactly two strings out of, from a device on our own LAN, in a feature whose failure mode
// is "no port mapping" rather than "wrong answer". Scanning for the tags we want is enough, and
// what it cannot handle it declines rather than guesses.
//
// WHAT IS ESCAPED AND WHY. The only value we put into a request that did not come from us is the
// description string, which carries the server name a player typed. XML has five characters that end
// an element early, and a server called `<Bob>` would otherwise produce a document the router either
// rejects or -- worse -- reads as something else. Escaped here, once, rather than trusted to be
// boring.
//
// BOTH PROTOCOLS GET MAPPED. The game speaks UDP and the direct-download listener speaks TCP on the
// same port number, so a mapping for one leaves the other broken in a way that looks like a
// different bug entirely: the server works and downloads mysteriously fail.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_UPNPSOAP_COMPUTE_H
#define ZX_UPNPSOAP_COMPUTE_H

#include <string>

namespace zx
{

// The two service types an internet gateway offers, in the order worth trying. IP first: PPP is the
// older DSL shape and rarer now, but still shipped.
extern const char *const kServiceWanIp;
extern const char *const kServiceWanPpp;

// Escape the five characters that would end an element early.
std::string XmlEscape(const std::string &text);

// The text of the first `<tag>` in `xml`, or "". Not namespace-aware, because device descriptions
// in the wild are not consistent about namespaces either.
std::string XmlTagValue(const std::string &xml, const std::string &tag);

// [rc4l] The control URL for `serviceType`, out of a device description.
//
// Scoped to the <service> block that declares that type -- a gateway lists several services and
// taking the first <controlURL> in the document would as often as not address the wrong one.
std::string ControlUrlForService(const std::string &deviceXml, const std::string &serviceType);

// What a mapping request is asking for.
struct PortMapRequest
{
	std::string serviceType;
	std::string internalHost;	// us, on the LAN
	std::string description;	// shown in the router's UI; carries the server name
	int externalPort;
	int internalPort;
	bool tcp;					// false for UDP
	int leaseSeconds;			// 0 means "until deleted", which many routers force anyway

	PortMapRequest() : externalPort(0), internalPort(0), tcp(false), leaseSeconds(0) {}
};

// The SOAP body for AddPortMapping.
std::string BuildAddPortMappingBody(const PortMapRequest &request);

// The SOAP body for DeletePortMapping. Only the external port and protocol identify a mapping.
std::string BuildDeletePortMappingBody(const std::string &serviceType, int externalPort, bool tcp);

// A complete HTTP request for `body`, addressed to `path` on `host`. SOAPAction is not optional --
// a gateway that cannot see which action is meant answers 500 to everything.
std::string BuildSoapRequest(const std::string &host, int port, const std::string &path,
	const std::string &serviceType, const std::string &action, const std::string &body);

// The status line's code, or 0 if this does not look like an HTTP response at all.
int HttpStatusOf(const std::string &response);

// [rc4l] What a gateway's answer means for us.
enum class MapResult
{
	Ok,
	Conflict,		// somebody else already has this port -- 718, and worth saying so
	Refused,		// the router understood and said no: unauthorised, or the action is disabled
	Failed,			// anything else, including a reply we could not make sense of
};

// Read a SOAP reply. The HTTP status alone is not the answer: gateways return 500 with a UPnP error
// code in the body, and the code is what distinguishes "that port is taken" from "no".
MapResult ReadMapResponse(const std::string &response);

// The UPnP error code in a fault body, or 0. Exposed because the specific numbers are worth logging
// even where they do not change what we do.
int UpnpErrorCode(const std::string &response);

} // namespace zx

#endif // ZX_UPNPSOAP_COMPUTE_H
