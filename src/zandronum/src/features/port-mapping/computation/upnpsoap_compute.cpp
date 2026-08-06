// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/port-mapping/computation/upnpsoap_compute.h"

#include <cstdio>
#include <cstdlib>

namespace zx
{

const char *const kServiceWanIp = "urn:schemas-upnp-org:service:WANIPConnection:1";
const char *const kServiceWanPpp = "urn:schemas-upnp-org:service:WANPPPConnection:1";

namespace
{
// Long enough for any plausible description; anything past it is a device misbehaving.
const size_t kMaxDescription = 80;

std::string IntToString(int value)
{
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%d", value);
	return std::string(buffer);
}
} // namespace

std::string XmlEscape(const std::string &text)
{
	std::string out;

	for (size_t i = 0; i < text.size(); ++i)
	{
		switch (text[i])
		{
		case '&':  out += "&amp;";  break;
		case '<':  out += "&lt;";   break;
		case '>':  out += "&gt;";   break;
		case '"':  out += "&quot;"; break;
		case '\'': out += "&apos;"; break;
		default:
			// Control characters are dropped rather than escaped: nothing legible needs them, and a
			// router's own web UI is where this string ends up being displayed.
			if (static_cast<unsigned char>(text[i]) >= 0x20)
				out += text[i];
			break;
		}
	}

	return out;
}

std::string XmlTagValue(const std::string &xml, const std::string &tag)
{
	const std::string open = "<" + tag + ">";
	const std::string close = "</" + tag + ">";

	const size_t start = xml.find(open);
	if (start == std::string::npos)
		return "";

	const size_t from = start + open.size();
	const size_t end = xml.find(close, from);
	if (end == std::string::npos)
		return "";

	return xml.substr(from, end - from);
}

std::string ControlUrlForService(const std::string &deviceXml, const std::string &serviceType)
{
	// [rc4l] Walk the <service> blocks rather than searching the document. A gateway lists several
	// services -- layer-3 forwarding, WAN common config, the connection service we want -- and each
	// has its own <controlURL>. Taking the first one in the file addresses whichever happened to be
	// listed first, which is a bug that works on the developer's router and nowhere else.
	size_t at = 0;

	for (;;)
	{
		const size_t start = deviceXml.find("<service>", at);
		if (start == std::string::npos)
			break;

		size_t end = deviceXml.find("</service>", start);
		if (end == std::string::npos)
			end = deviceXml.size();

		const std::string block = deviceXml.substr(start, end - start);

		if (XmlTagValue(block, "serviceType") == serviceType)
			return XmlTagValue(block, "controlURL");

		at = end + 1;
	}

	return "";
}

std::string BuildAddPortMappingBody(const PortMapRequest &request)
{
	std::string description = request.description;
	if (description.size() > kMaxDescription)
		description = description.substr(0, kMaxDescription);

	std::string out = "<?xml version=\"1.0\"?>\r\n";
	out += "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n";
	out += "<s:Body>\r\n";
	out += "<u:AddPortMapping xmlns:u=\"" + request.serviceType + "\">\r\n";

	// Empty means "any remote host", which is what a public game server wants.
	out += "<NewRemoteHost></NewRemoteHost>\r\n";
	out += "<NewExternalPort>" + IntToString(request.externalPort) + "</NewExternalPort>\r\n";
	out += "<NewProtocol>" + std::string(request.tcp ? "TCP" : "UDP") + "</NewProtocol>\r\n";
	out += "<NewInternalPort>" + IntToString(request.internalPort) + "</NewInternalPort>\r\n";
	out += "<NewInternalClient>" + XmlEscape(request.internalHost) + "</NewInternalClient>\r\n";
	out += "<NewEnabled>1</NewEnabled>\r\n";
	out += "<NewPortMappingDescription>" + XmlEscape(description) + "</NewPortMappingDescription>\r\n";
	out += "<NewLeaseDuration>" + IntToString(request.leaseSeconds) + "</NewLeaseDuration>\r\n";
	out += "</u:AddPortMapping>\r\n";
	out += "</s:Body>\r\n";
	out += "</s:Envelope>\r\n";
	return out;
}

std::string BuildDeletePortMappingBody(const std::string &serviceType, int externalPort, bool tcp)
{
	std::string out = "<?xml version=\"1.0\"?>\r\n";
	out += "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n";
	out += "<s:Body>\r\n";
	out += "<u:DeletePortMapping xmlns:u=\"" + serviceType + "\">\r\n";
	out += "<NewRemoteHost></NewRemoteHost>\r\n";
	out += "<NewExternalPort>" + IntToString(externalPort) + "</NewExternalPort>\r\n";
	out += "<NewProtocol>" + std::string(tcp ? "TCP" : "UDP") + "</NewProtocol>\r\n";
	out += "</u:DeletePortMapping>\r\n";
	out += "</s:Body>\r\n";
	out += "</s:Envelope>\r\n";
	return out;
}

std::string BuildSoapRequest(const std::string &host, int port, const std::string &path,
	const std::string &serviceType, const std::string &action, const std::string &body)
{
	std::string hostHeader = host;
	if (port != 80)
		hostHeader += ":" + IntToString(port);

	std::string out = "POST " + path + " HTTP/1.1\r\n";
	out += "HOST: " + hostHeader + "\r\n";
	out += "CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n";

	// [rc4l] Not optional. A gateway that cannot see which action is meant answers 500 to
	// everything, and the resulting "your router refused" is a lie about a request we malformed.
	out += "SOAPACTION: \"" + serviceType + "#" + action + "\"\r\n";

	out += "CONNECTION: close\r\n";
	out += "CONTENT-LENGTH: " + IntToString(static_cast<int>(body.size())) + "\r\n";
	out += "\r\n";
	out += body;
	return out;
}

int HttpStatusOf(const std::string &response)
{
	if (response.compare(0, 5, "HTTP/") != 0)
		return 0;

	const size_t space = response.find(' ');
	if (space == std::string::npos)
		return 0;

	return static_cast<int>(std::atol(response.c_str() + space + 1));
}

int UpnpErrorCode(const std::string &response)
{
	const std::string text = XmlTagValue(response, "errorCode");
	return text.empty() ? 0 : static_cast<int>(std::atol(text.c_str()));
}

MapResult ReadMapResponse(const std::string &response)
{
	const int status = HttpStatusOf(response);

	if (status == 200)
		return MapResult::Ok;

	// [rc4l] The status alone is not the answer. A gateway reports every refusal as 500 with a UPnP
	// error code in the body, and the code is the difference between "somebody already has that
	// port" -- which a player can fix by choosing another -- and "no", which they cannot.
	const int code = UpnpErrorCode(response);

	switch (code)
	{
	case 718:	// ConflictInMappingEntry
	case 724:	// SamePortValuesRequired, in practice another way of saying the port is spoken for
		return MapResult::Conflict;

	case 606:	// Action not authorized
	case 714:	// NoSuchEntryInArray -- on a delete, meaning it was not ours to remove
	case 725:	// OnlyPermanentLeasesSupported
		return MapResult::Refused;

	default:
		break;
	}

	return MapResult::Failed;
}

} // namespace zx
