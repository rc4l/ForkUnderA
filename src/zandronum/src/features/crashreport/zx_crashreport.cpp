// [rc4l] See zx_crashreport.h. sentry-native (MIT) is vendored at src/zandronum/sentry-native and
// reports to our self-hosted GlitchTip. Built with the inproc backend so there's no external
// handler binary to ship. The DSN is a compile-time define (ZX_SENTRY_DSN); a DSN is public by
// design (it ships in the client), so baking it in is expected.
//
// Reporting model: auto-send, opt-out. `cl_crashreports` (0 = off, >= 1 = on; default on) is a
// plain persistent setting in the FUA options menu -- there is NO per-crash prompt. A prompt can't
// reach a headless server, so the old "ask on next launch" dialog left dedicated servers unable to
// report; a setting works everywhere. On the launch after a crash we simply init sentry with
// consent = the setting: if on, the stored crash uploads silently (and this run is captured too);
// if off, it is discarded. We only init once the config is loaded (so the setting is known and we
// never init with an unknown consent, which would make sentry discard a stored crash). The pure
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
#include "v_text.h"       // TEXTCOLOR_* console color codes
#include "features/crashreport/computation/crash_report_compute.h"
#include "features/crashreport/computation/crash_symbolize_compute.h"

#ifndef _WIN32
// [rc4l] POSIX crash naming: dyld has every loaded module's symbols at crash time, so dladdr names
// both our own frames (we ship un-stripped) AND system frames -- which is the only place a crash on
// an Apple GL-driver worker thread has any name at all. GlitchTip can't do this (no Apple symbols),
// which is why unsymbolicated native crashes collapse into one "<unknown>" group. Windows keeps its
// own path untouched.
#include <dlfcn.h>
#include <cxxabi.h>
#include <vector>
#include <cstdint>
#endif

#ifndef ZX_SENTRY_DSN
#define ZX_SENTRY_DSN ""
#endif

// Crash reporting: 0 = off (opt out), >= 1 = on (auto-send). Default on. Exposed as an On/Off toggle
// in the FUA options menu (menudef "FUAOptions").
CVAR(Int, cl_crashreports, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

static bool g_sentryInited = false;

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
// disk tells us we crashed last run, so we can flush hard enough to guarantee that stored crash
// actually delivers before the process could crash again.
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
#ifndef _WIN32
// Resolve one instruction address to a demangled name via dladdr; mainModule = it's in our exe.
static std::string zx_resolve_addr(const void *addr, const std::string &mainFname, bool &mainModule)
{
	Dl_info info;
	if (dladdr(addr, &info) == 0 || info.dli_sname == NULL)
	{
		mainModule = false;
		return std::string();
	}
	mainModule = (info.dli_fname != NULL && mainFname == info.dli_fname);
	int status = 0;
	char *dem = abi::__cxa_demangle(info.dli_sname, NULL, NULL, &status);
	std::string out = (status == 0 && dem != NULL) ? std::string(dem) : std::string(info.dli_sname);
	free(dem);
	return out;
}

// Walk the crash stacktrace, name each frame in place via dladdr, and collect the resolved frames
// (outermost-first) for the title/fingerprint decision. Returns false if there are no frames.
static bool zx_resolve_event_frames(sentry_value_t event, std::vector<zx::crashreport::ResolvedFrame> &out)
{
	// A native crash nests its stack under exception.values[0].stacktrace.frames.
	sentry_value_t values = sentry_value_get_by_key(sentry_value_get_by_key(event, "exception"), "values");
	sentry_value_t stack  = sentry_value_get_by_key(sentry_value_get_by_index(values, 0), "stacktrace");
	sentry_value_t frames = sentry_value_get_by_key(stack, "frames");
	size_t n = sentry_value_get_length(frames);
	if (n == 0)
		return false;

	Dl_info self;
	std::string mainFname;
	if (dladdr((const void *)&zx_resolve_event_frames, &self) != 0 && self.dli_fname)
		mainFname = self.dli_fname;

	out.clear();
	out.reserve(n);
	for (size_t i = 0; i < n; ++i)
	{
		sentry_value_t fr = sentry_value_get_by_index(frames, i);
		const char *iaStr = sentry_value_as_string(sentry_value_get_by_key(fr, "instruction_addr"));
		zx::crashreport::ResolvedFrame rf;
		if (iaStr && iaStr[0])
		{
			const void *addr = (const void *)(uintptr_t)strtoull(iaStr, NULL, 16);
			rf.func = zx_resolve_addr(addr, mainFname, rf.mainModule);
			if (!rf.func.empty())
			{
				sentry_value_set_by_key(fr, "function", sentry_value_new_string(rf.func.c_str()));
				if (rf.mainModule)
					sentry_value_set_by_key(fr, "in_app", sentry_value_new_bool(1));
			}
		}
		out.push_back(rf);
	}
	return true;
}
#endif // !_WIN32

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

#ifndef _WIN32
	// [rc4l] Name every frame via dladdr and set a fingerprint so GlitchTip forms a distinct, titled
	// group per crash site (instead of collapsing all native crashes into one "<unknown>" group).
	std::vector<zx::crashreport::ResolvedFrame> resolved;
	if (zx_resolve_event_frames(event, resolved))
	{
		zx::crashreport::CrashIdentity id = zx::crashreport::ComputeCrashIdentity(resolved);

		sentry_value_t fp = sentry_value_new_list();
		sentry_value_append(fp, sentry_value_new_string(id.fingerprint.c_str()));
		sentry_value_set_by_key(event, "fingerprint", fp);

		sentry_value_t tags2 = sentry_value_get_by_key(event, "tags");
		if (sentry_value_is_null(tags2))
		{
			tags2 = sentry_value_new_object();
			sentry_value_set_by_key(event, "tags", tags2);
		}
		sentry_value_set_by_key(tags2, "zx_crash_site", sentry_value_new_string(id.title.c_str()));
	}
#endif
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
	// have captured; the report then uploads on the next launch. (sentry itself already sets
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

	// [rc4l] Dev/CI override: ZX_CRASH_CONSENT=1 gives consent up front so an intentional crash
	// verifies end-to-end without waiting for the config to load. Never set in shipped builds.
	const char *force = getenv("ZX_CRASH_CONSENT");
	if (force != NULL && force[0] == '1')
		ZX_CrashReportDoInit(1);

	// The normal path defers init to ZX_CrashReportCheckPreviousCrash(), which runs once the config
	// (cl_crashreports) is loaded -- so we never init with an unknown setting and discard a stored
	// crash. The pre-config startup window is a sub-second sliver where crashes are vanishingly rare.
}

