// [rc4l] Pure decision logic for crash-report consent + privacy-safe tag values, split out so it
// is unit-testable off-engine. The engine wrapper (zx_crashreport.cpp) calls these and does the
// sentry-native I/O. No engine headers here.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_CRASH_REPORT_COMPUTE_H
#define ZX_CRASH_REPORT_COMPUTE_H

#include <string>

namespace zx {

// cl_crashreports: 0 = never send/ask, 1 = ask after a crash (default), 2 = always send.
// On launch, decide what to do given the cvar and whether the previous run crashed.
enum class StartupAction
{
	Nothing,       // nothing to do (no crash, or ask-mode with no crash)
	RevokeConsent, // cvar==never: make sure nothing uploads
	GiveConsent,   // cvar==always: upload the stored crash silently
	ShowPrompt,    // cvar==ask and we crashed: ask once
};
StartupAction ComputeStartupAction(int crashreportsCvar, bool crashedLastRun);

// The prompt's choices. Deliberately NO permanent "never send" -- that stays a manual
// `cl_crashreports 0`, so the prompt can't be used to opt out of ever helping in one click.
enum class CrashChoice
{
	SendOnce,   // send this crash; keep asking next time
	AlwaysSend, // send this crash + set cl_crashreports=2 (never ask again)
	SaveToDisk, // don't send; export the report to a findable file
	NotNow,     // don't send; ask again next crash
};
struct CrashChoiceAction
{
	bool upload;        // send the stored crash now
	bool persistAlways; // set cl_crashreports = 2 (always; stop asking)
	bool saveToDisk;    // export the report locally instead of sending
	bool flush;         // block until the upload is delivered (else an async send is lost when the
	                    // process exits right after consent -- the v0.1.8 bug). INVARIANT: whenever
	                    // upload is true, flush must be true.
};
CrashChoiceAction ComputeChoiceAction(CrashChoice choice);

// Privacy: a tag value for a loaded file must be the bare filename, never the full path -- a path
// like C:\Users\aurat\wads\x.pk3 leaks the player's OS username. Strips any directory component
// (handles both / and \), leaving just the last segment.
std::string ComputeSafeFileLabel(const std::string &path);

} // namespace zx

#endif // ZX_CRASH_REPORT_COMPUTE_H
