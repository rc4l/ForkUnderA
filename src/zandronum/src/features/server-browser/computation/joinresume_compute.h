// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What to do when a download that something was waiting on finishes.
//
// Two things wait this way: a JOIN whose server wanted files we did not have, and HOSTING an
// experience whose files we did not have. The timing question is identical for both -- did it work,
// is the player still looking at the browser, are they mid-answer to a prompt -- so they share this
// rather than growing two copies of a truth table that would drift apart.
//
// Four things can be true at that moment and they interact, which is why this is a function with a
// truth table rather than a chain of ifs buried in a callback:
//
//   - the transfer succeeded, or it did not
//   - the browser is still on screen, or the player has gone off to do something else
//   - the player is mid-way through answering a question we asked them
//
// The dangerous combination is the one that used to be unhandled. Finishing while the player is away
// fired the join by itself: the game reinitialised for the new WAD set out from under whatever they
// were doing, with no sign beforehand that anything was downloading. Finishing while a "cancel this
// download?" prompt was up did the same thing THROUGH the prompt, so they were answering a question
// about something that had already resolved itself.
//
// Getting that wrong is not a cosmetic bug -- it throws away whatever the player was doing -- and it
// is invisible in review, because every individual branch reads fine. Written here, every
// combination can be asserted.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_JOINRESUME_COMPUTE_H
#define ZX_JOINRESUME_COMPUTE_H

namespace zx
{

enum class ResumeAction
{
	// Nothing to do -- there was nothing waiting on this.
	Nothing,

	// Park it. The player is being asked something they have to answer first, and the answer decides
	// what happens to this.
	Hold,

	// Say it is ready and wait to be come back to. The player is elsewhere, and taking the game away
	// from them unannounced is the behaviour this whole path exists to stop.
	NotifyReady,

	// Go ahead now -- join, or host. The browser is on screen, so the player is visibly waiting for
	// exactly this.
	ProceedNow,

	// The transfer failed. Say so, wherever they are.
	ReportFailure,
};

// `havePendingAction` false means nothing was waiting and everything else is irrelevant.
//
// Note failure is reported rather than notified even when the player is away: "it did not work" is
// not something to sit on until they wander back, and unlike a success it costs them nothing to be
// told immediately -- there is no game state to tear down.
ResumeAction ComputeResumeAction(bool havePendingAction, bool downloadSucceeded, bool browserOpen,
	bool answeringPrompt);

} // namespace zx

#endif // ZX_JOINRESUME_COMPUTE_H
