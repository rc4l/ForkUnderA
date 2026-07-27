// [rc4l] See zx_crashreport.h. sentry-native (MIT) is vendored at src/zandronum/sentry-native and
// reports to our self-hosted GlitchTip. Built with the inproc backend so there's no external
// handler binary to ship. The DSN is a compile-time define (ZX_SENTRY_DSN); a DSN is public by
// design (it ships in the client), so baking it in is expected.
//
// Consent model (deferred init): sentry-native DISCARDS a stored crash if consent is unknown at
// sentry_init. So on the launch after a crash we must NOT init until the player decides -- we
// detect the pending crash from the on-disk marker, hold init, ask, then init WITH the answer so
// the crash is uploaded (Send/Always), discarded (Not now), or left on disk (Save). The cvar
// `cl_crashreports` is the persistent control (0 = never, 1 = ask [default], 2 = always). The pure
// decision logic lives in computation/crash_report_compute.* so it is unit-tested off-engine.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "zx_crashreport.h"

#ifdef ZX_ENABLE_SENTRY

#include <cstdlib>
#include <cstdio>
#include <string>
#include <sentry.h>

#ifdef _WIN32
// sentry.h pulls in <windows.h>, which typedefs DWORD; tell the engine headers below to use that
// one instead of redefining it (see basictypes.h). Without this the Windows build fails C2371.
#define USE_WINDOWS_DWORD
#endif

#include "version.h"   // GetGitDescription(), GetGitHash()
#include "w_wad.h"     // Wads
#include "c_cvars.h"
#include "c_console.h"    // Printf
#include "c_dispatch.h"   // CCMD
#include "menu/menu.h"    // M_SetMenu, M_ClearMenus, NAME_CrashConsentMenu
#include "features/crashreport/computation/crash_report_compute.h"

#ifndef ZX_SENTRY_DSN
#define ZX_SENTRY_DSN ""
#endif

// 0 = never send or ask, 1 = ask after a crash (default), 2 = always send silently.
CVAR(Int, cl_crashreports, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

static bool g_sentryInited = false;
static bool g_pendingCrash = false;       // a crash from the previous run is waiting on disk
static bool g_wantsConsentPrompt = false; // set on the ask path; consumed by the consent menu

static std::string CrashDbPath()
{
#ifdef _WIN32
	const char *base = getenv("APPDATA");
	return (base ? std::string(base) + "\\ZandroX" : std::string(".")) + "\\crashdb";
#else
	const char *home = getenv("HOME");
	return (home ? std::string(home) + "/.config/zandrox" : std::string(".")) + "/crashdb";
#endif
}

// sentry writes a "last_crash" marker in the DB dir when it captures a crash. Checking for it on
// disk lets us know we crashed WITHOUT calling sentry_init (which would discard the crash).
static bool PendingCrashExists()
{
#ifdef _WIN32
	const std::string marker = CrashDbPath() + "\\last_crash";
#else
	const std::string marker = CrashDbPath() + "/last_crash";
#endif
	if (FILE *f = fopen(marker.c_str(), "rb"))
	{
		fclose(f);
		return true;
	}
	return false;
}

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

// The single init point. consentAction: 0 = use persisted consent, +1 = give, -1 = revoke.
// Giving consent here (right after sentry_init) is what actually uploads a stored crash.
static void ZX_CrashReportDoInit(int consentAction)
{
	if (g_sentryInited)
		return;
	const char *dsn = ZX_SENTRY_DSN;
	if (dsn == NULL || dsn[0] == '\0')
		return;

	sentry_options_t *options = sentry_options_new();
	sentry_options_set_dsn(options, dsn);
	sentry_options_set_require_user_consent(options, 1);
	sentry_options_set_before_send(options, zx_before_send, NULL);

	char release[128];
	snprintf(release, sizeof release, "ZandroX@%s", GetGitDescription());
	sentry_options_set_release(options, release);
	sentry_options_set_dist(options, GetGitHash());

	const std::string db = CrashDbPath();
	sentry_options_set_database_path(options, db.c_str());

#ifdef _DEBUG
	sentry_options_set_debug(options, 1);
	sentry_options_set_environment(options, "debug");
#else
	sentry_options_set_environment(options, "release");
#endif

	if (sentry_init(options) != 0)
		return;
	g_sentryInited = true;
	atexit(ZX_CrashReportShutdown);

#ifdef _WIN32
	// [rc4l] With the legacy Win32 crash handler removed, sentry-native owns crashes. Its handler
	// captures the crash and then returns EXCEPTION_CONTINUE_SEARCH, which would let Windows pop its
	// own "stopped working" (WER) dialog. Suppress that GPF box so the process dies quietly after we
	// have captured; our consent prompt then appears on the next launch. (sentry itself already sets
	// SEM_FAILCRITICALERRORS; we add the GPF-box bit on top and preserve any existing flags.)
	SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX);
#endif

	if (consentAction > 0)
		sentry_user_consent_give();     // uploads the stored crash, if any
	else if (consentAction < 0)
		sentry_user_consent_revoke();

	ZX_CrashReportSetLoadedFiles();      // tag this session's context
}

