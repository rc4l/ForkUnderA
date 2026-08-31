// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/continue/computation/continuerehost_compute.h"

using namespace zx;

TEST( ContinueRehost, TheOrdinaryCaseStartsItWhereWeStand )
{
	// Host something, leave it, press Continue in the same session: our files are already the
	// server's files, so there is nothing to do but start it.
	ContinueRehostInputs in;
	in.filesFound = true;
	in.filesMatchOurs = true;

	EXPECT_EQ( ContinueRehostStep::Host, DecideContinueRehost( in ));
}

TEST( ContinueRehost, DifferentFilesMeanTheEngineReloadsFirst )
{
	// Host a mod, quit, relaunch plain, press Continue. Starting the server as we are produces one
	// we are refused from with a page of hashes -- the shape of the reported bug.
	ContinueRehostInputs in;
	in.filesFound = true;
	in.filesMatchOurs = false;

	EXPECT_EQ( ContinueRehostStep::ReloadThenHost, DecideContinueRehost( in ));
}

TEST( ContinueRehost, MissingFilesAreRefusedRatherThanRestartedInto )
{
	ContinueRehostInputs in;
	in.filesFound = false;
	in.filesMatchOurs = false;

	EXPECT_EQ( ContinueRehostStep::RefuseMissing, DecideContinueRehost( in ));
}

TEST( ContinueRehost, MissingBeatsMatching )
{
	// The set can only "match" vacuously once the files are gone. Restarting would still find
	// nothing, so the answer must not depend on which of the two we noticed first.
	ContinueRehostInputs in;
	in.filesFound = false;
	in.filesMatchOurs = true;

	EXPECT_EQ( ContinueRehostStep::RefuseMissing, DecideContinueRehost( in ));
}

TEST( ContinueRehost, NothingKnownStartsNothing )
{
	EXPECT_EQ( ContinueRehostStep::RefuseMissing, DecideContinueRehost( ContinueRehostInputs( )));
}
