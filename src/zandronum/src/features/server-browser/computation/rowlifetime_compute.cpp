// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "rowlifetime_compute.h"

namespace zx
{

bool RowCanEverExpire(bool listed, bool refreshing, bool waitingForReply)
{
	// A row nobody is offering cannot be stuck in the list, so the question does not arise.
	if (!listed)
		return true;

	return refreshing || waitingForReply;
}

bool RowIsAwaitingReply(bool refreshing, bool waitingForReply)
{
	return refreshing || waitingForReply;
}

bool RefreshingAfterMirror(bool targetWasRefreshing, bool donorWasRefreshing)
{
	// donorWasRefreshing is deliberately unused. It is a parameter so that the one thing this
	// function exists to say is visible in its signature: the donor's deadline is not the target's,
	// and reaching for it is the mistake being prevented.
	(void)donorWasRefreshing;
	return targetWasRefreshing;
}

} // namespace zx
