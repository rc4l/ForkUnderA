// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-protocol/commandcapture.h"
#include "features/net-protocol/computation/commandcapture_compute.h"
#include "features/command-safety/computation/commandsafety_compute.h"   // [rc4l] ComputeOwnExtension

#include "c_dispatch.h"
#include "doomstat.h"   // [rc4l] gametic
#include "network_enums.h"   // [rc4l] SVC_EXTENDEDCOMMAND
#include <stdio.h>
#include <string>

namespace {

FILE *g_capture = NULL;
int g_lines = 0;

void Close()
{
	if (g_capture != NULL) fclose(g_capture);
	g_capture = NULL;
}

}

namespace zx { namespace netprotocol {

void CaptureCommand(int client, bool reliable, const unsigned char *bytes, int length)
{
	if (g_capture == NULL) return;
	fputs(ComputeCaptureLine(gametic, client, reliable, bytes, length, SVC_EXTENDEDCOMMAND).c_str(), g_capture);
	fputc('\n', g_capture);
	g_lines++;
}

}} // namespace zx::netprotocol

// [rc4l] Starts capturing to a file of its own extension, as every command that writes where it is told does (features/command-safety).
UNSAFE_CCMD(fua_netcap_start)
{
	if (argv.argc() < 2)
	{
		Printf("usage: fua_netcap_start <file>\n");
		return;
	}
	Close();
	const std::string name = zx::ComputeOwnExtension(argv[1], { ".netcap" });
	g_capture = fopen(name.c_str(), "w");
	g_lines = 0;
	Printf(g_capture != NULL ? "[netcap] capturing to %s\n" : "[netcap] cannot write %s\n", name.c_str());
}

CCMD(fua_netcap_stop)
{
	const bool was = g_capture != NULL;
	Close();
	Printf(was ? "[netcap] %d commands captured\n" : "[netcap] no capture running\n", g_lines);
}
