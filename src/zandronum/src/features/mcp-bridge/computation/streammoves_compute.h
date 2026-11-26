// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [Doruk-Aksoy] The sim trace's random-stream lines (mcp_simtrace.cpp): which streams a traced tic moved, so tracediff names the one whose draws shifted between runs.

#ifndef ZX_STREAMMOVES_COMPUTE_H
#define ZX_STREAMMOVES_COMPUTE_H

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace zx { namespace mcp {

// [Doruk-Aksoy] One random stream's place: its name CRC (0 for a nameless one), index and first state word.
struct StreamPlace
{
	uint32_t crc;
	uint32_t idx;
	uint32_t u0;
};

// [Doruk-Aksoy] Each stream's place at the end of the last traced tic, keyed by name CRC or, for nameless streams, their order.
typedef std::map<uint64_t, std::pair<uint32_t, uint32_t>> StreamPlaces;

// [Doruk-Aksoy] Whether a named stream is one the frames draw from, which the trace leaves out of its random-stream fingerprint.
bool ComputeFrameStream(uint32_t crc, const std::vector<uint32_t> &frameStreams);

// [Doruk-Aksoy] The " crc:index" words of every stream besides the frame streams that moved since last, which it updates.
std::string ComputeStreamMoves(const std::vector<StreamPlace> &now, const std::vector<uint32_t> &frameStreams, StreamPlaces &last);

}} // namespace zx::mcp

#endif
