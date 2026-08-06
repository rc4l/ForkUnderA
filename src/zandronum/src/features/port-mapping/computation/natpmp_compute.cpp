// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/port-mapping/computation/natpmp_compute.h"

namespace zx
{

const int kNatPmpPort = 5351;
const size_t kNatPmpRequestSize = 12;
const size_t kNatPmpResponseSize = 16;

namespace
{
const unsigned char kVersion = 0;
const unsigned char kOpAddress = 0;
const unsigned char kOpMapUdp = 1;
const unsigned char kOpMapTcp = 2;

// Every multi-byte field on this wire is big-endian, whatever the machine is.
void PushBE16(std::vector<unsigned char> &out, int value)
{
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
	out.push_back(static_cast<unsigned char>(value & 0xff));
}

void PushBE32(std::vector<unsigned char> &out, long value)
{
	out.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
	out.push_back(static_cast<unsigned char>(value & 0xff));
}

int ReadBE16(const std::vector<unsigned char> &bytes, size_t at)
{
	return (static_cast<int>(bytes[at]) << 8) | static_cast<int>(bytes[at + 1]);
}

long ReadBE32(const std::vector<unsigned char> &bytes, size_t at)
{
	return (static_cast<long>(bytes[at]) << 24) | (static_cast<long>(bytes[at + 1]) << 16)
		| (static_cast<long>(bytes[at + 2]) << 8) | static_cast<long>(bytes[at + 3]);
}
} // namespace

std::vector<unsigned char> BuildNatPmpMapRequest(int internalPort, int externalPort, bool tcp,
	int lifetimeSeconds)
{
	std::vector<unsigned char> out;

	out.push_back(kVersion);
	out.push_back(tcp ? kOpMapTcp : kOpMapUdp);

	// Two reserved bytes, which must be zero.
	PushBE16(out, 0);

	PushBE16(out, internalPort);
	PushBE16(out, externalPort);

	// A lifetime of 0 with an external port of 0 is how the protocol spells "delete"; a caller
	// wanting that passes both, and this does not second-guess it.
	PushBE32(out, (lifetimeSeconds < 0) ? 0 : lifetimeSeconds);

	return out;
}

std::vector<unsigned char> BuildNatPmpAddressRequest()
{
	std::vector<unsigned char> out;
	out.push_back(kVersion);
	out.push_back(kOpAddress);
	return out;
}

NatPmpReply ReadNatPmpMapReply(const std::vector<unsigned char> &bytes, bool tcp)
{
	NatPmpReply out;

	// [rc4l] Length first, and exactly. A short datagram read as a reply is how a mapping gets
	// "confirmed" against a router that said nothing of the sort -- every field below is a fixed
	// offset into a buffer that has to be big enough to hold it.
	if (bytes.size() < kNatPmpResponseSize)
		return out;

	if (bytes[0] != kVersion)
		return out;

	// Responses are the request opcode plus 128. Checking it is what stops a UDP answer being
	// accepted for the TCP question -- the two mappings we make differ only by protocol, and a
	// router under load does reorder datagrams.
	const unsigned char expected = static_cast<unsigned char>((tcp ? kOpMapTcp : kOpMapUdp) + 128);
	if (bytes[1] != expected)
		return out;

	out.resultCode = ReadBE16(bytes, 2);
	out.internalPort = ReadBE16(bytes, 8);
	out.externalPort = ReadBE16(bytes, 10);
	out.lifetimeSeconds = static_cast<int>(ReadBE32(bytes, 12));
	out.valid = true;
	return out;
}

const char *NatPmpResultText(int resultCode)
{
	switch (resultCode)
	{
	case 0: return "Success";
	case 1: return "The router speaks a newer version of this protocol than we do";
	case 2: return "The router refused -- automatic port opening is switched off on it";
	case 3: return "The router has no internet connection";
	case 4: return "The router is out of resources for new mappings";
	case 5: return "The router does not support this kind of mapping";
	default: break;
	}

	return "The router refused, without saying why";
}

} // namespace zx
