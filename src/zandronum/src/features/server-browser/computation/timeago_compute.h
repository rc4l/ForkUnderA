// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] How long ago something happened, in the largest unit that still says it.
//
// ONE UNIT, NEVER TWO. "1 hour 5 mins ago" is a precision nobody asked for and a string nobody can
// scan: this exists to be glanced at on a tooltip, and the only question it answers is "is that
// recent or stale". Seconds up to a minute, minutes up to an hour, hours up to a day, then days.
//
// Rounded DOWN throughout, so the number is never larger than the truth. A row that says "2 mins
// ago" at 2:59 is honest; one that says "3 mins ago" at 2:01 is not, and this is used to decide
// whether to trust what is on screen.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_TIMEAGO_COMPUTE_H
#define ZX_TIMEAGO_COMPUTE_H

#include <string>

namespace zx
{

// [rc4l] "just now" / "5 secs ago" / "1 min ago" / "3 hours ago" / "2 days ago".
//
// Singular is spelled out rather than left as "1 mins", because a tooltip is prose and the reader
// notices. Under a second reads "just now": zero of anything is a number pretending to be a fact,
// and the honest thing about something that happened this instant is that it just happened.
//
// A negative age means the clock moved, not that the future happened. Answered as unknown rather
// than guessed at, for the same reason the other units round down.
std::string TimeAgo(int seconds);

// The whole line, for a caller that has nothing to show yet. Kept here so "never refreshed" and
// "refreshed 5 mins ago" cannot drift apart in wording.
std::string LastRefreshedLine(bool everRefreshed, int seconds);

} // namespace zx

#endif // ZX_TIMEAGO_COMPUTE_H
