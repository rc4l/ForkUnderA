// [rc4l] Implementation of the pure crash-report decision logic. See crash_report_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "crash_report_compute.h"

namespace zx {

StartupAction ComputeStartupAction(int crashreportsCvar)
{
	// Any positive value means reporting is on; zero or negative means the player opted out.
	return crashreportsCvar >= 1 ? StartupAction::GiveConsent : StartupAction::RevokeConsent;
}

std::string ComputeSafeFileLabel(const std::string &path)
{
	const std::size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

PendingFatalAction ComputePendingFatalAction(bool recordExists, bool reportingOn)
{
	if (!recordExists)
		return PendingFatalAction::None;
	return reportingOn ? PendingFatalAction::Upload : PendingFatalAction::Discard;
}

} // namespace zx
