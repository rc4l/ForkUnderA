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

} // namespace zx

#endif // ZX_CRASH_REPORT_COMPUTE_H