void ZX_CrashReportInit()
{
	const char *dsn = ZX_SENTRY_DSN;
	if (dsn == NULL || dsn[0] == '\0')
		return; // no DSN baked in -> reporting disabled

	g_pendingCrash = PendingCrashExists();

	// [rc4l] Dev/CI override: ZX_CRASH_CONSENT=1 gives consent up front so an intentional crash
	// verifies end-to-end without a human answering the prompt. Never set in shipped builds.
	const char *force = getenv("ZX_CRASH_CONSENT");
	if (force != NULL && force[0] == '1')
	{
		ZX_CrashReportDoInit(1);
		return;
	}

	// No pending crash -> safe to init now (sentry uses the persisted consent). A pending crash is
	// held: we defer init to ZX_CrashReportCheckPreviousCrash / the prompt so it isn't discarded.
	if (!g_pendingCrash)
		ZX_CrashReportDoInit(0);
}

static void ClearCrashMarker()
{
	if (g_sentryInited)
		sentry_clear_crashed_last_run();
}

// Menu buttons -> here. When we get here with a pending crash, sentry is not yet initialized.
CCMD(crashreport_send)
{
	ZX_CrashReportDoInit(1);   // give consent -> uploads the pending crash
	ClearCrashMarker();
	M_ClearMenus();
}
CCMD(crashreport_always)
{
	UCVarValue v; v.Int = 2;   // remember: always send
	cl_crashreports.ForceSet(v, CVAR_Int);
	ZX_CrashReportDoInit(1);
	ClearCrashMarker();
	M_ClearMenus();
}
CCMD(crashreport_notnow)
{
	ZX_CrashReportDoInit(0);   // init with unknown consent -> discards the pending crash, asks again next time
	ClearCrashMarker();
	M_ClearMenus();
}
CCMD(crashreport_save)
{
	// Don't init (that would discard it): the crash stays in the DB on disk for the player to keep.
	Printf("Crash report kept locally in: %s\n", CrashDbPath().c_str());
	M_ClearMenus();
}

void ZX_CrashReportCheckPreviousCrash()
{
	if (g_sentryInited)
	{
		// Already inited at startup (no pending crash). Keep sentry consent in step with the cvar.
		if (cl_crashreports <= 0)
			sentry_user_consent_revoke();
		else if (cl_crashreports >= 2)
			sentry_user_consent_give();
		return;
	}

	// Deferred: a crash is pending and sentry is still uninitialized.
	switch (zx::ComputeStartupAction(cl_crashreports, g_pendingCrash))
	{
	case zx::StartupAction::GiveConsent:   // always
		ZX_CrashReportDoInit(1);
		ClearCrashMarker();
		break;
	case zx::StartupAction::RevokeConsent: // never
		ZX_CrashReportDoInit(-1);
		ClearCrashMarker();
		break;
	case zx::StartupAction::ShowPrompt:    // ask: stay deferred; the menu decides + inits
		g_wantsConsentPrompt = true;
		break;
	case zx::StartupAction::Nothing:
		ZX_CrashReportDoInit(0);           // safety (no pending crash after all)
		break;
	}

	// [rc4l] Announce status here (post-console, so it reliably reaches the log -- doing it in
	// ZX_CrashReportInit at main() is too early to print). If an upstream re-sync ever drops the
	// hook calls, this line stops appearing: a visible signal that crash reporting went missing.
	if (g_sentryInited)
		Printf("Crash reporting active (ZandroX@%s)\n", GetGitHash());
	else if (g_pendingCrash)
		Printf("Crash reporting: waiting for your choice on last run's crash\n");
}

void ZX_CrashReportTickPrompt()
{
	if (!g_wantsConsentPrompt)
		return;
	g_wantsConsentPrompt = false;              // open exactly once
	M_SetMenu(NAME_CrashConsentMenu, -1);
}

void ZX_CrashReportSetTag(const char *key, const char *value)
{
	if (g_sentryInited && key != NULL && value != NULL)
		sentry_set_tag(key, value);
}

void ZX_CrashReportSetMap(const char *mapname)
{
	if (g_sentryInited && mapname != NULL && mapname[0] != '\0')
		sentry_set_tag("map", mapname);
}

void ZX_CrashReportSetLoadedFiles()
{
	if (!g_sentryInited)
		return;

	std::string order;
	const char *name;
	for (int i = 0; (name = Wads.GetWadName(i)) != NULL; ++i)
	{
		const std::string label = zx::ComputeSafeFileLabel(name); // bare filename, never a path
		if (!order.empty())
			order += ", ";
		order += label;
		if (i == 1) // wad 0 is zandronum.pk3; wad 1 is the IWAD
			sentry_set_tag("iwad", label.c_str());
	}
	if (!order.empty())
		sentry_set_extra("load_order", sentry_value_new_string(order.c_str()));
}

#else // !ZX_ENABLE_SENTRY

void ZX_CrashReportInit() {}
void ZX_CrashReportShutdown() {}
void ZX_CrashReportCheckPreviousCrash() {}
void ZX_CrashReportTickPrompt() {}
void ZX_CrashReportSetLoadedFiles() {}
void ZX_CrashReportSetMap(const char *) {}
void ZX_CrashReportSetTag(const char *, const char *) {}

#endif
