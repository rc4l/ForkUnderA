// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/fatallog/computation/fatallog_compute.h"

namespace zx { namespace fatallog {

const char *const kFatalLogMarker = "fua: FATAL ERROR -- ";

std::string ComputeFatalLogText(const char *message)
{
	std::string body;
	for (const char *p = message ? message : ""; *p; ++p)
	{
		if (*p == '\r' && p[1] == '\n') continue;
		body.push_back(*p == '\r' ? '\n' : *p);
	}
	while (!body.empty() && (body.back() == '\n' || body.back() == ' ' || body.back() == '\t')) body.pop_back();
	if (body.empty()) body = "(no message)";

	std::string out = kFatalLogMarker;
	for (char c : body)
	{
		out.push_back(c);
		// [Doruk-Aksoy] Later lines are indented, so a reader of the log sees where the error ends.
		if (c == '\n') out.append("  ");
	}
	out.push_back('\n');
	return out;
}

bool ComputeFatalLogClaim(bool &alreadyLogged)
{
	if (alreadyLogged) return false;
	alreadyLogged = true;
	return true;
}

}} // namespace zx::fatallog
