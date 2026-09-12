// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/continue/computation/continuestatus_compute.h"

#include <string>

using namespace zx;

namespace
{

ContinueStatusInputs Solo()
{
	ContinueStatusInputs in;
	in.kind = ContinueKind::Single;
	return in;
}

ContinueStatusInputs Hosted()
{
	ContinueStatusInputs in;
	in.kind = ContinueKind::Hosted;
	return in;
}

ContinueStatusInputs Online( ServerProbe probe )
{
	ContinueStatusInputs in;
	in.kind = ContinueKind::Server;
	in.probe = probe;
	return in;
}

} // namespace

// ---------------------------------------------------------------- green

TEST( ContinueStatus, EverythingPresentIsGreen )
{
	EXPECT_EQ( ContinueStatus::Ready, DecideContinueStatus( Solo() ));
	EXPECT_EQ( ContinueStatus::Ready, DecideContinueStatus( Hosted() ));
	EXPECT_EQ( ContinueStatus::Ready, DecideContinueStatus( Online( ServerProbe::Alive )));
}

TEST( ContinueStatus, AServerNobodyHasAskedAboutIsGreen )
{
	// The same asymmetry the button already settles: an unasked server is not a dead one, and
	// painting "we have not checked" as a fault marks every row red for the second it takes to
	// answer.
	EXPECT_EQ( ContinueStatus::Ready, DecideContinueStatus( Online( ServerProbe::Unknown )));
}

TEST( ContinueStatus, GreenHasNothingToExplain )
{
	EXPECT_STREQ( "", ContinueStatusReason( Solo() ));
	EXPECT_STREQ( "", ContinueStatusReason( Online( ServerProbe::Alive )));
}

// ---------------------------------------------------------------- yellow

TEST( ContinueStatus, AMissingModIsYellow )
{
	// A download is a wait, not a wall.
	ContinueStatusInputs in = Solo();
	in.missingMods = 1;

	EXPECT_EQ( ContinueStatus::Fixable, DecideContinueStatus( in ));
	EXPECT_STRNE( "", ContinueStatusReason( in ));
}

TEST( ContinueStatus, AMissingButFreeIwadIsYellow )
{
	ContinueStatusInputs in = Hosted();
	in.iwadPresent = false;
	in.iwadIsFree = true;

	EXPECT_EQ( ContinueStatus::Fixable, DecideContinueStatus( in ));
}

TEST( ContinueStatus, AServerRunningDifferentFilesIsYellow )
{
	// It is up and it will let us in; what waits on the other side is a download. That it is no
	// longer quite the game we left is true, and is not the same as being unreachable.
	EXPECT_EQ( ContinueStatus::Fixable, DecideContinueStatus( Online( ServerProbe::WadsDiffer )));
}

TEST( ContinueStatus, EveryKindCanBeYellowForAMissingMod )
{
	const ContinueKind kinds[] = { ContinueKind::Single, ContinueKind::Hosted, ContinueKind::Server };

	for ( int i = 0; i < 3; ++i )
	{
		ContinueStatusInputs in;
		in.kind = kinds[i];
		in.probe = ServerProbe::Alive;
		in.missingMods = 3;

		EXPECT_EQ( ContinueStatus::Fixable, DecideContinueStatus( in )) << "kind " << i;
	}
}

// ---------------------------------------------------------------- red

TEST( ContinueStatus, AMissingCommercialIwadIsRed )
{
	// No amount of downloading will produce it, so it is not a wait.
	ContinueStatusInputs in = Solo();
	in.iwadPresent = false;
	in.iwadIsFree = false;

	EXPECT_EQ( ContinueStatus::Broken, DecideContinueStatus( in ));
	EXPECT_STRNE( "", ContinueStatusReason( in ));
}

TEST( ContinueStatus, AServerThatDoesNotAnswerIsRed )
{
	EXPECT_EQ( ContinueStatus::Broken, DecideContinueStatus( Online( ServerProbe::Gone )));
}

