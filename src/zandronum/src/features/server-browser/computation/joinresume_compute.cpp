// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/joinresume_compute.h"

namespace zx
{

ResumeAction ComputeResumeAction(bool havePendingAction, bool downloadSucceeded, bool browserOpen,
	bool answeringPrompt)
{
	// A prompt on screen outranks everything, including the join being ready and including the
	// transfer having failed. The player is mid-sentence; finishing it is their business, and the
	// answer is what decides this.
	if (answeringPrompt)
		return ResumeAction::Hold;

	if (!havePendingAction)
		return ResumeAction::Nothing;

	// Failure is told immediately wherever they are. Unlike success it tears nothing down, so there
	// is no reason to make them come back for it.
	if (!downloadSucceeded)
		return ResumeAction::ReportFailure;

	// Browser open means they are watching the progress bar. Asking them to confirm the thing they
	// are visibly waiting for is friction; anywhere else, taking the game away unannounced is worse.
	return browserOpen ? ResumeAction::ProceedNow : ResumeAction::NotifyReady;
}

} // namespace zx