static void ClearCrashMarker()
{
	if (g_sentryInited)
		sentry_clear_crashed_last_run();
}

// [rc4l] After consent is given the stored crash is queued on sentry's background transport. When a
// crash is actually pending, block briefly until it is delivered so an immediate re-crash (or a
// server auto-restart that exits fast) can't lose it -- and log the outcome. No-op until sentry is
// initialized (a DSN is baked in).
// After consent is given a stored crash is queued on sentry's background transport. When one is
// actually pending, block briefly until it delivers so an immediate re-crash (or a server that
// exits fast) can't lose it. Returns true if it was delivered within the timeout.
static bool FlushPendingReport()
{
	if (!g_sentryInited)
		return false;
	return sentry_flush(5000) == 0;   // wait up to 5s for the background transport to finish
}

// [rc4l] The startup status line, built during init but printed later (ZX_CrashReportLogStatus,
// from D_DoomLoop) so it lands at the BOTTOM of the startup log next to the other "... initialized"
// lines, where it's actually visible -- instead of scrolling off mid-startup.
static char g_statusLine[192] = { 0 };

// Called once the config (cl_crashreports) is loaded, before the game's late init. The real init
// point: bring sentry up with consent = the setting. If reporting is on, any crash stored from the
// last run uploads (silently) here; if off, it is discarded. Works headless (no prompt anywhere).
void ZX_CrashReportCheckPreviousCrash()
{
	const char *dsn = ZX_SENTRY_DSN;
	if (dsn == NULL || dsn[0] == '\0')
		return;

	const bool on = zx::ComputeStartupAction(cl_crashreports) == zx::StartupAction::GiveConsent;
	const bool pending = PendingCrashExists();   // a crash from the last run is waiting on disk

	if (g_sentryInited)
		// Already inited early (ZX_CRASH_CONSENT override): just keep consent in step with the cvar.
		{ if (on) sentry_user_consent_give(); else sentry_user_consent_revoke(); }
	else
		ZX_CrashReportDoInit(on ? 1 : -1);

	// Do the upload/flush now (so a stored crash delivers before a possible re-crash), but only
	// record the outcome -- the actual Printf is deferred to ZX_CrashReportLogStatus() for visibility.
	// Colors: green for the working/positive states (like the demo-playback notices), orange for a
	// retryable warning, red for an outright failure, default for the neutral "off" states.
	if (!g_sentryInited)
		snprintf(g_statusLine, sizeof g_statusLine,
			TEXTCOLOR_RED "Crash reporting: unavailable (init failed)" TEXTCOLOR_NORMAL);
	else if (pending && on)
	{
		const bool sent = FlushPendingReport();
		ClearCrashMarker();
		if (sent)
			snprintf(g_statusLine, sizeof g_statusLine,
				TEXTCOLOR_GREEN "Crash reporting: a crash from the last run was SENT. Thanks for helping fix ZandroX. (ZandroX@%s)" TEXTCOLOR_NORMAL, GetGitHash());
		else
			snprintf(g_statusLine, sizeof g_statusLine,
				TEXTCOLOR_ORANGE "Crash reporting: crash upload timed out; it will retry next launch. (ZandroX@%s)" TEXTCOLOR_NORMAL, GetGitHash());
	}
	else if (pending && !on)
	{
		ClearCrashMarker();
		snprintf(g_statusLine, sizeof g_statusLine,
			"Crash reporting: a crash from the last run was discarded (reporting is off).");
	}
	else if (on)
		snprintf(g_statusLine, sizeof g_statusLine,
			TEXTCOLOR_GREEN "Crash reporting: active, auto-send ON (ZandroX@%s)" TEXTCOLOR_NORMAL, GetGitHash());
	else
		snprintf(g_statusLine, sizeof g_statusLine,
			"Crash reporting: OFF -- enable it in Options > FUA Options > Crash Reporting.");
}

// Print the crash-reporting status exactly once. Called late (D_DoomLoop) so it shows at the bottom
// of the startup log, next to the sound/VoIP init lines, where a player or server operator sees it.
void ZX_CrashReportLogStatus()
{
	if (g_statusLine[0] != '\0')
	{
		Printf("%s\n", g_statusLine);
		g_statusLine[0] = '\0';
	}
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
void ZX_CrashReportLogStatus() {}
void ZX_CrashReportSetLoadedFiles() {}
void ZX_CrashReportSetMap(const char *) {}
void ZX_CrashReportSetTag(const char *, const char *) {}

#endif
