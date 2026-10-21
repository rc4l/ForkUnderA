// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/oracle_compute.h"

#include <cstdarg>
#include <cstdio>

namespace zx { namespace frame {

bool ComputeOracleDiffer(OracleTally &t, int keep)
{
	return t.differed++ < keep;
}

bool ComputeOracleRoom(const OracleTally &t, int keep, std::size_t maxChars)
{
	return t.differed < keep && t.firsts.size() < maxChars;
}

void ComputeOracleNote(OracleTally &t, const char *fmt, ...)
{
	char line[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	t.firsts += line;
}

void ComputeOracleReset(OracleTally &t)
{
	t.checked = t.differed = 0;
	t.firsts.clear();
}

}} // namespace zx::frame
