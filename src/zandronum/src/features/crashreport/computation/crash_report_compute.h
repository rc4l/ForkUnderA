// [rc4l] Pure decision logic for crash-report consent + privacy-safe tag values, split out so it
// is unit-testable off-engine. The engine wrapper (zx_crashreport.cpp) calls these and does the
// sentry-native I/O. No engine headers here.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_CRASH_REPORT_COMPUTE_H
#define ZX_CRASH_REPORT_COMPUTE_H

#include <string>

namespace zx {

// cl_crashreports: 0 = off (opt out), >= 1 = on (auto-send, the default). There is no per-crash
// prompt -- a prompt can't reach a headless server, so reporting is a plain persistent setting and
// consent is simply whatever the setting says. On launch, decide what to tell sentry.
enum class StartupAction
{
	GiveConsent,   // reporting on: capture this run + upload any crash stored from the last run
	RevokeConsent, // reporting off: discard any stored crash and upload nothing
};
StartupAction ComputeStartupAction(int crashreportsCvar);

// Privacy: a tag value for a loaded file must be the bare filename, never the full path -- a path
// like C:\Users\aurat\wads\x.pk3 leaks the player's OS username. Strips any directory component
// (handles both / and \), leaving just the last segment.
std::string ComputeSafeFileLabel(const std::string &path);

// A graceful fatal (I_FatalError) is NOT a signal, so sentry's crash handler never persists it -- and
// the in-process upload can lose the race (network torn down mid-restart, or the OS error dialog
// wedges/kills the process first). So the engine writes a tiny durable "pending fatal" record at
// fatal time; on the NEXT launch we decide what to do with it from whether it exists and whether
// reporting is on. Upload when both hold (deliver the fatal we couldn't send last time), discard when
// it exists but reporting was turned off, otherwise nothing to do.
enum class PendingFatalAction { None, Upload, Discard };
PendingFatalAction ComputePendingFatalAction(bool recordExists, bool reportingOn);

} // namespace zx

#endif // ZX_CRASH_REPORT_COMPUTE_H
