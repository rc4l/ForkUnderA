// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Who does an incoming packet belong to, and what does the footer say about what is missing.
//
// Both questions used to be answered inline in engine code, and both got them wrong in ways nobody
// could see: a launcher reply from the server you were playing on was handed to the game parser and
// the server vanished from your own list, and a server hidden for running another build left no
// trace at all. Neither failure was visible, and neither was testable. They are here so they are
// both.

#ifndef ZX_REPLYROUTING_COMPUTE_H
#define ZX_REPLYROUTING_COMPUTE_H

namespace zx
{

// The two launcher replies a server can send. Values match networkshared.h; passed in rather than
// included so this unit stays free of the engine.
struct LauncherCommands
{
	long whole;
	long segmented;
};

// Should this packet go to the browser rather than to the game parser?
//
// `bAwaitingReply` is the gate and it is not optional. The command is read out of a packet nobody
// has authenticated yet, and a game packet may begin with any bytes at all -- so the reading only
// means something when we actually asked this sender a question and have not been answered.
bool ShouldRouteToBrowser( bool bAwaitingReply, long peekedCommand, const LauncherCommands &commands );

// What the footer says when the visible list is empty. Ordered by how actionable it is: a player who
// is on the wrong build can do something about it, and should be told that before being told that
// something timed out.
enum class EmptyReason
{
	NothingHosted,		// we asked, everyone answered, there is simply nobody hosting
	HiddenBySearch,		// servers exist and the search text is hiding them
	WrongVersion,		// servers answered and run a build that cannot play with this one
	NoResponse,			// servers were listed and none of them answered
};

// `bHasSearch` means the search box is non-empty. Counts are of servers, not of rows.
EmptyReason ExplainEmptyList( bool bHasSearch, int activeCount, int versionMismatchCount,
	int noResponseCount );

} // namespace zx

#endif // ZX_REPLYROUTING_COMPUTE_H
