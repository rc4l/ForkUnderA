// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What keeps a browser row mortal.
//
// Every row in the list has to be capable of leaving it. That sounds too obvious to state, which is
// exactly why it went wrong: BROWSER_QueryTick can only expire a row that is REFRESHING (a re-check
// we are waiting on) or WAITING FOR REPLY (a first query we are waiting on). A row that is active
// and neither of those is not "healthy" -- it is unreachable by the only code that could ever
// remove it, and it will sit in the list until the client restarts.
//
// That is what browser_MirrorAnswerOntoOurOtherRows produced. It copies a live answer onto our
// server's other rows so the public row does not flicker while only the LAN row is replying, and it
// copied the answered row's bRefreshing=false along with everything else. Every row it touched
// became permanent. Changing the host port stranded a pair of them: they stopped matching "ours", so
// even the mirror could not reach them again, and four rows for one server -- two of them dead, two
// carrying flags -- stayed on screen.
//
// The same blind spot hid the punch bug. The punch ladder sat below an early return that handled
// refreshing rows, so only rows in WAITING FOR REPLY ever reached it, and a server that had already
// been seen once and then moved behind a carrier NAT was re-queried, ignored and dropped without a
// single punch ever being requested.
//
// Both were glue, not arithmetic, and neither existing unit could have caught them. These two
// predicates are the invariant they broke, small enough to assert and specific enough to fail.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_ROWLIFETIME_COMPUTE_H
#define ZX_ROWLIFETIME_COMPUTE_H

namespace zx
{

// [rc4l] Can the timeout machinery ever remove this row?
//
// The two flags are the only handles it has. A listed row that answers neither description is
// immortal, which is never a state worth being in: a server that has gone should stop being offered.
bool RowCanEverExpire(bool listed, bool refreshing, bool waitingForReply);

// [rc4l] Whether a row is waiting on a reply that may never come, and so should be running the punch
// ladder. True for BOTH kinds of waiting.
//
// Written as one predicate on purpose. The bug was a caller that remembered one kind and forgot the
// other, so the fix is not "call it from the second place too" but "stop there being two questions".
bool RowIsAwaitingReply(bool refreshing, bool waitingForReply);

// [rc4l] What a row's refreshing flag must be after a live answer is mirrored onto it from a sibling
// row. Its OWN value, never the donor's.
//
// The donor answered, so its flag is false; taking that is what made mirrored rows permanent. The
// row's deadline belongs to the row, exactly as its address and LAN badge do.
bool RefreshingAfterMirror(bool targetWasRefreshing, bool donorWasRefreshing);

} // namespace zx

#endif // ZX_ROWLIFETIME_COMPUTE_H
