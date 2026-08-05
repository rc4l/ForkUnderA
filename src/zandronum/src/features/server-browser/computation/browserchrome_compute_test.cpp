// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/browserchrome_compute.h"

using zx::BrowserPhase;
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

TEST( BrowserChrome, ShowsNothingButTheSpinnerWhileLooking )
{
	// The state this unit exists for. Tabs, a rule, column headings and an empty black panel around
	// the word "Looking for servers" are four promises that there is something there.
	const unsigned parts = ComputeVisibleParts( BrowserPhase::Loading, kNothingSelected, kIdle );

	EXPECT_TRUE( Shows( parts, zx::kPartPlaceholder ));
	EXPECT_FALSE( Shows( parts, zx::kPartTabs ));
	EXPECT_FALSE( Shows( parts, zx::kPartList ));
	EXPECT_FALSE( Shows( parts, zx::kPartDetail ));
	EXPECT_FALSE( Shows( parts, zx::kPartFooter ));
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

	// And nothing else comes back with it.
	EXPECT_FALSE( Shows( parts, zx::kPartTabs ));
	EXPECT_FALSE( Shows( parts, zx::kPartList ));
	EXPECT_FALSE( Shows( parts, zx::kPartDetail ));
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

TEST( BrowserChrome, NeverShowsTheDetailPanelWithoutTheListItBelongsTo )
{
	// The panel sits beside the list and describes a row in it. On its own it describes nothing.
	const BrowserPhase phases[] = { BrowserPhase::Loading, BrowserPhase::Empty, BrowserPhase::Ready };

	for ( int p = 0; p < 3; ++p )
		for ( int sel = 0; sel < 2; ++sel )
			for ( int dl = 0; dl < 2; ++dl )
			{
				const unsigned parts = ComputeVisibleParts( phases[p], sel != 0, dl != 0 );
				if ( Shows( parts, zx::kPartDetail ))
					EXPECT_TRUE( Shows( parts, zx::kPartList )) << p << "," << sel << "," << dl;
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

			// Everything visible when idle is still visible when busy, and the only thing that can be
			// added is the footer.
			EXPECT_EQ( 0u, idle & ~busy ) << "a download removed a part at phase " << p;
			EXPECT_EQ( 0u, busy & ~idle & ~static_cast<unsigned>( zx::kPartFooter ))
				<< "a download added something other than the footer at phase " << p;
		}
}
