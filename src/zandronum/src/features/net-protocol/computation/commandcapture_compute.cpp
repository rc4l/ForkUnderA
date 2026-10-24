// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-protocol/computation/commandcapture_compute.h"

namespace zx { namespace netprotocol {

void ComputeCommandIds(const unsigned char *bytes, int length, int extendedId, int &id, int &extended)
{
	id = length > 0 ? bytes[0] : -1;
	extended = (id == extendedId && length > 1) ? bytes[1] : -1;
}

std::string ComputeCaptureLine(int tic, int client, bool reliable, const unsigned char *bytes, int length, int extendedId)
{
	static const char kHex[] = "0123456789abcdef";
	int id, extended;
	ComputeCommandIds(bytes, length, extendedId, id, extended);
	std::string line = std::to_string(tic) + " " + std::to_string(client) + (reliable ? " r " : " u ") + std::to_string(id) + " " +
		std::to_string(extended) + " " + std::to_string(length > 0 ? length : 0) + " ";
	for (int i = 0; i < length; i++)
	{
		line += kHex[bytes[i] >> 4];
		line += kHex[bytes[i] & 15];
	}
	return line;
}

}} // namespace zx::netprotocol
