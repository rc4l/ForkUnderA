// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/browserchrome_compute.h"

using zx::BrowserPhase;
using zx::ComputeHostParts;
using zx::ComputeVisibleParts;

namespace
{
const bool kSelected = true;
const bool kNothingSelected = false;
const bool kDownloading = true;
const bool kIdle = false;

bool Shows( unsigned parts, unsigned part ) { return ( parts & part ) != 0; }
} // namespace

// ---------------------------------------------------------------- still looking

TEST( BrowserChrome, ShowsTheSpinnerAndNothingThatPromisesContent )
{
	// The state this unit exists for. A list, a detail panel and a footer would each be promising
	// something that genuinely is not there yet.
	const unsigned parts = ComputeVisibleParts( BrowserPhase::Loading, kNothingSelected, kIdle );

	EXPECT_TRUE( Shows( parts, zx::kPartPlaceholder ));
	EXPECT_FALSE( Shows( parts, zx::kPartList ));
	EXPECT_FALSE( Shows( parts, zx::kPartDetail ));
	EXPECT_FALSE( Shows( parts, zx::kPartFooter ));
}

TEST( BrowserChrome, KeepsTheTabsWhileLookingSoThePlayerIsNotTrapped )
{
	// [rc4l] The tabs are not part of the ANSWER, they are how the player asks a different question
	// -- so a query in flight is no reason to take them away.
	//
	// This was the spinner alone until HOST existed, which was defensible while every tab was a
	// filter over the same not-yet-arrived list. HOST never depended on the query, and hiding it
	// during one strands a player away from the panel holding the button that stops their own
	// server. With no internet, that lasts until the query times out.
	EXPECT_TRUE( Shows( ComputeVisibleParts( BrowserPhase::Loading, kNothingSelected, kIdle ),
		zx::kPartTabs ));
}

TEST( BrowserChrome, DrawsNoDetailPanelWhileLookingEvenIfSomethingIsStillSelected )
{
	// A selection can survive from a previous visit while the requery is in flight. It does not earn
	// the panel back: the list it indexes into is empty.
	const unsigned parts = ComputeVisibleParts( BrowserPhase::Loading, kSelected, kIdle );
	EXPECT_FALSE( Shows( parts, zx::kPartDetail ));
}

TEST( BrowserChrome, KeepsTheFooterWhileLookingIfATransferIsRunning )
{
	// Reopening the browser mid-download puts it back in the loading phase for a moment. The progress
	// line is the one thing the player is watching, and it lives in the footer.
	const unsigned parts = ComputeVisibleParts( BrowserPhase::Loading, kNothingSelected, kDownloading );

	EXPECT_TRUE( Shows( parts, zx::kPartFooter ));
	EXPECT_TRUE( Shows( parts, zx::kPartPlaceholder ));

	// The panel comes back with it, because CANCEL is in there. The LIST does not -- it is the one
	// thing here that would be promising content that has not arrived.
	EXPECT_TRUE( Shows( parts, zx::kPartDetail ));
	EXPECT_FALSE( Shows( parts, zx::kPartList ));
}

TEST( BrowserChrome, KeepsTheCancelButtonWhenTheServerItBelongsToDies )
{
	// The edge case this rule exists for. A server can die mid-transfer: it times out of the list, the
	// selection goes with it, and the download is still running. Without the panel there is no CANCEL
	// on screen at all, and the player watches a frozen progress line with nothing to press.
	const zx::BrowserPhase phases[] = { zx::BrowserPhase::Loading, zx::BrowserPhase::Empty,
		zx::BrowserPhase::Ready };

	for ( int p = 0; p < 3; ++p )
	{
		const unsigned parts = ComputeVisibleParts( phases[p], kNothingSelected, kDownloading );
		EXPECT_TRUE( Shows( parts, zx::kPartDetail )) << "no way to cancel at phase " << p;
	}
}

// ---------------------------------------------------------------- nothing out there

TEST( BrowserChrome, KeepsTheTabsWhenTheListIsEmpty )
{
	// The likeliest reason a player is reading "No servers found" is that they are on the wrong tab,
	// so the fix has to be reachable from the screen reporting the problem.
	const unsigned parts = ComputeVisibleParts( BrowserPhase::Empty, kNothingSelected, kIdle );

	EXPECT_TRUE( Shows( parts, zx::kPartTabs ));
	EXPECT_TRUE( Shows( parts, zx::kPartPlaceholder ));
	EXPECT_TRUE( Shows( parts, zx::kPartFooter ));
	EXPECT_FALSE( Shows( parts, zx::kPartList ));
	EXPECT_FALSE( Shows( parts, zx::kPartDetail ));
}

// ---------------------------------------------------------------- servers to show

TEST( BrowserChrome, ShowsTheWholeBrowserOnceThereAreServers )
{
	const unsigned parts = ComputeVisibleParts( BrowserPhase::Ready, kSelected, kIdle );

	EXPECT_TRUE( Shows( parts, zx::kPartTabs ));
	EXPECT_TRUE( Shows( parts, zx::kPartList ));
	EXPECT_TRUE( Shows( parts, zx::kPartDetail ));
	EXPECT_TRUE( Shows( parts, zx::kPartFooter ));
	EXPECT_FALSE( Shows( parts, zx::kPartPlaceholder ));
}

