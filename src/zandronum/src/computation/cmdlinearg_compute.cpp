// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/cmdlinearg_compute.h"

namespace zx {

std::string ComputeQuotedArgument(const std::string &arg)
{
	if (arg.empty()) return "\"\"";
	if (arg.find('"') != std::string::npos)
	{
		std::string out = "\"";
		for (char ch : arg)
		{
			if (ch == '"') out += "\\\"";
			else out += ch;
		}
		return out + "\"";
	}
	// [rc4l] Ported from zandronum@0d75cf18b6be67da24820e4ab59701f8e4664e01: a semicolon is quoted as a space is, or +sv_banfile "a.txt;b.txt" ran as two commands.
	if (arg.find(' ') != std::string::npos || arg.find(';') != std::string::npos) return "\"" + arg + "\"";
	return arg;
}

} // namespace zx