TEST( ContinueStatus, AServerOnAVersionWeCannotJoinIsRed )
{
	ContinueStatusInputs in = Online( ServerProbe::Alive );
	in.versionCompatible = false;

	EXPECT_EQ( ContinueStatus::Broken, DecideContinueStatus( in ));
}

// ---------------------------------------------------------------- precedence

TEST( ContinueStatus, RedOutranksYellow )
{
	// A dead server whose mod is also missing is still a dead server: telling the player to wait for
	// a download would be the wrong instruction.
	ContinueStatusInputs in = Online( ServerProbe::Gone );
	in.missingMods = 2;

	EXPECT_EQ( ContinueStatus::Broken, DecideContinueStatus( in ));
	EXPECT_STREQ( "that server is not answering", ContinueStatusReason( in ));
}

TEST( ContinueStatus, AnUnobtainableIwadOutranksEverything )
{
	ContinueStatusInputs in = Online( ServerProbe::Alive );
	in.iwadPresent = false;
	in.iwadIsFree = false;
	in.missingMods = 5;

	EXPECT_EQ( ContinueStatus::Broken, DecideContinueStatus( in ));
	EXPECT_EQ( std::string( "the game it needs is not on this machine, and cannot be downloaded" ),
		std::string( ContinueStatusReason( in )));
}

TEST( ContinueStatus, AVersionMismatchIsNamedAheadOfSilence )
{
	// Both are red; only one of them tells the player anything they could act on.
	ContinueStatusInputs in = Online( ServerProbe::Gone );
	in.versionCompatible = false;

	EXPECT_EQ( ContinueStatus::Broken, DecideContinueStatus( in ));
	EXPECT_EQ( std::string( "that server is running a version this build cannot join" ),
		std::string( ContinueStatusReason( in )));
}

TEST( ContinueStatus, TheReasonAlwaysMatchesTheColour )
{
	// Every combination: a row that is not green must say why, and a green one must not.
	for ( int kind = 1; kind <= 3; ++kind )
	{
		for ( int iwad = 0; iwad <= 1; ++iwad )
		{
			for ( int freeIwad = 0; freeIwad <= 1; ++freeIwad )
			{
				for ( int mods = 0; mods <= 1; ++mods )
				{
					for ( int probe = 0; probe <= 3; ++probe )
					{
						for ( int version = 0; version <= 1; ++version )
						{
							ContinueStatusInputs in;
							in.kind = static_cast<ContinueKind>( kind );
							in.iwadPresent = ( iwad == 1 );
							in.iwadIsFree = ( freeIwad == 1 );
							in.missingMods = mods;
							in.probe = static_cast<ServerProbe>( probe );
							in.versionCompatible = ( version == 1 );

							const bool bGreen = ( DecideContinueStatus( in ) == ContinueStatus::Ready );
							const bool bSilent = ( *ContinueStatusReason( in ) == 0 );

							EXPECT_EQ( bGreen, bSilent )
								<< "kind " << kind << " iwad " << iwad << " free " << freeIwad
								<< " mods " << mods << " probe " << probe << " version " << version;
						}
					}
				}
			}
		}
	}
}

TEST( ContinueStatus, AVersionMismatchOnlyMattersForAServer )
{
	// There is nobody to disagree with about a version in a game we start ourselves.
	ContinueStatusInputs in = Hosted();
	in.versionCompatible = false;

	EXPECT_EQ( ContinueStatus::Ready, DecideContinueStatus( in ));
}

TEST( ContinueStatus, AProbeVerdictOnlyMattersForAServer )
{
	// A hosted record carries no address, so whatever a stale probe field says about one is not
	// about this row.
	ContinueStatusInputs in = Hosted();
	in.probe = ServerProbe::Gone;

	EXPECT_EQ( ContinueStatus::Ready, DecideContinueStatus( in ));
}
