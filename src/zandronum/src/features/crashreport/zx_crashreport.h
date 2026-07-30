// [rc4l] Crash reporting glue: sentry-native captures crashes and (when reporting is enabled)
// uploads them to our self-hosted GlitchTip. Reporting is auto-send / opt-out via the persistent
// `cl_crashreports` setting in the FUA options menu -- there is no per-crash prompt (a prompt can't
// reach a headless server). All entry points are safe no-ops when built without ZX_ENABLE_SENTRY or
// with an empty DSN.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_CRASHREPORT_H
#define ZX_CRASHREPORT_H

// Lifecycle
void ZX_CrashReportInit();        // as early as possible in main()/WinMain() (CI override only)
void ZX_CrashReportShutdown();    // idempotent; also auto-registered via atexit()

// Called once the menu system is up and the config (cl_crashreports) is loaded. The real init
// point: brings sentry up with consent = the setting, uploading any crash stored from the last run
// silently if reporting is on, or discarding it if off. Safe on dedicated servers (no UI needed).
void ZX_CrashReportCheckPreviousCrash();

// Prints the crash-reporting status (active / a crash was sent / off) exactly once. Call late in
// startup (D_DoomLoop) so the line lands at the bottom of the log where it's actually visible.
void ZX_CrashReportLogStatus();

// Context tags (cheap, set-and-forget). Values are attached to every future crash event.
void ZX_CrashReportSetLoadedFiles(); // reads the wad list; strips paths to bare filenames
void ZX_CrashReportSetMap(const char *mapname);
void ZX_CrashReportSetTag(const char *key, const char *value);

// Report a graceful fatal (I_FatalError) before the process exits. A signal crash is caught by
// sentry's handler; an I_FatalError just exit()s, so bad-WAD / script-error / missing-resource fatals
// would otherwise go unreported. Safe no-op if reporting is off or sentry isn't up; blocks briefly to
// flush before the caller exits.
void ZX_CrashReportFatal(const char *message);

#endif // ZX_CRASHREPORT_H