TEST( BrowserChrome, DrawsNoDetailPanelWithoutASelection )
{
	// Every line in that panel describes the selected server, so without one it is an empty box.
	const unsigned parts = ComputeVisibleParts( BrowserPhase::Ready, kNothingSelected, kIdle );

	EXPECT_TRUE( Shows( parts, zx::kPartList ));
	EXPECT_FALSE( Shows( parts, zx::kPartDetail ));
}

// ---------------------------------------------------------------- invariants

TEST( BrowserChrome, NeverShowsTheListAndThePlaceholderAtOnce )
{
	// They occupy the same space and answer the same question. Both at once is a rendering bug.
	const BrowserPhase phases[] = { BrowserPhase::Loading, BrowserPhase::Empty, BrowserPhase::Ready };

	for ( int p = 0; p < 3; ++p )
		for ( int sel = 0; sel < 2; ++sel )
			for ( int dl = 0; dl < 2; ++dl )
			{
				const unsigned parts = ComputeVisibleParts( phases[p], sel != 0, dl != 0 );
				EXPECT_FALSE( Shows( parts, zx::kPartList ) && Shows( parts, zx::kPartPlaceholder ))
					<< p << "," << sel << "," << dl;
			}
}

TEST( BrowserChrome, ShowsTheDetailPanelOnlyWithTheListOrWithATransfer )
{
	// The panel normally sits beside the list and describes a row in it, so on its own it would
	// describe nothing -- EXCEPT while a transfer is running, when it is carrying the CANCEL button
	// and has a job of its own to do.
	const BrowserPhase phases[] = { BrowserPhase::Loading, BrowserPhase::Empty, BrowserPhase::Ready };

	for ( int p = 0; p < 3; ++p )
		for ( int sel = 0; sel < 2; ++sel )
			for ( int dl = 0; dl < 2; ++dl )
			{
				const unsigned parts = ComputeVisibleParts( phases[p], sel != 0, dl != 0 );
				if ( Shows( parts, zx::kPartDetail ))
					EXPECT_TRUE( Shows( parts, zx::kPartList ) || ( dl != 0 ))
						<< p << "," << sel << "," << dl;
			}
}

TEST( BrowserChrome, AlwaysShowsSomething )
{
	// A browser drawing an empty panel and nothing else is indistinguishable from a crash.
	const BrowserPhase phases[] = { BrowserPhase::Loading, BrowserPhase::Empty, BrowserPhase::Ready };

	for ( int p = 0; p < 3; ++p )
		for ( int sel = 0; sel < 2; ++sel )
			for ( int dl = 0; dl < 2; ++dl )
				EXPECT_NE( 0u, ComputeVisibleParts( phases[p], sel != 0, dl != 0 ))
					<< p << "," << sel << "," << dl;
}

TEST( BrowserChrome, TabsAndListAreNeverHiddenByADownload )
{
	// A transfer adds the footer and takes nothing away -- the browser stays usable while it runs,
	// which is the entire reason joining leaves it open.
	const BrowserPhase phases[] = { BrowserPhase::Loading, BrowserPhase::Empty, BrowserPhase::Ready };

	for ( int p = 0; p < 3; ++p )
		for ( int sel = 0; sel < 2; ++sel )
		{
			const unsigned idle = ComputeVisibleParts( phases[p], sel != 0, false );
			const unsigned busy = ComputeVisibleParts( phases[p], sel != 0, true );

			// Everything visible when idle is still visible when busy, and the only things a download
			// may ADD are the footer (its progress) and the panel (its CANCEL button).
			const unsigned mayAdd = static_cast<unsigned>( zx::kPartFooter ) |
				static_cast<unsigned>( zx::kPartDetail );

			EXPECT_EQ( 0u, idle & ~busy ) << "a download removed a part at phase " << p;
			EXPECT_EQ( 0u, busy & ~idle & ~mayAdd )
				<< "a download added something it has no business adding at phase " << p;
		}
}

// ---------------------------------------------------------------- the hosting tab

TEST( HostParts, ReplacesTheListRatherThanFilteringIt )
{
	// [rc4l] PUBLIC and PRIVATE are two views of one list; HOST is a different screen. Everything
	// about a server you might join -- the rows, the detail panel, the count -- is meaningless on the
	// screen where you are making one, so none of it is drawn.
	const unsigned parts = ComputeHostParts( false );

	EXPECT_TRUE( Shows( parts, zx::kPartHost ));
	EXPECT_TRUE( Shows( parts, zx::kPartTabs ));

	EXPECT_FALSE( Shows( parts, zx::kPartList ));
	EXPECT_FALSE( Shows( parts, zx::kPartDetail ));
	EXPECT_FALSE( Shows( parts, zx::kPartPlaceholder ));
	EXPECT_FALSE( Shows( parts, zx::kPartFooter ));
}

TEST( HostParts, KeepsTheTabsSoThereIsAWayBack )
{
	// A screen with no way off it is a trap, and the tabs are the only route.
	EXPECT_TRUE( Shows( ComputeHostParts( false ), zx::kPartTabs ));
	EXPECT_TRUE( Shows( ComputeHostParts( true ), zx::kPartTabs ));
}

TEST( HostParts, ARunningTransferSurvivesTheChangeOfTab )
{
	// The download does not care which screen the player wandered onto, and the button that stops it
	// lives in the detail panel. Taking that away would strand a transfer with no way to cancel it.
	const unsigned parts = ComputeHostParts( true );

	EXPECT_TRUE( Shows( parts, zx::kPartHost ));
	EXPECT_TRUE( Shows( parts, zx::kPartFooter ));
	EXPECT_TRUE( Shows( parts, zx::kPartDetail ));
}
