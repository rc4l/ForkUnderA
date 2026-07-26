// [rc4l] See zx_crashreport.h. sentry-native (MIT) is vendored at src/zandronum/sentry-native and
// reports to our self-hosted GlitchTip. Built with the inproc backend so there's no external
// handler binary to ship. The DSN is a compile-time define (ZX_SENTRY_DSN); a DSN is public by
// design (it ships in the client), so baking it in is expected.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "zx_crashreport.h"

#ifdef ZX_ENABLE_SENTRY

#include <cstdlib>
#include <cstdio>
#include <sentry.h>
#include "version.h"   // GetGitDescription(), GetGitHash()

#ifndef ZX_SENTRY_DSN
#define ZX_SENTRY_DSN ""
#endif

static bool g_sentryInited = false;

void ZX_CrashReportShutdown()
{
	if (!g_sentryInited)
		return;
	sentry_close();
	g_sentryInited = false;
}

void ZX_CrashReportInit()
{
	const char *dsn = ZX_SENTRY_DSN;
	if (dsn == NULL || dsn[0] == '\0')
		return; // no DSN baked in -> reporting disabled

	sentry_options_t *options = sentry_options_new();
	sentry_options_set_dsn(options, dsn);

	// Tie each crash to the exact build: release groups them, dist pins the commit.
	char release[128];
	snprintf(release, sizeof release, "ZandroX@%s", GetGitDescription());
	sentry_options_set_release(options, release);
	sentry_options_set_dist(options, GetGitHash());

	// Local staging DB for events that can't upload immediately (offline players).
	sentry_options_set_database_path(options, ".zandrox-sentry");

#ifdef _DEBUG
	sentry_options_set_debug(options, 1);
	sentry_options_set_environment(options, "debug");
#else
	sentry_options_set_environment(options, "release");
#endif

	if (sentry_init(options) == 0)
	{
		g_sentryInited = true;
		atexit(ZX_CrashReportShutdown); // flush/close even on paths that don't call us explicitly
	}
}

#else // !ZX_ENABLE_SENTRY

void ZX_CrashReportInit() {}
void ZX_CrashReportShutdown() {}

#endif
