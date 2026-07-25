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
	switch (choice)
	{
	case CrashChoice::SendOnce:   return {true,  false, false};
	case CrashChoice::AlwaysSend: return {true,  true,  false};
	case CrashChoice::SaveToDisk: return {false, false, true};
	case CrashChoice::NotNow:     return {false, false, false};
	}
	return {false, false, false};
}

std::string ComputeSafeFileLabel(const std::string &path)
{
	const std::size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace zx
