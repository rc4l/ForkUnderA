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

// [rc4l] GlitchTip keeps event tags but drops the native debug-image list, and it can't
// symbolicate C/C++ crashes server-side. So copy the main module's load address + debug id into
// tags; our crash-sync workflow uses them (with the raw addresses GlitchTip does keep and the
// build's symbols from the GitHub release) to symbolicate the stack itself.
static sentry_value_t zx_before_send(sentry_value_t event, void *hint, void *closure)
{
	(void)hint; (void)closure;
	sentry_value_t images = sentry_value_get_by_key(
		sentry_value_get_by_key(event, "debug_meta"), "images");
	if (sentry_value_get_length(images) > 0)
	{
		sentry_value_t img = sentry_value_get_by_index(images, 0); // main executable
		const char *base = sentry_value_as_string(sentry_value_get_by_key(img, "image_addr"));
		const char *did  = sentry_value_as_string(sentry_value_get_by_key(img, "debug_id"));
		sentry_value_t tags = sentry_value_get_by_key(event, "tags");
		if (sentry_value_is_null(tags))
		{
			tags = sentry_value_new_object();
			sentry_value_set_by_key(event, "tags", tags);
		}
		if (base && base[0]) sentry_value_set_by_key(tags, "zx_image_base", sentry_value_new_string(base));
		if (did && did[0])   sentry_value_set_by_key(tags, "zx_debug_id", sentry_value_new_string(did));
	}
	return event;
}

void ZX_CrashReportInit()
{
	const char *dsn = ZX_SENTRY_DSN;
	if (dsn == NULL || dsn[0] == '\0')
		return; // no DSN baked in -> reporting disabled

	sentry_options_t *options = sentry_options_new();
	sentry_options_set_dsn(options, dsn);
	sentry_options_set_before_send(options, zx_before_send, NULL);

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
