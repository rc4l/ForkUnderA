// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] configfile.cpp reaches the engine's RNG for one thing: the random end tag on a multi-line
// value, so the ini tests need FRandom to exist without m_random.cpp, which includes doomstat.h and
// pulls the whole game in behind it.

#include "m_random.h"
#include "zstring.h"

FRandom::FRandom() {}
FRandom::FRandom(const char *) {}
FRandom::~FRandom() {}

QWORD FRandom::GenRand64()
{
	// Deterministic, so a config written twice in one run is written the same way twice.
	static QWORD state = 0x2545F4914F6CDD1DULL;
	state = state * 6364136223846793005ULL + 1442695040888963407ULL;
	return state;
}

// [rc4l] The other symbol zstring.cpp needs: the engine's own printf, which lives in zstrformat.cpp
// on top of the gdtoa float formatter. The ini tests format nothing, so the platform's vsnprintf
// stands in rather than dragging a 44-file C library into a test target.
#include <cstdio>
#include <vector>

int StringFormat::VWorker(OutputFunc output, void *outputData, const char *fmt, va_list arglist)
{
	va_list copy;
	va_copy(copy, arglist);
	const int need = std::vsnprintf(NULL, 0, fmt, copy);
	va_end(copy);
	if (need < 0)
		return 0;

	std::vector<char> buf(size_t(need) + 1);
	std::vsnprintf(&buf[0], buf.size(), fmt, arglist);
	return output(outputData, &buf[0], need);
}
