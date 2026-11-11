// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/fatallog/fatallog.h"
#include "features/fatallog/computation/fatallog_compute.h"

#include <cstdio>
#include <mutex>
#include <string>

#include "mcp_bridge.h"

extern FILE *Logfile;

void ZX_FatalLog(const char *message)
{
	static std::mutex lock;
	static bool logged = false;
	std::lock_guard<std::mutex> guard(lock);
	if (!zx::fatallog::ComputeFatalLogClaim(logged)) return;

	const std::string text = zx::fatallog::ComputeFatalLogText(message);
	fputs(text.c_str(), stderr);
	fflush(stderr);
	if (Logfile)
	{
		fputs(text.c_str(), Logfile);
		fflush(Logfile);
	}
	MCP_Bridge_TeeOutput(text.c_str());
}
