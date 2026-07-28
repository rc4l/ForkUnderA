// [rc4l] Implementation of the pure crash-report decision logic. See crash_report_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "crash_report_compute.h"

namespace zx {

StartupAction ComputeStartupAction(int crashreportsCvar, bool crashedLastRun)
{
	if (crashreportsCvar <= 0)
		return StartupAction::RevokeConsent;   // never
	if (crashreportsCvar >= 2)
		return StartupAction::GiveConsent;      // always
	// ask (1): only relevant if we actually crashed
	return crashedLastRun ? StartupAction::ShowPrompt : StartupAction::Nothing;
}

CrashChoiceAction ComputeChoiceAction(CrashChoice choice)
{
	CrashChoiceAction a{false, false, false, false};
	switch (choice)
	{
	// Uploading always sets flush too: an unflushed (async) send is lost if the game exits right
	// after the player picks a Send option -- exactly the v0.1.8 regression this pairing prevents.
	case CrashChoice::SendOnce:   a.upload = true; a.flush = true; break;
	case CrashChoice::AlwaysSend: a.upload = true; a.persistAlways = true; a.flush = true; break;
	case CrashChoice::SaveToDisk: a.saveToDisk = true; break;
	case CrashChoice::NotNow:     break;
	}
	return a;
}

std::string ComputeSafeFileLabel(const std::string &path)
{
	const std::size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace zx
