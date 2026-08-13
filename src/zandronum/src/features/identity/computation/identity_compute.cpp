// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/identity/computation/identity_compute.h"

namespace
{

const char kHexDigits[] = "0123456789abcdef";

// [rc4l] Domain tags. Different on each side so neither proof can be presented as the other's.
const char kClientDomain[] = "FUA-IDENTITY-v1-client";
const char kServerDomain[] = "FUA-IDENTITY-v1-server";

int HexValue(char c)
{
	if ((c >= '0') && (c <= '9')) return c - '0';
	if ((c >= 'a') && (c <= 'f')) return c - 'a' + 10;
	if ((c >= 'A') && (c <= 'F')) return c - 'A' + 10;
	return -1;
}

std::string WithoutTrailingSlash(const std::string &s)
{
	std::string out = s;
	while (!out.empty() && ((out[out.size() - 1] == '/') || (out[out.size() - 1] == '\\')))
		out.erase(out.size() - 1);
	return out;
}

// [rc4l] A do-while rather than a zero special case, which nothing here can reach and so nothing
// can cover.
std::string IntToString(int v)
{
	std::string digits;
	int n = v;

	do
	{
		digits.insert(digits.begin(), char('0' + (n % 10)));
		n /= 10;
	}
	while (n > 0);

	return digits;
}

} // namespace

namespace zx
{

const size_t kAccountNameLength = 32;

std::string ToHex(const std::vector<unsigned char> &bytes)
{
	std::string out;
	out.reserve(bytes.size() * 2);

	for (size_t i = 0; i < bytes.size(); ++i)
	{
		out.push_back(kHexDigits[(bytes[i] >> 4) & 0x0F]);
		out.push_back(kHexDigits[bytes[i] & 0x0F]);
	}

	return out;
}

bool FromHex(const std::string &hex, std::vector<unsigned char> &out)
{
	out.clear();

	// An odd length is half a byte, which is not a truncated value but a malformed one.
	if ((hex.size() % 2) != 0)
		return false;

	out.reserve(hex.size() / 2);

	for (size_t i = 0; i < hex.size(); i += 2)
	{
		const int hi = HexValue(hex[i]);
		const int lo = HexValue(hex[i + 1]);

		if ((hi < 0) || (lo < 0))
		{
			out.clear();
			return false;
		}

		out.push_back(static_cast<unsigned char>((hi << 4) | lo));
	}

	return true;
}

std::string AccountNameFromDigest(const std::vector<unsigned char> &digest)
{
	// Half a name is not a name. A caller whose hash failed gets nothing back rather than a short
	// identifier that would collide with every other failure.
	if ((digest.size() * 2) < kAccountNameLength)
		return std::string();

	return ToHex(digest).substr(0, kAccountNameLength);
}

std::string ClientAuthKeyPath(const std::string &configRoot, int instance)
{
	if (configRoot.empty())
		return std::string();

	const std::string dir = WithoutTrailingSlash(configRoot) + "/identity/";

	// [rc4l] The first instance gets the plain name, so the file a player backs up is the one the
	// documentation names. Numbering from the second keeps that stable however many clients they
	// happen to open.
	if (instance <= 0)
		return dir + "client-auth.key";

	return dir + "client-auth." + IntToString(instance + 1) + ".key";
}

std::string ServerAuthKeyPath(const std::string &configRoot)
{
	if (configRoot.empty())
		return std::string();

	return WithoutTrailingSlash(configRoot) + "/identity/server-auth.key";
}

std::string ClientProofMessage(const std::string &sessionIdHex, const std::string &serverKeyHex)
{
	// Separated by a character that cannot appear in hex, so no two different field pairs can ever
	// produce one identical message. Without a separator, session "ab" + key "cd" and session "a" +
	// key "bcd" sign the same bytes.
	return std::string(kClientDomain) + "|" + sessionIdHex + "|" + serverKeyHex;
}

std::string ServerProofMessage(const std::string &clientNonceHex, const std::string &serverKeyHex)
{
	return std::string(kServerDomain) + "|" + clientNonceHex + "|" + serverKeyHex;
}

} // namespace zx
