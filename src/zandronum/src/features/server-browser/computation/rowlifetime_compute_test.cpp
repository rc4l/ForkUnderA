// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/rowlifetime_compute.h"

using zx::RefreshingAfterMirror;
using zx::RowCanEverExpire;
using zx::RowIsAwaitingReply;

// -------------------------------------------------------------- mortality

TEST( RowLifetime, AListedRowNobodyIsWaitingOnIsImmortal )
{
	// [rc4l] THE bug. BROWSER_QueryTick can only remove a row that is refreshing or waiting for a
	// reply, so a listed row that is neither cannot be reached by the only code able to delete it.
	// browser_MirrorAnswerOntoOurOtherRows produced exactly this and left four rows for one server
	// on screen, two of them long dead.
	EXPECT_FALSE( RowCanEverExpire( true, false, false ));
}

TEST( RowLifetime, EitherKindOfWaitingKeepsARowMortal )
{
	EXPECT_TRUE( RowCanEverExpire( true, true, false ));
	EXPECT_TRUE( RowCanEverExpire( true, false, true ));
	EXPECT_TRUE( RowCanEverExpire( true, true, true ));
}

TEST( RowLifetime, AnUnlistedRowCannotBeStuck )
{
	// Nothing is offering it, so there is nothing to get stuck in.
	EXPECT_TRUE( RowCanEverExpire( false, false, false ));
}

// ------------------------------------------------------ waiting is waiting

TEST( RowLifetime, BothKindsOfWaitingCountAsAwaitingAReply )
{
	// [rc4l] The punch bug in one line. The ladder ran only for waiting-for-reply rows, so a server
	// seen once and then moved behind a carrier NAT was re-checked, ignored, and dropped without a
	// punch ever being asked for. One predicate, so a caller cannot remember one kind and forget
	// the other.
	EXPECT_TRUE( RowIsAwaitingReply( true, false ));
	EXPECT_TRUE( RowIsAwaitingReply( false, true ));
	EXPECT_TRUE( RowIsAwaitingReply( true, true ));
}

TEST( RowLifetime, ARowNobodyIsWaitingOnIsNotAwaitingAReply )
{
	EXPECT_FALSE( RowIsAwaitingReply( false, false ));
}

TEST( RowLifetime, AnythingMortalIsEitherAwaitingOrUnlisted )
{
	// The two predicates have to agree, or a row could be expirable while nothing was waiting on it
	// -- which is how a row disappears out from under a punch in progress.
	for ( int bits = 0; bits < 8; ++bits )
	{
		const bool listed = (( bits & 1 ) != 0 );
		const bool refreshing = (( bits & 2 ) != 0 );
		const bool waiting = (( bits & 4 ) != 0 );

		if ( RowCanEverExpire( listed, refreshing, waiting ) && listed )
			EXPECT_TRUE( RowIsAwaitingReply( refreshing, waiting )) << "bits " << bits;
	}
}

// ---------------------------------------------------------------- mirroring

TEST( RowLifetime, AMirroredRowKeepsItsOwnDeadline )
{
	// The donor answered, so its flag is false. Taking that is precisely what made mirrored rows
	// permanent: the row stopped being refreshing without ever having been answered itself.
	EXPECT_TRUE( RefreshingAfterMirror( true, false ));
}

TEST( RowLifetime, MirroringNeverStartsADeadlineThatWasNotThere )
{
	// A row that was not being re-checked does not acquire a deadline just because a sibling
	// answered. It is shown because the sibling proved the server is alive, and it will be
	// re-checked on the next sweep like anything else.
	EXPECT_FALSE( RefreshingAfterMirror( false, true ));
	EXPECT_FALSE( RefreshingAfterMirror( false, false ));
}

TEST( RowLifetime, TheDonorNeverDecidesAnything )
{
	// Whatever the donor was doing, the answer depends only on the target. Stated as a property so
	// a future edit that reaches for the donor's flag fails here rather than on someone's screen.
	for ( int i = 0; i < 2; ++i )
	{
		const bool target = ( i != 0 );

		EXPECT_EQ( target, RefreshingAfterMirror( target, false ));
		EXPECT_EQ( target, RefreshingAfterMirror( target, true ));
	}
}

TEST( RowLifetime, MirroringOntoAWaitingRowLeavesItMortal )
{
	// The whole point, end to end: mirror a live answer onto a row being re-checked, and it must
	// still be removable afterwards.
	const bool refreshingAfter = RefreshingAfterMirror( true, false );

	EXPECT_TRUE( RowCanEverExpire( true, refreshingAfter, false ));
}
