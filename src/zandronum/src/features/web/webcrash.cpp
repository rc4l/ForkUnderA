// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The browser's side of zx_crashreport.h: the page catches a crash and builds, offers and sends its report (web/crashreport.js), and the engine keeps the page told what the report says about the game.

#include <emscripten.h>

#include "features/crashreport/zx_crashreport.h"
#include "features/web/computation/crashcontext_compute.h"
#include "c_cvars.h"
#include "c_console.h"
#include "version.h"
#include "w_wad.h"

#ifndef ZX_SENTRY_DSN
#define ZX_SENTRY_DSN ""
#endif

static zx::web::CrashContext g_context;
// [rc4l] Whether the config is loaded, so the page is never told a reporting setting that is not the player's.
static bool g_configured = false;

// [rc4l] Hands the page the context as it stands, once the config has said whether to send.
static void TellPage()
{
	if (!g_configured) return;
	const std::string json = zx::web::ComputeCrashContextJson(g_context);
	EM_ASM({ if (Module.fuaCrashContext) Module.fuaCrashContext(UTF8ToString($0)); }, json.c_str());
}

// [rc4l] The same setting as the desktop's: 0 off, 1 or more sends reports, on unless the player turns it off.
CUSTOM_CVAR(Int, cl_crashreports, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	g_context.reporting = self >= 1;
	TellPage();
}

void ZX_CrashReportInit() {}
void ZX_CrashReportShutdown() {}

void ZX_CrashReportCheckPreviousCrash()
{
#ifdef ZX_OFFICIAL_BUILD
	const bool official = true;
#else
	const bool official = false;
#endif
	g_context.release = std::string("ForkUnderA@") + GetGitDescription();
	g_context.dist = GetGitHash();
	g_context.dsn = zx::web::ComputeCrashDsn(official, ZX_SENTRY_DSN);
	g_context.reporting = cl_crashreports >= 1;
	g_configured = true;
	TellPage();
}

void ZX_CrashReportLogStatus()
{
	Printf("Crash reports: %s\n", g_context.dsn.empty() ? "offered on the page (this build does not send them)" : g_context.reporting ? "sent when the game crashes" : "off (cl_crashreports 0)");
}

void ZX_CrashReportSetLoadedFiles()
{
	std::vector<std::string> paths;
	const char *name;
	for (int i = 0; (name = Wads.GetWadName(i)) != NULL; ++i) paths.push_back(name);
	g_context.files = zx::web::ComputeCrashFiles(paths);
	TellPage();
}

void ZX_CrashReportSetMap(const char *mapname)
{
	g_context.map = mapname != NULL ? mapname : "";
	TellPage();
}

void ZX_CrashReportSetTag(const char *key, const char *value)
{
	if (key == NULL || value == NULL) return;
	zx::web::ComputeSetCrashTag(g_context, key, value);
	TellPage();
}

// [rc4l] The page reports the fatal it is shown (Module.fuaFatal), with the stack it was raised from.
void ZX_CrashReportFatal(const char *) {}
