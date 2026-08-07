// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "replyrouting_compute.h"

namespace zx
{

bool ShouldRouteToBrowser( bool bAwaitingReply, long peekedCommand, const LauncherCommands &commands )
{
	if ( bAwaitingReply == false )
		return false;

	return (( peekedCommand == commands.whole ) || ( peekedCommand == commands.segmented ));
}

EmptyReason ExplainEmptyList( bool bHasSearch, int activeCount, int versionMismatchCount,
	int noResponseCount )
{
	// The search is checked first because it is the only cause the player created deliberately and
	// can undo instantly. Telling someone nobody is hosting while their own search box is hiding six
	// servers is a wrong answer to the question they asked.
	if ( bHasSearch && ( activeCount > 0 ))
		return EmptyReason::HiddenBySearch;

	// Then the build mismatch, ahead of timeouts: it is a definite answer from a server that really
	// replied, where a timeout is the absence of one. A player told "nothing responded" goes looking
	// at their network; a player told "these are on another version" goes and updates.
	if ( versionMismatchCount > 0 )
		return EmptyReason::WrongVersion;

	if ( noResponseCount > 0 )
		return EmptyReason::NoResponse;

	return EmptyReason::NothingHosted;
}

} // namespace zx
