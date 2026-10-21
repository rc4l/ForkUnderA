// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/continue/computation/continuereturn_compute.h"

using namespace zx;

namespace
{

ContinueReturnInputs Ready()
{
	ContinueReturnInputs in;
	in.pending = true;
	in.inSession = false;
	in.engineIdle = true;
	return in;
}

} // namespace

TEST( ContinueReturn, OutOfTheSessionAndSettledIsWhenItHappens )
{
	EXPECT_EQ( ContinueReturnStep::Perform, DecideContinueReturn( Ready() ));
}

TEST( ContinueReturn, NothingOwedMeansNothingToDo )
{
	ContinueReturnInputs in = Ready();
	in.pending = false;
	EXPECT_EQ( ContinueReturnStep::Wait, DecideContinueReturn( in ));
}

TEST( ContinueReturn, StillConnectedMeansTheTeardownIsNotFinished )
{
	ContinueReturnInputs in = Ready();
	in.inSession = true;
	EXPECT_EQ( ContinueReturnStep::Wait, DecideContinueReturn( in ));
}

TEST( ContinueReturn, ABusyEngineWouldSwallowTheLoad )
{
	// The bug this unit exists for. A load is queued as a gameaction, and being kicked replaces it
	// with ga_fullconsole -- the load never runs, nothing is printed, and the feature looks broken
	// while the same file loads perfectly by hand.
	ContinueReturnInputs in = Ready();
	in.engineIdle = false;
	EXPECT_EQ( ContinueReturnStep::Wait, DecideContinueReturn( in ));
}

TEST( ContinueReturn, EveryConditionIsRequiredAtOnce )
{
	for ( int pending = 0; pending <= 1; ++pending )
	{
		for ( int session = 0; session <= 1; ++session )
		{
			for ( int idle = 0; idle <= 1; ++idle )
			{
				ContinueReturnInputs in;
				in.pending = ( pending == 1 );
				in.inSession = ( session == 1 );
				in.engineIdle = ( idle == 1 );

				const bool expected = ( pending == 1 ) && ( session == 0 ) && ( idle == 1 );
				EXPECT_EQ( expected, DecideContinueReturn( in ) == ContinueReturnStep::Perform )
					<< "pending=" << pending << " session=" << session << " idle=" << idle;
			}
		}
	}
}

TEST( ContinueReturn, WaitingIsNeverDestructive )
{
	// The asymmetry that governs it: waiting costs a frame, acting early costs the whole return and
	// leaves no trace that anything was attempted.
	ContinueReturnInputs in = Ready();
	in.engineIdle = false;
	EXPECT_EQ( ContinueReturnStep::Wait, DecideContinueReturn( in ));

	in.engineIdle = true;
	EXPECT_EQ( ContinueReturnStep::Perform, DecideContinueReturn( in ));
}
