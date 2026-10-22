// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "madeinplay_compute.h"

#include <cstdio>

namespace zx { namespace hwrender {

void ComputeMadeInPlayFrame(MadeInPlay &m, bool ready)
{
	m.readyFrames = ready ? m.readyFrames + 1 : 0;
}

bool ComputeMadeInPlayNote(MadeInPlay &m, const char *kind, const char *name)
{
	if (m.readyFrames < kMadeInPlayArmFrames) return false;
	m.total += 1;
	const std::string named = name != nullptr && name[0] != '\0' ? name : "(unnamed)";
	for (MadeInPlayEntry &e : m.entries)
		if (e.kind == kind && e.name == named) { e.count += 1; return true; }
	if (m.entries.size() < kMadeInPlayMaxEntries) m.entries.push_back({ kind, named, 1, m.readyFrames });
	return true;
}

void ComputeMadeInPlayClear(MadeInPlay &m)
{
	m = MadeInPlay();
}

std::string ComputeMadeInPlayReport(const MadeInPlay &m)
{
	char line[512];
	std::snprintf(line, sizeof(line), "%d GPU objects made in play\n", m.total);
	std::string out = line;
	for (const MadeInPlayEntry &e : m.entries)
	{
		std::snprintf(line, sizeof(line), "  %-8s %4d  frame %-6d %s\n", e.kind.c_str(), e.count, e.firstFrame, e.name.c_str());
		out += line;
	}
	if (m.entries.size() >= kMadeInPlayMaxEntries) out += "  (more not listed)\n";
	return out;
}

}} // namespace zx::hwrender
