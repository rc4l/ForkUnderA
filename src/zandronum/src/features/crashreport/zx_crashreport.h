// [rc4l] Crash reporting glue: initializes sentry-native, which captures crashes out of the
// engine's hot path and uploads them to our self-hosted GlitchTip. Original wiring (no upstream
// equivalent), so no provenance link. Both calls are safe no-ops when the engine is built
// without ZX_ENABLE_SENTRY or with an empty DSN.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_CRASHREPORT_H
#define ZX_CRASHREPORT_H

void ZX_CrashReportInit();      // call once, as early as possible in main()/WinMain()
void ZX_CrashReportShutdown();  // idempotent; also auto-registered via atexit()

#endif // ZX_CRASHREPORT_H
