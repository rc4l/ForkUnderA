// [rc4l] Crash reporting glue: sentry-native captures crashes and (once the player consents)
// uploads them to our self-hosted GlitchTip. Consent is asked once, on the next launch after a
// crash. Original wiring (no upstream equivalent). All entry points are safe no-ops when built
// without ZX_ENABLE_SENTRY or with an empty DSN.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_CRASHREPORT_H
#define ZX_CRASHREPORT_H

// Lifecycle
void ZX_CrashReportInit();        // as early as possible in main()/WinMain()
void ZX_CrashReportShutdown();    // idempotent; also auto-registered via atexit()

// Called once the menu system is up: if we crashed last run, either upload silently (consent
// already given) or pop the one-time consent prompt.
void ZX_CrashReportCheckPreviousCrash();

// Call once per rendered frame: opens the one-time consent prompt when one is pending (deferred
// here from ZX_CrashReportCheckPreviousCrash so it survives the title/demo loop startup).
void ZX_CrashReportTickPrompt();

// Context tags (cheap, set-and-forget). Values are attached to every future crash event.
void ZX_CrashReportSetLoadedFiles(); // reads the wad list; strips paths to bare filenames
void ZX_CrashReportSetMap(const char *mapname);
void ZX_CrashReportSetTag(const char *key, const char *value);

#endif // ZX_CRASHREPORT_H
