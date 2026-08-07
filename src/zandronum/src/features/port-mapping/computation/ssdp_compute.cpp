// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/port-mapping/computation/ssdp_compute.h"

#include <cstdio>
#include <cstdlib>

namespace zx
{

const char *const kSsdpAddress = "239.255.255.250";
const int kSsdpPort = 1900;

namespace
{
// Long enough for any real device description URL, short enough that a device answering with a
// megabyte of nonsense is refused rather than parsed.
const size_t kMaxUrlLength = 512;

char Lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool EqualsNoCase(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;

	for (size_t i = 0; i < a.size(); ++i)
	{
		if (Lower(a[i]) != Lower(b[i]))
			return false;
	}

	return true;
}

std::string Trim(const std::string &text)
{
	size_t first = 0;
	while ((first < text.size()) && ((text[first] == ' ') || (text[first] == '\t')))
		++first;

	size_t last = text.size();
	while ((last > first) && ((text[last - 1] == ' ') || (text[last - 1] == '\t')
		|| (text[last - 1] == '\r') || (text[last - 1] == '\n')))
	{
		--last;
	}

	return text.substr(first, last - first);
}

bool IsDigits(const std::string &text)
{
	if (text.empty())
		return false;

	for (size_t i = 0; i < text.size(); ++i)
	{
		if ((text[i] < '0') || (text[i] > '9'))
			return false;
	}

	return true;
}
} // namespace

std::string BuildSsdpSearch(const std::string &searchTarget, int mx)
{
	if (mx < 1)
		mx = 1;
	if (mx > 5)
		mx = 5;

	char header[64];
	std::snprintf(header, sizeof(header), "HOST: %s:%d\r\n", kSsdpAddress, kSsdpPort);

	std::string out = "M-SEARCH * HTTP/1.1\r\n";
	out += header;
	out += "MAN: \"ssdp:discover\"\r\n";

	char mxLine[32];
	std::snprintf(mxLine, sizeof(mxLine), "MX: %d\r\n", mx);
	out += mxLine;

	out += "ST: " + searchTarget + "\r\n";
	out += "\r\n";
	return out;
}

std::string HeaderValue(const std::string &response, const std::string &name)
{
	size_t at = 0;

	while (at < response.size())
	{
		size_t end = response.find('\n', at);
		if (end == std::string::npos)
			end = response.size();

		const std::string line = response.substr(at, end - at);
		const size_t colon = line.find(':');

		if (colon != std::string::npos)
		{
			if (EqualsNoCase(Trim(line.substr(0, colon)), name))
				return Trim(line.substr(colon + 1));
		}

		at = end + 1;
	}

	return "";
}

HttpUrl ParseHttpUrl(const std::string &url)
{
	HttpUrl out;

	// http only. https would need TLS to a device with a self-signed certificate, which is a
	// negotiation nobody wins; and any other scheme is not something to be fetching at all.
	const std::string prefix = "http://";
	if ((url.size() <= prefix.size()) || (url.compare(0, prefix.size(), prefix) != 0))
		return out;

	const size_t hostStart = prefix.size();
	size_t hostEnd = url.find_first_of("/?#", hostStart);
	if (hostEnd == std::string::npos)
		hostEnd = url.size();

	std::string authority = url.substr(hostStart, hostEnd - hostStart);
	if (authority.empty())
		return out;

	out.port = 80;

	const size_t colon = authority.find(':');
	if (colon != std::string::npos)
	{
		const std::string portText = authority.substr(colon + 1);
		if (!IsDigits(portText))
			return out;

		const long port = std::atol(portText.c_str());
		if ((port <= 0) || (port > 65535))
			return out;

		out.port = static_cast<int>(port);
		authority = authority.substr(0, colon);
	}

	if (authority.empty())
		return out;

	out.host = authority;
	out.path = (hostEnd < url.size()) ? url.substr(hostEnd) : "/";
	if (out.path.empty() || (out.path[0] != '/'))
		out.path = "/" + out.path;

	out.valid = true;
	return out;
}

bool IsPrivateIPv4(const std::string &host)
{
	int parts[4] = { -1, -1, -1, -1 };
	int index = 0;
	std::string current;

	for (size_t i = 0; i <= host.size(); ++i)
	{
		if ((i == host.size()) || (host[i] == '.'))
		{
			if (!IsDigits(current) || (current.size() > 3) || (index > 3))
				return false;

			const long value = std::atol(current.c_str());
			if (value > 255)
				return false;

			parts[index++] = static_cast<int>(value);
			current.clear();
			continue;
		}

		current += host[i];
	}

	if (index != 4)
		return false;

	if (parts[0] == 10)
		return true;
	if ((parts[0] == 172) && (parts[1] >= 16) && (parts[1] <= 31))
		return true;
	if ((parts[0] == 192) && (parts[1] == 168))
		return true;

	// Link-local. A router handing out no DHCP still answers here, and refusing it would fail the
	// one case where a player most needs help.
	if ((parts[0] == 169) && (parts[1] == 254))
		return true;

	return false;
}

bool IsAcceptableLocation(const std::string &url)
{
	if (url.empty() || (url.size() > kMaxUrlLength))
		return false;

	const HttpUrl parsed = ParseHttpUrl(url);
	if (!parsed.valid)
		return false;

	// [rc4l] The one that matters. A discovery reply naming a public address is either a broken
	// device or one hoping we will fetch something on its behalf -- and we are inside the network
	// where that would be worth doing.
	return IsPrivateIPv4(parsed.host);
}

std::string LocationFromSsdpReply(const std::string &response)
{
	const std::string location = HeaderValue(response, "LOCATION");
	return IsAcceptableLocation(location) ? location : "";
}

std::string ResolveUrl(const std::string &baseUrl, const std::string &reference)
{
	if (reference.empty())
		return "";

	// Already absolute. Still has to survive the same check as anything else, which is why callers
	// pass the result back through IsAcceptableLocation rather than trusting this to have judged it.
	if (reference.compare(0, 7, "http://") == 0)
		return reference;

	const HttpUrl base = ParseHttpUrl(baseUrl);
	if (!base.valid)
		return "";

	char root[600];
	if (base.port == 80)
		std::snprintf(root, sizeof(root), "http://%s", base.host.c_str());
	else
		std::snprintf(root, sizeof(root), "http://%s:%d", base.host.c_str(), base.port);

	if (reference[0] == '/')
		return std::string(root) + reference;

	// Relative to the description's own directory, which a handful of devices really do use.
	std::string dir = base.path;
	const size_t slash = dir.find_last_of('/');
	dir = (slash == std::string::npos) ? "/" : dir.substr(0, slash + 1);

	return std::string(root) + dir + reference;
}

} // namespace zx
