// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What should happen to one browser row this tick.
//
// BROWSER_QueryTick used to decide this inline, and the decisions were spread across early returns
// rather than written down anywhere. Two live bugs came out of that in one session, and neither was
// arithmetic -- both were DISPATCH, the question of which action a row in a given state gets:
//
//   - a row could be left active and not refreshing, which no timeout can reach, so it stayed in the
//     list forever. Four rows for one server, two of them long dead.
//   - the punch ladder ran only for rows waiting on a FIRST reply, so a server that had been seen
//     once and then moved behind carrier NAT was re-checked, ignored and dropped without a punch
//     ever being requested. A host on cellular was listed and verified by the registry while the
//     browser reported "3 did not respond".
//
// Predicates could not catch either, because the fault was never in a question, it was in which
// question got asked. So the whole decision is one function over one struct: every state the caller
// can be in is an input, every thing it may do is an output, and a test can walk the entire space
// instead of the cases somebody thought of.
//
// The caller keeps what genuinely cannot be pure -- sending the packet, mutating the row, reading
// the clock. It is not allowed to keep any judgement.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_ROWSTEP_COMPUTE_H
#define ZX_ROWSTEP_COMPUTE_H

namespace zx
{

// Everything known about a row at the moment it is stepped.
struct RowStepIn
{
	// [rc4l] The two kinds of waiting, kept apart because they run on different clocks and expire
	// differently, and folded together only where the answer is the same. Conflating them is what
	// hid the punch bug; pretending they are unrelated is what caused it.
	bool refreshing;        // a re-check of a row that is already listed
	bool waitingForReply;   // a first query, nothing shown yet

	bool lan;               // found by broadcast, so there is nothing to punch through
	bool punchRequested;    // we have already asked the registry to introduce us

	// Milliseconds since the relevant challenge went out. Non-positive means "not sent yet", which
	// is a real state: a row can be queued for a query that has not left.
	int msSinceQuery;
	int msSinceRefresh;

	int resends;            // how many extra challenges this row has already had
	bool punchBudgetLeft;   // whether the sweep can afford another introduction

	int refreshTimeoutMs;   // how long a re-check may go unanswered before the row goes

	RowStepIn()
		: refreshing(false), waitingForReply(false), lan(false), punchRequested(false),
		  msSinceQuery(0), msSinceRefresh(0), resends(0), punchBudgetLeft(false),
		  refreshTimeoutMs(0) {}
};

// Everything the caller may do about it. All false is a legitimate answer and the common one.
struct RowStepOut
{
	bool requestPunch;      // ask the registry to have the server punch toward us
	bool resendChallenge;   // send the query again, into the hole a punch may have opened
	bool dropFromList;      // the re-check gave up: stop offering this row at all
	bool markTimedOut;      // the first query gave up: keep the row, say it never answered

	RowStepOut()
		: requestPunch(false), resendChallenge(false), dropFromList(false), markTimedOut(false) {}
};

// [rc4l] The two give-up outcomes are deliberately different, and the difference is the honest one.
//
// A row we asked about and never heard from is worth SAYING that about -- the registry lists it, so
// somebody thinks it exists, and "we asked and got nothing" is information. A re-check that fails is
// about a row we were already showing: it answered once and has stopped, so it is simply gone, and
// leaving it on screen offers a server nobody can join.
RowStepOut StepRow(const RowStepIn &in);

} // namespace zx

#endif // ZX_ROWSTEP_COMPUTE_H
