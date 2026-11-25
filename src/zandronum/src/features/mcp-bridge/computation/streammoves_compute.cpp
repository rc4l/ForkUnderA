// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mcp-bridge/computation/streammoves_compute.h"

#include <cstdio>

namespace zx { namespace mcp {

bool ComputeFrameStream(uint32_t crc, const std::vector<uint32_t> &frameStreams)
{
	if (crc == 0) return false;
	for (uint32_t frame : frameStreams)
		if (frame == crc) return true;
	return false;
}

std::string ComputeStreamMoves(const std::vector<StreamPlace> &now, const std::vector<uint32_t> &frameStreams, StreamPlaces &last)
{
	std::string out;
	uint64_t nameless = 0;
	for (const StreamPlace &s : now)
	{
		const uint64_t key = s.crc != 0 ? (uint64_t)s.crc : ((1ull << 32) | nameless++);
		if (ComputeFrameStream(s.crc, frameStreams)) continue;
		std::pair<uint32_t, uint32_t> &place = last[key];
		if (place.first == s.idx && place.second == s.u0) continue;
		place = std::make_pair(s.idx, s.u0);
		char word[32];
		snprintf(word, sizeof word, " %u:%u", (unsigned)s.crc, (unsigned)s.idx);
		out += word;
	}
	return out;
}

}} // namespace zx::mcp
