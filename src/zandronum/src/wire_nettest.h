// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The writer and reader every wire test models a command with, over one poisoned buffer.

#ifndef ZX_WIRE_NETTEST_H
#define ZX_WIRE_NETTEST_H

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s

#include <cstddef>
#include <vector>

namespace zx { namespace nettest {

// [rc4l] Server-writes / client-reads views over the same bytes, 0xCC poisoned so an unwritten byte shows.
struct Wire {
	std::vector<BYTE> buf;
	BYTESTREAM_s w{}, r{};
	explicit Wire(size_t n = 4096) : buf(n, 0xCC) {
		w.pbStream = buf.data();
		w.pbStreamEnd = buf.data() + buf.size();
	}
	size_t written() const { return static_cast<size_t>(w.pbStream - buf.data()); }
	std::vector<BYTE> bytes() const { return { buf.begin(), buf.begin() + written() }; }
	// [rc4l] A reader over what was written, or over its first truncateTo bytes, as a short packet arrives.
	void openReader(size_t truncateTo = static_cast<size_t>(-1)) {
		const size_t n = (truncateTo == static_cast<size_t>(-1)) ? written() : truncateTo;
		r = BYTESTREAM_s{};
		r.pbStream = buf.data();
		r.pbStreamEnd = buf.data() + n;
	}
};

}} // namespace zx::nettest

#endif // ZX_WIRE_NETTEST_H
